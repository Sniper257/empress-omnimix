#!/usr/bin/env python3
"""Compile a redistributable/default or private user font profile into coverage data.

Maintainer/override tool only: Pillow and fontTools required. Normal rendering is
stdlib-only. Paths are relative to the profile, explicit, never system-discovered.
No input font file is copied to output. Retain OFL notices with default atlases.
"""
import argparse
import base64
import hashlib
import itertools
import json
import struct
from pathlib import Path
import unicodedata
import zlib

CAP=192
ROLES=('wheel','large_title','genre_artist','card_genre','card_artist','launcher')


def legacy_coverage(raw, num_glyphs):
    """Bounded format4 coverage for older fonts with a truncated final range.

    Some DF collections omit the final two currency glyph IDs. FreeType treats
    those as missing. Do the same; never read or invent an out-of-range glyph.
    """
    if len(raw)<4:raise ValueError('Truncated Unicode cmap')
    _,count=struct.unpack_from('>2H',raw)
    for i in range(count):
        platform,encoding,at=struct.unpack_from('>2HI',raw,4+i*8)
        if not(platform==0 or(platform,encoding)==(3,1))or at+14>len(raw):continue
        fmt,n,_,segments=struct.unpack_from('>4H',raw,at)
        if fmt!=4:continue
        segments//=2;data=raw[at:at+n]
        if not segments or len(data)!=n or 16+segments*8>n or n%2:raise ValueError('Invalid Unicode cmap bounds')
        vals=struct.unpack_from('>'+str((n-14)//2)+'H',data,14);end=vals[:segments];start=vals[segments+1:2*segments+1];delta=vals[2*segments+1:3*segments+1];ranges=vals[3*segments+1:4*segments+1];glyphs=vals[4*segments+1:];out=set()
        if start[-1]!=65535 or end[-1]!=65535 or sum(max(0,b-a+1)for a,b in zip(start,end))>262144:raise ValueError('Invalid Unicode cmap range budget')
        for j in range(segments-1):
            a,b=start[j],end[j]
            if a>b or ranges[j]%2:raise ValueError('Invalid Unicode cmap segment')
            for c in range(a,b+1):
                if ranges[j]:
                    ix=ranges[j]//2-a+j-segments+c
                    if not 0<=ix<len(glyphs):
                        if j==segments-2 and len(glyphs)<=ix<len(glyphs)+2:continue
                        raise ValueError('Unicode cmap glyph offset exceeds bounds')
                    gid=(glyphs[ix]+delta[j])&65535 if glyphs[ix]else 0
                else:gid=(c+delta[j])&65535
                if 0<gid<num_glyphs:out.add(c)
        return out
    raise ValueError('No usable Unicode cmap')


def font_coverage(tt):
    from fontTools.ttLib import TTLibError
    try:return set(tt.getBestCmap()or{})
    except TTLibError as e:
        try:return legacy_coverage(tt.reader['cmap'],tt['maxp'].numGlyphs)
        except (ValueError,struct.error,IndexError)as bad:raise ValueError('Could not read font Unicode coverage: '+str(bad))from e


def compile_profile(profile, corpus, output, fallback=None):
    try:
        from PIL import Image, ImageDraw, ImageFont, __version__ as pillow_version
        from PIL import features
        from fontTools.ttLib import TTFont
    except ImportError as e:
        raise ValueError('Font overrides require Pillow and fonttools: python -m pip install Pillow fonttools') from e
    path=Path(profile);spec=json.loads(path.read_text(encoding='utf-8'))
    base=None
    if fallback is not None:
        from text_raster import Atlas
        base=Atlas.load(fallback)
        if 'fonts'not in spec:
            overrides=spec.get('roles',spec)
            if not overrides or set(overrides)-set(ROLES):raise ValueError('Unknown font override role')
            spec=dict(format=1,name=base.info['profile']+'+custom',fonts={'override_'+k:v for k,v in overrides.items()},roles={k:dict(latin=['override_'+k]if k in overrides else [],japanese=['override_'+k]if k in overrides else [])for k in ROLES})
    if spec.get('format')!=1 or set(spec.get('roles',{}))!=set(ROLES):raise ValueError('Profile must define all six text roles')
    strings=json.loads(Path(corpus).read_text(encoding='utf-8'))
    if not isinstance(strings,list) or any(not isinstance(t,str) for t in strings):raise ValueError('Corpus must be a JSON list of strings')
    chars=set(''.join(strings))|set(chr(i) for i in range(32,127))|set('★☆→←×μéöñßłæ漢')
    chars={c for c in chars if ord(c)>=32 and unicodedata.category(c)!='Cc'}
    pairs=set(''.join(q) for q in itertools.product((chr(i) for i in range(32,127)),repeat=2))
    pairs.update(t[i:i+2] for t in strings for i in range(len(t)-1))
    fonts={};sources=list(base.info['sources'])if base else []
    for key,row in sorted(spec['fonts'].items()):
        p=(path.parent/row['file']).resolve();face=row.get('face',0)
        digest=hashlib.sha256(p.read_bytes()).hexdigest()
        if row.get('sha256') and digest!=row['sha256']:raise ValueError('Font hash mismatch: '+key)
        tt=TTFont(p,fontNumber=face);cmap=font_coverage(tt);copyrights=sorted({n.toUnicode() for n in tt['name'].names if n.nameID==0});tt.close()
        # BASIC avoids a hidden RAQM/HarfBuzz dependency and locale differences.
        def make(size):
            f=ImageFont.truetype(str(p),size,index=face,layout_engine=ImageFont.Layout.BASIC)
            if 'axes' in row:f.set_variation_by_axes(row['axes'])
            return f
        sample='漢' if row.get('japanese') else 'H';target=round(CAP*1.12) if row.get('japanese') else CAP
        size=target
        for _ in range(3):
            f=make(max(1,round(size)));bb=f.getbbox(sample);size*=target/(bb[3]-bb[1])
        f=make(max(1,round(size)));bb=f.getbbox(sample)
        origin=(bb[1]+bb[3])/2-CAP/2 if row.get('japanese') else bb[1]
        fonts[key]=(f,cmap,origin)
        sources.append(dict(key=key,sha256=digest,face=face,name=f.getname(),copyright=copyrights,license=row.get('license','user-supplied; not redistributed'),source=row.get('source'),axes=row.get('axes')))
    roles={};missing=[];shared=dict(base.info['glyphs'])if base else {}
    for role,chains in spec['roles'].items():
        primary=chains['latin'];jp=chains.get('japanese',primary);glyphs={};chosen={}
        for ch in sorted(chars):
            chain=jp if ord(ch)>=0x2000 else primary
            font_key=next((k for k in chain if ord(ch) in fonts[k][1]),None)
            if font_key is None:
                if base and ch in base.roles[role]['glyphs']:
                    glyphs[ch]=base.roles[role]['glyphs'][ch];chosen[ch]=None;continue
                missing.append((role,ch));continue
            f,_,origin=fonts[font_key];box=f.getbbox(ch);l,t,r,b=box
            # Padding supports subsequent quarter-pixel emboldening without clipping.
            pad=8;w,h=max(1,r-l)+2*pad,max(1,b-t)+2*pad
            im=Image.new('L',(w,h));ImageDraw.Draw(im).text((pad-l,pad-t),ch,font=f,fill=255)
            gid=font_key+':'+str(ord(ch))
            if gid not in shared:shared[gid]=dict(size=[w,h],offset=[l-pad,round(t-origin)-pad],advance=round(f.getlength(ch)*64),pixels=base64.b64encode(im.tobytes()).decode('ascii'),font=font_key)
            glyphs[ch]=gid
            chosen[ch]=font_key
        kern={}
        for pair in sorted(pairs):
            if base and pair[0] in chosen and pair[1] in chosen and chosen[pair[0]] is None and chosen[pair[1]] is None:
                delta=base.roles[role]['kern'].get(pair,0)
                if delta:kern[pair]=delta
                continue
            if chosen.get(pair[0])is None:continue
            if pair[0] not in chosen or chosen.get(pair[0])!=chosen.get(pair[1]):continue
            f=fonts[chosen[pair[0]]][0]
            delta=round(f.getlength(pair)*64)-shared[glyphs[pair[0]]]['advance']-shared[glyphs[pair[1]]]['advance']
            if delta:kern[pair]=delta
        roles[role]=dict(glyphs=glyphs,kern=kern)
    if missing:
        text=', '.join(f'{r}: U+{ord(c):04X} {c}' for r,c in missing[:30])
        raise ValueError('Missing font coverage: '+text)
    used={gid for role in roles.values() for gid in role['glyphs'].values()}
    shared={gid:g for gid,g in shared.items()if gid in used}
    data=dict(format=1,cap=CAP,profile=spec['name'],sources=sources,roles=roles,glyphs=shared,renderer=dict(pillow=pillow_version,freetype=features.version('freetype2'),layout='BASIC'),corpus_sha256=hashlib.sha256(json.dumps(strings,ensure_ascii=False,sort_keys=True).encode()).hexdigest())
    raw=json.dumps(data,ensure_ascii=False,separators=(',',':'),sort_keys=True).encode('utf-8')
    output=Path(output);output.write_bytes(b'MGA1'+zlib.compress(raw,9))
    receipt=dict(profile=spec['name'],glyphs_per_role=len(chars),bytes=output.stat().st_size,sha256=hashlib.sha256(output.read_bytes()).hexdigest(),sources=sources,renderer=data['renderer'])
    return receipt


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('profile',type=Path);p.add_argument('corpus',type=Path);p.add_argument('output',type=Path);p.add_argument('--fallback-atlas',type=Path);a=p.parse_args()
    try:print(json.dumps(compile_profile(a.profile,a.corpus,a.output,a.fallback_atlas),ensure_ascii=False,indent=2))
    except (ValueError,OSError) as e:p.exit(2,str(e)+'\n')
if __name__=='__main__':main()
