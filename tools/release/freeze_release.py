#!/usr/bin/env python3
"""Maintainer-only: freeze audited output trees/origin maps into a Python release.

Input plan describes exact source ISO fingerprints and every generated file's
original extent or generated-asset provenance. No heuristic title/disc matching,
no user profiles, no compiler/font tools in the resulting release.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import zipfile
import zlib
from audio_transform import convert as convert_audio,NAMES as AUDIO_NAMES,legacy_bank_recipe,assemble_bank
from xor_delta import difference, decode as decode_xor

from megamix import validate_manifest,canonical,hash_bytes,hash_file,relative,check_files,content_id
from vcdiff import decode
from megamix import Extent


def freeze(plan_path, output, registry=None):
    plan=json.loads(Path(plan_path).read_text());output=Path(output)
    if output.exists():raise ValueError('Release output already exists')
    if registry is not None:
        pinned={d['source']:d for d in json.loads(Path(registry).read_text())['discs']}
        for d in plan['discs']:
            known=pinned.get(d['source'])
            if not known or d['iso']['sha256']!=known['iso_sha256'] or d['iso']['bytes']!=known['iso_bytes']:
                raise ValueError('Source does not match the published fixed-disc registry: '+d['source'])
    source_ids=[d['source'] for d in plan['discs']]
    if len(source_ids)!=len(set(source_ids)):raise ValueError('Duplicate source discs')
    output.parent.mkdir(parents=True,exist_ok=True)
    stage=output.with_name(output.name+'.incomplete')
    if stage.exists():raise ValueError('Inspect incomplete release before retrying')
    release={'schema':1,'version':plan['version'],'contract':plan['contract'],'discs':[]}
    asset_map=plan.get('asset_map')
    if asset_map is None:
        asset_map={}
        for d in plan['discs']:
            for tree in d['targets'].values():
                for name,row in tree['files'].items():
                    if name.startswith('a/'):
                        h=hash_file(row['path'])
                        if name in asset_map and asset_map[name]!=h:raise ValueError('Static asset table collision')
                        asset_map[name]=h
    asset_map_hash=hash_bytes(canonical(asset_map))
    blobs=set();stats=[]
    with zipfile.ZipFile(stage,'x',zipfile.ZIP_DEFLATED,compresslevel=9) as archive,tempfile.TemporaryDirectory(prefix='megamix-freeze-') as temporary:
        work=Path(temporary)
        def blob(data):
            ident=hash_bytes(data)
            if ident not in blobs:
                archive.writestr('blobs/'+ident,data);blobs.add(ident)
            return ident
        for disc in plan['discs']:
            iso=Path(disc['image'])
            if iso.stat().st_size!=disc['iso']['bytes'] or hash_file(iso)!=disc['iso']['sha256']:
                raise ValueError('Original ISO fingerprint changed: '+str(iso))
            record={k:disc[k] for k in ('source','name','kind','iso')};record['recipes']={}
            with iso.open('rb') as f:
                for target,tree in disc['targets'].items():
                    print('Freezing',disc['source'],target,flush=True)
                    files={};steps={};counts={'copy':0,'patch':0,'xor':0,'audio':0,'bank':0,'chart':0,'bga':0,'text':0,'blob':0,'patch_bytes':0}
                    paths=tree['files']
                    casefold=set()
                    for i,(name,row) in enumerate(sorted(paths.items()),1):
                        relative(name)
                        if name.casefold() in casefold:raise ValueError('Case-insensitive destination collision')
                        casefold.add(name.casefold())
                        if name in ('launcher.cfg','debug.cfg','controllers.0','controllers.1') or name.startswith(('MemoryCards/','DVD/','logs/','cache/')):
                            raise ValueError('User/dev files cannot be frozen: '+name)
                        original=Path(row['path']);size=original.stat().st_size;wanted=asset_map.get(name) if name.startswith('a/') else None
                        if wanted is None:wanted=hash_file(original)
                        files[name]={'bytes':size,'sha256':wanted}
                        if 'text' in row:
                            from text_assets import Generator
                            from text_raster import Atlas
                            if 'origin'in row or 'derive'in row:raise ValueError('Text recipe has conflicting origin')
                            spec=dict(row['text']['spec'],op='text')
                            font_data=None
                            if spec['kind']!='native':
                                font_data=Path(row['text']['atlas']).read_bytes();spec['atlas']=blob(font_data)
                                if disc['kind']=='append'and spec.get('palette',{}).get('input')=='base':spec['atlas_bytes']=len(font_data)
                            def text_read(which,at,n):
                                if which=='base' and disc['source']!='id':
                                    base_image=plan.get('text_base_image')
                                    if not base_image:raise ValueError('Maintainer text verification requires text_base_image')
                                    with Path(base_image).open('rb')as base_input:return Extent(base_input,at,n,Path(base_image).stat().st_size).read(n)
                                return Extent(f,at,n,iso.stat().st_size).read(n)
                            data=Generator(Atlas(font_data)if font_data is not None else None,text_read).generate(spec)
                            if len(data)!=size or hash_bytes(data)!=wanted:raise ValueError('Text recipe did not reproduce '+name)
                            steps[name]=spec;counts['text']+=1
                        elif 'derive' in row:
                            if 'origin'in row:raise ValueError('BGA recipe cannot also name a single origin')
                            from bga_transform import from_iso
                            spec=row['derive']
                            def bga_read(at,n):return Extent(f,at,n,iso.stat().st_size).read(n)
                            data=from_iso(spec,bga_read)
                            if len(data)!=size or hash_bytes(data)!=wanted:raise ValueError('BGA recipe did not reproduce '+name)
                            steps[name]=spec;counts['bga']+=1
                        elif 'origin' not in row:
                            if not row.get('generated'):raise ValueError('Missing original extent or generated provenance: '+name)
                            data=original.read_bytes()
                            if hash_bytes(data)!=wanted:raise ValueError('Generated payload changed since planning: '+name)
                            steps[name]={'op':'blob','blob':blob(data),'provenance':row['generated']};counts['blob']+=1
                        else:
                            offset,length=row['origin'];view=Extent(f,offset,length,iso.stat().st_size)
                            # Most data is an exact prefix (movies may lose only sector padding).
                            same=False
                            if size<=length:
                                h=hashlib.sha256();left=size
                                while left:
                                    chunk=view.read(min(left,8*1024*1024));h.update(chunk);left-=len(chunk)
                                same=h.hexdigest()==wanted
                            if same:
                                steps[name]={'op':'copy','offset':offset};counts['copy']+=1
                            else:
                                audio=None
                                if max(size,length)<=64*1024*1024:
                                    view.seek(0);raw_source=view.read(length)
                                    try:
                                        prefix,spans=legacy_bank_recipe(raw_source,original.read_bytes());view.seek(0);h=hashlib.sha256()
                                        for chunk in assemble_bank(prefix,view,spans,size):h.update(chunk)
                                        if h.hexdigest()==wanted:
                                            steps[name]={'op':'bank','blob':blob(prefix),'spans':spans,'offset':offset,'source_bytes':length};counts['bank']+=1
                                            continue
                                    except (ValueError,struct.error):pass
                                    for transform in AUDIO_NAMES:
                                        try:converted=convert_audio(transform,raw_source,size)
                                        except (ValueError,struct.error):continue
                                        if hash_bytes(converted)==wanted:audio=transform;break
                                if audio:
                                    steps[name]={'op':'audio','transform':audio,'offset':offset,'source_bytes':length};counts['audio']+=1
                                    continue
                                xor_best=None
                                if max(size,length)<=64*1024*1024:
                                    view.seek(0);raw_source=view.read(length);raw_target=original.read_bytes()
                                    for shift in sorted({0,length-size}):
                                        data=difference(raw_target,raw_source,shift)
                                        if xor_best is None or len(data)<len(xor_best[0]):xor_best=(data,shift)
                                # Small sparse masks do not need an expensive run matcher.
                                if xor_best and len(xor_best[0])<max(128,size//20):
                                    data,shift=xor_best;view.seek(0);h=hashlib.sha256()
                                    for chunk in decode_xor(data,view,length,size,shift):h.update(chunk)
                                    if h.hexdigest()!=wanted:raise ValueError('XOR mask did not reproduce '+name)
                                    steps[name]={'op':'xor','offset':offset,'source_bytes':length,'shift':shift,'blob':blob(data)};counts['xor']+=1;counts['patch_bytes']+=len(data)
                                    continue
                                source=work/'source';patch=work/'patch';view.seek(0)
                                with source.open('wb') as dest:
                                    left=length
                                    while left:
                                        chunk=view.read(min(left,8*1024*1024));dest.write(chunk);left-=len(chunk)
                                patch.unlink(missing_ok=True)
                                env=dict(os.environ);env.pop('XDELTA',None)
                                subprocess.run(['xdelta3','-D','-R','-S','none','-A','-B','67108864','-W','8388608','-e','-s',str(source),str(original),str(patch)],check=True,env=env,capture_output=True)
                                data=patch.read_bytes()
                                # Verify with the exact standard-library decoder shipped to users.
                                view.seek(0);h=hashlib.sha256();n=0
                                for chunk in decode(data,view,size):h.update(chunk);n+=len(chunk)
                                if n!=size or h.hexdigest()!=wanted:raise ValueError('Frozen delta does not reproduce '+name)
                                if xor_best and len(xor_best[0])<len(zlib.compress(data,9)):
                                    data,shift=xor_best;view.seek(0);h=hashlib.sha256()
                                    for chunk in decode_xor(data,view,length,size,shift):h.update(chunk)
                                    if h.hexdigest()!=wanted:raise ValueError('XOR mask did not reproduce '+name)
                                    steps[name]={'op':'xor','offset':offset,'source_bytes':length,'shift':shift,'blob':blob(data)};counts['xor']+=1
                                else:
                                    steps[name]={'op':'patch','offset':offset,'source_bytes':length,'blob':blob(data)};counts['patch']+=1
                                counts['patch_bytes']+=len(data)
                        if i%200==0:print(' ',i,'/',len(paths),flush=True)
                    package={'schema':1,'kind':disc['kind'],'source':disc['source'],'disc':disc['iso'],'name':disc['name'],'contract':plan['contract'],'runtime_revision':plan['version'],'target':target,'files':files,'songs':tree['songs'],'payload_prefixes':tree.get('payload_prefixes',[]),'style_order':plan['style_order']}
                    for field in ('folder_presentation','folder_art','song_versions','alphabet_policy','coalesce_policy','listing_family_policy','text_bases'):
                        if field in tree:package[field]=tree[field]
                    package['asset_map_hash']=asset_map_hash
                    if disc['kind']=='base':
                        package['asset_map']=asset_map
                        if plan.get('selection'):package['selection']=plan['selection'];package['activation_policy']=2
                    if disc['kind']=='append':
                        package.pop('runtime_revision',None);package['generator_revision']=plan['version']
                    deferred={n:s for n,s in steps.items()if disc['kind']=='append'and s['op']=='text'and s.get('palette',{}).get('input')=='base'}
                    if deferred:package['deferred_text']=dict(schema=1,jobs=deferred)
                    package['content_id']=content_id(package)
                    validate_manifest(package)
                    recipe={'package':package,'files':steps}
                    name='recipes/'+disc['source']+'-'+target+'.json'
                    archive.writestr(name,canonical(recipe));record['recipes'][target]=name
                    stats.append(dict(source=disc['source'],target=target,files=len(files),bytes=sum(x['bytes'] for x in files.values()),**counts))
            release['discs'].append(record)
        release['tool_sources']={p.name:hash_file(p) for p in Path(__file__).parent.glob('*.py')}
        if registry is not None:release['disc_registry_sha256']=hash_file(registry)
        archive.writestr('release.json',canonical(release))
        archive.writestr('freeze-stats.json',canonical(stats))
    with stage.open('rb') as f:os.fsync(f.fileno())
    stage.rename(output)
    print('Release frozen:',output,'(',output.stat().st_size,'bytes )',flush=True)
    return stats


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('plan',type=Path);parser.add_argument('output',type=Path)
    parser.add_argument('--registry',type=Path,default=Path(__file__).with_name('retail_discs.json'))
    args=parser.parse_args();freeze(args.plan,args.output,args.registry)
