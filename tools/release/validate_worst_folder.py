"""Maintainer: native full build at the finite folder-mask row-count maximum."""
from pathlib import Path
import json,os,struct,subprocess,sys
import megamix as M
import validate_native as N

def validate(stage,harness):
    stage=Path(stage).resolve();plan=json.loads((stage/'release-plan.json').read_text());bound=json.loads((stage/'folder-policy-validation.json').read_text());mask=int(bound['max_mask'],16);root=stage/'native-validation-folder/all';prefix=stage/'worst-folder'
    styles=(root/'songs/styles.txt').read_text().splitlines();enabled={s for i,s in enumerate(styles)if mask>>i&1}
    pk={d['source']:{'songs':d['targets']['console'if d['source']=='id'else'append']['songs']}for d in plan['discs']};pk['id']['selection']=plan['selection'];active=M.active_songs({'packages':pk},enabled);expected=root/'.expected-active.json';before=expected.read_bytes()
    try:
        expected.write_text(json.dumps([r['style']+'/'+r['dir']for r in active]))
        env=dict(os.environ,CONFIG='1',STRICT_CONFIG='1',GAME_ROOT='mmce0:/MEGAMIX/',SETTINGS_ROOT='mmce0:/MEGAMIX/',STYLES_MASK=format(mask,'x'),USB_BIND_TEST='1')
        with(stage/'worst-folder.log').open('w')as f:r=subprocess.run([str(Path(harness).resolve()),str(root),str(prefix)],env=env,stdout=f,stderr=subprocess.STDOUT)
        if r.returncode:raise ValueError('Worst-folder native build failed')
        data=N.C.load(stage/'release-catalog.json');N.T.verify(root,prefix,data,runtime='vdisc-portable');audit=N.A.audit(root,prefix.with_suffix('.mem'),runtime='vdisc-portable');(stage/'worst-folder-workspace.json').write_text(json.dumps(audit,indent=2)+'\n')
        text=(root/'launcher/vdisc-portable.txt').read_text();at=int(next(l.split()[1]for l in text.splitlines()if l.startswith('asset_memory ')),16);mem=prefix.with_suffix('.mem').read_bytes();words=struct.unpack_from('<13I',mem,at-0x100000)
        assert words[7]==0x902a80 and words[8]==269440 and not words[9]and words[11]>=135168
        report=dict(mask=hex(mask),active_sources=len(active),bounded_resident_rows=bound['max_resident_rows'],MPEG=words[8],texture_tail=words[11],catalog_verified=True,all_reference_paths_verified=True)
        (stage/'worst-folder-validation.json').write_text(json.dumps(report,indent=2)+'\n');print('Worst folder native PASS',report,flush=True);return report
    finally:expected.write_bytes(before)

if __name__=='__main__':validate(sys.argv[1],sys.argv[2])
