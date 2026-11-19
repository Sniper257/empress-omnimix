"""Maintainer: replace audio deltas in an existing capsule with hash-proven recipes.
This is optional for new capsules: freeze_release now emits these operations itself.
"""
from pathlib import Path
import argparse,json,struct,zipfile,os,time,hashlib
from audio_transform import convert,NAMES,legacy_bank_recipe,assemble_bank
from megamix import canonical,hash_bytes,hash_file,Extent
import io

def optimize(stage):
    stage=Path(stage);capsule=stage/'release.mmr';plan=json.loads((stage/'release-plan.json').read_text());inputs={d['source']:d['image']for d in plan['discs']};new=capsule.with_suffix('.optimized.incomplete');reports=[]
    with zipfile.ZipFile(capsule)as src:
        release=json.loads(src.read('release.json'));stats=json.loads(src.read('freeze-stats.json'));recipes={};used=set();added={}
        for disc in release['discs']:
            changed=0;saved=0;proofs=[]
            with Path(inputs[disc['source']]).open('rb')as iso:
                size=os.fstat(iso.fileno()).st_size
                for target,name in disc['recipes'].items():
                    recipe=json.loads(src.read(name));count=next(r for r in stats if r['source']==disc['source']and r['target']==target);count.setdefault('audio',0);count.setdefault('bank',0)
                    for path,step in recipe['files'].items():
                        if step['op']not in ('xor','patch')or step['source_bytes']>64*1024*1024:continue
                        expected=recipe['package']['files'][path];view=Extent(iso,step['offset'],step['source_bytes'],size);raw=view.read(step['source_bytes'])
                        # Derive sample spans from the original format; keep only the
                        # frozen metadata header in the capsule. Hash proves all audio bytes.
                        target_path=plan['discs'][next(i for i,d in enumerate(plan['discs'])if d['source']==disc['source'])]['targets'][target]['files'][path]['path']
                        try:
                            prefix,spans=legacy_bank_recipe(raw,Path(target_path).read_bytes());h=hashlib.sha256()
                            for chunk in assemble_bank(prefix,io.BytesIO(raw),spans,expected['bytes']):h.update(chunk)
                            if h.hexdigest()==expected['sha256']:
                                info=src.getinfo('blobs/'+step['blob']);count[step['op']]-=1;count['bank']+=1;count['patch_bytes']-=info.file_size
                                ident=hash_bytes(prefix);added[ident]=prefix
                                recipe['files'][path]=dict(op='bank',blob=ident,spans=spans,offset=step['offset'],source_bytes=step['source_bytes']);changed+=1;saved+=info.compress_size;proofs.append(dict(path=path,transform='legacy-bank-v3',sha256=expected['sha256']));continue
                        except (ValueError,struct.error):pass
                        for kind in NAMES:
                            try:converted=convert(kind,raw,expected['bytes'])
                            except (ValueError,struct.error):continue
                            if hash_bytes(converted)!=expected['sha256']:continue
                            info=src.getinfo('blobs/'+step['blob']);count[step['op']]-=1;count['audio']+=1;count['patch_bytes']-=info.file_size
                            recipe['files'][path]=dict(op='audio',transform=kind,offset=step['offset'],source_bytes=step['source_bytes']);changed+=1;saved+=info.compress_size;proofs.append(dict(path=path,transform=kind,sha256=expected['sha256']));break
                    recipes[name]=recipe
                    used.update(r['blob']for r in recipe['files'].values()if 'blob'in r)
            reports.append(dict(source=disc['source'],conversions=changed,removed_encoded_bytes=saved,proofs=proofs));print('Audio recipe proof',disc['source'],changed,flush=True)
        release['tool_sources']={p.name:hash_file(p)for p in Path(__file__).parent.glob('*.py')}
        with zipfile.ZipFile(new,'x',zipfile.ZIP_DEFLATED,compresslevel=9)as out:
            for ident in sorted(used):out.writestr('blobs/'+ident,added[ident]if ident in added else src.read('blobs/'+ident))
            for name,recipe in recipes.items():out.writestr(name,canonical(recipe))
            out.writestr('release.json',canonical(release));out.writestr('freeze-stats.json',canonical(stats))
    before=capsule.stat().st_size
    with new.open('rb')as f:os.fsync(f.fileno())
    backup=stage/'release-before-audio-optimization.mmr';capsule.rename(backup);new.rename(capsule)
    receipt=dict(before_bytes=before,after_bytes=capsule.stat().st_size,sha256=hash_file(capsule),converted_files=sum(r['conversions']for r in reports),sources=reports)
    (stage/'capsule-optimized.json').write_text(json.dumps(receipt,indent=2)+'\n');print('Audio optimized capsule',before,'->',receipt['after_bytes'],flush=True);return receipt

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('stage',type=Path);a=p.parse_args();optimize(a.stage)
