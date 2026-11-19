"""Assemble a source-built, ISO-owned builder. No maintainer capsule input."""
from pathlib import Path
import hashlib,json,struct,sys,zipfile
HERE=Path(__file__).resolve().parent

def sha(b):return hashlib.sha256(b).hexdigest()
def canonical(v):return(json.dumps(v,sort_keys=True,separators=(',',':'),ensure_ascii=True)+'\n').encode()
def record(b):return dict(bytes=len(b),sha256=sha(b))
def fixed(z,name,data):
 info=zipfile.ZipInfo(name,(1980,1,1,0,0,0));info.compress_type=zipfile.ZIP_DEFLATED;info.create_system=3;info.external_attr=0o100644<<16;z.writestr(info,data,compresslevel=9)

def assemble(root,work,output):
 sys.path[:0]=[str(root/'tools/release'),str(root/'tools/offdisc'),str(root/'tools/cn'),str(root/'tools/e16')];from megamix import content_id,validate_manifest;import iso_launcher as L
 inputs=root/'release-inputs';release=json.loads((inputs/'release.json').read_bytes());recipes={p.relative_to(inputs).as_posix():json.loads(p.read_bytes())for p in sorted((inputs/'recipes').glob('*.json'))};blobs={p.name:p.read_bytes()for p in sorted((inputs/'blobs').iterdir())}
 for h,b in blobs.items():
  if sha(b)!=h:raise ValueError('Source input blob fingerprint mismatch: '+h)
 def add(b):h=sha(b);blobs[h]=b;return h
 compiled={};specs=json.loads((HERE/'launcher-resources.json').read_bytes());stub_elf=(work/'sdk/packer-source/ps2-packer/stub/unpacking-stub.elf').read_bytes();h=struct.unpack_from('<16sHHIIIIIHHHHHH',stub_elf);phs=[struct.unpack_from('<8I',stub_elf,h[5]+i*h[9])for i in range(h[10])];loads=[p for p in phs if p[0]==1 and p[4]]
 if len(loads)!=1:raise ValueError('Unexpected unpacker load layout')
 ph=loads[0];stub=stub_elf[ph[1]:ph[1]+ph[4]]
 for rn,r in recipes.items():
  p=r['package'];target=p.get('target')
  for n,s in list(r['files'].items()):
   if s['op']=='built':
    b=(work/s['artifact']).read_bytes();r['files'][n]=dict(op='blob',blob=add(b),provenance='Compiled from the corresponding public project source');p['files'][n]=record(b);compiled[target+':'+n]=record(b)
   elif s['op']=='built-launcher':
    spec=specs[target];template=(work/'sdk/ee'/(target+'-template.elf')).read_bytes()
    if record(template)!=spec['template']:raise ValueError('Launcher source build differs from its qualified template: '+target)
    # Every retail resource range must be initialized zeros inside a PT_LOAD.
    _,load=L.load_segments(template)
    for span in spec['recipe']['spans']:
     a,nbytes=span['at'],span['bytes']
     if not load[1]<=a<a+nbytes<=load[1]+load[4]or any(template[a:a+nbytes]):raise ValueError('Retail resource present or not file-backed in template')
    step=dict(op='launcher',template=add(template),recipe=spec['recipe'])
    if spec['packing']:
     if sha(stub)!=spec['packing']['stub_sha256']or ph[2]!=spec['packing']['stub_base']or h[4]!=spec['packing']['stub_entry']or ph[5]-ph[4]!=spec['packing']['stub_zero']:raise ValueError('Unpacker source build differs from qualified layout')
     step.update(stub=add(stub),packing=spec['packing'],memory=spec['memory'])
     p['derived_launcher']=dict(schema=1,template_sha256=spec['template']['sha256'],unpacked_sha256=spec['recipe']['unpacked_sha256'])
    r['files'][n]=step;p['files'][n]=spec['output']
  if p['kind']=='base':
   import patch_catalog,build_vdisc
   old=json.loads(blobs[r['files']['patch-catalog.json']['blob']]);patchdir=work/('patches-'+target);patchdir.mkdir(exist_ok=True)
   patch_catalog.write(work/'runtime'/(target+'-quiet')/'vdisc.txt',patchdir,build_vdisc,old['input_exe_sha256'],old['rating_changes'],bundled_custom_art=False)
   for n in('patch-catalog.json','PATCHES.md'):
    b=(patchdir/n).read_bytes();r['files'][n]=dict(op='blob',blob=add(b),provenance='Source-generated patch documentation');p['files'][n]=record(b)
  p['content_id']=content_id(p);validate_manifest(p)
 needed={s[k]for r in recipes.values()for s in r['files'].values()for k in('blob','atlas','template','stub','overlay')if k in s}
 if needed-blobs.keys():raise ValueError('Missing source-built capsule blobs')
 release['tool_sources']={p.relative_to(root/'tools/release').as_posix():sha(p.read_bytes())for p in sorted((root/'tools/release').glob('*.py'))}
 for field in('text_tool_sources','palette_tool_sources','bga_tool_sources','chart_tool_sources','font_tool_sources'):
  if field in release:release[field]={n:release['tool_sources'][n]for n in release[field]}
 release['ci_sources']=sha((root/'source-files.json').read_bytes())
 files={n:canonical(r)for n,r in recipes.items()};files['release.json']=canonical(release)
 for p in(inputs/'metadata').glob('*'):files[p.name]=p.read_bytes()
 for h in sorted(needed):files['blobs/'+h]=blobs[h]
 with zipfile.ZipFile(output,'x',zipfile.ZIP_DEFLATED,compresslevel=9)as z:
  for n,b in sorted(files.items()):fixed(z,n,b)
 return dict(capsule=record(output.read_bytes()),compiled=compiled,blobs=len(needed))
