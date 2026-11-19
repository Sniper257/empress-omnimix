"""Maintainer: freeze portable cache engines from the production launcher C.

python3 build_cache_engine.py release.mmr OUTPUT/cache-engine
Requires Zig; end users receive the compiled WASI engines and need no compiler.
"""
from pathlib import Path
import argparse,hashlib,json,shutil,subprocess,zipfile
ROOT=Path(__file__).resolve().parents[2]

def build(capsule,output):
    output=Path(output).resolve();output.mkdir(parents=True,exist_ok=True);targets={}
    with zipfile.ZipFile(capsule)as z:
        z_release=z.read('release.json')
        for target in ('console','host'):
            wasm=output/(target+'.wasm');flags=['-DPORTABLE_LAUNCHER','-DUSB_HID']if target=='console'else[]
            cmd=['zig','cc','-target','wasm32-wasi','-O2',*flags,'-I'+str(ROOT/'tools/offdisc'),'-Wl,--global-base=33554432','-Wl,-z,stack-size=1048576','-Wl,--initial-memory=67108864','-Wl,--max-memory=268435456',str(ROOT/'tools/offdisc/cache_builder.c'),str(ROOT/'tools/offdisc/launcher/cont.c'),'-o',str(wasm)]
            subprocess.run(cmd,check=True)
            recipe=json.loads(z.read('recipes/id-'+target+'.json'));package=recipe['package'];stem='vdisc-portable'if target=='console'else'vdisc'
            names=['MEGAMIX.ELF','EMPRESS/SLPM_552.21','catalog/charts.txt','catalog/sort.txt','launcher/'+stem+'.bin','launcher/'+stem+'.txt']
            targets[target]=dict(wasm=dict(bytes=wasm.stat().st_size,sha256=hashlib.sha256(wasm.read_bytes()).hexdigest()),files={n:package['files'][n]for n in names})
            if 'derived_launcher'in package:
                step=recipe['files']['MEGAMIX.ELF'];targets[target]['launcher']=dict(identity=package['derived_launcher'],memory=step['memory'],packing=step['packing'])
    notices=Path(__file__).with_name('cache-engine')/'licenses'
    if notices.is_dir()and notices.resolve()!=(output/'licenses').resolve():shutil.copytree(notices,output/'licenses',dirs_exist_ok=True)
    info=dict(schema=1,producer=json.loads(z_release)['version'],cache_abi='R171/wheel1/scalars1/alphabet1/coalesce1/listingfamily1/audit1',targets=targets)
    (output/'engine.json').write_text(json.dumps(info,indent=2)+'\n');return info
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('capsule',type=Path);p.add_argument('output',type=Path);a=p.parse_args();build(a.capsule,a.output)
