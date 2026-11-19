"""Experimental ISO-filled launcher templates; Python standard library only.

Templates contain our compiled launcher with retail image ranges zeroed. These
helpers do not compile code, modify the PS2 boot path, or read any other disc.
The compatible unpacking stub is separately built from ps2-packer sources and
retains that project's GPL-2.0-or-later notice and corresponding source.
"""
import hashlib
import lzma
import struct

from native_sprite import sprite

MAX_ELF = 16 * 1024 * 1024
MAX_IMAGE = 2 * 1024 * 1024


def digest(data):
    return hashlib.sha256(data).hexdigest()


def load_segments(data):
    if not 52 <= len(data) <= MAX_ELF:
        raise ValueError('Invalid launcher ELF size')
    h = struct.unpack_from('<16sHHIIIIIHHHHHH', data)
    if h[0][:7] != b'\x7fELF\x01\x01\x01' or h[1:4] != (2, 8, 1):
        raise ValueError('Expected little-endian MIPS ELF32 executable')
    if h[8] != 52 or h[9] != 32 or not 1 <= h[10] <= 32 or h[5] < 52 or h[5] + h[10] * 32 > len(data):
        raise ValueError('Invalid ELF program table')
    loads = []
    for i in range(h[10]):
        ph = struct.unpack_from('<8I', data, h[5] + i * 32)
        if ph[0] != 1:
            continue
        if ph[4] > ph[5] or ph[1] + ph[4] > len(data) or ph[2] + ph[5] > 0x02000000:
            raise ValueError('Invalid ELF load segment')
        if ph[4]:
            loads.append(ph)
    if len(loads) != 1 or not loads[0][2] <= h[4] < loads[0][2] + loads[0][4]:
        raise ValueError('Expected one loaded launcher segment containing entry')
    return h, loads[0]


def fill(template, recipe, read_iso):
    if recipe.get('format') != 'megamix/iso-launcher-template-v1':
        raise ValueError('Unknown launcher template format')
    if digest(template) != recipe['template_sha256']:
        raise ValueError('Launcher template hash mismatch')
    _, load = load_segments(template)
    out = bytearray(template)
    previous = 0
    for span in recipe['spans']:
        at, size, offset = span['at'], span['bytes'], span['iso_offset']
        if type(at) is not int or type(size) is not int or type(offset) is not int or at < previous or not 0 < size <= MAX_IMAGE or not load[1] <= at < at + size <= load[1] + load[4] or offset < 0:
            raise ValueError('Invalid or overlapping launcher image range')
        if any(out[at:at+size]):
            raise ValueError('Launcher template image range is not empty')
        source_size = span['source_bytes']
        if type(source_size) is not int or not 0 < source_size <= MAX_IMAGE:
            raise ValueError('Invalid launcher resource source size')
        source = read_iso(offset, source_size)
        if len(source) != source_size or digest(source) != span['source_sha256']:
            raise ValueError('Original ISO launcher resource hash mismatch')
        if span['transform'] == 'copy':
            image = source
        elif span['transform'] == 'sprite-rgba32':
            w, h, pixels = sprite(source, span['sprite'])
            if (w, h) != (span['width'], span['height']):
                raise ValueError('Unexpected native launcher sprite geometry')
            image = struct.pack('<%dI' % len(pixels), *pixels)
        else:
            raise ValueError('Unknown launcher resource transform')
        if len(image) != size or digest(image) != span['sha256']:
            raise ValueError('Decoded launcher resource hash mismatch')
        out[at:at+size] = image
        previous = at + size
    out = bytes(out)
    if digest(out) != recipe['unpacked_sha256']:
        raise ValueError('Reconstructed launcher hash mismatch')
    return out


def edit_source(source, edits):
    """Apply small reviewed byte edits to the original executable, fail closed."""
    if not 0 < len(source) <= MAX_ELF:
        raise ValueError('Invalid native executable size')
    out = bytearray(source)
    previous = 0
    for row in edits:
        at, before, after = row['at'], bytes.fromhex(row['before']), bytes.fromhex(row['after'])
        if type(at) is not int or at < previous or not before or len(before) != len(after) or at + len(before) > len(out):
            raise ValueError('Invalid or overlapping executable edit')
        if out[at:at+len(before)] != before:
            raise ValueError('Original executable edit precondition mismatch')
        out[at:at+len(before)] = after
        previous = at + len(before)
    return bytes(out)


def pack(raw, stub, spec):
    """Use Python LZMA and the qualified stub, preserving loaded memory exactly.

    LZMA bitstreams need not match the SDK encoder. The decoder format, entry,
    loaded bytes, BSS, and stub virtual addresses do match the current build.
    """
    h, ph = load_segments(raw)
    if digest(stub) != spec['stub_sha256'] or len(stub) < 32 or stub[:4] != b'LZMA':
        raise ValueError('Wrong launcher unpacking stub')
    base, entry, zero = spec['stub_base'], spec['stub_entry'], spec['stub_zero']
    if base != 0x01d00000 or entry != base + 28 or not 0 <= zero <= MAX_ELF:
        raise ValueError('Unsupported unpacking stub layout')
    payload = raw[ph[1]:ph[1]+ph[4]].rstrip(b'\0')
    if not payload:
        raise ValueError('Empty launcher load image')
    filters = [dict(id=lzma.FILTER_LZMA1, preset=9, dict_size=1 << 21, lc=3, lp=0, pb=2)]
    stream = b'\x5d\x00\x00\x20\x00' + lzma.compress(payload, format=lzma.FORMAT_RAW, filters=filters)
    block = struct.pack('<2I4I', h[4], 1, len(payload), ph[5]-len(payload), ph[2], len(stream)) + stream
    block += bytes(-len(block) % 16)
    address = base - len(block)
    if address < ph[2] + ph[5] or base + len(stub) + zero > 0x02000000:
        raise ValueError('Packed launcher overlaps its destination or exceeds EE RAM')
    decoder = bytearray(stub)
    struct.pack_into('<I', decoder, 4, address)
    block += decoder
    header = struct.pack('<16sHHIIIIIHHHHHH', h[0], 2, 8, 1, entry, 52, 0, h[7], 52, 32, 1, 0, 0, 0)
    program = struct.pack('<8I', 1, 96, address, address, len(block), len(block)+zero, 7, 16)
    return header + program + bytes(96-len(header)-len(program)) + block


def verify_packed(data, memory, spec):
    """Verify EE memory and the source-built decoder, independent of encoding."""
    h, ph = load_segments(data)
    if h[4] != spec['stub_entry'] or ph[5]-ph[4] != spec['stub_zero']:
        raise ValueError('Packed launcher entry/BSS mismatch')
    if h[10]!=1 or ph[1]!=96 or ph[2]!=ph[3] or ph[6:]!=(7,16) or ph[2]<memory['base']+memory['bytes']:raise ValueError('Packed launcher load placement mismatch')
    if ph[4] < 56:raise ValueError('Short packed launcher')
    entry,count,n,zero,base,size = struct.unpack_from('<6I',data,ph[1])
    if count!=1 or base!=memory['base'] or entry!=memory['entry'] or n+zero!=memory['bytes'] or not 0<n<=MAX_ELF or not 0<=zero<=MAX_ELF-n or not 5<size<=MAX_ELF:
        raise ValueError('Invalid packed launcher memory layout')
    start=ph[1]+24;end=start+size;decoder_at=ph[1]+spec['stub_base']-ph[2]
    if end>decoder_at or decoder_at>=ph[1]+ph[4] or any(data[end:decoder_at]) or data[start:start+5]!=b'\x5d\x00\x00\x20\x00':raise ValueError('Invalid packed launcher stream')
    decoder=bytearray(data[decoder_at:ph[1]+ph[4]])
    if len(decoder)<32 or spec['stub_base']+len(decoder)+spec['stub_zero']>0x02000000 or struct.unpack_from('<I',decoder,4)[0]!=ph[2]:raise ValueError('Invalid unpacker input pointer')
    decoder[4:8]=bytes.fromhex(spec['stub_pointer_before'])
    if digest(decoder)!=spec['stub_sha256']:raise ValueError('Unpacker code mismatch')
    try:
        d=lzma.LZMADecompressor(format=lzma.FORMAT_RAW,filters=[dict(id=lzma.FILTER_LZMA1,dict_size=1<<21,lc=3,lp=0,pb=2)])
        raw=d.decompress(data[start+5:end],max_length=n+1)
    except lzma.LZMAError as e:raise ValueError('Invalid packed launcher compression')from e
    if len(raw)!=n or not d.eof or d.unused_data or digest(raw+bytes(zero))!=memory['sha256']:raise ValueError('Packed launcher memory hash mismatch')
    return True
