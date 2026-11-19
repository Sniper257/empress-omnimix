"""Reject retail payloads and unaccounted files in the public source export."""
from pathlib import Path
import hashlib,json,zipfile
from source_tree import source_files
HERE=Path(__file__).resolve().parent

def sha(b):return hashlib.sha256(b).hexdigest()
def audit(root,check_manifest=True):
 root=Path(root);files=source_files(root)
 if any(p.is_file()and p.suffix.lower()in('.ttf','.otf','.ttc','.otc')for p in files):raise ValueError('Private font binaries must stay outside the public source export')
 inputs=root/'release-inputs';release=json.loads((inputs/'release.json').read_bytes());recipes={p.name:json.loads(p.read_bytes())for p in(inputs/'recipes').glob('*.json')}
 if len(release['discs'])!=16 or len(recipes)!=17:raise ValueError('Incomplete fixed-disc recipe set')
 uses={};counts={}
 for rn,r in recipes.items():
  p=r['package']
  if set(r['files'])!=set(p['files']):raise ValueError('Incomplete source recipe')
  for n,s in r['files'].items():
   op=s['op'];counts[op]=counts.get(op,0)+1
   if op not in('built','built-launcher','blob','text','copy','audio','bank','chart','bga','edit','container','startup'):raise ValueError('Unaudited operation: '+op)
   if op=='bank'and('blob'in s or'prefix_edit'not in s):raise ValueError('Bank header must come from its original ISO')
   if op=='blob':
    if n.startswith('a/')or n.endswith(('.bin','.if','.spr','.wvb','.pcm','.irx','.img','.ELF','.elf')):raise ValueError('Retail-shaped raw blob in source inputs: '+n)
    uses.setdefault(s['blob'],[]).append(n)
   if op=='startup':
    if p['source']!='id' or n!='a/2c/40':raise ValueError('Startup art must belong to Disc1')
    uses.setdefault(s['overlay'],[]).append('authored-transparent-startup-overlay')
   if 'atlas'in s:uses.setdefault(s['atlas'],[]).append('licensed-coverage-atlas')
   if op=='text'and s.get('kind')=='native'and s['source']['input']!='iso':raise ValueError('Native append artwork needs its owning ISO')
 for p in(inputs/'blobs').iterdir():
  b=p.read_bytes()
  if p.name not in uses or sha(b)!=p.name:raise ValueError('Unreferenced/corrupt source input')
  if b.startswith(b'MGA1'):
   if 'licensed-coverage-atlas'not in uses[p.name]:raise ValueError('Unknown compressed bitmap')
  elif b.startswith(b'PK'):
   if set(uses[p.name])!={'licenses/ps2-packer-source.zip'}:raise ValueError('Unknown binary archive')
   with zipfile.ZipFile(p)as z:
    if any(n.lower().endswith(('.elf','.o','.exe','.irx','.dll','.ttf','.otf'))for n in z.namelist()):raise ValueError('Non-source packer archive')
  else:b.decode('utf-8-sig')
 if set(uses)!={p.name for p in(inputs/'blobs').iterdir()}:raise ValueError('Missing source input')
 if check_manifest:
  expected=json.loads((root/'source-files.json').read_bytes());actual={p.relative_to(root).as_posix():sha(p.read_bytes())for p in files if p.name!='source-files.json'}
  if actual!=expected:raise ValueError('Public source changed or contains untracked files; refresh its reviewed manifest')
 return dict(schema=1,retail_payloads=0,recipes=len(recipes),sources=len(release['discs']),input_blobs=len(uses),operations=counts)

if __name__=='__main__':
 print(json.dumps(audit(HERE.parent),indent=2))
