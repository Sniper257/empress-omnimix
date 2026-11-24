"""Player keysounds for one arcade chart -> EMPRESS keysound slots and SPU ADPCM samples.

What is known about the EMPRESS bank (play function 0x140260 in SLPM_552.21):
  play(slot | bank << 16 | flags): slot 1..0x800, entry = 0x757600 + bank * 0x8000 + slot * 16.
  So a loaded bank begins with a 0x8000-byte table of 2048 16-byte slot entries; it is the part of
  the bank file before the (count, data size, 1, ...) header at 0x8000.
  Slot entry, as the play function reads it:
    +0 u8   priority (compared with the playing voices)
    +1 u8   exclusive group (0xff = none; a new sound stops the voice in the same group)
    +2 u8   low nibble and high nibble, both copied to the voice
    +3 u8   nonzero = slot in use (0 = play nothing)
    +4 s32  length in ms (converted to frames; 0 = none)
    +8 u8, +9 u8, +0xa u16 (probably the sample index into the 0x8010 table), +0xc u8
  The 16-byte sample entries at 0x8010 and the meaning of +2/+8/+9/+0xc still need a real bank to read.

This module does everything that does not depend on those unknowns:
  * the sounds the player triggers (type 2/3 sample-change events, heard when a note is hit)
  * one slot per sound, in first-use order, and the chart's type 2/3 values rewritten to slots
  * trim trailing silence, downmix to mono, resample to the highest rate that fits the budget
  * SPU ADPCM encoding (frames are written plain; the bank disguise is applied by the writer)
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import arcade_data as A  # noqa: E402
import spu_adpcm as S  # noqa: E402

MAX_SLOT = 0x800
RATES = (44100, 32000, 24000, 22050, 16000, 11025)
SILENCE = 64            # |sample| at or below this after the last loud sample is trimmed


def player_sounds(events):
    """Arcade keysound numbers that a note can play, in order of first use."""
    seen = {}
    for t, kind, param, value in sorted(events):
        if kind in (2, 3) and value and value not in seen:
            seen[value] = len(seen)
    return sorted(seen, key=seen.get)


def slot_map(sounds):
    if len(sounds) > MAX_SLOT:
        raise ValueError('%d player keysounds; an EMPRESS bank has %d slots' % (len(sounds), MAX_SLOT))
    return {s: i + 1 for i, s in enumerate(sounds)}


def remap(events, slots):
    """Events with type 2/3 values changed from arcade keysound numbers to bank slots."""
    return [(t, k, p, slots.get(v, 0) if k in (2, 3) else v) for t, k, p, v in events]


def mono(wav):
    """(rate, int16 mono samples with trailing silence removed)."""
    import numpy as np
    rate, channels = A.decode_wav(wav)
    a = np.array(channels, dtype=np.int32)
    m = a.mean(0) if a.shape[0] > 1 else a[0].astype(np.float64)
    loud = np.nonzero(np.abs(m) > SILENCE)[0]
    m = m[:loud[-1] + 1] if len(loud) else m[:0]
    return rate, np.clip(np.round(m), -32768, 32767).astype(np.int16)


def resample(samples, src, dst):
    import numpy as np
    if src == dst or len(samples) == 0:
        return samples
    n = max(1, int(round(len(samples) * dst / src)))
    x = np.arange(n) * (src / dst)
    return np.clip(np.round(np.interp(x, np.arange(len(samples)), samples.astype(np.float64))), -32768, 32767).astype(np.int16)


def adpcm_size(count):
    return ((max(count, 1) + 27) // 28) * 16


def choose_rate(lengths, budget, rates=RATES):
    """Highest rate whose ADPCM total fits `budget` bytes. lengths = [(rate, samples)]."""
    for rate in rates:
        total = sum(adpcm_size(int(round(n * rate / r))) for r, n in lengths)
        if total <= budget:
            return rate, total
    raise ValueError('Keysounds do not fit %d bytes even at %d Hz' % (budget, rates[-1]))


def build(events, keys, budget, rates=RATES, encode=True, gain=1.0):
    """{'slots': {arcade: slot}, 'rate', 'bytes', 'samples': [{slot, keysound, rate, count, adpcm}]}.

    gain: the scale applied to the BGM stream (32767 / autoplay peak), so notes and BGM stay balanced."""
    sounds = [s for s in player_sounds(events) if 0 < s <= len(keys)]
    slots = slot_map(sounds)
    pcm = {s: mono(keys[s - 1]['wav']) for s in sounds}
    rate, total = choose_rate([(r, len(m)) for r, m in pcm.values()], budget, rates)
    samples = []
    for s in sounds:
        r, m = pcm[s]
        m = resample(m, r, rate)
        if gain != 1.0:
            import numpy as np
            m = np.clip(np.round(m * gain), -32768, 32767).astype(np.int16)
        row = {'slot': slots[s], 'keysound': s, 'rate': rate, 'count': len(m)}
        if encode:
            row['adpcm'] = bytes(S.encode(m.tolist(), S.FLAG_END))
        samples.append(row)
    return {'slots': slots, 'rate': rate, 'bytes': total, 'gain': gain, 'samples': samples}
