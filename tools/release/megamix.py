#!/usr/bin/env python3
"""Build EMPRESS MEGAMIX from a supported original ISO. Python 3.10+, stdlib only.

  python3 megamix.py GAME.iso OUTPUT
  python3 megamix.py install APPEND_PACK MEGAMIX_DIRECTORY
  python3 megamix.py verify DIRECTORY
  python3 megamix.py cache DIRECTORY --game-path mmce0:/MEGAMIX/
  python3 megamix.py update "EMPRESS DVD1.iso" MEGAMIX_DIRECTORY
  python3 megamix.py index MEGAMIX_DIRECTORY

EMPRESS Disc1 creates a base; all other supported discs create independent append
packs. An adjacent release.mmr supplies pinned runtimes, graphics and recipes.
Maintainer generation lives separately in freeze_release.py and tools/offdisc.
"""
import argparse
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import sys
import time
import uuid
import zipfile

import source_versions
import folder_presentation

from audio_transform import convert as convert_audio,assemble_bank
from vcdiff import decode
from xor_delta import decode as decode_xor

SCHEMA = 1
CHUNK = 8 * 1024 * 1024
MUTABLE = {'launcher.cfg', 'launcher.cfg.pending', 'debug.cfg'}
DYNAMIC = {'songs/styles.txt', 'launcher/source.id'}
SOURCES = {'id','pb','djt','gold','distorted','happysky','red','tenth','ninth','usa','eighth','seventh','sixth','fifth','fourth','third'}


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(',', ':'), ensure_ascii=True).encode()


def hash_bytes(value):
    return hashlib.sha256(value).hexdigest()


def hash_file(path, progress=None):
    h = hashlib.sha256()
    total=Path(path).stat().st_size;done=0;last=0
    with Path(path).open('rb') as f:
        while block := f.read(CHUNK):
            h.update(block);done+=len(block)
            percent=done*100//max(1,total)
            if progress and percent>=last+10:
                progress(f'Checking ISO: {percent}%');last=percent
    return h.hexdigest()


def asset_path(sha256):
    # Short, bounded buckets shared by exact bytes across every disc pack.
    return 'a/'+sha256[:2]+'/'+sha256[2:34]


def relative(name):
    if not isinstance(name,str):raise ValueError('Invalid package path type')
    p = PurePosixPath(name)
    if not isinstance(name, str) or not name or '\\' in name or ':' in name or '\0' in name or p.is_absolute() or any(x in ('', '.', '..') for x in name.split('/')):
        raise ValueError('Invalid package path: '+repr(name))
    return p


def path_in(root, name):
    p = relative(name)
    root = Path(root).absolute()
    path = root.joinpath(*p.parts)
    if root.is_symlink() or any(q.is_symlink() for q in [path, *path.parents] if q != root.parent and root in [q, *q.parents]):
        raise ValueError('Symlink in destination: '+name)
    if not path.resolve().is_relative_to(root.resolve()):
        raise ValueError('Package path escapes destination: '+name)
    return path


def save_json(path, value):
    write_atomic(path, json.dumps(value, indent=2, sort_keys=True)+'\n')


def write_atomic(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name+'.writing')
    if tmp.is_symlink():
        raise ValueError('Refusing symlink temporary file: '+str(tmp))
    with tmp.open('wb') as f:
        f.write(value.encode() if isinstance(value, str) else value)
        f.flush()
        os.fsync(f.fileno())
    os.replace(tmp, path)


def sync_directory(root):
    # FAT/removable filesystems and Windows do not all support directory fsync.
    if os.name == 'posix':
        try:
            fd = os.open(root, os.O_RDONLY)
            try:
                os.fsync(fd)
            finally:
                os.close(fd)
        except OSError:
            pass


class Extent:
    def __init__(self, f, offset, size, total):
        if min(offset, size) < 0 or offset+size > total:
            raise ValueError('Source extent outside original ISO')
        self.f, self.offset, self.size, self.at = f, offset, size, 0

    def seek(self, at):
        if at < 0 or at > self.size:
            raise ValueError('Patch seeks outside source extent')
        self.at = at

    def read(self, n):
        if n < 0 or self.at+n > self.size:
            raise ValueError('Patch reads outside source extent')
        self.f.seek(self.offset+self.at)
        out = self.f.read(n)
        self.at += len(out)
        return out


def check_files(root, files, resolve=None):
    failures = []
    for name, row in sorted(files.items()):
        try:
            path = resolve(name) if resolve is not None else path_in(root, name)
            if path.stat().st_size != row['bytes'] or hash_file(path) != row['sha256']:
                failures.append(name+': size/SHA256 mismatch')
        except OSError as e:
            failures.append(name+': '+str(e))
    return failures


def content_id(manifest):
    core=dict(manifest);core.pop('content_id',None)
    if core.get('kind')=='append':
        # Provenance/global-table extensions do not change this source's bytes.
        core.pop('generator_revision',None);core.pop('asset_map_hash',None)
    return hash_bytes(canonical(core))


def validate_manifest(manifest):
    if manifest.get('schema') != SCHEMA or manifest.get('kind') not in ('base','append') or manifest.get('source') not in SOURCES:
        raise ValueError('Unsupported MEGAMIX package')
    if manifest.get('content_id') != content_id(manifest):
        raise ValueError('Package manifest identity mismatch')
    if any(k in manifest for k in ('font_profile','font_rendered','font_request')):
        from font_folder import validate as validate_fonts
        validate_fonts(manifest)
    source = manifest['source']
    if (source == 'id') != (manifest['kind'] == 'base'):
        raise ValueError('Only EMPRESS Disc1 can create a base')
    seen = set()
    for name, row in manifest['files'].items():
        relative(name)
        if name.casefold() in seen or row['bytes'] < 0 or len(row['sha256']) != 64:
            raise ValueError('Invalid/colliding package file record')
        seen.add(name.casefold())
    for song in manifest['songs']:
        if song['source'] != source or not isinstance(song['sid'],int) or song['sid'] < 0:
            raise ValueError('Invalid song source identity')
        if manifest['kind'] == 'append' and song['dir'] != source+'_'+str(song['sid']):
            raise ValueError('Append song folders must be source-owned')
    if manifest['kind']=='base':
        mapping=manifest['asset_map']
        if hash_bytes(canonical(mapping))!=manifest['asset_map_hash']:
            raise ValueError('Static asset table identity mismatch')
        for name,row in manifest['files'].items():
            original=manifest.get('font_rendered',{}).get('originals',{}).get(name,row)
            if name.startswith('a/') and mapping.get(name)!=original['sha256']:
                raise ValueError('Base asset does not match the static asset table')
    if manifest['kind']=='append':
        owned = ['a'] + ['songs/'+r['style']+'/'+r['dir'] for r in manifest['songs']]
        if sorted(manifest['payload_prefixes']) != sorted(owned):
            raise ValueError('Append prefixes do not match their source')
    if 'deferred_text' in manifest or 'text_bases' in manifest:
        import deferred_text
        deferred_text.validate_base(manifest);deferred_text.transport(manifest)
    source_versions.validate(manifest)
    folder_presentation.validate(manifest)
    return manifest


def read_manifest(root):
    path = path_in(root, 'megamix-package.json')
    manifest = json.loads(path.read_text(encoding='utf-8'))
    return validate_manifest(manifest)


def verify(root):
    root = Path(root).absolute()
    if (root/'megamix-package.json').is_file() and not (root/'.megamix/installed.json').exists():
        info = read_manifest(root)
        if info.get('deferred_text'):
            from deferred_text import pack_files
            return check_files(root,pack_files(info))
        return check_files(root, info['files'])
    state = json.loads(path_in(root, '.megamix/installed.json').read_text())
    if state.get('schema') != SCHEMA:
        raise ValueError('Unsupported installed manifest')
    if (root/'.megamix/install-journal.json').exists():
        raise ValueError('Installation interrupted; repeat the same install command to finish')
    presentation=folder_presentation.paths(state)
    expected = {}
    for source,package in state['packages'].items():
        validate_manifest(package)
        if source!=package['source']:raise ValueError('Installed source key mismatch')
        for name, row in package['files'].items():
            if name not in presentation and name not in MUTABLE and name not in DYNAMIC and not (name.startswith('songs/') and name.endswith('/index.txt')):
                if name in expected and expected[name] != row:
                    raise ValueError('Conflicting installed packages: '+name)
                expected[name] = row
    failures = check_files(root, expected)
    if not presentation.issubset(state['generated']):raise ValueError('Folder presentation activation is incomplete')
    failures += check_files(root, state['generated'])
    return failures


def active_songs(state, enabled_styles=None):
    """Resolve immutable chart coverage before allocating per-row game tables."""
    songs=[r for p in state['packages'].values()for r in p['songs']]
    if enabled_styles is not None:songs=[r for r in songs if r['style']in enabled_styles]
    selection=state['packages']['id'].get('selection')
    if selection is None:return songs # Compatibility with earlier manifests.
    available={r['source']+':'+str(r['sid']):r for r in songs};winners={}
    for key in available:
        row=selection[key]
        for chart in row['charts']:
            rank=(chart['preferred']>>1,row['rank'],chart['preferred']&1,-int(row['source_id'],16),chart['column'])
            if chart['group']not in winners or rank>winners[chart['group']][0]:winners[chart['group']]=(rank,key)
    keep={key for _,key in winners.values()}|{k for k,r in available.items()if r['source']=='id'}
    # Variants require their source-local parent to remain selectable, and vice versa.
    edges=[(key,peer)for key in available for peer in selection[key].get('peers',[])if peer in available]
    changed=True
    while changed:
        changed=False
        for a,b in edges:
            if (a in keep or b in keep)and not(a in keep and b in keep):keep.update((a,b));changed=True
    return [r for key,r in available.items()if key in keep]


def indexed_songs(state):
    """Keep every row that can be needed by a launcher folder selection."""
    rows=[r for p in state['packages'].values()for r in p['songs']]
    selection=state['packages']['id'].get('selection')
    if selection is None:return rows
    key=lambda r:r['source']+':'+str(r['sid'])
    keep={key(r)for style in {r['style']for r in rows}for r in active_songs(state,{style})}
    available={key(r):r for r in rows};changed=True
    while changed:
        old=set(keep)
        for k in list(keep):keep.update(p for p in selection[k].get('peers',[])if p in available)
        changed=keep!=old
    return [r for r in rows if key(r)in keep]


def coverage_text(state,indexed,styles):
    """Small conditional masks: evaluate AFTER the user selects enabled folders."""
    selection=state['packages']['id'].get('selection');lines=['V 1']
    if selection is None:return 'V 1\n'
    if len(styles)>32:raise ValueError('Too many folders for the release index ABI')
    for i,name in enumerate(styles):lines.append(f'S {i} {name}')
    rows={r['source']+':'+str(r['sid']):r for r in indexed};bits={s:1<<i for i,s in enumerate(styles)};groups={};ranks={};edges={k:set()for k in rows}
    for key,row in sorted(rows.items(),key=lambda item:(styles.index(item[1]['style']),item[1]['dir'])):
        m=selection[key];ranks[key]=[]
        for chart in m['charts']:
            rank=(chart['preferred']>>1,m['rank'],chart['preferred']&1,-int(m['source_id'],16),chart['column']);ranks[key].append((chart['group'],rank));groups.setdefault(chart['group'],[]).append((rank,bits[row['style']]))
        for peer in m.get('peers',[]):
            if peer in rows:edges[key].add(peer);edges[peer].add(key)
    for key,row in sorted(rows.items(),key=lambda item:(styles.index(item[1]['style']),item[1]['dir'])):
        if row['source']=='id':continue
        component={key};todo=[key]
        while todo:
            for peer in edges[todo.pop()]-component:component.add(peer);todo.append(peer)
        # Known retail variants share a folder. Preserve cross-folder components
        # conservatively if future metadata introduces one.
        if len({rows[k]['style']for k in component})!=1:continue
        masks=set()
        for peer in component:
            for group,rank in ranks[peer]:
                mask=0
                for other,bit in groups[group]:
                    if other>rank:mask|=bit
                masks.add(mask)
        if masks and 0 not in masks:lines.append('D '+str(styles.index(row['style']))+' '+row['dir']+' '+' '.join(format(m,'x')for m in sorted(masks)))
    return '\n'.join(lines)+'\n'


def indexes(state):
    by_style = {}
    indexed=indexed_songs(state)
    for song in indexed:
        style, folder = song['style'], song['dir']
        relative(style);relative(folder)
        if '/' in style or '/' in folder:
            raise ValueError('Invalid song folder identity')
        by_style.setdefault(style, set()).add(folder)
    known = state['style_order']
    if set(by_style)-set(known):
        raise ValueError('Unsupported style in package')
    out = {'songs/styles.txt': ''.join(st+'\n' for st in known if st in by_style)}
    for style in known:
        out['songs/'+style+'/index.txt'] = ''.join(n+'\n' for n in sorted(by_style.get(style, [])))
    out['catalog/coverage.txt']=coverage_text(state,indexed,[st for st in known if st in by_style])
    # Independent of preferences, save updates, import order and timestamps.
    seal = hash_bytes(canonical({'activation_policy':2,'contract': state['contract'], 'packages': {k:v['content_id'] for k,v in state['packages'].items()}}))
    out['launcher/source.id'] = seal
    return out


def local_songs(root, state):
    """Song folders added by hand (converted songs), as listed in the current
    indexes: kept when indexes are regenerated. {style: [folder, ...]} in order."""
    owned={(r['style'],r['dir']) for p in state['packages'].values() for r in p['songs']}
    styles_path=path_in(root,'songs/styles.txt')
    if not styles_path.is_file():return {}
    out={}
    for style in styles_path.read_text(encoding='utf-8',errors='replace').splitlines():
        style=style.strip()
        if not style or '/' in style or '\\' in style or style in ('.','..') or style in out:continue
        index=path_in(root,'songs/'+style+'/index.txt')
        if not index.is_file():continue
        rows=[]
        for d in index.read_text(encoding='utf-8',errors='replace').splitlines():
            d=d.strip()
            if d and '/' not in d and '\\' not in d and d not in ('.','..') and (style,d) not in owned and d not in rows and path_in(root,'songs/'+style+'/'+d+'/song.txt').is_file():rows.append(d)
        if rows:out[style]=rows
    return out


def activate_indexes(root, state):
    local=local_songs(root,state)
    generated = indexes(state)
    if local:
        styles=generated['songs/styles.txt'].splitlines()
        for style,rows in local.items():
            name='songs/'+style+'/index.txt'
            listed=generated.get(name,'').splitlines()
            generated[name]=''.join(n+'\n' for n in listed+[d for d in rows if d not in listed])
            if style not in styles:styles.append(style)
        known=state['style_order']
        styles=[st for st in known if st in styles]+[st for st in styles if st not in known]
        generated['songs/styles.txt']=''.join(st+'\n' for st in styles)
    generated.update(folder_presentation.generated(state,lambda name:path_in(root,name).read_bytes()))
    state['generated'] = {}
    for name, text in generated.items():
        write_atomic(path_in(root, name), text)
        data=text.encode() if isinstance(text,str) else text
        state['generated'][name] = {'bytes': len(data), 'sha256': hash_bytes(data)}
    save_json(path_in(root, '.megamix/installed.json'), state)
    sync_directory(root)


def finalize_base(stage, manifest):
    state = {'schema': SCHEMA, 'contract': manifest['contract'], 'style_order': manifest['style_order'], 'packages': {manifest['source']:manifest}, 'generated': {}}
    # These are new-user defaults, never copied from a development/user profile.
    write_atomic(stage/'launcher.cfg', 'video 0\nlanebright 10\nbeamlen 10\nnoteh 10\nsaveformat 2\nsaveuuid '+uuid.uuid4().hex+'\nprofileprompt 0\n')
    write_atomic(stage/'launcher.cfg.pending', '')
    write_atomic(stage/'debug.cfg', 'debug 0\nprepprobe 0\ninputlog 0\nlauncherlog 0\nsaveprofile 0\nselectprofile 0\nselectosd 0\n')
    for name in ['cache','logs']:
        (stage/name).mkdir(exist_ok=True)
    activate_indexes(stage, state)


def build(iso, output, bundle, target='console', progress=print, fonts=None):
    output=Path(output).absolute()
    if output.exists()or output.is_symlink():raise ValueError('Output already exists. Choose a new directory; use install to add a pack.')
    if not Path(iso).is_file():raise ValueError('ISO file does not exist: '+str(iso))
    output.parent.mkdir(parents=True,exist_ok=True)
    lock=output.with_name('.'+output.name+'.megamix-build.lock')
    try:fd=os.open(lock,os.O_WRONLY|os.O_CREAT|os.O_EXCL,0o600)
    except FileExistsError:
        raise ValueError('A build is active or was forcibly stopped. Check its process before removing '+str(lock))from None
    try:
        with os.fdopen(fd,'w')as f:f.write(str(os.getpid())+'\n')
        return _build(iso,output,bundle,target,progress,fonts)
    finally:lock.unlink(missing_ok=True)


def _build(iso, output, bundle, target='console', progress=print, fonts=None):
    iso, output = Path(iso).absolute(), Path(output).absolute()
    if output.exists() or output.is_symlink():
        raise ValueError('Output already exists. Choose a new directory; use install to add a pack.')
    with zipfile.ZipFile(bundle) as archive:
        release = json.loads(archive.read('release.json'))
        if release.get('schema') != SCHEMA:
            raise ValueError('Unsupported release bundle')
        candidates = [d for d in release['discs'] if d['iso']['bytes'] == iso.stat().st_size]
        if not candidates:
            raise ValueError('ISO size is not a supported original PS2 IIDX disc (3rd–16/PB/USA)')
        progress('Checking original ISO SHA256…')
        fingerprint = hash_file(iso, progress)
        disc = next((d for d in candidates if d['iso']['sha256'] == fingerprint), None)
        if disc is None:
            raise ValueError('Unsupported or modified ISO. Expected an exact original dump; input was not changed.')
        recipe_name = disc['recipes'][target] if disc['kind']=='base' else disc['recipes']['append']
        recipe = json.loads(archive.read(recipe_name))
        font_outputs=set()
        if fonts is not None:
            from font_folder import prepare
            archive,recipe,font_outputs=prepare(archive,recipe,fonts,progress)
        manifest = validate_manifest(recipe['package'])
        if manifest['kind']=='base'and manifest.get('selection')and manifest.get('activation_policy')!=2:raise ValueError('This bundle needs its matching launcher/index builder')
        progress('Recognized '+disc['name']+' → '+manifest['kind']+' package')
        output.parent.mkdir(parents=True, exist_ok=True)
        stage = output.with_name('.'+output.name+'.megamix-incomplete')
        if stage.is_symlink():
            raise ValueError('Staging directory is a symlink')
        job = {'schema':SCHEMA, 'iso':fingerprint, 'recipe':hash_bytes(canonical(recipe)), 'output':str(output)}
        if stage.exists():
            if json.loads((stage/'build-journal.json').read_text()) != job:
                raise ValueError('Staging directory belongs to a different build')
            progress('Resuming verified staged files…')
        else:
            stage.mkdir()
            save_json(stage/'build-journal.json', job)
        if manifest.get('deferred_text'):
            from deferred_text import pack_files
            build_files=pack_files(manifest)
        else:build_files=manifest['files']
        total = sum(r['bytes'] for r in build_files.values())
        present = sum(path_in(stage,n).stat().st_size for n in build_files if path_in(stage,n).is_file())
        if shutil.disk_usage(stage).free < max(0,total-present)+16*1024*1024:
            raise ValueError('Not enough free space for output (need approximately '+str(total)+' bytes)')
        blobs_checked = set();text_atlases={};text_generators={}
        deferred_jobs={}
        if manifest.get('deferred_text'):
            from deferred_text import capture,transport
            deferred_jobs,_=transport(manifest)
        with iso.open('rb') as src:
            if deferred_jobs:capture(stage,manifest,recipe,archive,lambda at,n:Extent(src,at,n,iso.stat().st_size).read(n))
            for i, (name, row) in enumerate(sorted(recipe['files'].items(), key=lambda item:(item[1].get('offset',iso.stat().st_size),item[0])), 1):
                if name in deferred_jobs:continue
                expected = manifest['files'][name]
                path = path_in(stage, name)
                if name not in font_outputs and row['op']!='launcher' and path.is_file() and path.stat().st_size == expected['bytes'] and hash_file(path) == expected['sha256']:
                    continue
                path.parent.mkdir(parents=True, exist_ok=True)
                tmp = path.with_name(path.name+'.building')
                if tmp.is_symlink():
                    raise ValueError('Symlink build temporary')
                with tmp.open('wb') as dst:
                    if row['op'] == 'copy':
                        view = Extent(src, row['offset'], expected['bytes'], iso.stat().st_size)
                        left = expected['bytes']
                        while left:
                            chunk = view.read(min(left, CHUNK))
                            if not chunk:raise ValueError('Short ISO read')
                            dst.write(chunk);left -= len(chunk)
                    elif row['op']=='bank':
                        if 'prefix_edit'in row:
                            from source_edit import apply
                            prefix=apply(row['prefix_edit'],lambda at,n:Extent(src,at,n,iso.stat().st_size).read(n))
                        else:
                            prefix=archive.read('blobs/'+row['blob'])
                            if hash_bytes(prefix)!=row['blob']:raise ValueError('Corrupt bank header blob')
                        if not 0<=row['source_bytes']<=64*1024*1024:raise ValueError('Bank source exceeds bounds')
                        view=Extent(src,row['offset'],row['source_bytes'],iso.stat().st_size)
                        bank_source=io.BytesIO(view.read(row['source_bytes']))
                        for chunk in assemble_bank(prefix,bank_source,row['spans'],expected['bytes']):dst.write(chunk)
                    elif row['op']=='chart':
                        from chart_transform import convert as convert_chart
                        if not 0<row['source_bytes']<=16*1024*1024:raise ValueError('Chart source exceeds bounds')
                        view=Extent(src,row['offset'],row['source_bytes'],iso.stat().st_size)
                        dst.write(convert_chart(row['transform'],view.read(row['source_bytes']),expected['bytes']))
                    elif row['op']=='bga':
                        from bga_transform import from_iso as convert_bga
                        total_iso_bytes=iso.stat().st_size
                        def bga_read(at,size):
                            return Extent(src,at,size,total_iso_bytes).read(size)
                        dst.write(convert_bga(row,bga_read))
                    elif row['op']=='startup':
                        from startup_art import from_iso
                        overlay_data=archive.read('blobs/'+row['overlay'])
                        dst.write(from_iso(row,lambda at,n:Extent(src,at,n,iso.stat().st_size).read(n),overlay_data))
                    elif row['op']=='text':
                        from text_assets import Generator
                        from text_raster import Atlas
                        text_font=None
                        if row['kind']!='native':
                            if row['atlas'] not in text_atlases:
                                font_data=archive.read('blobs/'+row['atlas'])
                                if hash_bytes(font_data)!=row['atlas']:raise ValueError('Corrupt font atlas blob')
                                text_atlases[row['atlas']]=Atlas(font_data)
                            text_font=text_atlases[row['atlas']]
                        # Base palette inputs can only be read from this owning ISO.
                        # Append/base-dependent jobs are finalized separately, never
                        # by borrowing another disc's bytes from this ISO.
                        def text_read(which,at,size):
                            if which=='base' and manifest['source']!='id':raise ValueError('Text palette requires installed base resources')
                            return Extent(src,at,size,iso.stat().st_size).read(size)
                        key=row.get('atlas')
                        if key not in text_generators:text_generators[key]=Generator(text_font,text_read)
                        dst.write(text_generators[key].generate(row))
                    elif row['op']=='container':
                        from source_edit import normalize_container
                        if not 0<row['source_bytes']<=16*1024*1024:raise ValueError('Container source exceeds bounds')
                        view=Extent(src,row['offset'],row['source_bytes'],iso.stat().st_size)
                        dst.write(normalize_container(view.read(row['source_bytes'])))
                    elif row['op']=='launcher':
                        from iso_launcher import fill,pack
                        if row.get('packing')and manifest.get('derived_launcher')!=dict(schema=1,template_sha256=row['recipe']['template_sha256'],unpacked_sha256=row['recipe']['unpacked_sha256']):raise ValueError('Launcher source identity mismatch')
                        template=archive.read('blobs/'+row['template'])
                        raw=fill(template,row['recipe'],lambda at,n:Extent(src,at,n,iso.stat().st_size).read(n))
                        if row.get('packing'):
                            stub=archive.read('blobs/'+row['stub']);raw=pack(raw,stub,row['packing'])
                            from iso_launcher import verify_packed
                            verify_packed(raw,row['memory'],row['packing'])
                        dst.write(raw)
                    elif row['op']=='edit':
                        from source_edit import apply
                        dst.write(apply(row,lambda at,n:Extent(src,at,n,iso.stat().st_size).read(n)))
                    elif row['op']=='audio':
                        if not 0<=row['source_bytes']<=64*1024*1024:raise ValueError('Audio source exceeds bounds')
                        view=Extent(src,row['offset'],row['source_bytes'],iso.stat().st_size)
                        dst.write(convert_audio(row['transform'],view.read(row['source_bytes']),expected['bytes']))
                    elif row['op'] in ('blob','patch','xor'):
                        blob = archive.read('blobs/'+row['blob'])
                        if row['blob'] not in blobs_checked:
                            if hash_bytes(blob) != row['blob']:raise ValueError('Corrupt release blob')
                            blobs_checked.add(row['blob'])
                        if row['op']=='blob':
                            dst.write(blob)
                        else:
                            view = Extent(src,row['offset'],row['source_bytes'],iso.stat().st_size)
                            chunks=decode_xor(blob,view,row['source_bytes'],expected['bytes'],row.get('shift',0))if row['op']=='xor'else decode(blob,view,expected['bytes'])
                            for chunk in chunks:dst.write(chunk)
                    else:
                        raise ValueError('Unknown release recipe operation')
                    dst.flush();os.fsync(dst.fileno())
                if name in font_outputs:
                    expected=dict(bytes=tmp.stat().st_size,sha256=hash_file(tmp));manifest['files'][name]=expected
                if row['op']=='launcher'and row.get('packing'):
                    expected=dict(bytes=tmp.stat().st_size,sha256=hash_file(tmp));manifest['files'][name]=expected
                if tmp.stat().st_size != expected['bytes'] or hash_file(tmp) != expected['sha256']:
                    raise ValueError('Generated file failed SHA256: '+name)
                os.replace(tmp,path)
                if i==1 or i%100==0:progress(f'Built {i}/{len(recipe["files"])} files')
        if set(recipe['files']) != set(manifest['files']):
            raise ValueError('Incomplete release recipe')
        if deferred_jobs:
            from deferred_text import pack_files
            failures=check_files(stage,pack_files(manifest))
        else:failures = check_files(stage, manifest['files'])
        if failures:raise ValueError('\n'.join(failures[:10]))
        manifest['content_id']=content_id(manifest)
        validate_manifest(manifest)
        save_json(stage/'megamix-package.json',manifest)
        if manifest['kind']=='base':finalize_base(stage,manifest)
        (stage/'build-journal.json').unlink()
        sync_directory(stage)
        # Do not replace a directory created by another process while we built.
        if output.exists():raise ValueError('Output appeared during build; staged files retained')
        stage.rename(output)
        sync_directory(output.parent)
        progress('Verified '+manifest['kind']+' ready: '+str(output))
    return manifest


def install(pack, root, progress=print):
    pack,root=Path(pack).absolute(),Path(root).absolute()
    manifest=read_manifest(pack)
    state=json.loads(path_in(root,'.megamix/installed.json').read_text())
    base=state['packages']['id']
    if base.get('font_profile') or manifest.get('font_request') or manifest.get('font_rendered'):
        from font_folder import normalized_pack
        validate_manifest(base)
        with normalized_pack(pack,root,manifest,base,progress)as (manifest,resolve):
            return _install(pack,root,progress,manifest,resolve)
    return _install(pack,root,progress,manifest,lambda n:path_in(pack,n))


def _install(pack, root, progress, manifest, resolve):
    if manifest['kind']!='append':raise ValueError('install expects an append pack')
    if manifest.get('deferred_text'):
        from deferred_text import pack_files
        failures=check_files(pack,pack_files(manifest),resolve)
    else:failures=check_files(pack,manifest['files'],resolve)
    if failures:raise ValueError('Damaged append pack: '+'; '.join(failures[:10]))
    state=json.loads(path_in(root,'.megamix/installed.json').read_text())
    for installed in state['packages'].values():validate_manifest(installed)
    if state['contract']!=manifest['contract']:raise ValueError('Pack/base release contracts differ; use a matching release')
    base=state['packages']['id']
    if base.get('listing_family_policy')==1 and manifest.get('song_versions')!=base.get('song_versions'):
        raise ValueError('Use the matching R167 or newer builder for additional append packs')
    if base.get('song_versions') and base['song_versions']!=source_versions.POLICY:
        raise ValueError('Update the base with the matching catalog hotfix before adding packs')
    routes={n:n for n in manifest['files']};rewritten={}
    if base.get('song_versions'):
        manifest,routes,rewritten=source_versions.normalize(manifest,lambda n:resolve(n).read_bytes())
    elif manifest.get('song_versions'):
        raise ValueError('Update the base song-version policy with the supplied hotfix first')
    if base.get('selection')and base.get('activation_policy')!=2:raise ValueError('Update the base to its paired folder-aware release first')
    for name,row in manifest['files'].items():
        original=manifest.get('font_rendered',{}).get('originals',{}).get(name,row)
        if name.startswith('a/') and base['asset_map'].get(name)!=original['sha256']:
            raise ValueError('Append asset does not match the base static asset table: '+name)
    key=manifest['source'];previous=state['packages'].get(key)
    if previous and previous['content_id']!=manifest['content_id']:
        if not folder_presentation.same_song_payload(previous,manifest):
            raise ValueError('Different revision of this source is installed; a runtime/data upgrade needs an explicit migration')
    if previous and previous.get('folder_art') and not manifest.get('folder_art'):
        if path_in(root,'.megamix/install-journal.json').exists():raise ValueError('Finish the interrupted pack installation first')
        # Reimporting an older PB pack must not undo the presentation hotfix.
        failures=check_files(root,{n:previous['files'][n] for r in previous['folder_art'].values() for k,n in r.items() if k in ('label','caption')})
        if failures:raise ValueError('Installed PB artwork is damaged: '+'; '.join(failures[:10]))
        progress('Source already installed with newer folder artwork; no downgrade')
        return
    # Packages own only namespaced payloads. They cannot carry configuration,
    # catalogs, executables, source seals or global style/index replacements.
    allowed=manifest['payload_prefixes']
    for n in manifest['files']:
        if not any(n.startswith(p+'/') for p in allowed) or n in MUTABLE or n in DYNAMIC or n.endswith('/index.txt'):
            raise ValueError('Append pack attempts to replace shared/user data: '+n)
    other={n:r for k,v in state['packages'].items() if k!=key for n,r in v['files'].items()}
    for name in set(other)&set(manifest['files']):
        if not name.startswith('a/') or other[name]!=manifest['files'][name]:
            raise ValueError('Append payload conflicts with another source: '+name)
    journal=path_in(root,'.megamix/install-journal.json')
    transaction={'source':key,'content_id':manifest['content_id']}
    if journal.exists() and json.loads(journal.read_text())!=transaction:
        raise ValueError('Finish the previous interrupted pack install first')
    # Refuse existing unrelated payloads before writing anything.
    missing=0
    for n,row in manifest['files'].items():
        dst=path_in(root,n)
        if not dst.exists():missing+=1
        if dst.exists() and (dst.stat().st_size!=row['bytes'] or hash_file(dst)!=row['sha256']):
            raise ValueError('Existing payload differs; preserved without replacement: '+n)
    if previous and previous['content_id']==manifest['content_id'] and not journal.exists()and not missing:
        progress('Source already installed; payload verified, nothing changed')
        return
    lock=path_in(root,'.megamix/install.lock')
    try:
        fd=os.open(lock,os.O_WRONLY|os.O_CREAT|os.O_EXCL,0o600)
    except FileExistsError:
        raise ValueError('Another installer is active, or left install.lock after a forced stop. Check it before removing that lock.') from None
    try:
        with os.fdopen(fd,'w') as f:f.write(str(os.getpid())+'\n')
        materialized=None
        if manifest.get('deferred_text'):
            from deferred_text import materialize
            materialized=materialize(pack,root,manifest,base,progress,resolve)
        save_json(journal,transaction)
        path_in(root,'launcher/source.id').unlink(missing_ok=True)
        for i,(n,row) in enumerate(sorted(manifest['files'].items()),1):
            dst=path_in(root,n)
            if not dst.exists():
                dst.parent.mkdir(parents=True,exist_ok=True)
                tmp=dst.with_name(dst.name+'.installing')
                if tmp.is_symlink():raise ValueError('Symlink installation temporary')
                with tmp.open('wb') as out:
                    if n in rewritten:out.write(rewritten[n])
                    else:
                        source=path_in(materialized,routes[n])if materialized is not None and routes[n] in manifest['deferred_text']['jobs']else resolve(routes[n])
                        with source.open('rb') as src:shutil.copyfileobj(src,out,CHUNK)
                    out.flush();os.fsync(out.fileno())
                if hash_file(tmp)!=row['sha256']:raise ValueError('Copied pack hash mismatch: '+n)
                os.replace(tmp,dst)
            if i==1 or i%200==0:progress(f'Installed {i}/{len(manifest["files"])} files')
        failures=check_files(root,manifest['files'])
        if failures:raise ValueError('; '.join(failures[:10]))
        state['packages'][key]=manifest
        activate_indexes(root,state)
        journal.unlink();sync_directory(root)
        if materialized is not None:shutil.rmtree(materialized)
        progress('Append installed and verified; settings and saves preserved')
    finally:
        lock.unlink(missing_ok=True)


# Files a newer release may replace in an installed base without rebuilding it:
# the launcher, runtimes, mods and documentation. Song data, catalogs, styles,
# generated text and everything append packs own stay as installed.
UPDATABLE_PREFIXES=('launcher/','mods/','licenses/')
UPDATABLE_FILES={'MEGAMIX.ELF','mods.txt','patch-catalog.json','PATCHES.md','README.txt','README-ja.txt','README-ko.txt'}


def updatable(name):
    return (name in UPDATABLE_FILES or name.startswith(UPDATABLE_PREFIXES)) and name not in MUTABLE and name not in DYNAMIC


def update(iso, root, bundle, progress=print):
    """Replace the launcher and runtime files of an installed base from a newer
    release with the same contract. Packs, settings, saves and caches' inputs stay."""
    iso,root=Path(iso).absolute(),Path(root).absolute()
    state_path=path_in(root,'.megamix/installed.json')
    state=json.loads(state_path.read_text())
    if state.get('schema')!=SCHEMA:raise ValueError('Unsupported installed manifest')
    if path_in(root,'.megamix/install-journal.json').exists():raise ValueError('Finish the interrupted pack installation first')
    for installed in state['packages'].values():validate_manifest(installed)
    base=state['packages']['id'];target=base['target']
    with zipfile.ZipFile(bundle) as archive:
        release=json.loads(archive.read('release.json'))
        if release.get('schema')!=SCHEMA:raise ValueError('Unsupported release bundle')
        if release['contract']!=state['contract']:
            raise ValueError('This release has a different data contract; build a new base instead (nothing was changed)')
        disc=next(d for d in release['discs'] if d['source']=='id')
        if iso.stat().st_size!=disc['iso']['bytes']:raise ValueError('Expected the EMPRESS Disc1 ISO used for the base')
        recipe=json.loads(archive.read(disc['recipes'][target]))
        manifest=recipe['package']
        steps={n:r for n,r in recipe['files'].items() if updatable(n) and r['op'] in ('blob','launcher')}
        journal_path=path_in(root,'.megamix/update-journal.json')
        journal=json.loads(journal_path.read_text()) if journal_path.exists() else None
        if journal and journal.get('runtime_revision')!=manifest.get('runtime_revision'):
            raise ValueError('Finish the interrupted update with the release that started it')
        changed={n for n,r in steps.items() if base['files'].get(n)!=manifest['files'].get(n) or r['op']=='launcher'}
        new_files={}
        stage=path_in(root,'.megamix/update-stage')
        stage.mkdir(parents=True,exist_ok=True)
        with iso.open('rb') as src:
            for name in sorted(changed):
                row=steps[name]
                if row['op']=='launcher':
                    from iso_launcher import fill,pack,verify_packed
                    if row.get('packing') and manifest.get('derived_launcher')!=dict(schema=1,template_sha256=row['recipe']['template_sha256'],unpacked_sha256=row['recipe']['unpacked_sha256']):raise ValueError('Launcher source identity mismatch')
                    template=archive.read('blobs/'+row['template'])
                    data=fill(template,row['recipe'],lambda at,n:Extent(src,at,n,iso.stat().st_size).read(n))
                    if row.get('packing'):
                        data=pack(data,archive.read('blobs/'+row['stub']),row['packing']);verify_packed(data,row['memory'],row['packing'])
                    record={'bytes':len(data),'sha256':hash_bytes(data)}
                else:
                    data=archive.read('blobs/'+row['blob'])
                    if hash_bytes(data)!=row['blob']:raise ValueError('Corrupt release blob')
                    record=manifest['files'][name]
                    if record!={'bytes':len(data),'sha256':hash_bytes(data)}:raise ValueError('Release file record mismatch: '+name)
                if base['files'].get(name)==record:continue
                new_files[name]=record
                write_atomic(stage/hash_bytes(name.encode()),data)
    if not new_files:
        shutil.rmtree(stage,ignore_errors=True);journal_path.unlink(missing_ok=True)
        progress('Launcher and runtime already up to date; nothing changed');return
    lock=path_in(root,'.megamix/install.lock')
    try:fd=os.open(lock,os.O_WRONLY|os.O_CREAT|os.O_EXCL,0o600)
    except FileExistsError:raise ValueError('Another installer is active, or left install.lock after a forced stop. Check it before removing that lock.') from None
    try:
        with os.fdopen(fd,'w') as f:f.write(str(os.getpid())+'\n')
        save_json(journal_path,{'runtime_revision':manifest.get('runtime_revision')})
        path_in(root,'launcher/source.id').unlink(missing_ok=True)
        for name,record in sorted(new_files.items()):
            dst=path_in(root,name);tmp=stage/hash_bytes(name.encode())
            if hash_file(tmp)!=record['sha256']:raise ValueError('Staged update damaged: '+name)
            dst.parent.mkdir(parents=True,exist_ok=True);os.replace(tmp,dst)
        failures=check_files(root,new_files)
        if failures:raise ValueError('; '.join(failures[:10]))
        files=dict(base['files']);files.update(new_files)
        base=dict(base,files=files,runtime_revision=manifest.get('runtime_revision'))
        if 'derived_launcher' in manifest:base['derived_launcher']=manifest['derived_launcher']
        base['content_id']=content_id(base);validate_manifest(base)
        state['packages']['id']=base
        save_json(path_in(root,'megamix-package.json'),base)
        activate_indexes(root,state)
        journal_path.unlink();shutil.rmtree(stage,ignore_errors=True);sync_directory(root)
        progress(f'Updated {len(new_files)} launcher/runtime files to {manifest.get("runtime_revision")}; packs, settings and saves preserved. Rebuild the launcher cache if you use one.')
    finally:
        lock.unlink(missing_ok=True)


def reindex(root, progress=print):
    """Record hand-added song folders (listed in songs/styles.txt and their
    index.txt) in the installed indexes, so verify and cache accept them."""
    root=Path(root).absolute()
    state=json.loads(path_in(root,'.megamix/installed.json').read_text())
    if state.get('schema')!=SCHEMA:raise ValueError('Unsupported installed manifest')
    for name in ('install-journal.json','update-journal.json'):
        if path_in(root,'.megamix/'+name).exists():raise ValueError('Finish the interrupted install or update first')
    lock=path_in(root,'.megamix/install.lock')
    try:fd=os.open(lock,os.O_WRONLY|os.O_CREAT|os.O_EXCL,0o600)
    except FileExistsError:raise ValueError('Another installer is active, or left install.lock after a forced stop. Check it before removing that lock.') from None
    try:
        with os.fdopen(fd,'w') as f:f.write(str(os.getpid())+'\n')
        local=local_songs(root,state);activate_indexes(root,state)
        progress('Indexes updated; %d hand-added song folders kept%s' % (sum(map(len,local.values())),(' ('+', '.join(local)+')') if local else ''))
    finally:
        lock.unlink(missing_ok=True)


def main(argv=None):
    # Redirected Windows output can use a legacy encoding even with Python 3.14.
    # Keep that encoding, but don't let progress text or a filename abort a build.
    for stream in (sys.stdout,sys.stderr):
        if hasattr(stream,'reconfigure'):stream.reconfigure(errors='backslashreplace')
    argv=list(sys.argv[1:] if argv is None else argv)
    if argv and argv[0]=='cache':
        from cache_builder import generate
        p=argparse.ArgumentParser(description='Build launcher caches from an existing MEGAMIX installation')
        p.add_argument('root',type=Path);p.add_argument('--game-path',required=True,help='exact PS2 path, e.g. mmce0:/MEGAMIX/ or mass:/MEGAMIX/; HOST: host:')
        p.add_argument('--config-dir',type=Path,help='PC directory containing the launcher.cfg actually used')
        p.add_argument('--settings-path',help='console path to that separate configuration directory')
        p.add_argument('--temp-dir',type=Path,help='PC directory for temporary build files')
        a=p.parse_args(argv[1:]);generate(a.root,a.game_path,a.config_dir,a.settings_path,a.temp_dir)
    elif argv and argv[0]=='index':
        p=argparse.ArgumentParser(description='Refresh song indexes, keeping hand-added song folders')
        p.add_argument('root',type=Path);a=p.parse_args(argv[1:]);reindex(a.root)
    elif argv and argv[0]=='update':
        p=argparse.ArgumentParser(description='Update the launcher and runtimes of an installed base from this release')
        p.add_argument('iso',type=Path,help='the EMPRESS Disc1 ISO the base was built from');p.add_argument('root',type=Path)
        p.add_argument('--bundle',type=Path,default=Path(__file__).with_name('release.mmr'))
        a=p.parse_args(argv[1:]);update(a.iso,a.root,a.bundle)
    elif argv and argv[0] in ('install','verify'):
        command=argv.pop(0);p=argparse.ArgumentParser(description=__doc__)
        if command=='install':
            p.add_argument('pack',type=Path);p.add_argument('root',type=Path);a=p.parse_args(argv);install(a.pack,a.root)
        else:
            p.add_argument('root',type=Path);a=p.parse_args(argv);failures=verify(a.root)
            if failures:raise ValueError('\n'.join(failures[:20]))
            print('Installation verified')
    else:
        p=argparse.ArgumentParser(description=__doc__)
        p.add_argument('iso',type=Path);p.add_argument('output',type=Path)
        p.add_argument('--bundle',type=Path,default=Path(__file__).with_name('release.mmr'))
        p.add_argument('--target',choices=('console','host'),default='console',help='base executable target; default console supports USB/MMCE')
        group=p.add_mutually_exclusive_group()
        group.add_argument('--fonts',type=Path,metavar='FOLDER',help='Local font family folders; default: fonts beside this script when populated')
        group.add_argument('--no-custom-fonts',action='store_true',help='Ignore local font folders and use bundled fallback rendering')
        a=p.parse_args(argv);fonts=a.fonts
        if fonts is None and not a.no_custom_fonts:
            folder=Path(__file__).with_name('fonts')
            if folder.is_dir():
                from font_folder import scan
                if scan(folder,explicit=False):fonts=folder
        build(a.iso,a.output,a.bundle,a.target,fonts=fonts)


if __name__=='__main__':
    try:main()
    except KeyboardInterrupt:
        print('Interrupted. Repeat the same command to resume.',file=sys.stderr);sys.exit(130)
    except (ValueError,OSError,KeyError,TypeError,zipfile.BadZipFile) as e:
        print('MEGAMIX: '+str(e),file=sys.stderr);sys.exit(1)
