"""Native PS2 sprite extraction, reused from base_assets.py; no fonts or Pillow."""
import struct

def lzss(data, size):
    ring = bytearray(4096)
    out = bytearray()
    at, pos = 0, 0xfee
    while len(out) < size:
        if at >= len(data):
            raise ValueError('Truncated sprite compression')
        flags = data[at]; at += 1
        for bit in range(8):
            if len(out) == size:
                break
            if flags >> bit & 1:
                if at >= len(data):
                    raise ValueError('Truncated literal')
                values = [data[at]]; at += 1
                for value in values:
                    out.append(value); ring[pos] = value; pos = (pos + 1) & 4095
            else:
                if at + 2 > len(data):
                    raise ValueError('Truncated match')
                lo, hi = data[at:at+2]; at += 2
                start = lo | (hi & 0xf0) << 4
                count = (hi & 15) + 3
                if len(out) + count > size:
                    raise ValueError('Sprite match exceeds declared size')
                for offset in range(count):
                    value = ring[(start + offset) & 4095]
                    out.append(value); ring[pos] = value; pos = (pos + 1) & 4095
    return bytes(out)


def sprite(container, number):
    off, count = struct.unpack_from('<HH', container)
    table = off - (count + 1) * 8
    if table < 4 or off > len(container) or count > 32:
        raise ValueError('Invalid native sprite container')
    chunks = []
    for i in range(count + 1):
        raw, packed = struct.unpack_from('<II', container, table + i * 8)
        if raw > 16 * 1024 * 1024 or off + packed > len(container):
            raise ValueError('Invalid native sprite chunk')
        chunks.append((raw, off, packed)); off += packed
    raw, off, packed = chunks[0]
    meta = lzss(container[off:off+packed], raw)
    if number >= struct.unpack_from('<H', meta, 6)[0]:
        raise ValueError('Native logo sprite absent')
    at = struct.unpack_from('<I', meta, 8)[0] + number * 8
    rx, sy, w, h = struct.unpack_from('<4H', meta, at)
    sheet, ry = divmod(sy, 1024)
    sw, sh = struct.unpack_from('<HH', meta, 16 + sheet * 4)
    if rx + w > sw or ry + h > sh:
        raise ValueError('Native sprite exceeds sheet')
    raw, off, packed = chunks[sheet + 1]
    pixels = lzss(container[off:off+packed], raw)
    if len(pixels) != 1024 + sw * sh:
        raise ValueError('Unexpected native texture format')
    palette = [struct.unpack_from('<I', pixels, (i ^ ((((i >> 3) ^ (i >> 4)) & 1) * 24)) * 4)[0]
               for i in range(256)]
    out = []
    for y in range(ry, ry+h):
        for x in range(rx, rx+w):
            xr = x ^ (4 * (((y >> 1) ^ (y >> 2)) & 1))
            offset = (y >> 2)*4*sw + (y & 1)*2*sw + (x >> 4)*32 + 4*(xr & 7) + 2*((xr >> 3) & 1) + ((y & 3) >> 1)
            out.append(palette[pixels[1024 + offset]])
    return w, h, out


