#!/usr/bin/env python3
"""Build the normal-user ZIP from public sources. Never accepts a private capsule."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess,sys
HERE=Path(__file__).resolve().parent;ROOT=HERE.parent
from audit import audit
from dependencies import compilers
from compile_sdk import build as sdk
from compile_runtime import build as runtime
from assemble import assemble
from preview_assets import build as preview

def build(work,output,deps,offline=False):
 proof=audit(ROOT);print('Public source audit passed',flush=True)
 work=Path(work).absolute();output=Path(output).absolute();deps=Path(deps).absolute()
 if work.exists():raise ValueError('Choose an unused build directory; build outputs are never reused')
 if output.exists():raise ValueError('Output already exists')
 work.mkdir(parents=True);toolchain=compilers(deps,offline)
 sdk(ROOT,work/'sdk',deps,offline);runtime(ROOT,work/'runtime',toolchain)
 p=work/'preview';p.mkdir()
 for name,b in preview(ROOT).items():(p/name).write_bytes(b)
 capsule=work/'release.mmr';proof.update(assemble(ROOT,work,capsule))
 env=dict(os.environ,PATH=str(toolchain/'zig')+os.pathsep+os.environ['PATH'],ZIG_GLOBAL_CACHE_DIR=str(work/'zig-global'),ZIG_LOCAL_CACHE_DIR=str(work/'zig-local'))
 engine=work/'package-tools/cache-engine';engine.parent.mkdir()
 # Package the exact consumer sources, not the build scripts or maintainer tools.
 import shutil
 for p in(ROOT/'tools/release').glob('*.py'):shutil.copy2(p,engine.parent/p.name)
 for n in('CACHE.txt','CACHE-ja.txt','CACHE-ko.txt','source_versions.json','requirements-fonts.txt'):shutil.copy2(ROOT/'tools/release'/n,engine.parent/n)
 for directory in('licenses','vendor','fonts'):shutil.copytree(ROOT/'tools/release'/directory,engine.parent/directory,ignore=shutil.ignore_patterns('__pycache__'))
 subprocess.run([sys.executable,str(ROOT/'tools/release/build_cache_engine.py'),str(capsule),str(engine)],env=env,check=True,stdout=(work/'cache-engine.log').open('w'),stderr=subprocess.STDOUT)
 shutil.copy2(ROOT/'tools/release/folder_glyphs.json',engine.parent/'folder_glyphs.json')
 output.parent.mkdir(parents=True,exist_ok=True)
 subprocess.run([sys.executable,str(engine.parent/'pack_release.py'),str(capsule),str(output),str(ROOT/'tools/release/README.txt')],check=True)
 audit(ROOT)
 proof['builder']=dict(bytes=output.stat().st_size,sha256=hashlib.sha256(output.read_bytes()).hexdigest());proof['hardware_acceptance']='pending';(work/'build-receipt.json').write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps(proof['builder'],indent=2));return proof

if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--work-dir',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--dependencies',type=Path,default=Path('.build-dependencies'));p.add_argument('--offline',action='store_true');a=p.parse_args();build(a.work_dir,a.output,a.dependencies,a.offline)
