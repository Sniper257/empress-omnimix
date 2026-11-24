"""Read Konami IFS archives (arcade song/data containers). Stdlib only.

  header  u32 magic 0x6cad8f89, u16 version, u16 ~version, u32 time,
          u32 tree size, u32 data start, [16-byte digest if version > 1]
  manifest  kbinxml tree from the end of the header up to data start:
            directories are void nodes, files are '3u32' nodes
            [offset, size, time] with offset relative to data start
  names   '_' prefixes a name starting with a digit; '_E' stands for '.', '__' for '_'

kbinxml (binary XML) is decoded only as far as IFS manifests need:
node/attribute names, the numeric types, strings and binary blobs.
"""
import struct

MAGIC = 0x6cad8f89
SIXBIT = '0123456789:ABCDEFGHIJKLMNOPQRSTUVWXYZ_abcdefghijklmnopqrstuvwxyz'
# type id -> (struct code, count)
TYPES = {2: ('b', 1), 3: ('B', 1), 4: ('h', 1), 5: ('H', 1), 6: ('i', 1), 7: ('I', 1), 8: ('q', 1), 9: ('Q', 1),
         12: ('B', 4), 13: ('I', 1), 14: ('f', 1), 15: ('d', 1), 52: ('B', 1)}
for base, (code, size) in enumerate((('b', 1), ('B', 1), ('h', 2), ('H', 2), ('i', 4), ('I', 4), ('q', 8), ('Q', 8), ('f', 4), ('d', 8))):
    for n, start in ((2, 16), (3, 26), (4, 36)):
        TYPES[start + base] = (code, n)
TYPES.update({53: ('B', 2), 54: ('B', 3), 55: ('B', 4)})
NODE_START, ATTR, NODE_END, SECTION_END = 1, 46, 190, 191


class FormatError(ValueError):
    pass


class Node:
    def __init__(self, name, value=None):
        self.name, self.value, self.attrs, self.children = name, value, {}, []

    def walk(self, path=()):
        yield path + (self.name,), self
        for c in self.children:
            yield from c.walk(path + (self.name,))


class _Data:
    """kbinxml data buffer: 4-byte aligned values, with 1- and 2-byte values packed."""

    def __init__(self, buf):
        self.buf, self.pos, self.bpos, self.wpos = buf, 0, 0, 0

    def take(self, n):
        if n == 1:
            if self.bpos % 4 == 0:
                self.bpos, self.pos = self.pos, self.pos + 4
            at, self.bpos = self.bpos, self.bpos + 1
        elif n == 2:
            if self.wpos % 4 == 0:
                self.wpos, self.pos = self.pos, self.pos + 4
            at, self.wpos = self.wpos, self.wpos + 2
        else:
            at, self.pos = self.pos, self.pos + (n + 3) // 4 * 4
        if at + n > len(self.buf):
            raise FormatError('kbinxml data buffer overrun')
        return self.buf[at:at + n]

    def blob(self):
        n = struct.unpack('>I', self.take(4))[0]
        at, self.pos = self.pos, self.pos + (n + 3) // 4 * 4
        if at + n > len(self.buf):
            raise FormatError('kbinxml data buffer overrun')
        return self.buf[at:at + n]


def kbinxml(data):
    """Decode a kbinxml document to a Node tree."""
    if len(data) < 8 or data[0] != 0xa0 or data[1] not in (0x42, 0x45) or data[2] ^ data[3] != 0xff:
        raise FormatError('Not kbinxml')
    compressed = data[1] == 0x42
    encoding = {0x00: 'cp932', 0x20: 'ascii', 0x40: 'latin-1', 0x60: 'euc_jp', 0x80: 'cp932', 0xa0: 'utf-8'}.get(data[2] & 0xe0, 'cp932')
    nlen = struct.unpack_from('>I', data, 4)[0]
    nodes = data[8:8 + nlen]
    dstart = 8 + nlen
    dlen = struct.unpack_from('>I', data, dstart)[0]
    vals = _Data(data[dstart + 4:dstart + 4 + dlen])
    at = 0

    def name():
        nonlocal at
        if compressed:
            n = nodes[at]
            at += 1
            nbytes = (n * 6 + 7) // 8
            bits = int.from_bytes(nodes[at:at + nbytes], 'big')
            at += nbytes
            bits >>= nbytes * 8 - n * 6
            return ''.join(SIXBIT[(bits >> (6 * (n - 1 - i))) & 63] for i in range(n))
        n = (nodes[at] & 0xbf) + 1
        s = nodes[at + 1:at + 1 + n].decode(encoding, 'replace')
        at += 1 + n
        return s

    root = Node('')
    stack = [root]
    while at < len(nodes):
        kind = nodes[at]
        at += 1
        array, kind = bool(kind & 0x40) and kind not in (NODE_END, SECTION_END), kind & ~0x40 if kind not in (NODE_END, SECTION_END) else kind
        if kind == 0:
            continue
        if kind in (NODE_END, SECTION_END):
            if len(stack) > 1:
                stack.pop()
            if kind == SECTION_END:
                break
            continue
        nm = name()
        if kind == ATTR:
            stack[-1].attrs[nm] = vals.blob().rstrip(b'\0').decode(encoding, 'replace')
            continue
        node = Node(nm)
        stack[-1].children.append(node)
        stack.append(node)
        if kind == NODE_START:
            continue
        if kind == 10:
            node.value = vals.blob()
        elif kind == 11:
            node.value = vals.blob().rstrip(b'\0').decode(encoding, 'replace')
        elif kind in TYPES:
            code, count = TYPES[kind]
            size = struct.calcsize('>' + code) * count
            raw = vals.blob() if array else vals.take(size)
            items = struct.unpack('>%d%s' % (len(raw) // struct.calcsize('>' + code), code), raw)
            node.value = list(items) if array or count > 1 else items[0]
        else:
            raise FormatError('Unsupported kbinxml node type %d' % kind)
    return root.children[0] if root.children else root


def unescape(name):
    """IFS node names: a leading '_' before a digit is dropped, '_E' is '.', '__' is '_'."""
    if name.startswith('_') and len(name) > 1 and name[1].isdigit():
        name = name[1:]
    out, i = [], 0
    while i < len(name):
        pair = name[i:i + 2]
        if pair == '_E':
            out.append('.'); i += 2
        elif pair == '__':
            out.append('_'); i += 2
        else:
            out.append(name[i]); i += 1
    return ''.join(out)


def read(data):
    """{path: bytes} of every file in an IFS archive."""
    if len(data) < 20 or struct.unpack_from('>I', data, 0)[0] != MAGIC:
        raise FormatError('Not an IFS archive')
    version, inverse, _, _, header_end = struct.unpack_from('>HHIII', data, 4)
    start = 20 + (16 if version > 1 else 0)
    if version ^ inverse != 0xffff or not start < header_end <= len(data):
        raise FormatError('Invalid IFS header')
    manifest = kbinxml(data[start:header_end])
    files = {}
    for path, node in manifest.walk():
        if isinstance(node.value, list) and len(node.value) == 3 and not node.children:
            offset, size, _ = node.value
            if header_end + offset + size > len(data):
                raise FormatError('IFS entry exceeds the archive: ' + '/'.join(path))
            key = '/'.join(unescape(p) for p in path[1:])
            files[key] = data[header_end + offset:header_end + offset + size]
    return files
