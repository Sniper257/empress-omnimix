#!/usr/bin/env python3
"""Measure how IIDX 17 SIRIUS arcade data maps onto EMPRESS's native song files.

  python3 sirius_probe.py SIRIUS_DATA MEGAMIX_DIR [--pair empress/id_97=17000 ...] [-o report.json]

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


def locate(data_dir, sound=None):
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
    for path in sorted(data_dir.rglob('music_data.bin')):
        music = path
        break
    return songs, music


def layout(data_dir, limit=40):
    """Short folder summary for reports when no song files are recognised."""
    out = {'top': sorted(p.name + ('/' if p.is_dir() else '') for p in data_dir.iterdir())[:limit], 'extensions': {}}
    for path in data_dir.rglob('*'):
        if path.is_file():
            ext = path.suffix.lower() or '(none)'
            out['extensions'][ext] = out['extensions'].get(ext, 0) + 1
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
            out['keysounds'] = {'error': str(err), 'head': hexrow(two.read_bytes()[:96])}
    return out, charts


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


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('sirius', type=Path, help='SIRIUS arcade data/ folder')
    p.add_argument('megamix', type=Path, help='built MEGAMIX base folder')
    p.add_argument('--sound', type=Path, help='folder holding the song files (default: sound/ or sd_data/ under SIRIUS_DATA)')
    p.add_argument('--pair', action='append', default=[], metavar='STYLE/DIR=ID', help='e.g. empress/id_97=17010')
    p.add_argument('--limit', type=int, default=6, help='songs to compare when pairing automatically')
    p.add_argument('-o', '--output', type=Path, default=Path('sirius-probe.json'))
    a = p.parse_args(argv)
    if not a.sirius.is_dir():
        raise SystemExit('Not a folder: ' + str(a.sirius))
    files, music = locate(a.sirius, a.sound)
    ids = sorted(files)
    if not ids:
        print('No <id>.1 / <id>.2dx song files found under', a.sound or a.sirius)
        print(json.dumps(layout(a.sound or a.sirius), indent=1, ensure_ascii=False))
        raise SystemExit('Pass the folder that holds the songs with --sound, or share the summary above.')
    print('Found', len(ids), 'songs', ids[0], '..', ids[-1], '| music_data.bin:', music or 'not found')
    report = {'probe': 2, 'songs': len(ids), 'id_range': [ids[0], ids[-1]],
              'with_chart': sum(1 for f in files.values() if '.1' in f), 'with_keysounds': sum(1 for f in files.values() if '.2dx' in f),
              'example_paths': [str(p.relative_to(a.sirius)) if p.is_relative_to(a.sirius) else p.name for p in list(files[ids[0]].values())]}
    titles = {}
    try:
        report['music_data'], _, titles = music_summary(music, ids)
    except (A.FormatError, ValueError) as e:
        report['music_data'] = {'error': str(e)}
    pairs = dict(x.split('=', 1) for x in a.pair)
    pairs = {k: int(v) for k, v in pairs.items()}
    if not pairs:
        pairs = dict(list(auto_pairs(a.megamix, titles).items())[:a.limit])
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
        for k, v in s.get('keysounds', {}).get('formats', {}).items():
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
