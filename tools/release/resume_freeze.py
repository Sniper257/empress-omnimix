"""Maintainer: reuse complete, checked recipe checkpoints after an interrupted freeze.
Only complete targets matching the current plan are reused; unfinished blobs are
ignored. The consumer and final all-disc package format remain unchanged.
"""
from pathlib import Path
import argparse,json,zipfile,os
from megamix import canonical,hash_bytes,hash_file,validate_manifest
from freeze_release import freeze

def matches(plan,disc,target,tree,recipe):
    p=validate_manifest(recipe['package'])
    if p['contract']!=plan['contract']or p['source']!=disc['source']or p['target']!=target or p['kind']!=disc['kind']or p['name']!=disc['name']or p['disc']!=disc['iso']or p['songs']!=tree['songs']:return False
    if p['style_order']!=plan['style_order']or p['payload_prefixes']!=tree.get('payload_prefixes',[]):return False
    if p.get('runtime_revision',p.get('generator_revision'))!=plan['version']:return False
    if any(p.get(k)!=tree.get(k) for k in ('folder_presentation','folder_art','song_versions','alphabet_policy','coalesce_policy','text_bases')):return False
    if p.get('asset_map_hash')!=hash_bytes(canonical(plan['asset_map'])):return False
    if disc['kind']=='base'and(p.get('selection')!=plan.get('selection')or p.get('asset_map')!=plan['asset_map']):return False
    if set(p['files'])!=set(tree['files'])or set(recipe['files'])!=set(tree['files']):return False
    for name,row in tree['files'].items():
        path=Path(row['path']);expected=p['files'][name];wanted=plan['asset_map'].get(name)if name.startswith('a/')else hash_file(path)
        if path.stat().st_size!=expected['bytes']or wanted!=expected['sha256']:return False
        step=recipe['files'][name]
        if 'text'in row:
            expected_step=dict(row['text']['spec'],op='text')
            if expected_step['kind']!='native':
                expected_step['atlas']=hash_file(row['text']['atlas'])
                if disc['kind']=='append'and expected_step.get('palette',{}).get('input')=='base':expected_step['atlas_bytes']=Path(row['text']['atlas']).stat().st_size
            if step!=expected_step:return False
        elif step['op']=='text':return False
        elif 'derive'in row:
            if step!=row['derive']:return False
        elif step['op']=='bga':return False
        elif step['op']!='blob':
            if 'origin'not in row or step['offset']!=row['origin'][0]:return False
            if step['op']!='copy'and step['source_bytes']!=row['origin'][1]:return False
    deferred={n:s for n,s in recipe['files'].items()if disc['kind']=='append'and s['op']=='text'and s.get('palette',{}).get('input')=='base'}
    if p.get('deferred_text')!=(dict(schema=1,jobs=deferred)if deferred else None):return False
    return True

def resume(plan_path,checkpoint,output,registry=None):
    plan_path,checkpoint,output=map(Path,(plan_path,checkpoint,output));stage=plan_path.parent;plan=json.loads(plan_path.read_bytes())
    if output.exists():raise ValueError('Final output exists')
    if registry:
        fixed={d['source']:d for d in json.loads(Path(registry).read_bytes())['discs']}
        for d in plan['discs']:
            f=fixed[d['source']]
            if(d['iso']['sha256'],d['iso']['bytes'])!=(f['iso_sha256'],f['iso_bytes']):raise ValueError('Unpinned source')
    remaining=dict(plan,discs=[]);reused={}
    with zipfile.ZipFile(checkpoint)as old:
        for disc in plan['discs']:
            todo={}
            for target,tree in disc['targets'].items():
                name='recipes/'+disc['source']+'-'+target+'.json'
                if name in old.namelist():
                    recipe=json.loads(old.read(name))
                    if matches(plan,disc,target,tree,recipe):reused[name]=recipe;print('Reusing verified target',disc['source'],target,flush=True);continue
                todo[target]=tree
            if todo:remaining['discs'].append(dict(disc,targets=todo))
    tail=stage/'release-remaining.mmr';tailplan=stage/'release-remaining-plan.json';tailplan.write_text(json.dumps(remaining,indent=2)+'\n')
    if remaining['discs']:
        if not tail.exists():freeze(tailplan,tail,registry)
    recipes=dict(reused);release={'schema':1,'version':plan['version'],'contract':plan['contract'],'discs':[]};stats=[];needed=set()
    with zipfile.ZipFile(checkpoint)as old:
        old_names=set(old.namelist())
        new=zipfile.ZipFile(tail)if remaining['discs']else None
        try:
            for disc in plan['discs']:
                record={k:disc[k]for k in ('source','name','kind','iso')};record['recipes']={}
                for target,tree in disc['targets'].items():
                    name='recipes/'+disc['source']+'-'+target+'.json'
                    if name not in recipes:
                        recipes[name]=json.loads(new.read(name))
                        if not matches(plan,disc,target,tree,recipes[name]):raise ValueError('Remaining target changed during resume')
                    r=recipes[name];record['recipes'][target]=name;c={op:0 for op in ['copy','patch','xor','audio','bank','chart','bga','text','blob']};patch_bytes=0
                    for step in r['files'].values():
                        c[step['op']]+=1
                        if step['op']=='text'and 'atlas'in step:needed.add(step['atlas'])
                        if 'blob'in step:
                            needed.add(step['blob'])
                            if step['op']in('patch','xor'):
                                key='blobs/'+step['blob'];a=old if key in old_names else new;patch_bytes+=a.getinfo(key).file_size
                    stats.append(dict(source=disc['source'],target=target,files=len(r['files']),bytes=sum(v['bytes']for v in r['package']['files'].values()),patch_bytes=patch_bytes,**c))
                release['discs'].append(record)
            release['tool_sources']={p.name:hash_file(p)for p in Path(__file__).parent.glob('*.py')}
            if registry:release['disc_registry_sha256']=hash_file(registry)
            temporary=output.with_suffix('.merged.incomplete')
            with zipfile.ZipFile(temporary,'x',zipfile.ZIP_DEFLATED,compresslevel=9)as out:
                for ident in sorted(needed):
                    key='blobs/'+ident;a=old if key in old_names else new;data=a.read(key)
                    if hash_bytes(data)!=ident:raise ValueError('Checkpoint blob hash mismatch')
                    out.writestr(key,data)
                for name,r in recipes.items():out.writestr(name,canonical(r))
                out.writestr('release.json',canonical(release));out.writestr('freeze-stats.json',canonical(stats))
            with temporary.open('r+b')as f:os.fsync(f.fileno())
            temporary.rename(output)
        finally:
            if new:new.close()
    (stage/'freeze-resume.json').write_text(json.dumps(dict(reused_targets=list(reused),remaining_sources=[d['source']for d in remaining['discs']],final_discs=len(plan['discs'])),indent=2)+'\n');print('Complete resumed capsule',output,flush=True)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('plan',type=Path);p.add_argument('checkpoint',type=Path);p.add_argument('output',type=Path);p.add_argument('--registry',type=Path,default=Path(__file__).with_name('retail_discs.json'));a=p.parse_args();resume(a.plan,a.checkpoint,a.output,a.registry)
