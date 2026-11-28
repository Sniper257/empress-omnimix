"""holds.bin: CN/HCN/BSS/HBSS list of one chart, read by the runtime hold module. Stdlib only.

  b'MMHL', u16 version (1), u16 count, then count x hold_def:
  s32 start, s32 end, u8 lane, u8 kind, u16 tick64       (tools/holds/holds.h, 12 bytes)

tick64 is the 16th-note length at the hold's start, in 1/64 chart ticks: HCN and
HBSS gain or lose gauge once per 16th (docs/holds-design.md). It is 0 for CN/BSS.

Lanes 0..6 P1 keys, 7 P1 turntable, 8..14 P2 keys, 15 P2 turntable. Kinds: 0 CN,
1 HCN, 2 BSS, 3 HBSS. Records are sorted by (start, lane). Times are chart time
of the converted EMPRESS chart.

Source charts (arcade .1 or bms.py) mark a hold as a note whose value is its
length. Whether a chart uses hell variants is a chart-wide choice in IIDX, so it
is passed as `hell` (BMS: #LNMODE 3).
"""
import struct

MAGIC = b'MMHL'
VERSION = 1
RECORD = struct.Struct('<iiBBH')
FP = 64                     # holds.h HOLDS_FP
CN, HCN, BSS, HBSS = range(4)
MAX_HOLDS = 1024


def kind_for(lane, hell):
    spin = (lane & 7) == 7
    return (HBSS if hell else BSS) if spin else (HCN if hell else CN)


def bpm_at(events, t, default=130.0):
    """BPM in force at source time t, from tempo events (type 4: value / param)."""
    bpm = default
    for et, kind, param, value in sorted(e for e in events if e[1] == 4):
        if et > t:
            break
        if param > 0 and value > 0:
            bpm = value / param
    return bpm


JOINT_SLACK_MS = 2   # overlap of back-to-back holds taken as a joint (source ms)


def from_events(events, hell=False, time=lambda t: t, ticks_per_ms=1.0):
    """Holds of an arcade-style event list: notes (type 0/1) with value > 0.

    `time` maps source time (ms for BMS/arcade) to EMPRESS chart time and must be
    monotonic; `ticks_per_ms` is the same scale as a number (1000 / 16683 for
    EMPRESS frames), used for the 16th-note tick length. Returns a sorted list of
    (start, end, lane, kind, tick64).
    """
    raw = []
    for t, kind, param, value in events:
        if kind not in (0, 1) or value <= 0:
            continue
        if param > 7:
            raise ValueError('Invalid note column %d' % param)
        raw.append([t, t + value, param + 8 * kind])
    # A multi-spin scratch is turntable holds that start as the previous ends; the arcade's whole ms can leave the
    # previous 1 ms past the next one's start (Speculation 31012, SUGAR RUSH 32068): it ends where the next begins.
    raw.sort()
    last = {}
    for h in raw:
        p = last.get(h[2])
        if p is not None and h[0] < p[1] <= h[0] + JOINT_SLACK_MS:
            p[1] = h[0]
        last[h[2]] = h
    holds = []
    for t0, t1, lane in raw:
        t = t0
        start, end = time(t0), time(t1)
        if end <= start:
            end = start + 1
        k = kind_for(lane, hell)
        tick64 = 0
        if k in (HCN, HBSS):
            tick64 = max(1, min(0xffff, round(15000.0 / bpm_at(events, t) * ticks_per_ms * FP)))
        holds.append((start, end, lane, k, tick64))
    holds.sort()
    for a, b in zip(holds, holds[1:]):
        if a[2] == b[2] and b[0] < a[1]:
            raise ValueError('Overlapping holds on lane %d at %d' % (a[2], b[0]))
    if len(holds) > MAX_HOLDS:
        raise ValueError('Chart has more than %d holds' % MAX_HOLDS)
    return holds


def pack(holds):
    holds = sorted(holds)
    return MAGIC + struct.pack('<HH', VERSION, len(holds)) + b''.join(RECORD.pack(*h) for h in holds)


def unpack(data):
    if len(data) < 8 or data[:4] != MAGIC:
        raise ValueError('Not a holds.bin')
    version, count = struct.unpack_from('<HH', data, 4)
    if version != VERSION or len(data) != 8 + RECORD.size * count or count > MAX_HOLDS:
        raise ValueError('Unsupported or truncated holds.bin')
    out = []
    for i in range(count):
        s, e, lane, kind, tick64 = RECORD.unpack_from(data, 8 + RECORD.size * i)
        if lane > 15 or kind > HBSS or e <= s or ((lane & 7) == 7) != (kind in (BSS, HBSS)):
            raise ValueError('Invalid hold record %d' % i)
        if (kind in (HCN, HBSS)) != (tick64 > 0):
            raise ValueError('Hold record %d: tick64 must be set for HCN/HBSS only' % i)
        out.append((s, e, lane, kind, tick64))
    if out != sorted(out):
        raise ValueError('holds.bin records are not sorted')
    return out


def strip_lengths(events):
    """Native chart events: hold heads become ordinary notes (value 0). The tail
    judgement is done by the hold module, so tails are not added as notes."""
    return [(t, k, p, 0 if k in (0, 1) else v) for t, k, p, v in events]


def note_total(events, holds):
    """Notes for score/gauge totals: IIDX counts a CN/BSS as two (head + tail)."""
    return sum(1 for e in events if e[1] in (0, 1)) + len(holds)
