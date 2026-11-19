"""Bounded RFC3284 default-table decoder for maintainer-generated xdelta3 patches.

No secondary compressors/custom tables. Encoder: xdelta3 -S none -A -e.
The xdelta3 per-window Adler32 extension is checked when present.
"""
import zlib

LIMIT = 64 * 1024 * 1024


class Reader:
    def __init__(self, data):
        self.data = memoryview(data)
        self.at = 0

    def take(self, n):
        if n < 0 or self.at + n > len(self.data):
            raise ValueError('Truncated VCDIFF section')
        value = self.data[self.at:self.at+n]
        self.at += n
        return value

    def byte(self):
        return self.take(1)[0]

    def integer(self):
        value = 0
        for _ in range(10):
            byte = self.byte()
            value = (value << 7) | (byte & 127)
            if not byte & 128:
                return value
        raise ValueError('Oversized VCDIFF integer')

    def done(self):
        return self.at == len(self.data)


def default_table():
    table = [(('RUN', 0, 0),)]
    table += [(('ADD', n, 0),) for n in range(18)]
    for mode in range(9):
        table += [(('COPY', n, mode),) for n in [0, *range(4, 19)]]
    for mode in range(6):
        for add in range(1, 5):
            for copy in range(4, 7):
                table.append((('ADD', add, 0), ('COPY', copy, mode)))
    for mode in range(6, 9):
        for add in range(1, 5):
            table.append((('ADD', add, 0), ('COPY', 4, mode)))
    for mode in range(9):
        table.append((('COPY', 4, mode), ('ADD', 1, 0)))
    assert len(table) == 256
    return table


TABLE = default_table()


def decode(patch, source, expected_size):
    """Yield complete target windows; source is a seekable *bounded* source view."""
    reader = Reader(patch)
    if bytes(reader.take(4)) != b'\xd6\xc3\xc4\0' or reader.byte() != 0:
        raise ValueError('Unsupported VCDIFF header')
    produced = 0
    while not reader.done():
        flags = reader.byte()
        # Our frozen profiles use original ISO extents, never earlier target windows.
        if flags & ~5:
            raise ValueError('Unsupported VCDIFF window flags')
        length = position = 0
        if flags & 1:
            length, position = reader.integer(), reader.integer()
        if length > LIMIT:
            raise ValueError('VCDIFF source window exceeds release bound')
        source.seek(position)
        dictionary = source.read(length)
        if len(dictionary) != length:
            raise ValueError('VCDIFF source window outside input extent')
        delta = Reader(reader.take(reader.integer()))
        size = delta.integer()
        if size > LIMIT or produced + size > expected_size or delta.byte() != 0:
            raise ValueError('Invalid VCDIFF target size/compression')
        lengths = [delta.integer() for _ in range(3)]
        checksum = int.from_bytes(delta.take(4), 'big') if flags & 4 else None
        data, inst, addr = [Reader(delta.take(n)) for n in lengths]
        if not delta.done():
            raise ValueError('Trailing VCDIFF window bytes')
        target = bytearray()
        near, same, next_near = [0]*4, [0]*768, 0
        while not inst.done():
            for kind, count, mode in TABLE[inst.byte()]:
                if count == 0:
                    count = inst.integer()
                if count == 0 or len(target) + count > size:
                    raise ValueError('VCDIFF instruction exceeds target')
                if kind == 'ADD':
                    target.extend(data.take(count))
                elif kind == 'RUN':
                    target.extend(bytes([data.byte()]) * count)
                else:
                    if mode == 0:
                        offset = addr.integer()
                    elif mode == 1:
                        offset = length + len(target) - addr.integer()
                    elif mode < 6:
                        offset = near[mode-2] + addr.integer()
                    else:
                        offset = same[(mode-6)*256 + addr.byte()]
                    if offset < 0 or offset >= length + len(target):
                        raise ValueError('Invalid VCDIFF COPY address')
                    near[next_near] = offset
                    next_near = (next_near + 1) % 4
                    same[offset % 768] = offset
                    # COPY may overlap its own new output. Copy the available span
                    # repeatedly, rather than one byte at a time for long RUN-like data.
                    while count:
                        if offset < length:
                            n = min(count, length-offset)
                            target.extend(dictionary[offset:offset+n])
                        else:
                            at = offset-length
                            n = min(count, len(target)-at)
                            if n <= 0:
                                raise ValueError('Invalid overlapping VCDIFF copy')
                            target.extend(target[at:at+n])
                        count -= n
                        offset += n
        if len(target) != size or not data.done() or not addr.done():
            raise ValueError('VCDIFF section consumption mismatch')
        if checksum is not None and zlib.adler32(target) & 0xffffffff != checksum:
            raise ValueError('VCDIFF window checksum mismatch')
        produced += size
        yield target
    if produced != expected_size:
        raise ValueError('VCDIFF output length mismatch')
