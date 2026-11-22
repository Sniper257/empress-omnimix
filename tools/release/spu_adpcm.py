"""PS2 SPU2 ADPCM (VAG-style) codec and EMPRESS frame framing. Stdlib only.

A frame is 16 bytes = 28 samples: byte0 = filter<<4 | shift, byte1 = loop
flags, bytes 2..15 = 4-bit residuals, low nibble first.

EMPRESS stores frames with two bytes disguised (see audio_transform.py, which
converts older discs to this form): byte0 ^ 0xff, and byte2 minus 36 in
keysound banks ("wp\\x03") or minus 2 in backing streams. `disguise` applies
that; `plausibility` lets the probe confirm the rule on real EMPRESS files.
"""
import struct

FILTERS = ((0, 0), (60, 0), (115, -52), (98, -55), (122, -60))
BANK_DELTA = 36
STREAM_DELTA = 2
FLAG_END = 1      # loop end: voice stops/releases after this frame
FLAG_REPEAT = 2
FLAG_START = 4
STREAM_MAGIC = 0x08640001


def _clamp(v):
    return -32768 if v < -32768 else 32767 if v > 32767 else v


def decode(data, state=(0, 0)):
    """Decode raw (undisguised) ADPCM frames to int samples."""
    if len(data) % 16:
        raise ValueError('ADPCM data is not whole frames')
    s1, s2 = state
    out = []
    for at in range(0, len(data), 16):
        head = data[at]
        f, shift = head >> 4, head & 15
        if f > 4 or shift > 12:
            raise ValueError('Invalid ADPCM frame header at %d' % at)
        f0, f1 = FILTERS[f]
        for byte in data[at + 2:at + 16]:
            for nib in (byte & 15, byte >> 4):
                r = (nib - 16 if nib & 8 else nib) << 12
                v = _clamp((r >> shift) + ((s1 * f0 + s2 * f1 + 32) >> 6))
                out.append(v)
                s2, s1 = s1, v
    return out


def _try(block, f, shift, s1, s2):
    f0, f1 = FILTERS[f]
    nibs = []
    err = 0
    scale = 1 << shift
    for x in block:
        p = (s1 * f0 + s2 * f1 + 32) >> 6
        q = ((x - p) * scale + 2048) >> 12
        q = -8 if q < -8 else 7 if q > 7 else q
        v = _clamp(((q << 12) >> shift) + p)
        err += (x - v) * (x - v)
        nibs.append(q & 15)
        s2, s1 = s1, v
    return err, nibs, s1, s2


def encode(samples, end_flags=FLAG_END, state=(0, 0)):
    """Encode int16 samples; the last frame gets `end_flags` (0 for streams)."""
    s1, s2 = state
    count = len(samples)
    if count == 0:
        samples = [0]
        count = 1
    frames = bytearray()
    total = (count + 27) // 28
    for n in range(total):
        block = list(samples[n * 28:n * 28 + 28])
        block += [0] * (28 - len(block))
        best = None
        for f in range(5):
            f0, f1 = FILTERS[f]
            # Peak residual with the ideal (unquantized) history picks the shift.
            a, b, peak = s1, s2, 0
            for x in block:
                r = x - ((a * f0 + b * f1 + 32) >> 6)
                peak = max(peak, r if r >= 0 else -r)
                b, a = a, x
            k = 0
            while k < 12 and peak > 7 << k:
                k += 1
            shift = 12 - k
            for sh in sorted({shift, max(0, shift - 1)}):
                got = _try(block, f, sh, s1, s2)
                if best is None or got[0] < best[0]:
                    best = (got[0], f, sh, got[1], got[2], got[3])
            if best[0] == 0:
                break
        _, f, sh, nibs, s1, s2 = best
        flags = end_flags if n == total - 1 else 0
        frames.append(f << 4 | sh)
        frames.append(flags)
        frames.extend(nibs[i] | nibs[i + 1] << 4 for i in range(0, 28, 2))
    return bytes(frames)


def disguise(data, delta, reverse=False):
    """EMPRESS frame disguise: byte0 ^ 0xff, byte2 - delta (reverse undoes it)."""
    if len(data) % 16:
        raise ValueError('ADPCM data is not whole frames')
    out = bytearray(data)
    out[0::16] = bytes(out[0::16]).translate(bytes(i ^ 255 for i in range(256)))
    step = delta if reverse else -delta
    out[2::16] = bytes(out[2::16]).translate(bytes((i + step) & 255 for i in range(256)))
    return bytes(out)


def plausibility(data, delta=None):
    """Fraction of frames with a legal header/flags; delta=None means raw frames."""
    if delta is not None:
        data = disguise(data[:len(data) - len(data) % 16], delta, reverse=True)
    frames = len(data) // 16
    if not frames:
        return 0.0, {}
    good = 0
    flags = {}
    for at in range(0, frames * 16, 16):
        head, flag = data[at], data[at + 1]
        if head >> 4 <= 4 and head & 15 <= 12 and flag <= 7:
            good += 1
        flags[flag] = flags.get(flag, 0) + 1
    return good / frames, flags


def stream(channels, rate, volume=0x7f, interleave=16):
    """EMPRESS backing stream: 0x800 header + disguised frames, stereo interleaved per frame.

    Header fields follow audio_transform.eighth-stream-v3 (the only native layout
    known from this repository); `volume` is the value stored at +0x28.
    """
    if len(channels) not in (1, 2) or interleave != 16:
        raise ValueError('Unsupported stream layout')
    length = max(len(c) for c in channels)
    coded = [encode(list(c) + [0] * (length - len(c)), end_flags=0) for c in channels]
    if len(coded) == 2:
        body = b''.join(coded[0][i:i + 16] + coded[1][i:i + 16] for i in range(0, len(coded[0]), 16))
    else:
        body = coded[0]
    head = bytearray(0x800)
    struct.pack_into('<12I', head, 0, STREAM_MAGIC, 0, 0x800, len(body), 0, 0, rate,
                     len(channels), 1, 16, volume, 1)
    return bytes(head) + disguise(body, STREAM_DELTA)


def resample(samples, src, dst):
    """Linear-interpolation resampler for keysounds that must fit the bank budget."""
    if src == dst or not samples:
        return list(samples)
    n = max(1, (len(samples) * dst + src - 1) // src)
    out = []
    last = len(samples) - 1
    for i in range(n):
        pos = i * src
        j, frac = divmod(pos, dst)
        if j >= last:
            out.append(samples[last])
        else:
            a, b = samples[j], samples[j + 1]
            out.append(a + (b - a) * frac // dst)
    return out
