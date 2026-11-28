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


# --native: the CS disc's own timing. All My Turn's CS charts put each event at floor(ms * 60/1001 + 1.63)
# frames (0 stays 0), against round(ms / 16.683) here: about one frame later. That matches 98.3% of
# 15,216 event times across six charts exactly, the rest by one frame (the arcade's ms are rounded).
NATIVE_LEAD = 163   # hundredths of a frame
native_timing = False


def tick(ms):
    if native_timing and ms:
        t = (ms * 6000 + NATIVE_LEAD * 1001) // 100100
    else:
        t = (ms * 1000 + CLOCK // 2) // CLOCK
    if t >= END:
        raise ValueError('Chart is longer than EMPRESS charts allow (%d ms)' % ms)
    return t


TAIL_SHIFT = 2   # holds.h HOLDS_TAIL_SHIFT


def tail_tick(start, stop, taken, after=False):
    """Tick for a hold's tail note: its end, or the nearest tick within TAIL_SHIFT (earlier first)
    that has no other note. The game judges all notes at one tick as a row, and a never-judged tail
    in another hold's head row left that head unjudged until its window ran out.

    after=True (a multi-spin scratch joint: the next section starts on the same turntable where this one
    ends): later ticks first, so the next section's head comes before the tail in the lane. With the
    tail first, the reversal at the joint found the never-judged tail and the game showed an empty POOR."""
    for off in ((1, 2, -1, -2) if after else (0, -1, 1, -2, 2)):
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
        starts = {(h[0], h[2]) for h in holds}
        for start, stop, lane, kind, tick64 in holds:
            out.append((tail_tick(start, stop, {e[0] for e in out if e[1] & 15 in (0, 1)}, after=(stop, lane) in starts),
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


def _song_files(path):
    """(get(name suffix) -> bytes or None, song id) for an .ifs or a folder with <id>.1/.2dx."""
    path = Path(path)
    if path.is_file() and path.suffix.lower() == '.ifs':
        import ifs
        files = ifs.read(path.read_bytes())
        one = next((k.rsplit('/', 1)[-1] for k in files if k.lower().endswith('.1')), None)
        sid = one[:-2] if one else path.stem                 # the song id the files inside are named by
        def get(suffix):
            want = (sid + suffix).lower()
            return next((v for k, v in files.items() if k.lower().rsplit('/', 1)[-1] == want), None)
        return get, sid
    sid = path.name
    return (lambda suffix: (path / (sid + suffix)).read_bytes() if (path / (sid + suffix)).is_file() else None), sid


def load_keys(path, code='0'):
    """The keysound entries of one keysound file: '0' is <id>.2dx (or .s3p), another code c is the
    <id><c>.2dx some charts use instead (music_data +0x684; bm2dx.dll "%05d/%05d%c.2dx")."""
    get, sid = _song_files(path)
    tail = '' if code == '0' else code
    two = get(tail + '.2dx')
    if two:
        return A.read_2dx(two)[1]
    s3p = get(tail + '.s3p')
    if s3p:
        return s3p_keys(s3p)
    if code != '0':
        raise ValueError('%s: no %s%s.2dx or .s3p for the charts that use keysound file %r' % (path, sid, tail, code))
    return []


def load_song(path):
    """(charts {slot: events}, keysound entries, song id) from an .ifs or a folder with <id>.1/.2dx."""
    get, sid = _song_files(path)
    one = get('.1')
    if one is None:
        raise ValueError('No .1 chart in ' + str(path))
    return A.read_charts(one), load_keys(path), sid


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
# Loudness each chart's autoplay mix is brought to (RMS over both channels, int16 scale): the level
# Kailua (HCN Ver.) came out at with peak scaling, which plays level with the game's own songs. Peak
# scaling alone left hot masters quiet: Purple Perplex's raw mix peaks at 89,616 (about 1% of its samples
# over full scale), so scaled to its peak it came out at RMS 3,600 against 4,700-4,950 for the others.
# The premixed BGM goes through keysound_bank.soft_limit for what still goes over full scale.
TARGET_RMS = 4950


def premix_bgm(charts, keys, piece_ticks=PIECE_TICKS):
    """Each chart's BGM (type-7 events) mixed into one track and cut into piece_ticks pieces (0.5 s),
    each a new keysound started on its tick. EMPRESS's own BGM is short sounds at a modest rate; the
    arcade's long backing tracks and ~2,500 layered BGM events a chart lost voices on the PS2 (long
    sounds cut, BGM silent now and then). Records the gain each chart needs in report_gain (the mix
    and the chart's note sounds scaled alike, so the mix keeps its balance): the autoplay mix brought
    to TARGET_RMS, its rare peaks over full scale soft-limited in the bank."""
    import numpy as np
    keys = list(keys)
    out = {}
    for slot, events in charts.items():
        _, full, _ = render(events, keys, raw=True)
        rms = float(np.sqrt(np.mean(np.square(full, dtype=np.float64)))) if full.size else 0.0
        gain = min(1.0, TARGET_RMS / rms) if rms else 1.0
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


NATIVE_MAX_SLOTS = 2047      # keysound_bank.MAX_SLOT: slots of an EMPRESS bank
NATIVE_REVISION = 3          # 2: the stream starts with the track (the game starts it at the track's event); 3: per-chart keysound files
NATIVE_STREAM_LEAD = 28      # samples of silence the CS stream starts with (one ADPCM frame), as its bank samples do
NATIVE_STREAM_GAIN = 0.965    # All My Turn's CS stream against arcade sound 1: 0.9647-0.9652 in every 10 s window, 25 dB SNR (ADPCM noise)


def keyed_sounds(charts):
    """Arcade sounds some note plays in any chart (the sample assigned to its lane when it is hit)."""
    out = set()
    for ev in charts.values():
        cur = {}
        for t, typ, p, v in sorted(ev, key=lambda e: e[0]):
            if typ in (2, 3):
                cur[(typ - 2, p)] = v
            elif typ in (0, 1) and cur.get((typ, p)):
                out.add(cur[(typ, p)])
    return out


def backing_track(charts, keys):
    """The arcade sound the stream carries: the BGM sound every chart starts at the same time, that no note
    plays, the longest such (All My Turn: sound 1 at 0 ms, the whole 121 s backing track). None if none."""
    keyed = keyed_sounds(charts)
    sets = [set((e[0], e[3]) for e in ev if e[1] == 7 and e[3]) for ev in charts.values()]
    common = {v for t, v in set.intersection(*sets)} - keyed if sets else set()
    if not common:
        return None
    import keysound_bank as KB
    return max(sorted(common), key=lambda v: len(KB.stereo(keys[v - 1]['wav'])[1][0]))


def stream_start(source):
    """ms at which the --native stream's track (backing_track) starts in the charts; None without one."""
    charts, keys, sid = load_song(source)
    track = backing_track(charts, keys)
    if not track:
        return None
    return min(t for ev in charts.values() for t, k, p, v in ev if k == 7 and v == track)


# music_data +0x684: one character per chart in this order; '0' = the song's own keysound file
VARIANT_ORDER = ['SPB', 'SPN', 'SPH', 'SPA', 'SPX', 'DPB', 'DPN', 'DPH', 'DPA', 'DPX']
# record bank groups (bank +0, stream +4), picked by difficulty; 0xe8 is the fallback (arcade_songfolder)
GROUP_FIELD = {'H': 0xe8, 'X': 0xe8, 'A': 0xf0, 'N': 0xf8, 'B': 0x100}


def chart_codes(variants, names):
    """{chart name: keysound file code} from the 10-character +0x684 string ('0' when absent)."""
    v = (variants or '').ljust(10, '0')[:10]
    return {n: (v[VARIANT_ORDER.index(n)] if n in VARIANT_ORDER else '0').replace('\0', '0') or '0' for n in names}


def native(a, charts, keys, sid):
    """--split --native: the CS disc's layout (see the --native help). With per-chart keysound files
    (--variants), each difficulty group (GROUP_FIELD) gets a bank of the files its charts use, one after
    another, and a stream of the backing track of the file most of them use."""
    import numpy as np
    import keysound_bank as KB
    import spu_adpcm as S
    global native_timing
    native_timing = True
    names = {slot: SLOTS.get(slot, 'slot%d' % slot) for slot in charts}
    codes = chart_codes(a.variants, names.values())
    files = {'0': keys}
    for c in sorted(set(codes.values()) - {'0'}):
        files[c] = load_keys(a.song, c)
    by_code = {c: {slot: ev for slot, ev in charts.items() if codes[names[slot]] == c} for c in files}
    tracks = {c: backing_track(chs, files[c]) if chs else None for c, chs in by_code.items()}
    report = {'song': sid, 'keysounds': len(keys), 'charts': {}, 'layout': 'native', 'native_revision': NATIVE_REVISION,
              'variants': {n: c for n, c in codes.items() if c != '0'}, 'banks': {}, 'groups': {}}
    groups = {}
    for slot, n in names.items():
        groups.setdefault(GROUP_FIELD.get(n[-1], 0xe8), []).append(slot)
    if 0xe8 not in groups:                                   # the fallback group must hold a bank
        groups[0xe8] = []
    simple = set(codes.values()) == {'0'}
    built = {}
    volume = max(0, min(127, round(a.volume * 100 / 127)))
    for field in sorted(groups):
        slots_here = groups[field]
        used = [codes[names[s]] for s in slots_here] or ['0']
        order = sorted(set(used), key=lambda c: (c != '0', c))
        lead = max(order, key=lambda c: (used.count(c), c == '0'))      # the file whose backing track streams
        key = (tuple(order), lead)
        if key not in built or built[key][4]:          # a mixed stream is the group's own
            tag = '' if simple else '_' + ''.join(order) + ('' if len(order) == 1 else '-' + lead)
            offset, combined = {}, []
            for c in order:
                offset[c] = len(combined)
                combined += files[c]
            sounds = list(range(1, len(combined) + 1))
            mixed = set()                                   # (ms, sound) BGM events this group's stream carries
            if len(sounds) > NATIVE_MAX_SLOTS:
                # More sounds than a bank has slots (VØID: 2,496). The BGM every chart of the group plays at the same
                # time, from sounds no note of them plays, goes into the group's stream with the backing track (the
                # layout of --stream common); the bank keeps what the charts still play.
                if len(order) > 1:
                    raise ValueError('%d sounds in keysound files %s; an EMPRESS bank has %d slots'
                                     % (len(combined), ', '.join(order), NATIVE_MAX_SLOTS))
                group_charts = {s_: charts[s_] for s_ in slots_here}
                keyed = keyed_sounds(group_charts)
                sets_ = [set((t, v) for t, k, p, v in ev if k == 7 and v) for ev in group_charts.values()]
                track_ = tracks.get(lead)
                if not track_:
                    raise ValueError('%d sounds and no backing track to mix BGM into; an EMPRESS bank has %d slots'
                                     % (len(combined), NATIVE_MAX_SLOTS))
                start_ = min(t for ev in by_code[lead].values() for t, k, p, v in ev if k == 7 and v == track_)
                common_ = set.intersection(*sets_) if sets_ else set()
                mixed = {(t, v) for t, v in common_ if v not in keyed and v != track_ and t >= start_}
                used_here = {v for ev in group_charts.values() for t, k, p, v in ev
                             if v and ((k in (2, 3)) or (k == 7 and (t, v) not in mixed))}
                used_here.add(track_)
                sounds = sorted(used_here)
                print('%d sounds, more than a bank\'s %d slots: %d BGM events mixed into the stream, %d sounds left'
                      % (len(combined), NATIVE_MAX_SLOTS, len(mixed), len(sounds)))
                if len(sounds) > NATIVE_MAX_SLOTS:
                    raise ValueError('%d sounds still played by the %s charts; an EMPRESS bank has %d slots'
                                     % (len(sounds), '/'.join(names[s_] for s_ in slots_here), NATIVE_MAX_SLOTS))
            track = tracks.get(lead)
            alias = None
            if track:
                own = [x for x in sounds if offset[lead] < x <= offset[lead] + len(files[lead])]
                t = offset[lead] + track
                alias = next((x for x in own[own.index(t) + 1:] + own[:own.index(t)][::-1]), None)
            data, slots, info = KB.build('SP', sounds, combined, 1.0, a.bank_budget or None,
                                         aliases={offset[lead] + track: alias} if track and alias else None)
            bank = 'keys_%x.bank' % field if mixed else 'keys%s.bank' % tag
            (a.output / bank).write_bytes(data)
            report['banks'][bank] = dict(info, files=order)
            print('%s: %d slots, %d bytes, rates %s%s' % (bank, info['sounds'], info['bytes'], info['rates'],
                  '' if simple else ' (keysound files %s)' % ', '.join(order)))
            stream = None
            if track:
                # The game starts the stream at the chart's event for the track's slot (the stream replaces that slot),
                # not at the chart's start: so the stream begins with the track itself. Light Shine (17026) starts its
                # track at 875 ms; with that much silence in front, a PCSX2 capture had the backing track 0.9 s late.
                start = min(t for ev in by_code[lead].values() for t, k, p, v in ev if k == 7 and v == track)
                rate, mix, rinfo = render([(0, 7, 0, track)] + [(t - start, 7, 0, v) for t, v in sorted(mixed)],
                                          files[lead], bgm_only=True, raw=True)
                mix = np.clip(np.round(mix * NATIVE_STREAM_GAIN), -32768, 32767).astype(np.int16)
                mix = np.hstack([np.zeros((2, NATIVE_STREAM_LEAD), np.int16), mix])
                stream = 'stream%s.bin' % (('_%x' % field) if mixed else '' if simple else '_' + lead)
                if not (a.output / stream).is_file():
                    (a.output / stream).write_bytes(S.stream([mix[0].tolist(), mix[1].tolist()], rate, volume=volume))
                    print('%s: sound %d of keysound file %s, %.1f s, header volume %#x%s'
                          % (stream, track, lead, mix.shape[1] / rate, volume,
                             '; its slot points at sound %d\'s sample' % (alias - offset[lead]) if alias else ''))
                report.setdefault('streams', {})[stream] = dict(rinfo, file=lead, sound=track, start_ms=start,
                                                                seconds=round(mix.shape[1] / rate, 2), volume=volume)
            elif not simple or field == 0xe8:
                print('%s: no stream (no backing track every chart shares); all BGM plays from the bank' % bank)
            built[key] = (bank, stream, slots, offset, mixed)
        bank, stream, slots, offset, mixed = built[key]
        report['groups']['%#x' % field] = {'bank': bank, 'stream': stream, 'charts': [names[s] for s in slots_here]}
        for slot in slots_here:
            events, name, c = charts[slot], names[slot], codes[names[slot]]
            hell = a.hell or is_hell(events)
            holds = H.from_events(events, hell=hell, time=tick, ticks_per_ms=1000 / CLOCK)
            if mixed:                                   # these play from the stream
                events = [e for e in events if not (e[1] == 7 and (e[0], e[3]) in mixed)]
            ev = KB.remap(events, {v: slots[offset[c] + v] for v in range(1, len(files[c]) + 1) if offset[c] + v in slots}, bgm=True)
            ply, warnings = empress_events(ev, holds, keysounds=True, bgm_slots=True)
            (a.output / (name + '.bin')).write_bytes(encrypt(ply))
            notes = sum(1 for e in events if e[1] in (0, 1))
            bpms = sorted({round(e[3] / e[2], 2) if e[2] else e[3] for e in events if e[1] == 4})
            report['charts'][name] = {'notes': notes, 'holds': len(holds), 'mss_joints': len(A.mss_joints(events)), 'hcn': hell,
                                      'total': notes + len(holds), 'bpm': [bpms[0], bpms[-1]] if bpms else None,
                                      'warnings': warnings, 'keysound_file': c}
            print('%s: %d notes, %d %s%s%s' % (name, notes, len(holds), 'HCN' if hell else 'holds',
                  '' if c == '0' else ', keysound file ' + c, (' (' + '; '.join(warnings) + ')') if warnings else ''))
    # the single-file case keeps the earlier report fields
    if simple:
        report['stream'] = next(iter(report.get('streams', {}).values()), None)
    (a.output / 'convert.json').write_text(json.dumps(report, indent=1) + '\n', encoding='utf-8')


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('song', type=Path, help='<id>.ifs, or a folder holding <id>.1 and <id>.2dx')
    p.add_argument('output', type=Path)
    p.add_argument('--stream', metavar='CHART', help='render the backing stream from this chart (e.g. SPA), or "common": '
                   'the BGM every chart shares, as EMPRESS\'s own songs do (with --split: one bank of the sounds '
                   'played by notes in any chart plus each chart\'s own extra BGM, which stays bank-triggered)')
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
    p.add_argument('--native', action='store_true',
                   help='with --split: EMPRESS\'s own layout, as on the CS disc (All My Turn, 16017, checked sound by '
                        'sound): every arcade sound in one bank at full level, slot n = arcade sound n, every chart\'s '
                        'events kept (BGM bank-triggered), and a stream of the backing track (the BGM sound every chart '
                        'starts, no note plays) at 0.965, its own slot pointed at the next sound\'s sample, the arcade volume '
                        'in the stream header (--volume)')
    p.add_argument('--variants', default='', help='with --native: music_data +0x684, the keysound file per chart '
                   '(10 characters, %s; 0 = <id>.2dx, c = <id>c.2dx)' % ' '.join(VARIANT_ORDER))
    p.add_argument('--volume', type=int, default=127, help='the song\'s arcade volume (music_data +0x680, 0..127); '
                   'with --native it goes to the stream header as volume * 100 / 127 (All My Turn: 65 -> 0x33)')
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
    if a.native:
        if not a.split or a.stream:
            raise SystemExit('--native needs --split and no --stream')
        return native(a, charts, keys, sid)
    common_mode = bool(a.stream) and a.stream.lower() == 'common'
    if common_mode and not a.split:
        raise SystemExit('--stream common needs --split')
    common_events = []
    if common_mode:
        # EMPRESS layout (All My Turn: one 5.3 MB bank at 0xe8 for every chart, a 6.1 MB stream at 0xec): the
        # BGM events every chart has alike go to the stream; what a chart has beyond them (sounds another chart
        # plays with notes) stays a bank-triggered BGM event of that chart.
        full_charts = charts
        sets = [set((e[0], e[3]) for e in ev if e[1] == 7 and e[3]) for ev in charts.values()]
        common = set.intersection(*sets) if sets else set()
        common_events = sorted((t, 7, 0, v) for t, v in common)
        charts = {s: [e for e in ev if not (e[1] == 7 and e[3] and (e[0], e[3]) in common)] for s, ev in charts.items()}
        report['common_bgm_events'] = len(common_events)
    if a.split:
        import keysound_bank as KB
        gain = 1.0
        if common_mode:                               # the loudest chart's whole mix sets the gain of both
            peak = max(render(ev, keys)[2]['peak'] for ev in full_charts.values())
            gain = min(1.0, 32767 / peak) if peak else 1.0
        elif a.stream:                                # the bank gets the gain the stream gets
            peak = render(charts[next(s for s, n in SLOTS.items() if n == a.stream.upper())], keys)[2]['peak']
            gain = min(1.0, 32767 / peak) if peak else 1.0
        if common_mode:
            members = sorted(charts)
            sounds = KB.player_sounds(*(charts[s] for s in members))
            sounds += [v for v in KB.bgm_sounds(*(charts[s] for s in members)) if v not in set(sounds)]
            data, slots, info = KB.build('SP', sounds, keys, gain, a.bank_budget or None)
            (a.output / 'keys.bank').write_bytes(data)
            banks.update({s: slots for s in members})
            report.setdefault('banks', {})['keys'] = info
            print('keys.bank: %d sounds, %d bytes, rates %s%s' % (info['sounds'], info['bytes'], info['rates'],
                                                            ' (' + info['warning'] + ')' if 'warning' in info else ''))
        elif a.stream:
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
        bank_bgm = slot in banks and (not a.stream or common_mode)
        if slot in banks:
            events = KB.remap(events, banks[slot], bgm=bank_bgm)
        ply, warnings = empress_events(events, holds, keysounds=slot in banks, bgm_slots=bank_bgm)
        (a.output / (name + '.bin')).write_bytes(encrypt(ply))
        notes = sum(1 for e in events if e[1] in (0, 1))
        bpms = sorted({round(e[3] / e[2], 2) if e[2] else e[3] for e in events if e[1] == 4})
        report['charts'][name] = {'notes': notes, 'holds': len(holds), 'mss_joints': len(A.mss_joints(events)), 'hcn': hell, 'total': notes + len(holds),
                                  'bpm': [bpms[0], bpms[-1]] if bpms else None, 'warnings': warnings}
        print('%s: %d notes, %d %s%s' % (name, notes, len(holds), 'HCN' if hell else 'holds', (' (' + '; '.join(warnings) + ')') if warnings else ''))
    if common_mode:
        import spu_adpcm as S
        rate, channels, info = render(common_events, keys, bgm_only=True, peak=peak if a.split else None)
        (a.output / 'stream.bin').write_bytes(S.stream(channels, rate))
        report['stream'] = dict(info, chart='common', seconds=round(len(channels[0]) / rate, 2), bgm_only=True)
        print('stream: %.1f s of the BGM all charts share (%d events)' % (len(channels[0]) / rate, len(common_events)))
    elif a.stream:
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
