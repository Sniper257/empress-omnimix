"""Keysounds -> EMPRESS keysound bank ("wp\\x03\\x00"), one bank per play side (SP, DP).

Layout, read from a real EMPRESS bank (All My Turn, "SP": 497 slots, 495 samples, 5.3 MB) and from
the play function 0x140260 in SLPM_552.21:

  0x0000  header (where slot 0 would be): "wp\\x03\\x00", u32 0x10, name (8 bytes, "SP" / "DP")
  EMPRESS's own songs keysound the BGM from the bank too (All My Turn SPN: 286 BGM sounds,
  2498 events (tick, 0x87, 0, slot); slot 1 is the 3.4 s intro at tick 0). Banks above ~7 MB need
  the launcher's large-bank workspace (record byte 0xbd, launcher/main.c add_song).
  0x0010  slot entries 1..2047, 16 bytes each (play(slot | bank << 16) reads bank + slot * 16)
            +0 u8 priority (0)        +1 u8 exclusive group (0xff = none)   +2 u8 0
            +3 u8 2 (0 = empty slot)  +4 s32 length in ms                    +8 u8 1
            +9 u8 volume (0x7f)       +0xa u16 sample index                  +0xc u8 76 (meaning unknown;
            a few real slots have 86, slot 1 has 0)   +0xd..0xf 0
  0x8000  u32 sample count, u32 data bytes, u32 1, u32 0
  0x8010  sample entries, 16 bytes: u32 offset (from the data start), u32 bytes per channel,
            u8 channels (2), u8 0, u16 rate code, u32 0
  data    0x8010 + 16 * count. Per sample, channel 0 then channel 1. Each channel: a zero frame,
          the SPU ADPCM frames (the last one has flag 1), then 00 07 77 x14. Every frame is stored
          disguised: byte0 XOR 0xff, byte2 - 36 (spu_adpcm.BANK_DELTA).

Rate codes seen (checked against each slot's length in ms): 0x3e44 = 44100, 0x447d = 32000,
0x4800 = 24000. Other rates are resampled to 44100.
"""
import math
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import arcade_data as A  # noqa: E402
import spu_adpcm as S  # noqa: E402

MAGIC = b'wp\x03\x00'
TABLE = 0x8000
MAX_SLOT = TABLE // 16 - 1
RATE_CODES = {44100: 0x3e44, 32000: 0x447d, 24000: 0x4800}
END_FRAME = bytes([0, 7]) + b'\x77' * 14
SLOT_TAIL = 76
SILENCE = 64            # |sample| at or below this after the last loud sample is trimmed
LARGEST_SEEN = 6121248  # the largest real EMPRESS bank listed so far (All My Turn, DP)
LARGEST_LOADED = 11859552  # the largest converted bank seen to load in PCSX2 (Kailua, with the 0xbd flag);
                           # Elisha's 16 MB banks crashed on load


class FormatError(ValueError):
    pass


def player_sounds(*event_lists):
    """Arcade keysound numbers that a note can play (type 2/3 sample changes), first use first."""
    seen = {}
    for events in event_lists:
        for t, kind, param, value in sorted(events):
            if kind in (2, 3) and value and value not in seen:
                seen[value] = len(seen)
    return sorted(seen, key=seen.get)


def bgm_sounds(*event_lists):
    """Arcade keysound numbers the BGM (type 7) plays, first use first."""
    seen = {}
    for events in event_lists:
        for t, kind, param, value in sorted(events):
            if kind == 7 and value and value not in seen:
                seen[value] = len(seen)
    return sorted(seen, key=seen.get)


def slot_map(sounds):
    if len(sounds) > MAX_SLOT:
        raise ValueError('%d player keysounds; an EMPRESS bank has %d slots' % (len(sounds), MAX_SLOT))
    return {s: i + 1 for i, s in enumerate(sounds)}


def remap(events, slots, bgm=False):
    """Events with type 2/3 (and with bgm=True type 7) values changed from arcade keysound numbers to slots."""
    kinds = (2, 3, 7) if bgm else (2, 3)
    return [(t, k, p, slots.get(v, 0) if k in kinds else v) for t, k, p, v in events]


def stereo(wav):
    """(rate, int16 array [2, n]) with trailing silence removed; mono is duplicated."""
    import numpy as np
    rate, channels = A.decode_wav(wav)
    a = np.array(channels, dtype=np.int32)
    if a.shape[0] == 1:
        a = np.vstack([a, a])
    loud = np.nonzero(np.abs(a).max(0) > SILENCE)[0]
    a = a[:, :loud[-1] + 1] if len(loud) else a[:, :0]
    return rate, a


def resample(samples, src, dst):
    import numpy as np
    if src == dst or samples.shape[-1] == 0:
        return samples
    if dst < src:                                  # low-pass below the new Nyquist first (windowed sinc)
        taps = 63
        k = np.arange(taps) - (taps - 1) / 2
        h = np.sinc(k * 0.9 * dst / src) * np.hamming(taps)
        h /= h.sum()
        samples = np.vstack([np.convolve(c.astype(np.float64), h, mode='same') for c in samples])
    n = max(1, int(round(samples.shape[-1] * dst / src)))
    x = np.arange(n) * (src / dst)
    idx = np.arange(samples.shape[-1])
    return np.vstack([np.interp(x, idx, c.astype(np.float64)) for c in samples])


def channel_frames(samples):
    """Zero frame + ADPCM (flag 1 on the last frame) + end frame; plain, not disguised."""
    return bytes(16) + bytes(S.encode([int(v) for v in samples], S.FLAG_END)) + END_FRAME


def adpcm_bytes(n):
    """Stored size of one stereo sound of n samples per channel."""
    return 2 * (16 + -(-max(n, 1) // 28) * 16 + 16)


def fit_rates(lengths, budget, fixed=0):
    """{index: rate} for sounds of the given lengths at 44.1 kHz so the total stays within budget:
    the longest are lowered to 32 kHz first, then (still over) the longest to 24 kHz. Real EMPRESS
    banks mix these three rates. Indices left out stay at 44.1 kHz."""
    out = {}
    size = lambda i: adpcm_bytes(round(lengths[i] * out.get(i, 44100) / 44100))
    total = fixed + sum(size(i) for i in range(len(lengths)))
    order = sorted(range(len(lengths)), key=lambda i: -lengths[i])
    for rate in (32000, 24000):
        for i in order:
            if total <= budget:
                return out
            before = size(i)
            out[i] = rate
            total += size(i) - before
    return out


def build(name, sounds, keys, gain=1.0, budget=None):
    """(bank bytes, {arcade keysound: slot}, info) for the given arcade keysound numbers.
    budget: largest bank size in bytes; the longest sounds are stored at 32 or 24 kHz to fit."""
    import numpy as np
    sounds = [s for s in sounds if 0 < s <= len(keys)]
    slots = slot_map(sounds)
    entries, data, table = [], bytearray(), bytearray(TABLE)
    table[:16] = MAGIC + struct.pack('<I', 0x10) + name.encode('ascii')[:8].ljust(8, b'\0')
    rates = {}
    pcms = []
    for s in sounds:
        rate, pcm = stereo(keys[s - 1]['wav'])
        if rate not in RATE_CODES:
            pcm, rate = resample(pcm, rate, 44100), 44100
        pcms.append((rate, pcm))
    lower = {}
    if budget:                                     # only 44.1 kHz sounds are lowered; others are fixed cost
        full = [n for n, (r, _) in enumerate(pcms) if r == 44100]
        fixed = TABLE + 16 + 16 * len(sounds) + sum(adpcm_bytes(p.shape[1]) for r, p in pcms if r != 44100)
        lower = {full[k]: r for k, r in fit_rates([pcms[n][1].shape[1] for n in full], budget, fixed).items()}
    for n, s in enumerate(sounds):
        rate, pcm = pcms[n]
        if n in lower:
            pcm, rate = resample(pcm, 44100, lower[n]), lower[n]
        if gain != 1.0:
            pcm = pcm * gain
        pcm = np.clip(np.round(pcm), -32768, 32767).astype(np.int16)
        left, right = channel_frames(pcm[0]), channel_frames(pcm[1])
        index = len(entries)
        entries.append(struct.pack('<IIBBHI', len(data), len(left), 2, 0, RATE_CODES[rate], 0))
        data += left + right
        ms = math.ceil(pcm.shape[1] * 1000 / rate)
        struct.pack_into('<BBBBiBBHB3x', table, slots[s] * 16, 0, 0xff, 0, 2, ms, 1, 0x7f, index, SLOT_TAIL)
        rates[rate] = rates.get(rate, 0) + 1
    head = struct.pack('<4I', len(entries), len(data), 1, 0) + b''.join(entries)
    bank = bytes(table) + head + bytes(S.disguise(bytes(data), S.BANK_DELTA))
    info = {'name': name, 'sounds': len(sounds), 'bytes': len(bank), 'rates': rates, 'gain': gain}
    if budget and len(bank) > budget:
        info['warning'] = 'over the %d-byte budget even with every sound at 24 kHz' % budget
    elif len(bank) > LARGEST_SEEN:
        info['warning'] = 'larger than any real EMPRESS bank seen so far (%d bytes)' % LARGEST_SEEN
    return bank, slots, info


def read(bank):
    """Parse a bank: {'name', 'slots': {slot: dict}, 'samples': [dict with plain 'frames']}."""
    if len(bank) < TABLE + 16 or bank[:4] != MAGIC:
        raise FormatError('Not an EMPRESS keysound bank')
    count, size, one, _ = struct.unpack_from('<4I', bank, TABLE)
    base = TABLE + 16 + 16 * count
    if count > MAX_SLOT * 2 or base + size != len(bank):
        raise FormatError('Bank sizes do not add up')
    codes = {v: k for k, v in RATE_CODES.items()}
    samples = []
    plain = bytes(S.disguise(bank[base:], S.BANK_DELTA, reverse=True))
    for i in range(count):
        off, per, ch, _, code, _ = struct.unpack_from('<IIBBHI', bank, TABLE + 16 + 16 * i)
        if off + per * ch > size:
            raise FormatError('Sample %d runs past the data' % i)
        samples.append({'offset': off, 'bytes': per, 'channels': ch, 'rate_code': code, 'rate': codes.get(code),
                        'frames': [plain[off + c * per:off + (c + 1) * per] for c in range(ch)]})
    slots = {}
    for n in range(1, MAX_SLOT + 1):
        row = struct.unpack_from('<BBBBiBBHB', bank, n * 16)
        if row[3]:
            slots[n] = dict(zip(('priority', 'group', 'b2', 'used', 'ms', 'b8', 'volume', 'sample', 'tail'), row))
    return {'name': bank[8:16].rstrip(b'\0').decode('latin-1'), 'slots': slots, 'samples': samples}
