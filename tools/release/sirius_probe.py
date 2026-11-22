#!/usr/bin/env python3
"""Measure how IIDX 17 SIRIUS arcade data maps onto EMPRESS's native song files.

  python3 sirius_probe.py SIRIUS_DATA MEGAMIX_DIR [--pair empress/id_97=17000 ...] [-o report.json]
  python3 sirius_probe.py --bms SIRIUS_BMS MEGAMIX_DIR [--pair empress/id_97=FOLDER ...]

SIRIUS_DATA is the arcade data/ folder. Song files (<id>.1, <id>.2dx) are found
anywhere under sound/ or sd_data/ (or --sound); music_data.bin anywhere below it.
MEGAMIX_DIR is a built MEGAMIX base. Songs that exist in both games (EMPRESS's
arcade songs, Bad Maniacs, DOMINION...) are compared to measure, from real
files rather than guesses:

  * the chart time unit (least-squares fit of EMPRESS note times on arcade ones)
  * which arcade .1 slot becomes which EMPRESS chart field
  * how player keysounds and BGM events are represented on each side
  * EMPRESS bank/stream headers and whether the ADPCM disguise rule holds
  * the music_data.bin entry layout next to the matching EMPRESS song record

The report holds structure and statistics only (no audio, no chart data), so it
can be shared to finish the converter. Nothing is written except the report.
"""
import argparse
import json
from pathlib import Path
import re
import struct
import sys

import arcade_data as A
import spu_adpcm as S

RECORD = 0x134
EXE_TABLE = 0x178bf0
EXE = 'EMPRESS/SLPM_552.21'


def hexrow(data, limit=None):
    data = data if limit is None else data[:limit]
    return data.hex()


def histogram(values):
    out = {}
    for v in values:
        out[str(v)] = out.get(str(v), 0) + 1
    return dict(sorted(out.items(), key=lambda kv: -kv[1]))


# ----------------------------------------------------------------- EMPRESS side

def empress_chart(raw):
    from chart_transform import crypt, decode_lz
    if len(raw) < 8 or len(raw) > 1 << 20:
        return None
    try:
        ply = decode_lz(crypt(raw, 'empress', True))
    except (ValueError, IndexError):
        return None
    if len(ply) < 8:
        return None
    off, clock = struct.unpack_from('<II', ply)
    if off not in (8, 12, 16):
        return None
    events = []
    for at in range(off, len(ply) - 7, 8):
        t, cmd, c2, value = struct.unpack_from('<IBBH', ply, at)
        if t == 0x7fff:
            break
        events.append((t, cmd, c2, value))
    return {'clock': clock, 'events': events}


def chart_summary(events):
    kinds = histogram(e[1] & 15 for e in events)
    notes = [e for e in events if e[1] & 15 in (0, 1)]
    return {'events': len(events), 'types': kinds, 'notes': len(notes),
            'first_note': notes[0][0] if notes else None, 'last_note': notes[-1][0] if notes else None,
            'tempo_events': [list(e) for e in events if e[1] & 15 == 4][:4],
            'bgm_events': sum(1 for e in events if e[1] & 15 == 7),
            'bgm_first': [list(e) for e in events if e[1] & 15 == 7][:4],
            'sample_values': sorted({e[3] for e in events if e[1] & 15 in (2, 3)})[:8],
            'sample_distinct': len({e[3] for e in events if e[1] & 15 in (2, 3)}),
            'note_cn': sum(1 for e in notes if e[3]), 'first_events': [list(e) for e in events[:12]]}


def bank_summary(raw):
    out = {'bytes': len(raw), 'magic': raw[:4].hex(), 'head': hexrow(raw, 64)}
    if len(raw) >= 0x8010:
        count, size, flag, extra = struct.unpack_from('<4I', raw, 0x8000)
        out.update(count=count, size=size, flag=flag, extra=extra)
        prefix = raw[:0x8000]
        nz = [i for i in range(0, 0x8000, 16) if prefix[i:i + 16].strip(b'\0')]
        out['prefix_nonzero_rows'] = len(nz)
        out['prefix_rows'] = {hex(i): hexrow(prefix[i:i + 16]) for i in nz[:24]}
        if count <= 4096:
            start = 0x8010 + 16 * count
            out['table'] = [hexrow(raw[0x8010 + 16 * i:0x8020 + 16 * i]) for i in range(min(count, 24))]
            body = raw[start:start + size]
            for name, delta in (('raw', None), ('bank_delta', S.BANK_DELTA), ('stream_delta', S.STREAM_DELTA)):
                score, flags = S.plausibility(body, delta)
                out['adpcm_' + name] = {'legal': round(score, 5), 'flags': {str(k): v for k, v in sorted(flags.items())}}
    return out


def stream_summary(raw):
    out = {'bytes': len(raw), 'header': list(struct.unpack_from('<12I', raw)) if len(raw) >= 48 else None}
    if len(raw) > 0x800:
        body = raw[0x800:]
        for name, delta in (('raw', None), ('stream_delta', S.STREAM_DELTA), ('bank_delta', S.BANK_DELTA)):
            score, flags = S.plausibility(body, delta)
            out['adpcm_' + name] = {'legal': round(score, 5), 'flags': {str(k): v for k, v in sorted(flags.items())[:8]}}
    return out


def classify(raw):
    if raw[:2] == b'wp':
        return 'bank'
    if len(raw) >= 4 and struct.unpack_from('<I', raw)[0] == S.STREAM_MAGIC:
        return 'stream'
    if len(raw) < 1 << 20 and empress_chart(raw):
        return 'chart'
    return 'other'


def empress_song(root, folder):
    base = root / 'songs' / folder
    text = (base / 'song.txt').read_text(encoding='utf-8')
    out = {'folder': folder, 'song_txt': text.splitlines()[:40], 'files': [], 'record': None}
    name = next((l[5:] for l in text.splitlines() if l.startswith('name ')), folder)
    out['name'] = name
    sid = next((int(l.split()[1]) for l in text.splitlines() if re.fullmatch(r'id \d+', l)), None)
    paths = []
    for line in text.splitlines():
        parts = line.split()
        if parts[:1] == ['E'] and len(parts) == 5:
            paths.append((None, parts[4]))
        elif parts[:1] == ['F'] and len(parts) == 4:
            paths.append((parts[1], parts[3]))
        elif parts[:1] == ['record']:
            rec = (base / parts[1]).read_bytes()
            out['record'] = hexrow(rec)
    if sid is not None and (root / EXE).is_file():
        exe = (root / EXE).read_bytes()
        at = EXE_TABLE + sid * RECORD
        out['record'] = hexrow(exe[at:at + RECORD])
    charts = []
    seen = set()
    for field, rel in paths:
        p = root / rel.lstrip('/') if rel.startswith('/') or field is None else base / rel
        if p in seen or not p.is_file():
            continue
        seen.add(p)
        raw = p.read_bytes() if p.stat().st_size < 96 << 20 else p.open('rb').read(1 << 20)
        kind = classify(raw)
        row = {'field': field, 'path': rel, 'bytes': p.stat().st_size, 'kind': kind}
        if kind == 'chart':
            ply = empress_chart(raw)
            row['clock'] = ply['clock']
            row['summary'] = chart_summary(ply['events'])
            charts.append((rel, ply['events']))
        elif kind == 'bank':
            row['summary'] = bank_summary(raw)
        elif kind == 'stream':
            row['summary'] = stream_summary(raw)
        else:
            row['head'] = hexrow(raw, 32)
        out['files'].append(row)
    return out, charts


# ------------------------------------------------------------------ Sirius side

SOUND_DIRS = ('sound', 'sd_data')


def locate(data_dir, sound=None, keysounds=None):
    """Find song files anywhere under the data folder: {id: {'.1': path, '.2dx': path, ...}}.

    Arcade dumps differ (sound/<id>/<id>.1, sd_data/<id>.1, ...), so the song id
    is taken from each file name, e.g. 17010.1, 17010.2dx, 17010_pre.2dx.
    """
    roots = [sound] if sound else [data_dir / d for d in SOUND_DIRS if (data_dir / d).is_dir()] or [data_dir]
    songs, music = {}, None
    for root in roots:
        for path in sorted(root.rglob('*')):
            if not path.is_file():
                continue
            stem, suffix = path.stem.lower(), path.suffix.lower()
            sid = stem[:-4] if stem.endswith('_pre') else stem
            if suffix in ('.1', '.2dx') and sid.isdigit():
                key = suffix if not stem.endswith('_pre') else '_pre' + suffix
                songs.setdefault(int(sid), {})[key] = path
    if keysounds:
        for path in sorted(keysounds.rglob('*')):
            stem = path.stem.lower()
            sid = stem[:-4] if stem.endswith('_pre') else stem
            if path.is_file() and path.suffix.lower() == '.2dx' and sid.isdigit():
                songs.setdefault(int(sid), {})['_pre.2dx' if stem.endswith('_pre') else '.2dx'] = path
    for path in sorted(data_dir.rglob('music_data.bin')):
        music = path
        break
    return songs, music


def layout(data_dir, limit=40):
    """Short folder summary for reports when no song files are recognised."""
    out = {'top': sorted(p.name + ('/' if p.is_dir() else '') for p in data_dir.iterdir())[:limit], 'extensions': {}}
    for path in data_dir.rglob('*'):
        if path.is_file():
            rel = path.relative_to(data_dir).parts
            top = rel[0] if len(rel) > 1 else '.'
            ext = path.suffix.lower() or '(none)'
            row = out['extensions'].setdefault(top, {})
            row[ext] = row.get(ext, 0) + 1
    return out


def sirius_song(files, sid):
    paths = files.get(sid, {})
    out = {'id': sid, 'files': sorted(k + ':' + v.name for k, v in paths.items())}
    charts = {}
    one = paths.get('.1')
    if one:
        raw = one.read_bytes()
        out['directory'] = [list(r) for r in A.chart_directory(raw)]
        charts = A.read_charts(raw)
        out['charts'] = {str(k): chart_summary(v) for k, v in charts.items()}
    two = paths.get('.2dx')
    if two:
        head = two.open('rb').read(4)
        if head[:1] == b'%':
            out['keysounds'] = {'error': 'encrypted .2dx (' + head.decode('latin-1') + ')'}
            return out, charts
        try:
            name, entries = A.read_2dx(two.read_bytes())
            fmts = []
            for e in entries:
                try:
                    i = A.wav_info(e['wav'])
                    fmts.append((i['format'], i['channels'], i['rate'], i['block'], i['bits']))
                except A.FormatError as err:
                    fmts.append(str(err))
            out['keysounds'] = {'name': name, 'count': len(entries), 'formats': histogram(fmts),
                                'attenuation': histogram(e['attenuation'] for e in entries),
                                'track': histogram(e['track'] for e in entries),
                                'unk': histogram(e['unk'] for e in entries), 'loop': histogram(e['loop'] for e in entries),
                                'wav_bytes': sum(len(e['wav']) for e in entries)}
        except A.FormatError as err:
            out['keysounds'] = {'error': str(err), 'head': hexrow(two.read_bytes()[:160])}
    return out, charts


def bms_songs(root):
    """{key: (folder, {difficulty/file: Chart})} for every BMS song folder."""
    import bms
    out = {}
    for i, (folder, paths) in enumerate(bms.song_folders(root).items()):
        charts = {}
        for path in paths:
            try:
                chart = bms.read(path)
            except (A.FormatError, ValueError, OSError) as e:
                charts[path.name] = e
                continue
            charts[chart.difficulty + ':' + path.name] = chart
        out[i] = (folder, charts)
    return out


def bms_summary(folder, charts, ffmpeg=None):
    import bms
    out = {'folder': folder.name, 'charts': {}}
    wavs = {}
    for name, chart in charts.items():
        if isinstance(chart, Exception):
            out['charts'][name] = {'error': str(chart)}
            continue
        row = chart_summary(chart.events)
        row.update(notes_p1p2=list(chart.notes), random=chart.random, ignored=chart.ignored, bga_images=chart.bga,
                   header={k: chart.header[k] for k in ('TITLE', 'SUBTITLE', 'ARTIST', 'GENRE', 'BPM', 'PLAYLEVEL', 'RANK', 'TOTAL', 'DIFFICULTY', 'PLAYER') if k in chart.header})
        out['charts'][name] = row
        wavs.update(chart.wav)
    index = {p.name.lower(): p for p in folder.iterdir() if p.is_file()}
    found = {k: bms.find_audio(folder, v, index) for k, v in wavs.items()}
    out['keysounds'] = {'defined': len(wavs), 'missing': sum(1 for v in found.values() if v is None),
                        'extensions': histogram(v.suffix.lower() for v in found.values() if v),
                        'bytes': sum(v.stat().st_size for v in found.values() if v)}
    sample = [v for v in found.values() if v][:3]
    formats = []
    for path in sample:
        try:
            if path.suffix.lower() == '.wav':
                i = A.wav_info(path.read_bytes())
                formats.append([i['format'], i['channels'], i['rate'], i['bits']])
            else:
                rate, ch = bms.decode_audio(path, ffmpeg)
                formats.append(['decoded', len(ch), rate, len(ch[0])])
        except (A.FormatError, OSError, ValueError) as e:
            formats.append(str(e))
    out['keysounds']['sample_formats'] = formats
    return out


def bms_pairs(root, songs, limit):
    """Pair BMS songs with installed EMPRESS songs by chart note counts."""
    prints = empress_fingerprints(root)
    counts = {k: {sum(c.notes) for c in charts.values() if not isinstance(c, Exception)} for k, (_, charts) in songs.items()}
    rows, scores = {}, {}
    for folder, got in prints.items():
        need = min(3, len(got))
        if need < 2:
            continue
        hits = sorted((sum(1 for c in got if c in have), k) for k, have in counts.items())
        hits = [h for h in hits if h[0] >= need]
        if hits and (len(hits) == 1 or hits[-1][0] > hits[-2][0]):
            rows[folder] = hits[-1][1]
            scores[folder] = hits[-1][0]
    order = sorted(rows, key=lambda f: (f not in ('empress/id_97', 'empress/id_98'), not f.split('/')[1].startswith('id_'), -scores[f], f))
    return {f: rows[f] for f in order[:limit]}, {'empress_songs_with_charts': len(prints), 'matched': len(rows)}


def bms_main(a):
    songs = bms_songs(a.bms)
    if not songs:
        raise SystemExit('No .bms/.bme/.bml charts found under ' + str(a.bms))
    print('Found', len(songs), 'BMS song folders')
    report = {'probe': 5, 'source': 'bms', 'song_folders': len(songs),
              'charts': sum(len(c) for _, c in songs.values())}
    errors = {}
    diffs = {}
    for _, charts in songs.values():
        for name, chart in charts.items():
            if isinstance(chart, Exception):
                errors[str(chart)] = errors.get(str(chart), 0) + 1
            else:
                diffs[chart.difficulty] = diffs.get(chart.difficulty, 0) + 1
    report['chart_errors'] = errors
    report['difficulties'] = dict(sorted(diffs.items()))
    by_name = {folder.name: k for k, (folder, _) in songs.items()}
    if a.pair:
        pairs = {k: by_name[v] if v in by_name else int(v) for k, v in (x.split('=', 1) for x in a.pair)}
    else:
        print('Matching songs by chart note counts...')
        pairs, report['count_pairing'] = bms_pairs(a.megamix, songs, a.limit)
        if not report['count_pairing']['empress_songs_with_charts']:
            report['megamix_diagnostics'] = empress_diagnostics(a.megamix)
            print('No EMPRESS charts found in', a.megamix, '- see megamix_diagnostics in the report')
    report['pairs'] = []
    for folder, key in pairs.items():
        bms_folder, charts = songs[key]
        row = {'empress_folder': folder, 'bms_folder': bms_folder.name}
        try:
            row['empress'], ec = empress_song(a.megamix, folder)
            row['bms'] = bms_summary(bms_folder, charts, a.ffmpeg)
            good = {n: c.events for n, c in charts.items() if not isinstance(c, Exception)}
            row['matches'] = compare(ec, good)
        except (OSError, ValueError, KeyError, struct.error) as e:
            row['error'] = type(e).__name__ + ': ' + str(e)
        report['pairs'].append(row)
        print('compared', folder, '<->', bms_folder.name, '|', len(row.get('matches', [])), 'chart matches', row.get('error', ''))
    keys = sorted(songs)
    report['survey'] = [bms_summary(*songs[k], a.ffmpeg) | {'charts': None} for k in keys[::max(1, len(keys) // 10)]]
    a.output.write_text(json.dumps(report, indent=1, ensure_ascii=False) + '\n', encoding='utf-8')
    print('Wrote', a.output)


def fit(xs, ys):
    n = len(xs)
    mx, my = sum(xs) / n, sum(ys) / n
    var = sum((x - mx) ** 2 for x in xs)
    if not var:
        return None
    a = sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / var
    b = my - a * mx
    worst = max(abs(y - (a * x + b)) for x, y in zip(xs, ys))
    return {'scale': a, 'offset': b, 'max_residual': worst}


def compare(emp_charts, arc_charts):
    """Pair charts by note count, then fit EMPRESS time = scale * arcade time + offset."""
    rows = []
    for rel, ev in emp_charts:
        notes = sorted((e[0], e[1] & 15, e[2]) for e in ev if e[1] & 15 in (0, 1))
        for slot, aev in arc_charts.items():
            anotes = sorted((e[0], e[1], e[2]) for e in aev if e[1] in (0, 1))
            if len(anotes) != len(notes) or not notes:
                continue
            row = {'empress': rel, 'arcade_slot': slot, 'notes': len(notes),
                   'same_lanes': sum(1 for a, b in zip(notes, anotes) if a[1:] == b[1:]),
                   'fit': fit([a[0] for a in anotes], [b[0] for b in notes])}
            rows.append(row)
    return rows


# ---------------------------------------------------------------- music_data

def music_summary(path, ids):
    if path is None:
        raise ValueError('music_data.bin not found under the data folder')
    md = A.read_music_data(path.read_bytes())
    index = md['index']
    pairs = [(sid, index[sid]) for sid in ids if sid < len(index) and 0 <= index[sid] < md['count']]
    out = {k: md[k] for k in ('version', 'count', 'index_count', 'reserved', 'entry_size')}
    out['indexed_folders'] = len(pairs)
    out['id_fields'] = [[at, width, hits] for (at, width), hits in A.find_id_fields([md['entries'][e] for _, e in pairs], [s for s, _ in pairs])[:6]]
    titles = {}
    for sid, e in pairs:
        titles[sid] = A.entry_strings(md['entries'][e])
    out['sample_entries'] = {str(sid): {'strings': titles[sid], 'hex': hexrow(md['entries'][e])} for sid, e in pairs[:3]}
    return out, md, titles


def norm(s):
    return re.sub(r'[^0-9A-Z]', '', s.upper())


def auto_pairs(root, titles):
    rows = {}
    for st in ('empress',):
        folder = root / 'songs' / st
        if not folder.is_dir():
            continue
        for song in sorted(folder.iterdir()):
            t = song / 'song.txt'
            if not t.is_file():
                continue
            name = next((l[5:] for l in t.read_text(encoding='utf-8').splitlines() if l.startswith('name ')), '')
            key = norm(name)
            if len(key) < 4:
                continue
            hits = [sid for sid, strings in titles.items() if any(norm(x).startswith(key) or (len(norm(x)) >= 4 and key.startswith(norm(x))) for x in strings[:2])]
            if len(hits) == 1:
                rows[st + '/' + song.name] = hits[0]
    return rows


def empress_fingerprints(root):
    """{style/dir: sorted chart note counts} for every installed song, charts only (small files)."""
    out = {}
    for txt in sorted((root / 'songs').glob('*/*/song.txt')):
        base = txt.parent
        counts = []
        for line in txt.read_text(encoding='utf-8').splitlines():
            parts = line.split()
            if parts[:1] == ['E'] and len(parts) == 5 and parts[3].isdigit() and int(parts[3]) < 1 << 20:
                path = root / parts[4]
            elif parts[:1] == ['F'] and len(parts) == 4 and parts[1] in ('c0', 'c4', 'c8', 'cc', 'd0', 'd4'):
                path = root / parts[3].lstrip('/') if parts[3].startswith('/') else base / parts[3]
            else:
                continue
            if not path.is_file() or path.stat().st_size >= 1 << 20:
                continue
            ply = empress_chart(path.read_bytes())
            if ply:
                n = sum(1 for e in ply['events'] if e[1] & 15 in (0, 1))
                if n:
                    counts.append(n)
        if counts:
            out[base.parent.name + '/' + base.name] = sorted(counts)
    return out


def count_pairs(root, files, limit):
    """Pair songs by chart note counts when no titles are available."""
    arcade = {}
    for sid, paths in files.items():
        if '.1' not in paths:
            continue
        try:
            charts = A.read_charts(paths['.1'].read_bytes())
        except (A.FormatError, OSError):
            continue
        arcade[sid] = {sum(A.note_count(ev)) for ev in charts.values()}
    rows, scores = {}, {}
    prints = empress_fingerprints(root)
    for folder, counts in prints.items():
        need = min(3, len(counts))
        if need < 2:
            continue
        hits = [(sum(1 for c in counts if c in got), sid) for sid, got in arcade.items()]
        hits = sorted(h for h in hits if h[0] >= need)
        if hits and (len(hits) == 1 or hits[-1][0] > hits[-2][0]):
            rows[folder] = hits[-1][1]
            scores[folder] = [hits[-1][0], len(counts)]
    # prefer EMPRESS disc songs (Bad Maniacs, DOMINION first), then strongest matches
    order = sorted(rows, key=lambda f: (f not in ('empress/id_97', 'empress/id_98'), not f.split('/')[1].startswith('id_'), -scores[f][0], f))
    return {f: rows[f] for f in order[:limit]}, {'empress_songs_with_charts': len(prints), 'matched': len(rows)}


def metadata_candidates(data_dir, sound_roots, limit=60):
    """Files under info/ (and other non-song, non-graphics folders): the song list is one of them."""
    out = []
    skip = [data_dir / 'graph_data', data_dir / 'movie', *sound_roots]
    paths = sorted(data_dir.rglob('*'), key=lambda p: ('info' not in [x.lower() for x in p.relative_to(data_dir).parts[:1]], str(p)))
    for path in paths:
        if not path.is_file() or any(path.is_relative_to(r) for r in skip):
            continue
        with path.open('rb') as f:
            head = f.read(32)
        out.append({'path': str(path.relative_to(data_dir)), 'bytes': path.stat().st_size, 'head': head.hex()})
        if len(out) >= limit:
            break
    return out


def empress_diagnostics(root, limit=3):
    """Why no EMPRESS charts were found: song.txt count, a sample, and decode attempts."""
    songs = sorted((root / 'songs').glob('*/*/song.txt')) if (root / 'songs').is_dir() else []
    out = {'megamix_top': sorted(p.name for p in root.iterdir())[:40] if root.is_dir() else 'not a folder',
           'song_txt_found': len(songs), 'has_package_json': (root / 'megamix-package.json').is_file()}
    if not songs:
        return out
    from chart_transform import crypt
    out['sample_song_txt'] = songs[0].read_text(encoding='utf-8').splitlines()[:16]
    tries = []
    for txt in songs[:10]:
        for line in txt.read_text(encoding='utf-8').splitlines():
            parts = line.split()
            path = None
            if parts[:1] == ['E'] and len(parts) == 5:
                path = root / parts[4]
            elif parts[:1] == ['F'] and len(parts) == 4 and parts[1] in ('c0', 'c4'):
                path = root / parts[3].lstrip('/') if parts[3].startswith('/') else txt.parent / parts[3]
            if path is None:
                continue
            row = {'song': txt.parent.name, 'line': line, 'exists': path.is_file()}
            if path.is_file() and path.stat().st_size < 1 << 20:
                raw = path.read_bytes()
                row['bytes'] = len(raw)
                row['head'] = raw[:16].hex()
                try:
                    row['decrypted_head'] = crypt(raw, 'empress', True)[:16].hex()
                except ValueError as e:
                    row['decrypted_head'] = str(e)
                row['decoded'] = empress_chart(raw) is not None
                tries.append(row)
            if len(tries) >= limit * 3:
                break
        if len(tries) >= limit * 3:
            break
    out['chart_attempts'] = tries
    return out


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('sirius', type=Path, nargs='?', help='SIRIUS arcade data/ folder (omit with --bms)')
    p.add_argument('megamix', type=Path, nargs='?', help='built MEGAMIX base folder')
    p.add_argument('--bms', type=Path, help='folder of SIRIUS songs as BMS (one subfolder per song), instead of arcade data')
    p.add_argument('--ffmpeg', help='ffmpeg executable for OGG/FLAC keysounds (default: ffmpeg on PATH)')
    p.add_argument('--keysounds', type=Path, help='folder of decrypted .2dx files (same names as in sd_data), used instead of the encrypted ones')
    p.add_argument('--music', type=Path, help='song list file if it is not named music_data.bin')
    p.add_argument('--sound', type=Path, help='folder holding the song files (default: sound/ or sd_data/ under SIRIUS_DATA)')
    p.add_argument('--pair', action='append', default=[], metavar='STYLE/DIR=ID', help='e.g. empress/id_97=17010')
    p.add_argument('--limit', type=int, default=6, help='songs to compare when pairing automatically')
    p.add_argument('-o', '--output', type=Path, default=Path('sirius-probe.json'))
    a = p.parse_args(argv)
    if a.bms:
        if a.megamix is None and a.sirius is not None:
            a.megamix, a.sirius = a.sirius, None
        if a.megamix is None:
            raise SystemExit('Give the MEGAMIX folder: sirius_probe.py --bms SONGS MEGAMIX')
        return bms_main(a)
    if a.sirius is None or a.megamix is None:
        raise SystemExit('Usage: sirius_probe.py SIRIUS_DATA MEGAMIX, or sirius_probe.py --bms SONGS MEGAMIX')
    if not a.sirius.is_dir():
        raise SystemExit('Not a folder: ' + str(a.sirius))
    files, music = locate(a.sirius, a.sound, a.keysounds)
    if a.music:
        music = a.music
    ids = sorted(files)
    if not ids:
        print('No <id>.1 / <id>.2dx song files found under', a.sound or a.sirius)
        print(json.dumps(layout(a.sound or a.sirius), indent=1, ensure_ascii=False))
        raise SystemExit('Pass the folder that holds the songs with --sound, or share the summary above.')
    print('Found', len(ids), 'songs', ids[0], '..', ids[-1], '| music_data.bin:', music or 'not found')
    report = {'probe': 4, 'songs': len(ids), 'id_range': [ids[0], ids[-1]],
              'with_chart': sum(1 for f in files.values() if '.1' in f), 'with_keysounds': sum(1 for f in files.values() if '.2dx' in f),
              'example_paths': [str(p.relative_to(a.sirius)) if p.is_relative_to(a.sirius) else p.name for p in list(files[ids[0]].values())]}
    report['layout'] = layout(a.sirius)
    roots = [a.sound] if a.sound else [a.sirius / d for d in SOUND_DIRS if (a.sirius / d).is_dir()]
    if music is None:
        report['metadata_candidates'] = metadata_candidates(a.sirius, roots)
    titles = {}
    try:
        report['music_data'], _, titles = music_summary(music, ids)
    except (A.FormatError, ValueError) as e:
        report['music_data'] = {'error': str(e)}
    pairs = dict(x.split('=', 1) for x in a.pair)
    pairs = {k: int(v) for k, v in pairs.items()}
    if not pairs:
        pairs = dict(list(auto_pairs(a.megamix, titles).items())[:a.limit])
    if not pairs:
        print('Matching songs by chart note counts...')
        pairs, report['count_pairing'] = count_pairs(a.megamix, files, a.limit)
        if not report['count_pairing']['empress_songs_with_charts']:
            report['megamix_diagnostics'] = empress_diagnostics(a.megamix)
            print('No EMPRESS charts found in', a.megamix, '- see megamix_diagnostics in the report')
    report['pairs'] = []
    for folder, sid in pairs.items():
        row = {'empress_folder': folder, 'sirius_id': sid}
        try:
            row['empress'], ec = empress_song(a.megamix, folder)
            row['sirius'], ac = sirius_song(files, sid)
            row['matches'] = compare(ec, ac)
            if titles.get(sid):
                row['sirius_strings'] = titles[sid]
        except (OSError, ValueError, KeyError, struct.error) as e:
            row['error'] = type(e).__name__ + ': ' + str(e)
        report['pairs'].append(row)
        print('compared', folder, '<->', sid, '|', len(row.get('matches', [])), 'chart matches', row.get('error', ''))
    survey = {'slots': {}, 'types': {}, 'wav': {}}
    for sid in ids[:: max(1, len(ids) // 40)]:
        try:
            s, charts = sirius_song(files, sid)
        except (OSError, ValueError) as e:
            survey.setdefault('errors', []).append([sid, str(e)])
            continue
        for slot, ev in charts.items():
            survey['slots'][str(slot)] = survey['slots'].get(str(slot), 0) + 1
            for e in ev:
                survey['types'][str(e[1])] = survey['types'].get(str(e[1]), 0) + 1
        ks = s.get('keysounds', {})
        if 'error' in ks:
            errs = survey.setdefault('keysound_errors', {})
            errs[ks['error']] = errs.get(ks['error'], 0) + 1
            if len(survey.setdefault('keysound_error_heads', [])) < 3:
                survey['keysound_error_heads'].append({'id': sid, 'bytes': files[sid]['.2dx'].stat().st_size, 'head': ks.get('head')})
        elif ks and len(survey.setdefault('keysound_samples', [])) < 3:
            survey['keysound_samples'].append({'id': sid, **{k: ks[k] for k in ('count', 'formats', 'attenuation', 'unk', 'loop')}})
        for k, v in ks.get('formats', {}).items():
            survey['wav'][k] = survey['wav'].get(k, 0) + v
    report['survey'] = survey
    a.output.write_text(json.dumps(report, indent=1, ensure_ascii=False) + '\n', encoding='utf-8')
    print('Wrote', a.output)


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError) as e:
        print('sirius_probe: ' + str(e), file=sys.stderr)
        sys.exit(1)
