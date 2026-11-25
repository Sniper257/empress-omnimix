"""Readers for beatmania IIDX arcade song data (IIDX 17 SIRIUS era). Stdlib only.

Formats read here (all little-endian):

  data/sound/<id>/<id>.1     chart container: a directory of 12 (offset, size)
                             u32 pairs, then 8-byte events per chart
                             (s32 time, u8 type, u8 param, s16 value),
                             ended by time 0x7fffffff.
  data/sound/<id>/<id>.2dx   keysound archive: 16-byte name, u32 header size,
                             u32 count, 48 reserved bytes, u32 offsets[count];
                             each entry is a 24-byte "2DX9" header + RIFF WAVE.
  data/info/music_data.bin   song list: "IIDX" magic, u32 version, u16 entry
                             count, u16 index count, u32 reserved, s16 index
                             table, fixed-size entries.

Nothing here assumes the chart time unit, the chart slot order or the
music_data entry layout of a particular dump: those are reported by
sirius_probe.py and pinned in sirius_layout.json once confirmed on real data.
"""
import struct

MAX_CHART = 16 * 1024 * 1024
MAX_ARCHIVE = 512 * 1024 * 1024
CHART_SLOTS = 12
END_TIME = 0x7fffffff

# Event type names as used by the arcade .1 format (low byte of the type).
EVENT_NAMES = {
    0: 'p1_note', 1: 'p2_note', 2: 'p1_sample', 3: 'p2_sample', 4: 'tempo',
    5: 'meter', 6: 'end', 7: 'bgm_sample', 8: 'timing_window', 12: 'measure',
    16: 'note_count',
}


class FormatError(ValueError):
    pass


# --------------------------------------------------------------------------- .1

def chart_directory(data):
    """Return [(slot, offset, size)] for the non-empty charts of a .1 file."""
    if len(data) < CHART_SLOTS * 8 or len(data) > MAX_CHART:
        raise FormatError('Not an arcade .1 chart container (size)')
    out = []
    for slot in range(CHART_SLOTS):
        offset, size = struct.unpack_from('<II', data, slot * 8)
        if size == 0:
            continue
        if size % 8 or offset < CHART_SLOTS * 8 or offset + size > len(data):
            raise FormatError('Invalid .1 chart directory entry %d' % slot)
        out.append((slot, offset, size))
    if not out:
        raise FormatError('Arcade .1 container has no charts')
    return out


def chart_events(data, offset, size):
    """Events of one chart up to (not including) the terminator."""
    events = []
    for at in range(offset, offset + size - 7, 8):
        time, kind, param, value = struct.unpack_from('<iBBh', data, at)
        if time == END_TIME:
            return events
        if time < 0:
            raise FormatError('Negative chart event time')
        events.append((time, kind, param, value))
    raise FormatError('Chart has no terminator')


MSS_LANE = 107   # note column of a multi-spin scratch section (29 CastHour on): the turntable, 7 + 100


def read_charts(data):
    """{slot: [events]} for every chart present in a .1 file.

    Multi-spin scratch (MSS) sections are notes in column 107 with their length as value, one per
    section, the sections of one MSS end to end (Purple Perplex SPH: 36800+800, 37600+800, 38400+1200).
    They come back as turntable notes (column 7): each section is a backspin scratch, and a section
    starting where another ends on the same turntable is a middle note."""
    def mss(e):
        t, kind, param, value = e
        return (t, kind, 7, value) if kind in (0, 1) and param == MSS_LANE else e
    return {slot: [mss(e) for e in chart_events(data, off, size)] for slot, off, size in chart_directory(data)}


def mss_joints(events):
    """Middle notes of multi-spin scratches: (time, kind) where a turntable section starts as another ends."""
    ends = {(t + v, k) for t, k, p, v in events if k in (0, 1) and p == 7 and v > 0}
    return sorted((t, k) for t, k, p, v in events if k in (0, 1) and p == 7 and v > 0 and (t, k) in ends)


def note_count(events):
    """(p1, p2) visible notes, charge-note heads counted once."""
    p1 = sum(1 for e in events if e[1] == 0)
    p2 = sum(1 for e in events if e[1] == 1)
    return p1, p2


# -------------------------------------------------------------------------- .2dx

def read_2dx(data):
    """Return (name, [entry]) where entry is a dict with the 2DX9 header and WAV bytes."""
    if len(data) < 0x48 or len(data) > MAX_ARCHIVE:
        raise FormatError('Not a .2dx archive (size)')
    name = data[:16].split(b'\0', 1)[0].decode('ascii', 'replace')
    header, count = struct.unpack_from('<II', data, 16)
    if count > 8192 or header < 0x48 or header > len(data) or 0x48 + 4 * count > len(data):
        raise FormatError('Invalid .2dx header')
    offsets = struct.unpack_from('<%dI' % count, data, 0x48)
    entries = []
    for index, at in enumerate(offsets):
        if at + 24 > len(data) or data[at:at + 4] != b'2DX9':
            raise FormatError('.2dx entry %d is not 2DX9 (encrypted or different format?)' % index)
        hsize, wsize, unk1, track, unk2, attenuation, loop = struct.unpack_from('<IIHhhhi', data, at + 4)
        start = at + hsize
        if hsize < 24 or start + wsize > len(data):
            raise FormatError('.2dx entry %d exceeds the archive' % index)
        entries.append({'index': index, 'track': track, 'attenuation': attenuation,
                        'loop': loop, 'unk': (unk1, unk2), 'wav': data[start:start + wsize]})
    return name, entries


# -------------------------------------------------------------------------- .s3p

ASF_GUID = bytes.fromhex('3026b2758e66cf11a6d900aa0062ce6c')   # ASF header object (WMA)


def read_s3p(data):
    """Keysound archive of newer arcade games: "S3P0", u32 count, count x (u32 offset, u32 size);
    each entry is an "S3V0" header followed by an ASF/WMA file. Returns [entry dict]."""
    if len(data) < 8 or data[:4] != b'S3P0' or len(data) > MAX_ARCHIVE:
        raise FormatError('Not an .s3p archive')
    count = struct.unpack_from('<I', data, 4)[0]
    if count > 8192 or 8 + 8 * count > len(data):
        raise FormatError('Invalid .s3p header')
    entries = []
    for index in range(count):
        at, size = struct.unpack_from('<II', data, 8 + 8 * index)
        if at + size > len(data) or size < 8:
            raise FormatError('.s3p entry %d exceeds the archive' % index)
        blob = data[at:at + size]
        if blob[:4] != b'S3V0':
            raise FormatError('.s3p entry %d is not S3V0' % index)
        start = blob.find(ASF_GUID)
        if start < 0:
            raise FormatError('.s3p entry %d has no WMA/ASF stream' % index)
        head = blob[:start]
        entries.append({'index': index, 'header': head, 'audio': blob[start:],
                        'header_size': struct.unpack_from('<I', blob, 4)[0] if len(blob) >= 8 else 0})
    return entries


# ------------------------------------------------------------------------- WAVE

ADAPT = (230, 230, 230, 230, 307, 409, 512, 614, 768, 614, 512, 409, 307, 230, 230, 230)


def wav_info(wav):
    """Parse RIFF chunks: returns dict(format, channels, rate, block, bits, extra, data)."""
    if len(wav) < 12 or wav[:4] != b'RIFF' or wav[8:12] != b'WAVE':
        raise FormatError('Not a RIFF WAVE')
    at = 12
    fmt = data = None
    while at + 8 <= len(wav):
        cid, size = wav[at:at + 4], struct.unpack_from('<I', wav, at + 4)[0]
        body = wav[at + 8:at + 8 + size]
        if cid == b'fmt ':
            fmt = body
        elif cid == b'data':
            data = body
        at += 8 + size + (size & 1)
    if fmt is None or data is None or len(fmt) < 16:
        raise FormatError('WAVE without fmt/data')
    tag, ch, rate, _, block, bits = struct.unpack_from('<HHIIHH', fmt)
    if ch not in (1, 2) or not 1000 <= rate <= 192000:
        raise FormatError('Unsupported WAVE channels/rate')
    return {'format': tag, 'channels': ch, 'rate': rate, 'block': block, 'bits': bits,
            'extra': fmt[18:] if len(fmt) > 18 else b'', 'data': data}


def _msadpcm(info):
    ch, block, extra, data = info['channels'], info['block'], info['extra'], info['data']
    if len(extra) < 4:
        raise FormatError('MS-ADPCM fmt chunk is too short')
    per_block, ncoef = struct.unpack_from('<HH', extra)
    if ncoef < 7 or len(extra) < 4 + 4 * ncoef:
        raise FormatError('MS-ADPCM coefficient table is missing')
    coefs = [struct.unpack_from('<hh', extra, 4 + 4 * i) for i in range(ncoef)]
    head = 7 * ch
    if block <= head or per_block < 2:
        raise FormatError('Invalid MS-ADPCM block layout')
    out = [[] for _ in range(ch)]
    for start in range(0, len(data), block):
        blk = data[start:start + block]
        if len(blk) < head:
            break
        pred = blk[0:ch]
        delta = list(struct.unpack_from('<%dh' % ch, blk, ch))
        s1 = list(struct.unpack_from('<%dh' % ch, blk, 3 * ch))
        s2 = list(struct.unpack_from('<%dh' % ch, blk, 5 * ch))
        if any(p >= ncoef for p in pred):
            raise FormatError('MS-ADPCM predictor out of range')
        c = [coefs[p] for p in pred]
        for k in range(ch):
            out[k].append(s2[k])
            out[k].append(s1[k])
        left = per_block - 2
        k = 0
        for byte in blk[head:]:
            for nib in (byte >> 4, byte & 15):
                if left <= 0:
                    break
                signed = nib - 16 if nib & 8 else nib
                p = (s1[k] * c[k][0] + s2[k] * c[k][1]) >> 8
                v = p + signed * delta[k]
                v = -32768 if v < -32768 else 32767 if v > 32767 else v
                s2[k], s1[k] = s1[k], v
                out[k].append(v)
                d = (ADAPT[nib] * delta[k]) >> 8
                delta[k] = d if d >= 16 else 16
                k += 1
                if k == ch:
                    k = 0
                    left -= 1
    return out


def decode_wav(wav):
    """Return (rate, [channel samples as int16-range lists]) for PCM (8/16/24/32-bit,
    float32, WAVE_FORMAT_EXTENSIBLE) or MS-ADPCM WAVs."""
    info = wav_info(wav)
    tag, bits, ch, data = info['format'], info['bits'], info['channels'], info['data']
    if tag == 0xfffe and len(info['extra']) >= 24:
        tag = struct.unpack_from('<H', info['extra'], 8)[0]   # SubFormat GUID starts with the format tag
    if tag == 2:
        return info['rate'], _msadpcm(info)
    if tag == 1 and bits == 16:
        n = len(data) // (2 * ch)
        flat = struct.unpack_from('<%dh' % (n * ch), data)
    elif tag == 1 and bits == 8:
        flat = [(b - 128) << 8 for b in data[:len(data) // ch * ch]]
    elif tag == 1 and bits == 24:
        n = len(data) // (3 * ch) * ch
        flat = [int.from_bytes(data[i * 3:i * 3 + 3], 'little', signed=True) >> 8 for i in range(n)]
    elif tag == 1 and bits == 32:
        n = len(data) // (4 * ch)
        flat = [v >> 16 for v in struct.unpack_from('<%di' % (n * ch), data)]
    elif tag == 3 and bits == 32:
        n = len(data) // (4 * ch)
        flat = [max(-32768, min(32767, int(round(v * 32767)))) for v in struct.unpack_from('<%df' % (n * ch), data)]
    else:
        raise FormatError('Unsupported WAVE format tag %d / %d bits' % (tag, bits))
    return info['rate'], [list(flat[k::ch]) for k in range(ch)]


# -------------------------------------------------------------- music_data.bin

def _text(raw):
    raw = raw.split(b'\0', 1)[0]
    for codec in ('cp932', 'shift_jis'):
        try:
            return raw.decode(codec)
        except UnicodeDecodeError:
            pass
    return raw.decode('latin-1')


def read_music_data(data):
    """Header, id index and raw fixed-size entries. Field layout is left to the probe."""
    if len(data) < 16 or data[:4] != b'IIDX':
        raise FormatError('Not an IIDX music_data.bin')
    version = struct.unpack_from('<I', data, 4)[0]
    # older: u16 count, u16 index count, u32; newer (32): u32 count, u32 index count.
    # The id -> entry table is s16 in older versions and s32 in newer ones.
    layouts = [(struct.unpack_from('<HH', data, 8), 2, 'h'), (struct.unpack_from('<II', data, 8), 4, 'i'),
               (struct.unpack_from('<HH', data, 8), 4, 'i'), (struct.unpack_from('<II', data, 8), 2, 'h')]
    for (count, index_count), width, code in layouts:
        table = 16 + width * index_count
        if count and index_count and table < len(data) and (len(data) - table) % count == 0:
            break
    else:
        raise FormatError('music_data.bin entries are not a whole number of records')
    reserved = struct.unpack_from('<I', data, 12)[0]
    size = (len(data) - table) // count
    index = struct.unpack_from('<%d%s' % (index_count, code), data, 16)
    entries = [data[table + i * size:table + (i + 1) * size] for i in range(count)]
    return {'version': version, 'count': count, 'index_count': index_count, 'index_width': width,
            'reserved': reserved, 'index': index, 'entry_size': size, 'entries': entries}


def entry_strings(entry, width=64, limit=8):
    """Leading fixed-width text fields of a music_data entry (title, ascii title, genre, artist...)."""
    out = []
    for at in range(0, min(len(entry), width * limit), width):
        field = entry[at:at + width]
        if not field.strip(b'\0'):
            out.append('')
            continue
        if any(b < 0x20 and b != 0 for b in field.split(b'\0', 1)[0]):
            break
        out.append(_text(field))
    return out


def _plausible_text(s):
    """Every character from blocks song titles use: a Shift-JIS field read as UTF-16 gives code
    units scattered over unrelated blocks (and usually control characters or surrogates)."""
    ok = ((0x20, 0x7e), (0xa0, 0x24f), (0x370, 0x4ff), (0x2000, 0x2bff), (0x3000, 0x30ff), (0x3200, 0x33ff),
          (0x4e00, 0x9fff), (0xf900, 0xfaff), (0xff00, 0xffef))
    return bool(s) and all(any(a <= ord(c) <= b for a, b in ok) for c in s)


def entry_title(entry):
    """(title, ascii title): UTF-16 title + ASCII at 0x100 in newer lists (32), SJIS fields in older ones.
    A Japanese title starts with a non-ASCII code unit (メズマライザー: e1 30), so the UTF-16 layout is
    told by the whole title decoding to plausible text, not by an ASCII first byte."""
    if len(entry) >= 0x140:
        raw = entry[:0x100]
        end = next((i for i in range(0, len(raw) - 1, 2) if raw[i] == 0 and raw[i + 1] == 0), len(raw))
        try:
            title = raw[:end].decode('utf-16-le')
        except UnicodeDecodeError:
            title = None
        if title is not None and _plausible_text(title):
            return title, entry[0x100:0x140].split(b'\0')[0].decode('latin-1')
    strings = entry_strings(entry)
    return (strings[0] if strings else ''), (strings[1] if len(strings) > 1 else '')


def find_id_fields(entries, song_ids):
    """Offsets where each entry stores its own song id (u32 or u16), for layout discovery."""
    hits = {}
    for entry, sid in zip(entries, song_ids):
        for width, fmt in ((4, '<I'), (2, '<H')):
            for at in range(0, len(entry) - width + 1, 2):
                if struct.unpack_from(fmt, entry, at)[0] == sid:
                    hits[(at, width)] = hits.get((at, width), 0) + 1
    return sorted(hits.items(), key=lambda kv: -kv[1])
