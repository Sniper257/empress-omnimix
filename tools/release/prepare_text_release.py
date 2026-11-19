#!/usr/bin/env python3
"""Maintainer: replace frozen text cards with a coherent generated-font profile.

No ISO, SDK, installed font, Pillow or game image is needed at this stage. The
fixed catalog contains reviewed text/ownership metadata, never raster artwork.
Native/source text sprites remain untouched pending base-palette finalization.
"""
import argparse,copy,hashlib,json,os,tempfile,zipfile
from pathlib import Path
from megamix import canonical,content_id,validate_manifest
from text_raster import Atlas
from text_assets import Generator


def sha(data):return hashlib.sha256(data).hexdigest()


def prepare(capsule,output,catalog=None,atlas=None,progress=print,rendered_dir=None):
    here=Path(__file__).parent;output=Path(output)
    if output.exists():raise ValueError('Output already exists')
    catalog=json.loads(Path(catalog or here/'text_cards.json').read_text(encoding='utf-8'))
    if catalog.get('format')!=1:raise ValueError('Unsupported text catalog')
    font_data=Path(atlas or here/'text-fonts/open-v1.mga').read_bytes();font_hash=sha(font_data);font=Atlas(font_data)
    generator=Generator(font,lambda *args:(_ for _ in()).throw(ValueError('Card recipes must not require ISO resources')))
    targets={};specs={}
    for i,(old,row)in enumerate(sorted(catalog['cards'].items()),1):
        spec=dict(kind='card',texts=row['texts'])
        if rendered_dir is not None:
            if font_hash!=catalog['default_atlas_sha256']:raise ValueError('Rendered cache requires the pinned default atlas')
            data=(Path(rendered_dir)/(old+'.if')).read_bytes()
        else:data=generator.generate(spec)
        target=dict(bytes=len(data),sha256=sha(data))
        if font_hash==catalog['default_atlas_sha256'] and target!=row['default']:raise ValueError('Default text hash mismatch: '+old)
        targets[old]=target;specs[old]=spec
        if i%50==0:progress(f'{"Checked cached" if rendered_dir is not None else "Generated"} {i}/{len(catalog["cards"])} title cards')
    output.parent.mkdir(parents=True,exist_ok=True)
    fd,temp=tempfile.mkstemp(prefix=output.name+'.',suffix='.incomplete',dir=output.parent);os.close(fd)
    try:
        with zipfile.ZipFile(capsule)as source:
            recipes={n:json.loads(source.read(n))for n in source.namelist()if n.startswith('recipes/')};originals=copy.deepcopy(recipes)
            if len(recipes)!=17 or any(s['op']=='text'for r in recipes.values()for s in r['files'].values()):raise ValueError('Expected17 unprofiled recipes')
            release=json.loads(source.read('release.json'))
            if release.get('schema')!=1 or len(release['discs'])!=16:raise ValueError('Complete fixed-disc capsule required')
            pooled={};count=0
            for ref in catalog['references']:
                rn,name,old=ref['recipe'],ref['path'],ref['old'];r=recipes[rn];step=r['files'][name]
                if step.get('op')!='blob'or step.get('blob')!=old or r['package']['files'][name]['sha256']!=old:raise ValueError('Text catalog does not match input capsule: '+rn+':'+name)
                r['files'][name]=dict(specs[old],op='text',atlas=font_hash);r['package']['files'][name]=targets[old];count+=1
                if name.startswith('a/'):
                    if name in pooled and pooled[name]!=targets[old]['sha256']:raise ValueError('Conflicting shared card output')
                    pooled[name]=targets[old]['sha256']
            base=next(r['package']for r in recipes.values()if r['package']['kind']=='base');asset_map=dict(base['asset_map']);asset_map.update(pooled)
            catalog_hash=sha(canonical(catalog));profile_hash=sha(canonical(dict(atlas=font_hash,catalog=catalog_hash,renderer=1)))
            asset_hash=sha(canonical(asset_map));contract=base['contract']+'/text1-'+profile_hash[:16]
            for rn,r in recipes.items():
                p=r['package'];p['contract']=contract;p['asset_map_hash']=asset_hash;p['text_profile']=dict(name=font.info['profile'],atlas_sha256=font_hash,catalog_sha256=catalog_hash,renderer_revision=1)
                if p['kind']=='base':p['asset_map']=asset_map
                p['content_id']=content_id(p);validate_manifest(p)
                if p['songs']!=originals[rn]['package']['songs']:raise ValueError('Text conversion changed song identities')
                for name,row in p['files'].items():
                    if name.startswith('a/')and asset_map[name]!=row['sha256']:raise ValueError('Pooled asset profile conflict: '+name)
                    if r['files'][name]['op']!='text'and(row!=originals[rn]['package']['files'][name]or r['files'][name]!=originals[rn]['files'][name]):raise ValueError('Nontext asset changed')
            needed={s['blob']for r in recipes.values()for s in r['files'].values()if'blob'in s}|{s['atlas']for r in recipes.values()for s in r['files'].values()if s['op']=='text'}
            old_blobs={s['blob']for r in originals.values()for s in r['files'].values()if'blob'in s};removed=old_blobs-needed
            release['contract']=contract;release['version']+='-text1';release['experimental_text_profile']=dict(name=font.info['profile'],atlas_sha256=font_hash,catalog_sha256=catalog_hash,renderer_revision=1)
            release['text_tool_sources']={n:sha((here/n).read_bytes())for n in ('megamix.py','freeze_release.py','resume_freeze.py','pack_release.py','text_assets.py','text_raster.py','text_native.py','text_launcher.py','prepare_text_release.py','prepare_text_fonts.py','fetch_text_fonts.py','generate-text.py')}
            release['text_resources']={n:sha((here/n).read_bytes())for n in ('text_cards.json','text-fonts/open-v1.mga','text-fonts/open-fonts.sources.json','text-fonts/corpus.json')}
            release['tool_sources'].update({n:sha((here/n).read_bytes())for n in ('megamix.py','text_assets.py','text_raster.py','text_native.py','text_launcher.py')})
            updates={n:canonical(r)for n,r in recipes.items()};updates['release.json']=canonical(release)
            stats=json.loads(source.read('freeze-stats.json'))
            for row in stats:
                r=recipes['recipes/'+row['source']+'-'+row['target']+'.json'];n=sum(s['op']=='text'for s in r['files'].values());row['text']=n;row['blob']-=n;row['bytes']=sum(f['bytes']for f in r['package']['files'].values())
            updates['freeze-stats.json']=canonical(stats)
            with zipfile.ZipFile(temp,'w',zipfile.ZIP_DEFLATED,compresslevel=9)as dest:
                for name in sorted(source.namelist()):
                    if name.startswith('blobs/')and name[6:]not in needed:continue
                    dest.writestr(name,updates.get(name,source.read(name)))
                dest.writestr('blobs/'+font_hash,font_data)
            receipt=dict(cards=len(targets),unique_outputs=len({r['sha256']for r in targets.values()}),render_cache_verified=rendered_dir is not None,references=count,removed_blobs=len(removed),removed_zip_payload_bytes=sum(source.getinfo('blobs/'+h).compress_size for h in removed),atlas_sha256=font_hash,profile=font.info['profile'],all17_manifests_validated=len(recipes)==17,chart_score_ids_and_all_nontext_assets_unchanged=True,default_hashes_checked=font_hash==catalog['default_atlas_sha256'])
        if output.exists():raise ValueError('Output appeared during text conversion')
        os.replace(temp,output);receipt['capsule']=dict(bytes=output.stat().st_size,sha256=sha(output.read_bytes()));return receipt
    finally:Path(temp).unlink(missing_ok=True)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('capsule',type=Path);p.add_argument('output',type=Path);p.add_argument('--catalog',type=Path);p.add_argument('--atlas',type=Path);p.add_argument('--rendered-dir',type=Path,help='Reuse a default-profile cache only after checking every fixed output hash');a=p.parse_args()
    try:print(json.dumps(prepare(a.capsule,a.output,a.catalog,a.atlas,rendered_dir=a.rendered_dir),indent=2))
    except (ValueError,OSError,KeyError)as e:p.exit(2,str(e)+'\n')
