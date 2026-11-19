"""Maintainer: build the actual independently extracted/installed consumer tree."""
from pathlib import Path
import json,os,struct,subprocess,sys
import megamix as M
import validate_native as N

def validate(stage,harness):
    stage=Path(stage).resolve();root=stage/'consumer-base';state=json.loads((root/'.megamix/installed.json').read_text());active=M.active_songs(state);expected=root/'.expected-active.json';prefix=stage/'materialized-native'
    expected.write_text(json.dumps([r['style']+'/'+r['dir']for r in active]))
    try:
        env=dict(os.environ,CONFIG='1',STRICT_CONFIG='1',GAME_ROOT='mmce0:/MEGAMIX/',SETTINGS_ROOT='mmce0:/MEGAMIX/',USB_BIND_TEST='1')
        with(stage/'materialized-native.log').open('w')as f:r=subprocess.run([str(Path(harness).resolve()),str(root),str(prefix)],env=env,stdout=f,stderr=subprocess.STDOUT)
        if r.returncode:raise ValueError('Materialized consumer native build failed')
        data=N.C.load(stage/'release-catalog.json');N.T.verify(root,prefix,data,runtime='vdisc-portable');a=N.A.audit(root,prefix.with_suffix('.mem'),runtime='vdisc-portable')
        (stage/'materialized-native-workspace.json').write_text(json.dumps(a,indent=2)+'\n')
        text=(root/'launcher/vdisc-portable.txt').read_text();at=int(next(l.split()[1]for l in text.splitlines()if l.startswith('asset_memory ')),16);mem=prefix.with_suffix('.mem').read_bytes();words=struct.unpack_from('<13I',mem,at-0x100000)
        assert words[7]==0x902a80 and words[8]==269440 and not words[9]
        report=dict(real_ISOs_extracted=['id','red','third'],actual_consumer_tree=True,active_rows=len(active),MPEG=words[8],texture_tail=words[11],catalog_and_reference_paths_verified=True,USB_ABI_verified=True)
        (stage/'materialized-native-validation.json').write_text(json.dumps(report,indent=2)+'\n');print('Materialized native PASS',report,flush=True);return report
    finally:expected.unlink(missing_ok=True)

if __name__=='__main__':validate(sys.argv[1],sys.argv[2])
