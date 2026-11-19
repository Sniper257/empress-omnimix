"""Optional, local folder fonts. Default builds never import font libraries."""
from contextlib import contextmanager
from pathlib import Path
import copy,hashlib,json,re,struct,tempfile

FAMILIES=('compacta','eurostile','handel','helvetica','gothic','mincho')
EXTENSIONS={'.ttf','.otf','.ttc','.otc'}
PROFILE_PATH='.megamix/fonts/profile.json'
ATLAS_PATH='.megamix/fonts/coverage.mga'
MAX_DOCUMENT=8*1024*1024

def sha(b):return hashlib.sha256(b).hexdigest()
def canonical(x):return json.dumps(x,sort_keys=True,separators=(',',':'),ensure_ascii=True).encode()
def record(b):return dict(bytes=len(b),sha256=sha(b))
def valid_hash(s):return isinstance(s,str)and len(s)==64 and all(c in'0123456789abcdef'for c in s)

def scan(root,explicit=True):
    root=Path(root)
    if not root.is_dir():
        if explicit:raise ValueError('Font folder does not exist: '+str(root))
        return {}
    out={};unknown=[]
    for directory in sorted(root.iterdir(),key=lambda p:(p.name.casefold(),p.name)):
        if not directory.is_dir():continue
        paths=sorted((p for p in directory.rglob('*')if p.is_file()and p.suffix.lower()in EXTENSIONS),key=lambda p:(p.as_posix().casefold(),p.as_posix()))
        if not paths:continue
        family=directory.name.casefold()
        if family not in FAMILIES:unknown.append(directory.name);continue
        if family in out:raise ValueError('Duplicate font family folder: '+family)
        out[family]=paths
    loose=[p.name for p in root.iterdir()if p.is_file()and p.suffix.lower()in EXTENSIONS]
    if unknown or loose:raise ValueError('Put fonts in the named family subfolders ('+', '.join(FAMILIES)+'). Unrecognized: '+', '.join(unknown+loose))
    if explicit and not out:raise ValueError('No fonts found. Put .ttf/.otf/.ttc files in '+str(root/'compacta')+' or another named family folder.')
    return out

def score(face,weight):
    # Normal width/upright first. File names do not decide which weight is used.
    return (bool(face['italic']),abs(face['width']-5),0 if face.get('variable_weight') else abs(face['weight']-weight),face['face'],face['name'].casefold(),face['sha256'])

def inspect(paths):
    try:from fontTools.ttLib import TTFont
    except ImportError as e:raise ValueError('Custom fonts need Pillow and fontTools. Run: python -m pip install Pillow fonttools')from e
    out=[];seen=set()
    for path in paths:
        data=path.read_bytes();digest=sha(data)
        count=struct.unpack_from('>I',data,8)[0]if data[:4]==b'ttcf'and len(data)>=12 else 1
        if not 1<=count<=64:raise ValueError('Unsupported font collection: '+str(path))
        for face in range(count):
            if(digest,face)in seen:continue
            seen.add((digest,face))
            try:
                with TTFont(path,fontNumber=face)as tt:
                    names=tt['name'];family=names.getDebugName(16)or names.getDebugName(1)or path.stem;style=names.getDebugName(17)or names.getDebugName(2)or'Regular';os2=tt.get('OS/2');head=tt.get('head')
                    axes=[(a.axisTag,a.minValue,a.defaultValue,a.maxValue)for a in tt['fvar'].axes]if'fvar'in tt else []
                    weight=int(os2.usWeightClass)if os2 else 400
                    named=re.search(r'(?:^|[- ])W([1-9])(?:$|[- ])',family+' '+style,re.I)
                    if weight==400 and named:weight=int(named.group(1))*100
                    out.append(dict(file=str(path.resolve()),face=face,sha256=digest,name=family+' '+style,weight=weight,width=int(os2.usWidthClass)if os2 else 5,italic=bool((os2.fsSelection&1)if os2 else(head.macStyle&2)if head else 0),axes=axes,variable_weight=any(a[0]=='wght'for a in axes)))
            except Exception as e:raise ValueError('Could not read font '+str(path)+' (face '+str(face)+'): '+str(e))from e
    return out

def folder_profile(families,progress=print):
    faces={k:inspect(v)for k,v in families.items()};fonts={};roles={};selected={}
    def choose(family,weight,japanese=False):
        key=family+'_'+str(weight)
        if key not in fonts:
            f=min(faces[family],key=lambda f:score(f,weight));row={k:f[k]for k in('file','face','sha256')}
            if f['axes']:row['axes']=[max(lo,min(hi,weight if tag=='wght'else default))for tag,lo,default,hi in f['axes']]
            if japanese:row['japanese']=True
            fonts[key]=row;selected[key]=dict(family=family,name=f['name'],file=Path(f['file']).name,face=f['face'],sha256=f['sha256'],axes=row.get('axes'))
            progress('Font '+family+': '+f['name']+' ('+Path(f['file']).name+')')
        return key
    display='handel'if'handel'in faces else'eurostile'if'eurostile'in faces else None
    for role in('wheel','large_title','genre_artist','card_genre','card_artist','launcher'):
        family='compacta'if role=='large_title'and'compacta'in faces else'helvetica'if role in('genre_artist','card_artist')and'helvetica'in faces else display if role in('wheel','card_genre','launcher')else None
        weight=300 if role=='card_genre'and family=='handel'else 400 if family in('compacta','handel')else 700
        latin=[choose(family,weight)]if family else []
        jp='mincho'if role in('large_title','card_artist')and'mincho'in faces else'gothic'if'gothic'in faces else None
        jpweight=700 if role in('large_title','genre_artist')else 300 if role=='card_genre'else 500
        roles[role]=dict(latin=latin,japanese=[choose(jp,jpweight,True)]if jp else [])
    return dict(format=1,name='local-folder-fonts',fonts=fonts,roles=roles),selected

def generated(step):
    return step.get('op')=='text'and(step.get('kind')in('card','sprite')or(step.get('kind')=='folder'and step.get('layout')=='pb_caption'))

def catalog(archive):
    recipes={n:json.loads(archive.read(n))for n in sorted(archive.namelist())if n.startswith('recipes/')and n.endswith('.json')};sources={};pooled={};fixed=set();strings=[];atlas=None
    for rn,r in recipes.items():
        source=r['package']['source'];jobs=sources.setdefault(source,{})
        for name,step in r['files'].items():
            if generated(step):
                s=copy.deepcopy(step)
                for k in('source','palette'):
                    if k in s and s[k]['input']=='iso':
                        if source!='id':raise ValueError('Generated font art needs an owning base palette')
                        s[k]['input']='base'
                entry=dict(step=s,default=r['package']['files'][name]);jobs[name]=entry
                if name.startswith('a/'):
                    if name in pooled and pooled[name]['default']!=entry['default']:raise ValueError('Conflicting shared font artwork')
                    pooled.setdefault(name,entry)
                strings.extend(step.get('texts',{}).values())
                if'text'in step:strings.append(step['text'])
                if'atlas'in step:
                    if atlas is not None and atlas!=step['atlas']:raise ValueError('Mixed default font atlases')
                    atlas=step['atlas']
            elif name.startswith('a/'):fixed.add(name)
    for jobs in sources.values():
        for name in list(jobs):
            if name in fixed:del jobs[name]
            elif name in pooled:jobs[name]=copy.deepcopy(pooled[name])
    return sources,sorted(set(strings)),atlas

class OverlayArchive:
    def __init__(self,archive,extras):self.archive=archive;self.extras=extras
    def read(self,name):return self.extras.get(name)if name in self.extras else self.archive.read(name)

def prepare(archive,recipe,folder,progress=print):
    """Pure metadata overlay; default expected hashes remain strict elsewhere."""
    from prepare_text_fonts import compile_profile
    families=scan(folder);profile,selected=folder_profile(families,progress);sources,strings,default_atlas=catalog(archive)
    if default_atlas is None:raise ValueError('This release has no customizable font atlas')
    fallback=archive.read('blobs/'+default_atlas)
    if sha(fallback)!=default_atlas:raise ValueError('Damaged bundled font coverage')
    progress('Preparing local font coverage...')
    with tempfile.TemporaryDirectory(prefix='megamix-fonts-')as tmp:
        tmp=Path(tmp);(tmp/'profile.json').write_bytes(canonical(profile));(tmp/'corpus.json').write_bytes(canonical(strings));(tmp/'fallback.mga').write_bytes(fallback)
        compile_profile(tmp/'profile.json',tmp/'corpus.json',tmp/'custom.mga',tmp/'fallback.mga');atlas=(tmp/'custom.mga').read_bytes()
    if len(atlas)>16*1024*1024:raise ValueError('Compiled custom font coverage exceeds16 MiB')
    h=sha(atlas);r=copy.deepcopy(recipe);p=r['package'];extras={};names=set()
    if p['kind']=='append':
        p['font_request']=dict(schema=1,id=h)
        progress('Append pack will use this font profile when installed into a matching base.')
    else:
        for jobs in sources.values():
            for entry in jobs.values():entry['step'].update(atlas=h,atlas_bytes=len(atlas))
        doc=canonical(dict(schema=1,id=h,contract=p['contract'],selected=selected,sources=sources))
        if len(doc)>MAX_DOCUMENT:raise ValueError('Font recipe catalog exceeds bounds')
        info=dict(schema=1,id=h,profile=PROFILE_PATH,atlas=ATLAS_PATH);p['font_profile']=info
        originals={n:copy.deepcopy(p['files'][n])for n in sources['id']};p['font_rendered']=dict(schema=1,id=h,originals=originals)
        for name,b in((PROFILE_PATH,doc),(ATLAS_PATH,atlas)):
            q=sha(b);extras['blobs/'+q]=b;r['files'][name]=dict(op='blob',blob=q);p['files'][name]=record(b)
        extras['blobs/'+h]=atlas
        for n,e in sources['id'].items():r['files'][n]=copy.deepcopy(e['step']);names.add(n)
    from megamix import content_id
    p['content_id']=content_id(p)
    return OverlayArchive(archive,extras),r,names

def validate(manifest):
    from megamix import relative
    for field in('font_request','font_rendered','font_profile'):
        info=manifest.get(field)
        if info is None:continue
        if not isinstance(info,dict)or info.get('schema')!=1 or not valid_hash(info.get('id')):raise ValueError('Invalid custom font receipt')
        if field=='font_profile':
            if manifest['kind']!='base' or set(info)!={'schema','id','profile','atlas'}or(info['profile'],info['atlas'])!=(PROFILE_PATH,ATLAS_PATH):raise ValueError('Invalid base font profile')
            if PROFILE_PATH not in manifest['files']or manifest['files'].get(ATLAS_PATH,{}).get('sha256')!=info['id']:raise ValueError('Unsealed base font profile')
        if field=='font_request'and(manifest['kind']!='append'or set(info)!={'schema','id'}):raise ValueError('Invalid append font request')
        if field=='font_rendered':
            if set(info)!={'schema','id','originals'}or not isinstance(info['originals'],dict):raise ValueError('Invalid font output receipt')
            for n,row in info['originals'].items():
                relative(n)
                # Generated, source-owned title cards may be loose files rather
                # than pooled a/ assets. Do not accept arbitrary .if binaries.
                card=n.startswith('songs/')and n.endswith('/card.if')and len(n.split('/'))==4
                if n not in manifest['files']or not(n.startswith('a/')or n.endswith('.spr')or card)or set(row)!={'bytes','sha256'}or type(row['bytes'])is not int or row['bytes']<0 or not valid_hash(row['sha256']):raise ValueError('Invalid original font-art receipt')
            if manifest['kind']=='base'and manifest.get('font_profile',{}).get('id')!=info['id']:raise ValueError('Base font profile disagrees with its outputs')

def original_row(manifest,name):return manifest.get('font_rendered',{}).get('originals',{}).get(name,manifest['files'][name])

def load_profile(root,base):
    from megamix import path_in,check_files
    info=base['font_profile'];validate(base)
    failures=check_files(root,{n:base['files'][n]for n in(PROFILE_PATH,ATLAS_PATH)})
    if failures:raise ValueError('Base font profile is damaged: '+'; '.join(failures))
    b=path_in(root,PROFILE_PATH).read_bytes()
    if len(b)>MAX_DOCUMENT:raise ValueError('Font recipe catalog exceeds bounds')
    doc=json.loads(b);atlas=path_in(root,ATLAS_PATH).read_bytes()
    if doc.get('schema')!=1 or doc.get('id')!=info['id']or doc.get('contract')!=base['contract']:raise ValueError('Font profile belongs to a different base')
    return doc,atlas

@contextmanager
def normalized_pack(pack,root,manifest,base,progress=print):
    """Render a small overlay; large chart/audio files stay in the original pack."""
    from megamix import path_in,content_id,validate_manifest
    info=base.get('font_profile');request=manifest.get('font_request');prior=manifest.get('font_rendered')
    if not info:
        if request or prior:raise ValueError('This append needs a base created with the same --fonts folder')
        yield manifest,lambda n:path_in(pack,n);return
    if request and request['id']!=info['id']or prior and prior['id']!=info['id']:raise ValueError('Append and base use different fonts. Rebuild the append without --fonts to inherit the base fonts.')
    doc,atlas=load_profile(root,base);plan=doc['sources'].get(manifest['source'],{});jobs={n:e for n,e in plan.items()if n in manifest['files']}
    # Original bytes/transport are checked even if they will be replaced.
    from deferred_text import pack_files
    from megamix import check_files
    failures=check_files(pack,pack_files(manifest)if manifest.get('deferred_text')else manifest['files'])
    if failures:raise ValueError('Damaged append pack: '+'; '.join(failures[:10]))
    if prior and(set(prior['originals'])!=set(jobs)):raise ValueError('Append custom font outputs do not match the base recipe catalog')
    for n,e in jobs.items():
        if original_row(manifest,n)!=e['default']:raise ValueError('Font recipe does not match append source: '+n)
        if n.startswith('a/')and base['asset_map'].get(n)!=e['default']['sha256']:raise ValueError('Font recipe is outside the published asset table')
    if not jobs:
        m=copy.deepcopy(manifest);m.pop('font_request',None);m['font_rendered']=dict(schema=1,id=info['id'],originals={});m['content_id']=content_id(m);validate_manifest(m)
        yield m,lambda n:path_in(pack,n);return
    from text_assets import Generator
    from text_raster import Atlas
    from deferred_text import resource_key
    def read(which,at,size):
        if which!='base':raise ValueError('Font artwork cannot borrow another disc')
        matches=[v for v in base.get('text_bases',{}).values()if v['ref']['offset']==at and v['ref']['bytes']==size]
        if len(matches)!=1:raise ValueError('Matching base text palette is missing')
        row=matches[0];b=path_in(root,row['path']).read_bytes()
        if record(b)!=base['files'][row['path']]:raise ValueError('Installed base palette hash mismatch')
        return b
    generator=Generator(Atlas(atlas),read);m=copy.deepcopy(manifest);m.pop('font_request',None);m['font_rendered']=dict(schema=1,id=info['id'],originals={n:e['default']for n,e in jobs.items()})
    with tempfile.TemporaryDirectory(prefix='megamix-font-pack-')as tmp:
        tmp=Path(tmp);overrides={}
        def put(n,b):p=tmp/n;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(b);overrides[n]=p
        deferred=m.get('deferred_text',{}).get('jobs',{})
        if any(n in deferred for n in jobs):
            from deferred_text import input_path
            put(input_path(info['id']),atlas)
        for i,(n,e)in enumerate(sorted(jobs.items()),1):
            b=generator.generate(e['step']);m['files'][n]=record(b)
            if prior and record(b)!=manifest['files'][n]:raise ValueError('Previously rendered font output differs: '+n)
            if n in deferred:deferred[n]=copy.deepcopy(e['step'])
            else:put(n,b)
            if i==1 or i%100==0:progress('Rendered '+str(i)+'/'+str(len(jobs))+' custom font assets')
        m['content_id']=content_id(m);validate_manifest(m)
        yield m,lambda n:overrides[n]if n in overrides else path_in(pack,n)
