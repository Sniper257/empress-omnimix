"""Fetch and verify pinned open-source build dependencies (CI/maintainers only)."""
from pathlib import Path
import hashlib,json,os,shutil,subprocess,tarfile,urllib.request
HERE=Path(__file__).resolve().parent

def digest(path):
 h=hashlib.sha256()
 with path.open('rb')as f:
  while b:=f.read(8*1024*1024):h.update(b)
 return h.hexdigest()

def archive(root,key,offline=False):
 spec=json.loads((HERE/'dependencies.json').read_text())['archives'][key];p=root/spec['file'];root.mkdir(parents=True,exist_ok=True)
 if not p.is_file():
  if offline:raise ValueError('Missing pinned dependency: '+str(p))
  tmp=p.with_suffix(p.suffix+'.incomplete')
  with urllib.request.urlopen(spec['url'],timeout=90)as src,tmp.open('wb')as out:shutil.copyfileobj(src,out,1024*1024)
  if tmp.stat().st_size!=spec['bytes']or digest(tmp)!=spec['sha256']:raise ValueError('Downloaded dependency fingerprint mismatch: '+key)
  os.replace(tmp,p)
 if p.stat().st_size!=spec['bytes']or digest(p)!=spec['sha256']:raise ValueError('Dependency fingerprint mismatch: '+key)
 return p

def unpack(path,out,mode='r:*',select=None):
 out.mkdir(parents=True,exist_ok=False)
 with tarfile.open(path,mode)as tf:
  for m in tf:
   q=m.name.split('/',1)
   if len(q)<2 or not q[1]:continue
   m.name=q[1]
   if select is None or select(m.name):tf.extract(m,out,filter='data')

def compilers(root,offline=False):
 # The source distribution stores no compiler binaries. A cached extracted tree
 # is accepted only if every file matches its extraction receipt.
 paths={k:archive(root,k,offline)for k in('llvm','zig')};tool=root/'toolchain';receipt=tool/'files.json'
 if receipt.is_file():
  rows=json.loads(receipt.read_text())
  if all((tool/n).is_file()and digest(tool/n)==h for n,h in rows.items()):return tool
 if tool.exists():shutil.rmtree(tool)
 tool.mkdir()
 proc=subprocess.Popen(['zstd','-d','-c','--memory=2048MB',str(paths['llvm'].resolve())],stdout=subprocess.PIPE)
 def selected(p):return p in('bin/clang','bin/clang-23','bin/ld.lld','bin/lld','bin/llvm-objcopy','bin/llvm-nm','bin/llvm-objdump','bin/llvm-readobj')or p.startswith('lib/clang/23/include/')or p.startswith('lib/')and'.so'in p and p.count('/')<2
 try:
  with tarfile.open(fileobj=proc.stdout,mode='r|')as tf:
   for m in tf:
    q=m.name.split('/',1)
    if len(q)<2 or not selected(q[1]):continue
    m.name=q[1];tf.extract(m,tool/'llvm',filter='data')
 finally:
  proc.stdout.close();status=proc.wait()
 if status:raise ValueError('LLVM archive extraction failed')
 unpack(paths['zig'],tool/'zig')
 receipt.write_text(json.dumps({p.relative_to(tool).as_posix():digest(p)for p in sorted(tool.rglob('*'))if p.is_file()},sort_keys=True)+'\n')
 return tool
