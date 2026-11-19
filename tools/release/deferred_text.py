"""Append transport and installed-base palette finalization. Standard library only.

Transport files are never game payloads. The immutable manifest names both their
hashes and the final game hashes. Activation happens only after materialization.
"""
import hashlib
from pathlib import Path

MAX_RESOURCE=16*1024*1024

def digest(data):return hashlib.sha256(data).hexdigest()

def resource_key(ref):return str(ref['offset'])+':'+str(ref['bytes'])+':'+ref['sha256']

def references(step):
    return [step[k] for k in ('source','palette') if k in step]

def valid_hash(h):return isinstance(h,str) and len(h)==64 and all(c in '0123456789abcdef' for c in h)

def valid_ref(ref):
    if not isinstance(ref,dict) or set(ref)!={'input','offset','bytes','sha256'} or ref['input'] not in ('iso','base') or type(ref['offset'])is not int or ref['offset']<0 or type(ref['bytes'])is not int or not 0<ref['bytes']<=MAX_RESOURCE or not valid_hash(ref['sha256']):
        raise ValueError('Invalid deferred text resource')

def input_path(h):
    if not valid_hash(h):raise ValueError('Invalid deferred input hash')
    return '.megamix-inputs/'+h

def transport(manifest):
    from megamix import relative
    info=manifest.get('deferred_text')
    if info is None:return {},{}
    if manifest['kind']!='append' or not isinstance(info,dict) or set(info)!={'schema','jobs'} or info['schema']!=1 or not info['jobs']:raise ValueError('Invalid deferred text manifest')
    inputs={}
    def add(h,n):
        if not valid_hash(h)or type(n)is not int or not 0<n<=MAX_RESOURCE:raise ValueError('Invalid deferred transport resource')
        p=input_path(h);row=dict(bytes=n,sha256=h)
        if p in inputs and inputs[p]!=row:raise ValueError('Conflicting deferred input')
        inputs[p]=row
    for name,step in info['jobs'].items():
        relative(name)
        if name not in manifest['files'] or step.get('op')!='text' or step.get('kind')not in ('native','sprite'):raise ValueError('Invalid deferred output')
        refs=references(step)
        if not refs or step['palette']['input']!='base':raise ValueError('Deferred text must use an installed base palette')
        for ref in refs:
            valid_ref(ref)
            if ref['input']=='iso':add(ref['sha256'],ref['bytes'])
        if step['kind']=='native':
            if step['source']['input']!='iso':raise ValueError('Native artwork must belong to the append ISO')
        else:
            add(step['atlas'],step['atlas_bytes'])
    return info['jobs'],inputs

def validate_base(manifest):
    from megamix import relative
    refs=manifest.get('text_bases',{})
    if refs and manifest['kind']!='base':raise ValueError('Only a base may supply text palettes')
    for key,row in refs.items():
        if set(row)!={'ref','path'}:raise ValueError('Invalid base text resource')
        ref=row['ref'];valid_ref(ref);relative(row['path'])
        if ref['input']!='base' or key!=resource_key(ref) or manifest['files'].get(row['path'])!=dict(bytes=ref['bytes'],sha256=ref['sha256']):raise ValueError('Base palette is not a verified installed file')

def pack_files(manifest):
    jobs,inputs=transport(manifest)
    out={n:r for n,r in manifest['files'].items() if n not in jobs}
    out.update(inputs)
    return out

def capture(stage,manifest,recipe,archive,read):
    from megamix import path_in,write_atomic,hash_file
    jobs,inputs=transport(manifest)
    for name,step in jobs.items():
        if recipe['files'].get(name)!=step:raise ValueError('Deferred job does not match recipe')
    refs={r['sha256']:r for s in jobs.values()for r in references(s)if r['input']=='iso'}
    for name,row in inputs.items():
        path=path_in(stage,name);h=row['sha256']
        if path.is_file()and path.stat().st_size==row['bytes']and hash_file(path)==h:continue
        if h in refs:
            r=refs[h];data=read(r['offset'],r['bytes'])
        else:data=archive.read('blobs/'+h)
        if len(data)!=row['bytes']or digest(data)!=h:raise ValueError('Deferred source hash mismatch: '+h)
        write_atomic(path,data)

def materialize(pack,root,manifest,base,progress=print,resolve=None):
    from megamix import path_in,write_atomic,hash_file,check_files
    from text_assets import Generator
    from text_raster import Atlas
    if resolve is None:resolve=lambda n:path_in(pack,n)
    jobs,inputs=transport(manifest)
    if not jobs:return None
    # Verify both sides before creating staging files or invalidating any cache.
    failures=check_files(pack,inputs,resolve)
    if failures:raise ValueError('Damaged deferred inputs: '+'; '.join(failures[:10]))
    required={resource_key(r):r for s in jobs.values()for r in references(s)if r['input']=='base'}
    bases={}
    for key,r in required.items():
        entry=base.get('text_bases',{}).get(key)
        if not entry or entry['ref']!=r:raise ValueError('Matching installed base palette is missing')
        data=path_in(root,entry['path']).read_bytes()
        if len(data)!=r['bytes']or digest(data)!=r['sha256']:raise ValueError('Installed base palette hash mismatch: '+entry['path'])
        bases[key]=data
    def read(which,at,size):
        # A single extent may never be given conflicting identities.
        refs=[r for s in jobs.values()for r in references(s)if r['input']==which and r['offset']==at and r['bytes']==size]
        hashes={r['sha256']for r in refs}
        if len(hashes)!=1:raise ValueError('Ambiguous deferred extent')
        ref=refs[0]
        return bases[resource_key(ref)] if which=='base' else resolve(input_path(ref['sha256'])).read_bytes()
    fonts={};generators={};stage=path_in(root,'.megamix/materialized/'+manifest['content_id'])
    for i,(name,step)in enumerate(sorted(jobs.items()),1):
        wanted=manifest['files'][name];path=path_in(stage,name)
        if path.is_file()and path.stat().st_size==wanted['bytes']and hash_file(path)==wanted['sha256']:continue
        h=step.get('atlas')if step['kind']=='sprite'else None
        if h and h not in fonts:fonts[h]=Atlas(resolve(input_path(h)).read_bytes())
        if h not in generators:generators[h]=Generator(fonts.get(h),read)
        data=generators[h].generate(step)
        if len(data)!=wanted['bytes']or digest(data)!=wanted['sha256']:raise ValueError('Finalized text hash mismatch: '+name)
        write_atomic(path,data)
        if i==1 or i%100==0:progress(f'Finalized {i}/{len(jobs)} text assets')
    return stage
