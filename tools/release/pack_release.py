"""Package the normal-user Python entry point and frozen release capsule."""
import argparse,hashlib,json,zipfile
from pathlib import Path
from readme_text import guides

HERE=Path(__file__).resolve().parent


def pack(capsule,output,readme):
    capsule,output=Path(capsule),Path(output)
    with zipfile.ZipFile(capsule) as f:
        release=json.loads(f.read('release.json'))
        if release.get('schema')!=1 or len(release.get('discs',[]))!=16:raise ValueError('Refusing an incomplete retail release')
        needs_bga=False;needs_chart=False;needs_text=False;needs_launcher=False;needs_startup=False
        for disc in release['discs']:
            for recipe in disc['recipes'].values():
                r=json.loads(f.read(recipe))
                needs_startup|=any(step['op']=='startup'for step in r['files'].values())
                needs_launcher|=any(step['op']=='launcher'for step in r['files'].values())
                needs_text|=any(step['op']=='text'for step in r['files'].values())
                needs_bga|=any(step['op']=='bga'for step in r['files'].values())
                needs_chart|=any(step['op']=='chart'for step in r['files'].values())
                if disc['kind']=='base' and ('MEGAMIX.ELF' not in r['package']['files'] or 'LAUNCHER.ELF' in r['package']['files']):
                    raise ValueError('Base must contain only MEGAMIX.ELF as its boot executable')
                if any(n.startswith('custom/') for n in r['package']['files']):
                    raise ValueError('Release payload must not bundle optional artwork')
    paths={'source_versions.py':HERE/'source_versions.py','source_versions.json':HERE/'source_versions.json','folder_presentation.py':HERE/'folder_presentation.py','megamix.py':HERE/'megamix.py','vcdiff.py':HERE/'vcdiff.py','xor_delta.py':HERE/'xor_delta.py','audio_transform.py':HERE/'audio_transform.py','font_folder.py':HERE/'font_folder.py','prepare_text_fonts.py':HERE/'prepare_text_fonts.py','source_edit.py':HERE/'source_edit.py','release.mmr':capsule}
    if needs_startup:
        for name in ('startup_art.py','native_sprite.py','text_native.py','text_raster.py','bga_codec.py','bga_quantize.py','licenses/QuantOctree.txt'):paths[name]=HERE/name
    if needs_launcher:
        for name in ('iso_launcher.py','native_sprite.py'):paths[name]=HERE/name
    if needs_chart:
        for name in ('chart_transform.py','chart_lz.py','vendor/__init__.py','vendor/blowfish.py','licenses/blowfish-GPL-3.0.txt'):
            paths[name]=HERE/name
    if needs_bga:
        for name in ('bga_transform.py','bga_source_lz.py','bga_codec.py','bga_metadata.py','bga_quantize.py','licenses/QuantOctree.txt'):
            paths[name]=HERE/name
    if needs_text:
        for name in ('deferred_text.py','folder_text.py','folder_glyphs.json','text_assets.py','text_raster.py','text_native.py','text_launcher.py','bga_codec.py','bga_metadata.py'):
            paths[name]=HERE/name
        for name in ('electrolize-OFL.txt','play-OFL.txt','anton-OFL.txt','arimo-OFL.txt','noto-cjk-OFL.txt'):
            p=HERE/'licenses/text-fonts'/name
            if not p.is_file():raise ValueError('Missing text font notice: '+name)
            paths[p.relative_to(HERE).as_posix()]=p
    # Optional cache tools do not change normal build/install dependencies.
    if (HERE/'cache_builder.py').is_file() and (HERE/'cache-engine/engine.json').is_file():
        for name in ('cache_builder.py','build-cache.py','CACHE.txt','CACHE-ja.txt','CACHE-ko.txt'):paths[name]=HERE/name
        for p in sorted((HERE/'cache-engine').rglob('*')):
            if p.is_file():paths[p.relative_to(HERE).as_posix()]=p
    paths['LICENSE']=HERE/'licenses/MEGAMIX-MIT.txt'
    paths['NOTICE.txt']=HERE/'licenses/MEGAMIX-NOTICES.txt'
    paths['requirements-fonts.txt']=HERE/'requirements-fonts.txt'
    for p in sorted((HERE/'fonts').rglob('*.txt')):paths[p.relative_to(HERE).as_posix()]=p
    documents=guides(Path(readme).parent)
    manifest={}
    for name,p in paths.items():
        h=hashlib.sha256()
        with p.open('rb')as f:
            while chunk:=f.read(8*1024*1024):h.update(chunk)
        manifest[name]=h.hexdigest()
    manifest.update({name:hashlib.sha256(data).hexdigest() for name,data in documents.items()})
    with zipfile.ZipFile(output,'x',zipfile.ZIP_STORED) as archive:
        def fixed(name,data):
            info=zipfile.ZipInfo(name,(1980,1,1,0,0,0));info.create_system=3;info.external_attr=0o100644<<16;archive.writestr(info,data)
        for name,p in sorted(paths.items()):fixed(name,p.read_bytes())
        for name,data in sorted(documents.items()):fixed(name,data)
        fixed('release-files.json',(json.dumps(manifest,sort_keys=True,indent=2)+'\n').encode('utf-8'))
    print('Normal-user release:',output)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('capsule',type=Path);p.add_argument('output',type=Path);p.add_argument('readme',type=Path);a=p.parse_args();pack(a.capsule,a.output,a.readme)
