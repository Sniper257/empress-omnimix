"""Maintainer: refresh only paired precompiled launcher blobs in a qualified capsule."""
from pathlib import Path
import argparse,hashlib,json,os,zipfile
from megamix import canonical,content_id,hash_file

def refresh(stage):
    stage=Path(stage);capsule=stage/'release.mmr';plan=json.loads((stage/'release-plan.json').read_text());new=capsule.with_suffix('.launcher-refresh.incomplete');added={};recipes={};used=set();updates=[]
    with zipfile.ZipFile(capsule)as src:
        release=json.loads(src.read('release.json'));stats=json.loads(src.read('freeze-stats.json'))
        for disc in release['discs']:
            for target,name in disc['recipes'].items():
                r=json.loads(src.read(name));p=r['package']
                if disc['kind']=='base':
                    elf=stage/'launcher-build/sdk'/('MEGAMIX-packed.ELF'if target=='console'else'HOST.ELF');data=elf.read_bytes();sha=hashlib.sha256(data).hexdigest();added[sha]=data
                    p['files'].pop('LAUNCHER.ELF',None);r['files'].pop('LAUNCHER.ELF',None)
                    p['files']['MEGAMIX.ELF']=dict(bytes=len(data),sha256=sha);r['files']['MEGAMIX.ELF']=dict(op='blob',blob=sha,provenance='Precompiled folder-aware R158 launcher; R157 game/IOP unchanged')
                    p['activation_policy']=2;p['content_id']=content_id(p);updates.append(dict(target=target,bytes=len(data),sha256=sha,content_id=p['content_id']))
                recipes[name]=r;used.update(v['blob']for v in r['files'].values()if 'blob'in v)
                row=next(s for s in stats if s['source']==disc['source']and s['target']==target);row['bytes']=sum(f['bytes']for f in p['files'].values())
        release['tool_sources']={p.name:hash_file(p)for p in Path(__file__).parent.glob('*.py')}
        with zipfile.ZipFile(new,'x',zipfile.ZIP_DEFLATED,compresslevel=9)as out:
            for sha in sorted(used):
                data=added[sha]if sha in added else src.read('blobs/'+sha)
                if hashlib.sha256(data).hexdigest()!=sha:raise ValueError('Blob hash mismatch')
                out.writestr('blobs/'+sha,data)
            for name,r in recipes.items():out.writestr(name,canonical(r))
            out.writestr('release.json',canonical(release));out.writestr('freeze-stats.json',canonical(stats))
    with new.open('rb')as f:os.fsync(f.fileno())
    capsule.rename(stage/'release-before-folder-policy.mmr');new.rename(capsule)
    (stage/'launcher-refresh.json').write_text(json.dumps(dict(updates=updates,capsule_bytes=capsule.stat().st_size,sha256=hash_file(capsule)),indent=2)+'\n');print('Paired launcher refresh complete',updates,flush=True)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('stage',type=Path);a=p.parse_args();refresh(a.stage)
