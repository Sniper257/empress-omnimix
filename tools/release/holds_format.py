"""holds.bin: CN/HCN/BSS/HBSS list of one chart, read by the runtime hold module. Stdlib only.

  b'MMHL', u16 version (1), u16 count, then count x hold_def:
  s32 start, s32 end, u8 lane, u8 kind, u16 0            (tools/holds/holds.h, 12 bytes)

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
CN, HCN, BSS, HBSS = range(4)
MAX_HOLDS = 1024


def kind_for(lane, hell):
    spin = (lane & 7) == 7
    return (HBSS if hell else BSS) if spin else (HCN if hell else CN)


def from_events(events, hell=False, time=lambda t: t):
    """Holds of an arcade-style event list: notes (type 0/1) with value > 0.

    `time` maps source time (ms for BMS/arcade) to EMPRESS chart time; it must be
    monotonic. Returns a sorted list of (start, end, lane, kind).
    """
    holds = []
    for t, kind, param, value in events:
        if kind not in (0, 1) or value <= 0:
            continue
        if param > 7:
            raise ValueError('Invalid note column %d' % param)
        lane = param + 8 * kind
        start, end = time(t), time(t + value)
        if end <= start:
            end = start + 1
        holds.append((start, end, lane, kind_for(lane, hell)))
    holds.sort()
    for a, b in zip(holds, holds[1:]):
        if a[2] == b[2] and b[0] < a[1]:
            raise ValueError('Overlapping holds on lane %d at %d' % (a[2], b[0]))
    if len(holds) > MAX_HOLDS:
        raise ValueError('Chart has more than %d holds' % MAX_HOLDS)
    return holds


def pack(holds):
    holds = sorted(holds)
    return MAGIC + struct.pack('<HH', VERSION, len(holds)) + b''.join(RECORD.pack(s, e, l, k, 0) for s, e, l, k in holds)


def unpack(data):
    if len(data) < 8 or data[:4] != MAGIC:
        raise ValueError('Not a holds.bin')
    version, count = struct.unpack_from('<HH', data, 4)
    if version != VERSION or len(data) != 8 + RECORD.size * count or count > MAX_HOLDS:
        raise ValueError('Unsupported or truncated holds.bin')
    out = []
    for i in range(count):
        s, e, lane, kind, pad = RECORD.unpack_from(data, 8 + RECORD.size * i)
        if pad or lane > 15 or kind > HBSS or e <= s or ((lane & 7) == 7) != (kind in (BSS, HBSS)):
            raise ValueError('Invalid hold record %d' % i)
        out.append((s, e, lane, kind))
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
