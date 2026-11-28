#!/usr/bin/env python3
"""Convert a whole folder of arcade IIDX songs into MEGAMIX song folders, one style folder per version.

  python3 arcade_songdb.py music_data.bin music_omni.bin -o arcade-songs.json      (once)
  python3 arcade_batch.py ARCADE_DATA MEGAMIX --db arcade-songs.json --style pinky_crush [--jobs 4] [--install]

ARCADE_DATA is the game's contents folder: songs are taken from data/sound and every data_mods/*/sound
(omnimix_32, omnimix_33, ...; a mod's copy of a song wins). Any other folder is searched as it is. Songs are
<id>.ifs files, or <id>/ folders holding <id>.1 with <id>.2dx or <id>.s3p. MEGAMIX is your built MEGAMIX folder: it supplies the template record
(EMPRESS/SLPM_552.21) and the native text and folder containers. Hold sprites: --hold-sprites, else those of a
song converted before, else made from the arcade's own notes_cn00.ifs (found under ARCADE_DATA; needs Pillow). The song database (arcade_songdb.py; or --music music_data.bin, repeatable) gives
each song's title, artist, genre, levels and version. --style picks the style folders to convert (repeatable or
comma separated: names as below, or version numbers); without it every style from SIRIUS on.

Each song goes to OUT/songs/<style>/<folder>/ (OUT defaults to MEGAMIX/arcade-batch), where <style> is its
version's folder: sirius, resort_anthem, lincle, ... pinky_crush, sparkle_shower. A version without a style
folder in MEGAMIX gets one (label, caption, style.txt). Work files stay in OUT/work/<id>/, so running the
same command again skips songs already done and retries the ones that failed; OUT/batch.json lists both.

--install then copies the songs into MEGAMIX/songs, adds them to their styles' index.txt and new styles to
songs/styles.txt (arcade styles kept in version order, starting where the first one already is, otherwise at
the top), and runs `megamix.py index`. Existing songs, settings and saves are not touched. Rebuild the launcher
cache afterwards if you use one.

Needs numpy, and ffmpeg on the PATH for .s3p (WMA) keysounds. A version whose titles use characters the
bundled font atlas lacks gets its own atlas from the pinned open fonts (downloaded once into OUT/fonts; needs
Pillow and fonttools).
"""
import argparse
import concurrent.futures
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import traceback
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import arcade_data as A  # noqa: E402

GAME_VERSION = 988          # music_data v32 entry: u16 game version (0 1st style, 1 substream, 2 2nd style, ...)
# version -> (style folder, folder label (native label glyphs: upper case), caption, style name)
STYLES = {
    17: ('sirius', 'SIRIUS', 'SIRIUS'),
    18: ('resort_anthem', 'RESORT ANTHEM', 'Resort Anthem'),
    19: ('lincle', 'LINCLE', 'Lincle'),
    20: ('tricoro', 'TRICORO', 'tricoro'),
    21: ('spada', 'SPADA', 'SPADA'),
    22: ('pendual', 'PENDUAL', 'PENDUAL'),
    23: ('copula', 'COPULA', 'copula'),
    24: ('sinobuz', 'SINOBUZ', 'SINOBUZ'),
    25: ('cannon_ballers', 'CANNON BALLERS', 'CANNON BALLERS'),
    26: ('rootage', 'ROOTAGE', 'Rootage'),
    27: ('heroic_verse', 'HEROIC VERSE', 'HEROIC VERSE'),
    28: ('bistrover', 'BISTROVER', 'BISTROVER'),
    29: ('casthour', 'CASTHOUR', 'CastHour'),
    30: ('resident', 'RESIDENT', 'RESIDENT'),
    31: ('epolis', 'EPOLIS', 'EPOLIS'),
    32: ('pinky_crush', 'PINKY CRUSH', 'Pinky Crush'),
    33: ('sparkle_shower', 'SPARKLE SHOWER', 'Sparkle Shower'),
}
TEMPLATE_SONG = 2           # All My Turn: the record template (dump_record.py)


def parse_versions(text):
    out = set()
    for part in text.split(','):
        lo, _, hi = part.partition('-')
        out.update(range(int(lo), int(hi or lo) + 1))
    return out


def _scan(root, found):
    for dirpath, dirnames, filenames in os.walk(root):
        d = Path(dirpath)
        for f in filenames:
            m = re.fullmatch(r'(\d{4,5})\.ifs', f, re.I)
            if m:
                found[int(m.group(1))] = d / f
        if re.fullmatch(r'\d{4,5}', d.name) and (d / (d.name + '.1')).is_file():
            found[int(d.name)] = d


def sound_folders(root):
    """The sound folders of a game's contents folder: data/sound, then every data_mods/*/sound (omnimix_32,
    omnimix_33, ...), so a mod's copy of a song wins over the game's. Any other folder is searched as it is."""
    root = Path(root)
    mods = sorted(p for p in (root / 'data_mods').glob('*/sound') if p.is_dir()) if (root / 'data_mods').is_dir() else []
    base = [root / 'data' / 'sound'] if (root / 'data' / 'sound').is_dir() else []
    return base + mods if base or mods else [root]


def song_volume(entry64):
    """The song's arcade volume, 0..127 (music_data entry +0x680)."""
    import base64
    e = base64.b64decode(entry64)
    return min(127, e[0x680]) if len(e) > 0x680 else 127


def song_variants(entry64):
    """The keysound file per chart (music_data entry +0x684, 10 characters, arcade_convert.VARIANT_ORDER):
    '0' = the song's own <id>.2dx, another character c = <id>c.2dx."""
    import base64
    e = base64.b64decode(entry64)[0x684:0x68e]
    return ''.join(chr(c) if 0x21 <= c < 0x7f else '0' for c in e).ljust(10, '0')


def find_sources(root):
    """{song id: path} of every <id>.ifs and <id>/ folder with <id>.1 in the sound folders (sound_folders);
    within one folder an <id>/ folder wins over an <id>.ifs."""
    found = {}
    for folder in sound_folders(root):
        part = {}
        _scan(folder, part)
        found.update(part)
    return found


def slug(title, used):
    s = re.sub(r'[^a-z0-9]+', '_', title.lower()).strip('_')[:40].strip('_') or 'song'
    name, n = s, 2
    while name in used:
        name, n = '%s_%d' % (s, n), n + 1
    used.add(name)
    return name


def song_info(db, sid):
    r = db['songs'].get(str(sid))
    if r is None:
        return None
    import arcade_text as X
    r = dict(r, **X.decode_entities({k: r[k] for k in ('title', 'ascii_title', 'genre', 'artist')}))  # databases built before
    return {'id': sid, 'entry': r['entry'], 'version': r['version'], 'texts': {k: r[k] for k in ('title', 'ascii_title', 'genre', 'artist')},
            'levels': r['levels'], 'title': r['title'] or r['ascii_title'], 'ascii': r['ascii_title'] or r['title']}


def parse_styles(text):
    """versions named by --style: style folder names (pinky_crush), or version numbers / ranges (32, 17-20)"""
    names = {s[0]: v for v, s in STYLES.items()}
    out = set()
    for part in text.replace(' ', '').split(','):
        if not part:
            continue
        if part.lower() in names:
            out.add(names[part.lower()])
        elif re.fullmatch(r'\d+(-\d+)?', part):
            out |= parse_versions(part)
        else:
            raise SystemExit('unknown style %r: one of %s, or a version number' % (part, ', '.join(names)))
    return out


def style_atlas(out, style, texts, profile):
    """The bundled arcade atlas when it draws every text of the style, else one built for them (cached)."""
    import arcade_text as X
    import text_raster as R
    base = X.atlas()
    roles = (('title', 'large_title'), ('genre', 'card_genre'), ('artist', 'card_artist'))
    if not any(X.missing_glyphs(base, t[k], r) for t in texts for k, r in roles):
        return None
    corpus = json.loads((HERE / 'text-fonts' / 'corpus.json').read_text(encoding='utf-8'))
    seen = set(corpus)
    for t in texts:
        for k in ('title', 'ascii_title', 'genre', 'artist'):
            if t.get(k) and t[k] not in seen:
                seen.add(t[k]); corpus.append(t[k])
    blob = json.dumps(corpus, ensure_ascii=False).encode()
    path = out / 'fonts' / ('%s-%s.mga' % (style, hashlib.sha256(blob).hexdigest()[:12]))
    if not path.is_file():
        fonts = out / 'fonts' / 'src'
        if not (fonts / 'open-fonts.json').is_file():
            print('fetching the pinned open fonts into', fonts)
            subprocess.run([sys.executable, str(HERE / 'fetch_text_fonts.py'), str(profile), str(fonts)], check=True)
        corpus_path = out / 'fonts' / (style + '-corpus.json')
        corpus_path.write_bytes(blob)
        print('%s: building a font atlas for its titles' % style)
        subprocess.run([sys.executable, str(HERE / 'prepare_text_fonts.py'), 'open-fonts.json', str(corpus_path.resolve()),
                        str(path.resolve())], check=True, cwd=fonts, stdout=subprocess.DEVNULL)
    return path


def convert_one(job):
    """Worker: one song, source -> work/<id> -> songs/<style>/<folder>. Returns a report row."""
    import arcade_convert as C
    import arcade_songfolder as F
    import arcade_text as X
    import text_raster as R
    row = {'id': job['id'], 'title': job['title'], 'style': job['style'], 'folder': job['folder']}
    try:
        work = Path(job['work'])
        layout = 'native' if job.get('empress_audio') else 'split'
        made = work / 'convert.json'
        if made.is_file():
            old = json.loads(made.read_text(encoding='utf-8'))
            if old.get('layout', 'split') != layout:
                made.unlink()                               # converted with the other audio layout: redo
            elif layout == 'native' and old.get('native_revision', 1) < C.NATIVE_REVISION:
                rev = old.get('native_revision', 1)
                if ((rev < 3 and set(job.get('variants', '0')) - {'0'})     # per-chart keysound files: were ignored
                        or (rev < 2 and C.stream_start(job['source']))):    # its track starts after 0 ms: the stream was late
                    made.unlink()
                else:                                       # nothing changed for it
                    old['native_revision'] = C.NATIVE_REVISION
                    made.write_text(json.dumps(old, indent=1) + '\n', encoding='utf-8')
        if not made.is_file():
            if work.exists():
                shutil.rmtree(work)
            C.main([job['source'], str(work), '--split'] +
                   (['--native', '--volume', str(job.get('volume', 127)), '--variants', job.get('variants', '0' * 10)]
                    if job.get('empress_audio') else []))
        atlas = R.Atlas(Path(job['atlas']).read_bytes()) if job['atlas'] else None
        containers = X.base_containers(Path(job['megamix']))
        text_files = X.render(job['texts'], containers, atlas)
        dest = Path(job['dest'])
        if dest.exists():
            shutil.rmtree(dest)
        info = F.build(work, Path(job['template']).read_bytes(), dest, job['ascii'], job['levels'], job.get('version', 17),
                       Path(job['sprites']) if job['sprites'] else None, text_files, job['id'])
        row.update(ok=True, charts=info['charts'], bpm=info['bpm'], layout=layout,
                   **({'native_revision': C.NATIVE_REVISION} if layout == 'native' else {}))
    except Exception as e:  # noqa: BLE001 - one song's failure must not stop the batch
        row.update(ok=False, error='%s: %s' % (type(e).__name__, e), trace=traceback.format_exc()[-1500:])
    return row


def hold_sprites(megamix, given, arcade=None, out=None):
    """The hold sprites folder: --hold-sprites, a song converted before, or made from the arcade's own
    notes_cn00.ifs (data/graphic) with konami_tex.py and tools/holds/hold_sprites.py (needs Pillow)."""
    import arcade_songfolder as F
    names = F.hold_sprite_names()
    have = lambda d: d.is_dir() and all((d / (n + '.spr')).is_file() for n in names)
    if given:
        if not have(Path(given)):
            raise SystemExit('%s does not hold the %d hold sprites' % (given, len(names)))
        return Path(given)
    if out is not None and have(out / 'hold-sprites'):
        return out / 'hold-sprites'
    for d in sorted((megamix / 'songs').glob('*/*/holds')):
        if have(d):
            return d
    if arcade is None or out is None:
        return None
    tex = next((p for folder in [Path(arcade)] for p in sorted(folder.rglob('notes_cn00.ifs'))), None)
    if tex is None:
        return None
    print('making the hold sprites from', tex)
    png = out / 'hold-sprites-png'
    subprocess.run([sys.executable, str(HERE / 'konami_tex.py'), str(tex), str(png)], check=True, stdout=subprocess.DEVNULL)
    subprocess.run([sys.executable, str(HERE.parent / 'holds' / 'hold_sprites.py'), str(png), str(png), str(out / 'hold-sprites')],
                   check=True, stdout=subprocess.DEVNULL)
    return out / 'hold-sprites' if have(out / 'hold-sprites') else None


def install(megamix, out, done, fresh=None):
    """Copy the converted songs into MEGAMIX and list them; then megamix.py index. With `fresh` (ids converted in
    this run), only those and songs MEGAMIX does not have yet are copied: the rest are already installed."""
    import megamix as M
    songs = megamix / 'songs'
    styles_path = songs / 'styles.txt'
    order = [l.strip() for l in styles_path.read_text(encoding='utf-8').splitlines() if l.strip()]
    arcade = [s for s, _, _ in STYLES.values()]
    by_style, copy = {}, set()
    for r in done:
        by_style.setdefault(r['style'], []).append(r['folder'])
        if fresh is None or r['id'] in fresh:
            copy.add((r['style'], r['folder']))
    for style, folders in by_style.items():
        src = out / 'songs' / style
        # Windows folders ignore case: a hand-made "Sirius" line is this style's folder, not a new one
        style_dir = next((s for s in order if s.casefold() == style.casefold()), style)
        dst = songs / style_dir
        dst.mkdir(parents=True, exist_ok=True)
        for f in ('style.txt', 'label.spr', 'caption.spr'):
            if (src / f).is_file() and not (dst / f).is_file():
                shutil.copyfile(src / f, dst / f)
        for folder in folders:
            if (style, folder) not in copy and (dst / folder / 'song.txt').is_file():
                continue                             # installed by an earlier run, unchanged since
            if (dst / folder).exists():
                shutil.rmtree(dst / folder)
            shutil.copytree(src / folder, dst / folder)
        index = dst / 'index.txt'
        listed = [l.strip() for l in index.read_text(encoding='utf-8').splitlines() if l.strip()] if index.is_file() else []
        index.write_text(''.join(n + '\n' for n in listed + [f for f in folders if f not in listed]), encoding='utf-8')
        if style_dir not in order:                   # arcade styles in version order, from the first one
            after = [s for s in order if s in arcade and arcade.index(s) < arcade.index(style)]
            before = [s for s in order if s in arcade and arcade.index(s) > arcade.index(style)]
            at = order.index(after[-1]) + 1 if after else order.index(before[0]) if before else 0
            order.insert(at, style_dir)
    M.write_atomic(styles_path, ''.join(s + '\n' for s in order))
    M.reindex(megamix)


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('arcade', type=Path, help='folder searched for <id>.ifs files and <id>/ song folders')
    p.add_argument('megamix', type=Path, help='your MEGAMIX folder')
    p.add_argument('--db', type=Path, help='song database (arcade_songdb.py)')
    p.add_argument('--music', type=Path, action='append', help='music_data.bin instead of --db (repeatable, the preferred first)')
    p.add_argument('--style', action='append', help='style folders to convert: names (pinky_crush) or versions (32, 17-20); '
                   'repeatable or comma separated; default every style from sirius on')
    p.add_argument('--out', type=Path, help='output and work folder (default MEGAMIX/arcade-batch)')
    p.add_argument('--ids', help='only these song ids, comma separated')
    p.add_argument('--limit', type=int, help='convert at most this many songs (smallest ids first)')
    p.add_argument('--jobs', type=int, default=max(1, min(4, (os.cpu_count() or 2) // 2)),
                   help='songs converted in parallel (each needs about 1 GB of memory; default %(default)s)')
    p.add_argument('--hold-sprites', type=Path, help='hold sprites folder (default: from a song converted before)')
    p.add_argument('--install', action='store_true', help='copy the results into MEGAMIX and run megamix.py index '
                   '(songs converted in this run, and any MEGAMIX does not have yet)')
    p.add_argument('--reinstall', action='store_true', help='with --install: copy every converted song again')
    p.add_argument('--empress-audio', dest='empress_audio', action='store_true', default=True,
                   help='EMPRESS\'s own layout, as on the CS disc (arcade_convert.py --native; the default): every arcade '
                        'sound in one bank, the backing track in the stream at the song\'s arcade volume (ALL MY TURN: '
                        '11.8 MB instead of about 60; plays the same as the CS version)')
    p.add_argument('--legacy-audio', dest='empress_audio', action='store_false',
                   help='the earlier layout: one bank per difficulty, the autoplay mix of the BGM in the stream')
    p.add_argument('--version', type=int, default=17,
                   help='record version byte (default 17, SIRIUS; 16 = EMPRESS slowed arcade songs down in PCSX2)')
    a = p.parse_args(argv)
    megamix, out = a.megamix.resolve(), (a.out or a.megamix / 'arcade-batch').resolve()
    try:
        import numpy  # noqa: F401
    except ImportError:
        sys.exit('needs numpy: python3 -m pip install numpy')
    import arcade_songdb as D
    if a.db:
        db = D.load(a.db)
    elif a.music:
        db = D.build(a.music)
    else:
        p.error('give --db (arcade_songdb.py) or --music')
    sources = find_sources(a.arcade)
    versions = parse_styles(','.join(a.style)) if a.style else set(STYLES)
    ids = {int(x) for x in a.ids.split(',')} if a.ids else None
    songs = []
    unknown = []
    for sid in sorted(sources):
        if ids is not None and sid not in ids:
            continue
        info = song_info(db, sid)
        if info is None:
            unknown.append(sid)
            continue
        if info['version'] in versions and info['version'] in STYLES:
            songs.append(info)
    if a.limit:
        songs = songs[:a.limit]
    if not songs:
        sys.exit('no songs of %s found in %s (%d song sources seen)' % (', '.join(STYLES[v][0] for v in sorted(versions) if v in STYLES), a.arcade, len(sources)))
    out.mkdir(parents=True, exist_ok=True)
    sprites = hold_sprites(megamix, a.hold_sprites, a.arcade, out)
    if not sprites:
        print('warning: no hold sprites (no notes_cn00.ifs under %s; give --hold-sprites): charge notes will draw as '
              'plain notes' % a.arcade, file=sys.stderr)
    template = out / 'record_template.bin'
    if not template.is_file():
        import dump_record
        dump_record.main([str(megamix), str(TEMPLATE_SONG), str(template)])
    # folder names: kept from an earlier run so a rerun replaces, not duplicates
    report_path = out / 'batch.json'
    previous = {r['id']: r for r in json.loads(report_path.read_text())['songs']} if report_path.is_file() else {}
    used = {}
    for s in songs:
        style = STYLES[s['version']][0]
        taken = used.setdefault(style, {d.name for d in (megamix / 'songs' / style).glob('*') if d.is_dir()} - {r['folder'] for r in previous.values()})
        s['style'] = style
        s['folder'] = previous[s['id']]['folder'] if s['id'] in previous and previous[s['id']]['style'] == style else slug(s['ascii'], taken)
        taken.add(s['folder'])
    # styles: folder art for versions MEGAMIX has no folder for; one font atlas per style if needed
    import arcade_style as S
    import arcade_text as X
    containers = S.base_containers(megamix)
    atlases = {}
    for v in sorted({s['version'] for s in songs}):
        style, label, caption = STYLES[v]
        members = [s for s in songs if s['style'] == style]
        if not (megamix / 'songs' / style / 'style.txt').is_file() and not (out / 'songs' / style / 'style.txt').is_file():
            S.write_style(out, style, caption, label, caption, [], containers)
        texts = [s['texts'] for s in members]
        atlases[style] = style_atlas(out, style, texts, HERE / 'text-fonts' / 'open-fonts.sources.json')
    import arcade_convert as C
    jobs = []
    for s in songs:
        done = (previous.get(s['id'], {}).get('ok') and (out / 'songs' / s['style'] / s['folder'] / 'song.txt').is_file()
                and previous[s['id']].get('layout', 'split') == ('native' if a.empress_audio else 'split')
                and (not a.empress_audio or previous[s['id']].get('native_revision', 1) >= C.NATIVE_REVISION))
        if done:
            continue
        jobs.append({'id': s['id'], 'title': s['title'], 'ascii': s['ascii'], 'style': s['style'], 'folder': s['folder'],
                     'texts': s['texts'], 'levels': s['levels'], 'empress_audio': a.empress_audio, 'version': a.version,
                     'volume': song_volume(s['entry']), 'variants': song_variants(s['entry']),
                     'source': str(sources[s['id']]), 'work': str(out / 'work' / str(s['id'])),
                     'megamix': str(megamix), 'template': str(template), 'sprites': str(sprites) if sprites else None,
                     'atlas': str(atlases[s['style']]) if atlases[s['style']] else None,
                     'dest': str(out / 'songs' / s['style'] / s['folder'])})
    print('%d songs (%d already done), %d jobs in parallel' % (len(songs), len(songs) - len(jobs), a.jobs))
    rows = {i: r for i, r in previous.items() if r.get('ok')}     # earlier runs' songs stay in the report
    with concurrent.futures.ProcessPoolExecutor(max_workers=a.jobs) as pool:
        for n, r in enumerate(pool.map(convert_one, jobs), 1):
            rows[r['id']] = r
            print('[%d/%d] %s %s: %s' % (n, len(jobs), r['id'], r['title'], 'ok' if r.get('ok') else r['error']), flush=True)
            report = {'songs': sorted(rows.values(), key=lambda x: x['id']), 'unknown_ids': unknown}
            report_path.write_text(json.dumps(report, ensure_ascii=False, indent=1))
    report = {'songs': sorted(rows.values(), key=lambda x: x['id']), 'unknown_ids': unknown}
    report_path.write_text(json.dumps(report, ensure_ascii=False, indent=1))
    this = {s['id'] for s in songs}
    ok = [r for r in rows.values() if r.get('ok') and r['id'] in this]
    failed = [r for r in rows.values() if not r.get('ok') and r['id'] in this]
    by = {}
    for r in ok:
        by[r['style']] = by.get(r['style'], 0) + 1
    print('converted %d songs: %s' % (len(ok), ', '.join('%s %d' % kv for kv in sorted(by.items()))))
    if failed:
        print('%d failed (details in %s): %s' % (len(failed), report_path, ', '.join(str(r['id']) for r in failed[:20])))
    if unknown:
        print('%d song sources are not in the music data: %s' % (len(unknown), ', '.join(map(str, unknown[:20]))))
    if a.install:
        install(megamix, out, ok, None if a.reinstall else {j['id'] for j in jobs})
        print('installed into %s; rebuild the launcher cache if you use one' % megamix)
    else:
        print('results in %s; run again with --install to add them to MEGAMIX' % (out / 'songs'))


if __name__ == '__main__':
    main()
