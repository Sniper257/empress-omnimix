#!/usr/bin/env python3
"""Convert an arcade IIDX song (.1 chart + .2dx keysounds, loose or in an .ifs) to
EMPRESS files. Work in progress: charts and the backing stream; song folder next.

  python3 arcade_convert.py 23069.ifs OUT [--stream SPA] [--hell]

Writes, per chart present (SPN/SPH/SPA/SPB/SPL, DPN/DPH/DPA/DPB/DPL):
  OUT/<chart>.bin   EMPRESS chart: encrypted chart-LZ (chart_transform 'empress'),
                    with the hold list after the terminator when the chart has holds
  OUT/stream.bin    with --stream CHART: every keysound of that chart (BGM and
                    notes) rendered into one EMPRESS backing stream (needs numpy)
  OUT/convert.json  what was converted, note/hold counts, BPM range, warnings

EMPRESS chart events are (u32 tick, u8 cmd, u8 c2, u16 value), with the type
in cmd's low nibble and lane/side in its high nibble (chart_transform.normalize):
  0/1 note: cmd = type | column << 4          2/3 keysound: value = slot (0 = none)
  4 tempo: integer BPM = c2 + (cmd >> 4) * 256 (the reader at 0x13c280)
  5 meter, 8 timing window: c2 = value         7 BGM: value 1 starts the stream
  12 measure bar: cmd = 12 | side << 4          6 end
Ticks are 60 Hz frames: tick = ms * 1000 / 16683. Arcade note-count events
(type 16) do not fit the 4-bit type and are dropped.

Until the keysound bank format is pinned down, keysound changes are written
as slot 0 (no sound) and the stream carries all audio.
"""
import argparse
import json
from pathlib import Path
import struct
import sys

import arcade_data as A
import holds_format as H

CLOCK = 16683
SLOTS = {0: 'SPH', 1: 'SPN', 2: 'SPA', 3: 'SPB', 4: 'SPL', 6: 'DPH', 7: 'DPN', 8: 'DPA', 9: 'DPB', 10: 'DPL'}
END = 0x7fff


def tick(ms):
    t = (ms * 1000 + CLOCK // 2) // CLOCK
    if t >= END:
        raise ValueError('Chart is longer than EMPRESS charts allow (%d ms)' % ms)
    return t


def empress_events(events, holds=()):
    """Arcade events (ms) -> EMPRESS ply bytes (header, events, terminator, optional hold trailer)."""
    out = []
    warnings = set()
    end = 0
    started = False
    for t, kind, param, value in sorted(events, key=lambda e: (e[0], e[1] == 6)):
        k = tick(t)
        if kind in (0, 1):
            out.append((k, kind | (param & 15) << 4, 0, 0))            # hold length lives in the trailer
        elif kind in (2, 3):
            out.append((k, kind | (param & 15) << 4, 0, 0))            # keysound slot 0 until banks exist
        elif kind == 4:
            bpm = round(value / param) if param else value
            if not 1 <= bpm <= 4095:
                raise ValueError('BPM %r outside EMPRESS range' % bpm)
            if param and value % param:
                warnings.add('non-integer BPM rounded (scroll speed only; note times are exact)')
            out.append((k, 4 | (bpm >> 8) << 4, bpm & 0xff, 0))
        elif kind == 5:
            out.append((k, 5 | (param & 15) << 4, value & 0xff, value & 0xffff))
        elif kind == 6:
            end = max(end, k)
        elif kind == 7:
            if not started:                                             # one stream start; the stream has all BGM
                out.append((0, 7, 0, 1))
                started = True
        elif kind == 8:
            out.append((k, 8 | (param & 15) << 4, value & 0xff, 0))
        elif kind == 12:
            out.append((k, 12 | (param & 15) << 4, 0, 0))
        elif kind == 16:
            continue
        else:
            warnings.add('dropped event type %d' % kind)
    if not started:
        out.append((0, 7, 0, 1))
    end = max([end] + [e[0] for e in out]) + 1
    out.append((end, 6, 0, 0))
    out.sort(key=lambda e: (e[0], e[1] & 15 == 6))
    body = b''.join(struct.pack('<IBBH', *e) for e in out)
    ply = struct.pack('<II', 8, CLOCK) + body + struct.pack('<IBBH', END, 0, 0, 0)
    if holds:
        ply += H.pack(holds)
    return ply, sorted(warnings)


def encrypt(ply):
    from chart_lz import lz_encode_opt
    from chart_transform import crypt
    return crypt(lz_encode_opt(ply), 'empress')


def load_song(path):
    """(charts {slot: events}, keysound entries, song id) from an .ifs or a folder with <id>.1/.2dx."""
    path = Path(path)
    if path.is_file() and path.suffix.lower() == '.ifs':
        import ifs
        files = ifs.read(path.read_bytes())
        get = lambda suffix: next((v for k, v in files.items() if k.lower().endswith(suffix) and '_pre' not in k.lower()), None)
        sid = path.stem
    else:
        sid = path.name
        get = lambda suffix: (path / (sid + suffix)).read_bytes() if (path / (sid + suffix)).is_file() else None
    one, two = get('.1'), get('.2dx')
    if one is None:
        raise ValueError('No .1 chart in ' + str(path))
    keys = A.read_2dx(two)[1] if two else []
    return A.read_charts(one), keys, sid


def render(events, keys, rate=44100):
    """Mix every keysound the chart plays (BGM and note sounds, as in autoplay). Needs numpy."""
    import numpy as np
    current = {}
    plays = []
    for t, kind, param, value in sorted(events):
        if kind in (2, 3):
            current[(kind - 2, param)] = value
        elif kind in (0, 1):
            s = current.get((kind, param), 0)
            if s:
                plays.append((t, s))
        elif kind == 7 and value:
            plays.append((t, value))
    cache = {}
    length = 0
    for t, s in plays:
        if s not in cache and 0 < s <= len(keys):
            r, ch = A.decode_wav(keys[s - 1]['wav'])
            if r != rate:
                raise ValueError('Keysound %d is %d Hz, expected %d' % (s, r, rate))
            a = np.array(ch, dtype=np.int32)
            if a.shape[0] == 1:
                a = np.vstack([a, a])
            cache[s] = a
        if s in cache:
            length = max(length, t * rate // 1000 + cache[s].shape[1])
    mix = np.zeros((2, length), dtype=np.int64)
    for t, s in plays:
        if s in cache:
            at = t * rate // 1000
            mix[:, at:at + cache[s].shape[1]] += cache[s]
    peak = int(np.abs(mix).max()) if length else 0
    if peak > 32767:                      # keep the mix's own balance; scale down instead of clipping
        mix = mix * 32767 // peak
    return rate, [mix[0].astype(np.int16).tolist(), mix[1].astype(np.int16).tolist()], {'keysounds_played': len(plays), 'peak': peak}


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('song', type=Path, help='<id>.ifs, or a folder holding <id>.1 and <id>.2dx')
    p.add_argument('output', type=Path)
    p.add_argument('--stream', metavar='CHART', help='render the backing stream from this chart (e.g. SPA)')
    p.add_argument('--hell', action='store_true', help='holds are HCN/HBSS (the arcade marks HCN per chart)')
    a = p.parse_args(argv)
    charts, keys, sid = load_song(a.song)
    a.output.mkdir(parents=True, exist_ok=True)
    report = {'song': sid, 'keysounds': len(keys), 'charts': {}}
    for slot, events in sorted(charts.items()):
        name = SLOTS.get(slot, 'slot%d' % slot)
        holds = H.from_events(events, hell=a.hell, time=tick, ticks_per_ms=1000 / CLOCK)
        ply, warnings = empress_events(events, holds)
        (a.output / (name + '.bin')).write_bytes(encrypt(ply))
        notes = sum(1 for e in events if e[1] in (0, 1))
        bpms = sorted({round(e[3] / e[2], 2) if e[2] else e[3] for e in events if e[1] == 4})
        report['charts'][name] = {'notes': notes, 'holds': len(holds), 'total': notes + len(holds),
                                  'bpm': [bpms[0], bpms[-1]] if bpms else None, 'warnings': warnings}
        print('%s: %d notes, %d holds%s' % (name, notes, len(holds), (' (' + '; '.join(warnings) + ')') if warnings else ''))
    if a.stream:
        slot = next((s for s, n in SLOTS.items() if n == a.stream.upper()), None)
        if slot not in charts:
            raise SystemExit('No %s chart in this song' % a.stream)
        import spu_adpcm as S
        rate, channels, info = render(charts[slot], keys)
        (a.output / 'stream.bin').write_bytes(S.stream(channels, rate))
        report['stream'] = dict(info, chart=a.stream.upper(), seconds=round(len(channels[0]) / rate, 2))
        print('stream: %.1f s from %s, %d keysounds' % (len(channels[0]) / rate, a.stream.upper(), info['keysounds_played']))
    (a.output / 'convert.json').write_text(json.dumps(report, indent=1) + '\n', encoding='utf-8')


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError) as e:
        print('arcade_convert: ' + str(e), file=sys.stderr)
        sys.exit(1)
