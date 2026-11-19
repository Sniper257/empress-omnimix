"""EMPRESS animation metadata writer, from tools/animwrite.py."""
import struct
RECT_OFF=0xd0

def keys(points):
    return b''.join(struct.pack('<hhhh', f, a, b, 0) for f, a, b in points) + struct.pack('<hhhh', -1, 0, 0, 0)


def metadata(sheet_sizes, rects, anims):
    """anims: list of (length, canvas_half (x, y), elements); element dict: sprite, start, end, pivot (x, y),
    pos [(frame, x, y)], zoom [(frame, px, py)] or None."""
    assert len(sheet_sizes) <= 48
    out = bytearray(RECT_OFF)
    for i, (w, h) in enumerate(sheet_sizes):
        struct.pack_into('<HH', out, 0x10 + 4 * i, w, h)
    for r in rects:
        out += struct.pack('<HHHH', *r)
    while len(out) % 16:
        out.append(0)
    lists = []
    for length, half, els in anims:
        recs = []
        for e in els:
            po = len(out); out += keys(e['pos'])
            zo = 0
            if e.get('zoom'):
                zo = len(out); out += keys(e['zoom'])
            recs.append(struct.pack('<HHHHHHIhhIIII', 0, e['sprite'], 0, 100, e['start'], e['end'], 0,
                                    e['pivot'][0], e['pivot'][1], po, zo, 0, 0))
        recs.append(struct.pack('<HHHHHHIhhIIII', 0xffff, 0xffff, 0, 0xffff, 0, length, 0, half[0], half[1], 0, 0, 0, 0))
        lists.append(recs)
    offs = []
    for recs in lists:
        offs.append(len(out)); out += b''.join(recs)
    ano = len(out)
    for o in offs:
        out += struct.pack('<I', o)
    struct.pack_into('<HHHHII', out, 0, 0, len(sheet_sizes), len(anims), len(rects), RECT_OFF, ano)
    # the loader unpacks all chunks back to back and DMAs each sheet from there: keep sheets 16-byte aligned
    # (a 257500-byte metadata chunk shifted every palette/texture by 12 bytes -> garbage colours in game)
    out += bytes(-len(out) % 16)
    return bytes(out)


