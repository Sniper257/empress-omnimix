"""Deterministic fixed-disc folder presentation; standard library only.

PB owns its original artwork beneath one of its song directories. Base assets
are independent generated fallbacks; activation selects from the installed set.
"""
import hashlib,json
from pathlib import PurePosixPath
STYLES=('1st_style','substream','2nd_style','3rd_style','4th_style','5th_style','6th_style','7th_style','8th_style','9th_style','10th_style','red','happy_sky','distorted','gold','dj_troopers')
NAMES={s:s.replace('_',' ') for s in STYLES}
NAMES.update(red='IIDX RED',happy_sky='HAPPY SKY',distorted='DistorteD',gold='GOLD',dj_troopers='DJ TROOPERS')
VOICES={s:i+19 for i,s in enumerate(STYLES)}

def validate(package):
    art=package.get('folder_art')
    if art is None:return
    if package.get('source')!='pb' or package.get('kind')!='append' or set(art)!=set(STYLES):raise ValueError('Invalid PB folder-art owner/styles')
    owned=['songs/'+s['style']+'/'+s['dir']+'/' for s in package['songs']]
    paths=set()
    for style,row in art.items():
        if set(row)!=set(('label','caption','voice')) or row['voice']!=VOICES[style]:raise ValueError('Invalid PB folder-art metadata')
        for kind in ('label','caption'):
            name=row[kind];p=PurePosixPath(name)
            if p.is_absolute() or any(q in ('','.','..') for q in name.split('/')) or '\\' in name or ':' in name or not any(name.startswith(o) for o in owned) or name not in package['files'] or not name.endswith('/'+style+'/'+kind+'.spr') or name in paths:
                raise ValueError('PB folder artwork must be owned, pinned song payload')
            paths.add(name)

def paths(state):
    if state['packages']['id'].get('folder_presentation')!=1:return set()
    result={'songs/'+s+'/style.txt' for s in STYLES}
    pb=state['packages'].get('pb',{})
    if pb.get('folder_art'):
        for s in STYLES:
            result|={'songs/'+s+'/pb-label.spr','songs/'+s+'/pb-caption.spr'}
    return result

def generated(state,read_payload):
    if state['packages']['id'].get('folder_presentation')!=1:return {}
    pb=state['packages'].get('pb',{});validate(pb);art=pb.get('folder_art',{});result={}
    for style in STYLES:
        label,caption='label.spr','caption.spr'
        if style in art:
            for kind in ('label','caption'):
                src=art[style][kind];b=read_payload(src);expected=pb['files'][src]
                if len(b)!=expected['bytes'] or hashlib.sha256(b).hexdigest()!=expected['sha256']:raise ValueError('Damaged PB presentation payload: '+src)
                result['songs/'+style+'/pb-'+kind+'.spr']=b
            label,caption='pb-label.spr','pb-caption.spr'
        result['songs/'+style+'/style.txt']=('name '+NAMES[style]+'\nfolder '+label+'\ncaption '+caption+'\nvoice '+str(VOICES[style])+'\n').encode()
    return result


def same_song_payload(a,b):
    """Only allow a PB revision that adds/removes the pinned presentation layer."""
    if a.get('source')!='pb' or b.get('source')!='pb':return False
    def core(p):
        p=dict(p);validate(p)
        art=p.pop('folder_art',{})
        omitted={r[k] for r in art.values() for k in ('label','caption')}
        p['files']={n:r for n,r in p['files'].items() if n not in omitted}
        for k in ('content_id','generator_revision','asset_map_hash'):p.pop(k,None)
        return p
    return core(a)==core(b)
