#!/usr/bin/env python3
"""Maintainer: turn fixed append text bitmaps into ISO-owned/deferred recipes.

Input catalog contains only exact source extents, text/layout metadata and output
hashes. It is prepared/qualified with the user's original discs, not by public CI.
No retail artwork is added to the capsule. Native charts/score identities stay put.
"""
import argparse,copy,hashlib,json,os,tempfile,zipfile
from pathlib import Path
from megamix import canonical,content_id,validate_manifest
from deferred_text import resource_key,transport


def sha(data):return hashlib.sha256(data).hexdigest()

def prepare(capsule,output,catalog=None,atlas=None):
    here=Path(__file__).parent;output=Path(output)
    if output.exists():raise ValueError('Output already exists')
    catalog_path=Path(catalog or here/'text_sprites.json');catalog_bytes=catalog_path.read_bytes();catalog=json.loads(catalog_bytes.decode('utf-8'))
    if catalog.get('format')!=1 or not catalog.get('references'):raise ValueError('Unsupported sprite catalog')
    font_data=Path(atlas or here/'text-fonts/open-v1.mga').read_bytes();font_hash=sha(font_data)
    if catalog.get('atlas_sha256')!=font_hash:raise ValueError('Sprite catalog requires its qualified font atlas')
    with zipfile.ZipFile(capsule)as source:
        recipes={n:json.loads(source.read(n))for n in source.namelist()if n.startswith('recipes/')};before=copy.deepcopy(recipes)
        if len(recipes)!=17:raise ValueError('Complete fixed-disc capsule required')
        replacements={};owners={};seen=set();counts={'native':0,'sprite':0}
        for ref in catalog['references']:
            rn,name,old=ref['recipe'],ref['path'],ref['old'];r=recipes[rn];step=r['files'][name];wanted=ref['default'];spec=dict(ref['spec'],op='text')
            if (rn,name)in seen:raise ValueError('Duplicate sprite reference')
            seen.add((rn,name))
            if r['package']['kind']!='append' or step.get('op')!='blob' or step['blob']!=old or r['package']['files'][name]['sha256']!=old:raise ValueError('Sprite catalog does not match capsule: '+rn+':'+name)
            if spec['kind']=='sprite':spec.update(atlas=font_hash,atlas_bytes=len(font_data))
            else:
                if spec['kind']!='native' or wanted['sha256']!=old:raise ValueError('Native extraction must preserve exact output')
            if old in replacements and replacements[old][0]!=wanted:raise ValueError('Conflicting shared sprite output')
            replacements[old]=(wanted,spec);owners[rn,old]=spec;counts[spec['kind']]+=1
            r['files'][name]=spec;r['package']['files'][name]=wanted
        # Preserve unused pooled aliases coherently as well as live descriptor refs.
        extra=0
        for rn,r in recipes.items():
            for name,step in list(r['files'].items()):
                if step.get('op')=='blob'and step['blob']in replacements:
                    wanted,spec=replacements[step['blob']]
                    if spec['kind']=='native':
                        spec=owners.get((rn,step['blob']))
                        if spec is None:raise ValueError('Unused native alias needs an owning-ISO reference: '+rn+':'+name)
                    if r['package']['kind']=='base':raise ValueError('Unexpected base sprite alias requires an owning-ISO recipe')
                    r['files'][name]=copy.deepcopy(spec);r['package']['files'][name]=wanted;extra+=1
        if any(r['package'].get('text_profile',{}).get('atlas_sha256')!=font_hash for r in recipes.values()):raise ValueError('Input capsule font profile differs from sprite catalog')
        base=next(r['package']for r in recipes.values()if r['package']['kind']=='base');mapping={n:replacements[h][0]['sha256']if h in replacements else h for n,h in base['asset_map'].items()}
        palette_hash=sha(canonical(catalog));contract=base['contract']+'/palette1-'+palette_hash[:16];map_hash=sha(canonical(mapping))
        bases={resource_key(x['ref']):x for x in catalog['bases']}
        for rn,r in recipes.items():
            p=r['package'];p['contract']=contract;p['asset_map_hash']=map_hash;p['text_palette_policy']=dict(schema=1,catalog_sha256=palette_hash)
            if p['kind']=='base':p['text_bases']=bases;p['asset_map']=mapping
            else:
                jobs={n:s for n,s in r['files'].items()if s['op']=='text'and s.get('palette',{}).get('input')=='base'}
                if jobs:p['deferred_text']=dict(schema=1,jobs=jobs)
                for step in jobs.values():
                    ref=step['palette']
                    if resource_key(ref)not in bases or bases[resource_key(ref)]['ref']!=ref:raise ValueError('Missing base palette provider')
            p['content_id']=content_id(p);validate_manifest(p)
            if p['songs']!=before[rn]['package']['songs']:raise ValueError('Song identities changed')
            for name,row in p['files'].items():
                if name.startswith('a/')and mapping[name]!=row['sha256']:raise ValueError('Shared sprite map mismatch')
                if (rn,name)not in seen and r['files'][name]==before[rn]['files'][name]and row!=before[rn]['package']['files'][name]:raise ValueError('Nontext output changed')
        needed={s['blob']for r in recipes.values()for s in r['files'].values()if'blob'in s}|{s['atlas']for r in recipes.values()for s in r['files'].values()if s['op']=='text'and'atlas'in s}
        release=json.loads(source.read('release.json'));release['contract']=contract;release['version']+='-palette1';release['deferred_text_policy']=dict(schema=1,catalog_sha256=palette_hash)
        names=('deferred_text.py','megamix.py','pack_release.py','freeze_release.py','resume_freeze.py','prepare_palette_release.py')
        release['palette_tool_sources']={n:sha((here/n).read_bytes())for n in names};release['palette_resources']={'text_sprites.json':sha(catalog_bytes)}
        release['tool_sources'].update(release['palette_tool_sources'])
        # Refresh hashes from the earlier component rather than retain stale provenance.
        for field in ('text_tool_sources',):
            for n in release.get(field,{}):release[field][n]=sha((here/n).read_bytes())
        updates={n:canonical(r)for n,r in recipes.items()};updates['release.json']=canonical(release)
        stats=json.loads(source.read('freeze-stats.json'))
        for row in stats:
            r=recipes['recipes/'+row['source']+'-'+row['target']+'.json'];ops=[s['op']for s in r['files'].values()]
            for op in ('copy','blob','patch','xor','audio','bank','chart','bga','text'):row[op]=ops.count(op)
            row['deferred_text']=len(transport(r['package'])[0]);row['bytes']=sum(f['bytes']for f in r['package']['files'].values())
        updates['freeze-stats.json']=canonical(stats)
        output.parent.mkdir(parents=True,exist_ok=True);fd,temp=tempfile.mkstemp(prefix=output.name+'.',suffix='.incomplete',dir=output.parent);os.close(fd)
        try:
            with zipfile.ZipFile(temp,'w',zipfile.ZIP_DEFLATED,compresslevel=9)as dest:
                for n in sorted(source.namelist()):
                    if n.startswith('blobs/')and n[6:]not in needed:continue
                    dest.writestr(n,updates[n]if n in updates else source.read(n))
            if output.exists():raise ValueError('Output appeared during build')
            os.replace(temp,output)
        finally:Path(temp).unlink(missing_ok=True)
        removed={n[6:]for n in source.namelist()if n.startswith('blobs/')}-needed
        return dict(references=len(seen),extra_aliases=extra,**counts,removed_blobs=len(removed),removed_zip_payload_bytes=sum(source.getinfo('blobs/'+h).compress_size for h in removed),capsule_bytes=output.stat().st_size,capsule_sha256=sha(output.read_bytes()),all17_manifests_valid=True,chart_score_ids_preserved=True)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('capsule',type=Path);p.add_argument('output',type=Path);p.add_argument('--catalog',type=Path);p.add_argument('--atlas',type=Path);a=p.parse_args()
    try:print(json.dumps(prepare(a.capsule,a.output,a.catalog,a.atlas),indent=2))
    except (ValueError,OSError,KeyError)as e:p.exit(2,str(e)+'\n')
