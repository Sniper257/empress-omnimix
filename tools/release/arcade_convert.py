#!/usr/bin/env python3
"""Convert an arcade IIDX song (.1 chart + .2dx keysounds, loose or in an .ifs) to
EMPRESS files. Work in progress: charts and the backing stream; song folder next.

  python3 arcade_convert.py 23069.ifs OUT [--stream SPA] [--hell] [--split]

HCN charts are found by themselves: the HCN Ver. song (e.g. 18102 Kailua (HCN Ver.)) has the
same notes as the normal song plus one type-13 event at time 0 in every chart.

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


TAIL_SHIFT = 2   # holds.h HOLDS_TAIL_SHIFT


def tail_tick(start, stop, taken):
    """Tick for a hold's tail note: its end, or the nearest tick within TAIL_SHIFT (earlier first)
    that has no other note. The game judges all notes at one tick as a row, and a never-judged tail
    in another hold's head row left that head unjudged until its window ran out."""
    for off in (0, -1, 1, -2, 2):
        t = stop + off
        if t > start and t not in taken:
            return t
    return stop


def empress_events(events, holds=(), keysounds=False, bgm_slots=False, tail_notes=True):
    """Arcade events (ms) -> EMPRESS ply bytes (header, events, terminator, optional hold trailer).

    tail_notes=True: every hold also gets a note at its end tick, same lane. The game scrolls and
    draws it like any note; the HOLDS runtime (holds_spawn_hook) makes it never judged, so the hold
    renderer knows where the tail is on screen. Without a HOLDS runtime it would be a normal note.

    keysounds=False: type 2/3 values become 0 (no sound; the stream carries everything).
    keysounds=True: the events already hold bank slots (keysound_bank.remap) and are kept.
    bgm_slots=True: BGM is keysounded from the bank as in EMPRESS's own songs: (tick, 0x87, 0, slot)
    per type-7 event (values already remapped); otherwise one stream start (0, 7, 0, 1)."""
    out = []
    warnings = set()
    end = 0
    started = False
    for t, kind, param, value in sorted(events, key=lambda e: (e[0], e[1] == 6)):
        k = tick(t)
        if kind in (0, 1):
            out.append((k, kind | (param & 15) << 4, 0, 0))            # hold length lives in the trailer
        elif kind in (2, 3):
            if keysounds and not 0 <= value <= 0x800:
                raise ValueError('keysound slot %d outside the bank' % value)
            out.append((k, kind | (param & 15) << 4, 0, value if keysounds else 0))
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
        elif kind == 7 and bgm_slots:
            if value:
                if not 0 < value <= 0x7ff:
                    raise ValueError('BGM slot %d outside the bank' % value)
                out.append((k, 7 | 8 << 4, 0, value))
        elif kind == 7:
            if not started:                                             # one stream start; the stream has all BGM
                out.append((0, 7, 0, 1))
                started = True
        elif kind == 8:
            out.append((k, 8 | (param & 15) << 4, value & 0xff, 0))
        elif kind == 12:
            out.append((k, 12 | (param & 15) << 4, 0, 0))
        elif kind in (13, 16):   # 13 HCN flag (carried by the hold trailer), 16 note count
            continue
        else:
            warnings.add('dropped event type %d' % kind)
    if not started and not bgm_slots:
        out.append((0, 7, 0, 1))
    if tail_notes:
        for start, stop, lane, kind, tick64 in holds:
            out.append((tail_tick(start, stop, {e[0] for e in out if e[1] & 15 in (0, 1)}),
                        (lane >> 3) | (lane & 7) << 4, 0, 0))
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
    if two:
        keys = A.read_2dx(two)[1]
    else:
        s3p = get('.s3p')
        keys = s3p_keys(s3p) if s3p else []
    return A.read_charts(one), keys, sid


def _wma_to_wav(audio):
    import subprocess
    out = subprocess.run(['ffmpeg', '-v', 'error', '-f', 'asf', '-i', 'pipe:0', '-f', 'wav', '-acodec', 'pcm_s16le', 'pipe:1'],
                         input=audio, stdout=subprocess.PIPE, check=True).stdout
    # ffmpeg streams a WAV with unknown sizes (0xffffffff) to a pipe: fix the RIFF / data sizes
    import struct
    at = out.find(b'data')
    if at < 0:
        raise ValueError('ffmpeg wrote no WAV data')
    out = bytearray(out)
    struct.pack_into('<I', out, 4, len(out) - 8)
    struct.pack_into('<I', out, at + 4, len(out) - at - 8)
    return bytes(out)


def s3p_keys(data):
    """Keysounds of an .s3p (newer arcade games: one WMA per sound), decoded with ffmpeg into the
    entry shape read_2dx gives ({'index', 'wav'}). The S3V0 header holds no length or volume, so
    ffmpeg's own WMA trimming decides the length."""
    from concurrent.futures import ThreadPoolExecutor
    entries = A.read_s3p(data)
    with ThreadPoolExecutor(8) as pool:
        wavs = list(pool.map(lambda e: _wma_to_wav(e['audio']), entries))
    return [{'index': e['index'], 'track': 0, 'attenuation': 0, 'loop': 0, 'unk': (0, 0), 'wav': w}
            for e, w in zip(entries, wavs)]


def render(events, keys, rate=44100, bgm_only=False, peak=None, raw=False):
    """Mix the keysounds the chart plays: BGM and note sounds as in autoplay, or BGM only. Needs numpy.

    peak: scale as if the mix peaked here (pass the full autoplay peak for a BGM-only stream, so the
    BGM keeps its level against the bank's note sounds)."""
    import numpy as np
    current = {}
    plays = []
    for t, kind, param, value in sorted(events):
        if kind in (2, 3):
            current[(kind - 2, param)] = value
        elif kind in (0, 1) and not bgm_only:
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
    own = int(np.abs(mix).max()) if length else 0
    if raw:                               # the unscaled int64 mix
        return rate, mix, {'keysounds_played': len(plays), 'peak': own}
    peak = max(own, peak or 0)
    if peak > 32767:                      # keep the mix's own balance; scale down instead of clipping
        mix = mix * 32767 // peak
    return rate, [mix[0].astype(np.int16).tolist(), mix[1].astype(np.int16).tolist()], {'keysounds_played': len(plays), 'peak': peak}


HCN_FLAG = 13  # .1 event type: this chart's holds are HCN/HBSS


# Banks per difficulty, as the record's bank groups are chosen (FUN_001e4050; difficulty = 0x205de0(),
# 0 N 1 H 2 A 3 B 4 X): 0xe8 H (and X, the fallback), 0xf0 A, 0xf8 N, 0x100 B. SP and DP of one
# difficulty share a group, so its bank holds both charts' sounds.
DIFFICULTY_GROUPS = {'H': (0, 6, 4, 10), 'A': (2, 8), 'N': (1, 7), 'B': (3, 9)}
DIFFICULTY_FIELDS = {'H': 0xe8, 'A': 0xf0, 'N': 0xf8, 'B': 0x100}
PIECE_TICKS = 30
report_gain = {}


def premix_bgm(charts, keys, piece_ticks=PIECE_TICKS):
    """Each chart's BGM (type-7 events) mixed into one track and cut into piece_ticks pieces (0.5 s),
    each a new keysound started on its tick. EMPRESS's own BGM is short sounds at a modest rate; the
    arcade's long backing tracks and ~2,500 layered BGM events a chart lost voices on the PS2 (long
    sounds cut, BGM silent now and then). Records the gain each chart needs in report_gain (the mix
    and the chart's note sounds scaled alike, so a mix that would clip keeps its balance)."""
    import numpy as np
    keys = list(keys)
    out = {}
    for slot, events in charts.items():
        full_peak = render(events, keys)[2]['peak']
        gain = min(1.0, 32767 / full_peak) if full_peak else 1.0
        report_gain[slot] = gain
        bgm = [e for e in events if e[1] == 7 and e[3]]
        rest = [e for e in events if not (e[1] == 7)]
        if not bgm:
            out[slot] = rest
            continue
        rate, mix, _ = render(bgm, keys, bgm_only=True, raw=True)
        pieces = []
        k = 0
        while True:
            a = round(k * piece_ticks * CLOCK * rate / 1e6)
            if a >= mix.shape[1]:
                break
            b = round((k + 1) * piece_ticks * CLOCK * rate / 1e6)
            part = mix[:, a:b]
            if np.abs(part).max() > 0:                                       # silent pieces need no sound
                keys.append({'pcm': (rate, part)})
                ms = round(k * piece_ticks * CLOCK / 1000)
                assert tick(ms) == k * piece_ticks
                pieces.append((ms, 7, 0, len(keys)))
            k += 1
        out[slot] = sorted(rest + pieces, key=lambda e: e[0])
    return out, keys


def silent_stream(samples, rate=44100):
    """An EMPRESS backing stream of stereo silence (zero ADPCM frames), without encoding sample by sample."""
    import spu_adpcm as S
    frames = -(-samples // 28)
    head = bytearray(0x800)
    body = bytes(16 * frames * 2)
    struct.pack_into('<12I', head, 0, S.STREAM_MAGIC, 0, 0x800, len(body), 0, 0, rate, 2, 1, 16, 0x7f, 1)
    return bytes(head) + S.disguise(body, S.STREAM_DELTA)


def is_hell(events):
    return any(e[1] == HCN_FLAG for e in events)


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('song', type=Path, help='<id>.ifs, or a folder holding <id>.1 and <id>.2dx')
    p.add_argument('output', type=Path)
    p.add_argument('--stream', metavar='CHART', help='render the backing stream from this chart (e.g. SPA)')
    p.add_argument('--bank-budget', type=int, default=16000000,
                   help='largest bank in bytes (default 16 MB: Elisha\'s 16 MB bank loads in PCSX2 with All My Turn\'s '
                        'record); sounds up to 6 s go to 32 or 24 kHz to fit. 0 = no limit')
    p.add_argument('--piece-ticks', type=int, default=None,
                   help='length of the BGM pieces in ticks (default 30 = 0.5 s for the premixed BGM, 15 = 0.25 s with '
                        '--one-bank; the game cuts long sounds when it runs short of voices)')
    p.add_argument('--one-bank', dest='premix', action='store_false',
                   help='with --split: one bank for every chart, the arcade BGM events kept and only BGM over 1 s cut '
                        'into pieces (about a third of the space; long sounds still drop in PCSX2). Default: each '
                        'chart\'s whole BGM mixed and cut into pieces, one bank per difficulty (0xe8 H, 0xf0 A, '
                        '0xf8 N, 0x100 B), which plays best')
    p.add_argument('--hell', action='store_true', help='treat holds as HCN/HBSS even without the type-13 HCN event')
    p.add_argument('--split', action='store_true',
                   help='keysounds to one keys.bank for every chart (an EMPRESS "wp" bank; native songs have one '
                        'bank for SP and DP). Alone: every sound, BGM included, is in the bank, and stream.bin is '
                        'silence the length of the song (the record\'s stream field must name a real stream). With '
                        '--stream: player sounds only in the bank, BGM in stream.bin')
    a = p.parse_args(argv)
    charts, keys, sid = load_song(a.song)
    a.output.mkdir(parents=True, exist_ok=True)
    report = {'song': sid, 'keysounds': len(keys), 'charts': {}}
    banks = {}
    if a.split:
        import keysound_bank as KB
        gain = 1.0
        if a.stream:                                  # the bank gets the gain the stream gets
            peak = render(charts[next(s for s, n in SLOTS.items() if n == a.stream.upper())], keys)[2]['peak']
            gain = min(1.0, 32767 / peak) if peak else 1.0
        if a.stream:
            members = sorted(charts)
            sounds = KB.player_sounds(*(charts[s] for s in members))
            data, slots, info = KB.build('SP', sounds, keys, gain, a.bank_budget or None)
            (a.output / 'keys.bank').write_bytes(data)
            banks.update({s: slots for s in members})
            report.setdefault('banks', {})['keys'] = info
            print('keys.bank: %d sounds, %d bytes, rates %s%s' % (info['sounds'], info['bytes'], info['rates'],
                                                            ' (' + info['warning'] + ')' if 'warning' in info else ''))
        elif not a.premix:
            charts, keys = KB.split_long_bgm(charts, keys, tick, CLOCK, chunk_ticks=a.piece_ticks or 15, longest=44100)   # over 1 s
            members = sorted(charts)
            sounds = KB.player_sounds(*(charts[s] for s in members))
            sounds += [v for v in KB.bgm_sounds(*(charts[s] for s in members)) if v not in set(sounds)]
            data, slots, info = KB.build('SP', sounds, keys, gain, a.bank_budget or None)
            (a.output / 'keys.bank').write_bytes(data)
            banks.update({s: slots for s in members})
            report.setdefault('banks', {})['keys'] = info
            print('keys.bank: %d sounds, %d bytes, rates %s%s' % (info['sounds'], info['bytes'], info['rates'],
                                                            ' (' + info['warning'] + ')' if 'warning' in info else ''))
        else:
            charts, keys = premix_bgm(charts, keys, a.piece_ticks or PIECE_TICKS)
            for group, members in DIFFICULTY_GROUPS.items():
                members = [m for m in members if m in charts]
                if not members:
                    continue
                g = min(report_gain.pop(m) for m in members)
                sounds = KB.player_sounds(*(charts[m] for m in members))
                sounds += [v for v in KB.bgm_sounds(*(charts[m] for m in members)) if v not in set(sounds)]
                data, slots, info = KB.build('SP', sounds, keys, g, a.bank_budget or None)
                (a.output / ('keys_%s.bank' % group)).write_bytes(data)
                banks.update({m: slots for m in members})
                report.setdefault('banks', {})[group] = info
                print('keys_%s.bank (%s): %d sounds, %d bytes, rates %s%s' % (
                    group, ' '.join(SLOTS[m] for m in members), info['sounds'], info['bytes'], info['rates'],
                    ' (' + info['warning'] + ')' if 'warning' in info else ''))
    for slot, events in sorted(charts.items()):
        name = SLOTS.get(slot, 'slot%d' % slot)
        hell = a.hell or is_hell(events)
        holds = H.from_events(events, hell=hell, time=tick, ticks_per_ms=1000 / CLOCK)
        bank_bgm = slot in banks and not a.stream
        if slot in banks:
            events = KB.remap(events, banks[slot], bgm=bank_bgm)
        ply, warnings = empress_events(events, holds, keysounds=slot in banks, bgm_slots=bank_bgm)
        (a.output / (name + '.bin')).write_bytes(encrypt(ply))
        notes = sum(1 for e in events if e[1] in (0, 1))
        bpms = sorted({round(e[3] / e[2], 2) if e[2] else e[3] for e in events if e[1] == 4})
        report['charts'][name] = {'notes': notes, 'holds': len(holds), 'mss_joints': len(A.mss_joints(events)), 'hcn': hell, 'total': notes + len(holds),
                                  'bpm': [bpms[0], bpms[-1]] if bpms else None, 'warnings': warnings}
        print('%s: %d notes, %d %s%s' % (name, notes, len(holds), 'HCN' if hell else 'holds', (' (' + '; '.join(warnings) + ')') if warnings else ''))
    if a.stream:
        slot = next((s for s, n in SLOTS.items() if n == a.stream.upper()), None)
        if slot not in charts:
            raise SystemExit('No %s chart in this song' % a.stream)
        import spu_adpcm as S
        full = render(charts[slot], keys)[2]['peak'] if a.split else None
        rate, channels, info = render(charts[slot], keys, bgm_only=a.split, peak=full)
        (a.output / 'stream.bin').write_bytes(S.stream(channels, rate))
        report['stream'] = dict(info, chart=a.stream.upper(), seconds=round(len(channels[0]) / rate, 2), bgm_only=a.split)
        print('stream: %.1f s from %s, %d keysounds' % (len(channels[0]) / rate, a.stream.upper(), info['keysounds_played']))
    elif a.split:
        # The record's stream field (0xec) always names a stream: the game reads its header (rate at
        # 0x18, channels at 0x1c) and divides by the channel count on the first refill. With the BGM in
        # the bank, a stream of silence the length of the longest chart.
        import spu_adpcm as S
        end = max(max(tick(e[0]) for e in ev) for ev in charts.values()) + 120
        n = (end * CLOCK // 1000000 + 1) * 44100
        (a.output / 'stream.bin').write_bytes(silent_stream(n))
        report['stream'] = {'seconds': round(n / 44100, 2), 'silent': True}
        print('stream: %.1f s of silence' % (n / 44100))
    (a.output / 'convert.json').write_text(json.dumps(report, indent=1) + '\n', encoding='utf-8')


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError) as e:
        print('arcade_convert: ' + str(e), file=sys.stderr)
        sys.exit(1)
