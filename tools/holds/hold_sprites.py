"""Arcade charge-note textures (konami_tex.py PNGs) -> EMPRESS-size SPR1 sprites for the hold renderer.

  python3 tools/release/konami_tex.py notes_cn00.ifs GFX/full
  python3 tools/holds/hold_sprites.py GFX/full GFX/full OUT/
  (the first folder may be a note-height variant, e.g. notes_cn00_short.ifs; missing names come from the second)

Writes OUT/<name>.spr (SPR1, 8-bit, own palette), OUT/sprites.json (sizes, pivots) and OUT/preview.png.

Sizes, measured on an EMPRESS capture (PCSX2, 640x360 of the 640x448 frame): white-key lanes
18 px, blue-key lanes 14 px, turntable 30 px; notes 16 px wide, 6 frame rows tall (the game's note
sprites are 6 rows: launcher/main.c note_hook). Caps keep the arcade texture's aspect (a 54 x 26
cap is about 2x a normal note's height in the arcade, as on an arcade capture of a held HCN), so they
are 16 x 10, 12 x 9 and 28 x 10 frame rows; frame rows = capture px * 448 / 360. Bodies are one tile
of the arcade beam scaled the same way, drawn stretched (zoomY) between the caps.

SPR1 (launcher/main.c spr_file / spr_tex): "SPR1", u16 w, u16 h, u16 sheet (0xffff = own palette),
u8 clear index, u8 0, w*h index bytes, then 256 RGBA palette entries in GS CSM1 order (entry
i ^ (((i >> 3) ^ (i >> 4)) & 1) * 0x18 holds colour i), alpha 0..0x80.
"""
import json
import struct
import sys
from pathlib import Path

WIDTH = {'white': 16, 'black': 12, 'scr': 28}
ROWS_PER_PX = 448 / 360            # EMPRESS frame rows per capture pixel (640 x 448 shown at 640 x 360)

# (output name, arcade name, lane kind, part)
def catalogue():
    rows = []
    for kind, pre in (('cn', 'notes_cn'), ('hcn', 'notes_cn2')):
        for col in ('white', 'black'):
            for part in ('start', 'end'):
                rows.append(('%s_%s_%s' % (kind, col, part), '%s_%s_%s' % (pre, col, part), col, 'cap'))
            for b in range(5 if kind == 'hcn' else 3):
                rows.append(('%s_%s_beam%d' % (kind, col, b), '%s_%s_beam%d' % (pre, col, b), col, 'body'))
    for kind, pre in (('bss', 'notes_bss'), ('hbss', 'notes_bss2')):
        for part in ('start', 'end'):
            rows.append(('%s_%s' % (kind, part), '%s_%s' % (pre, part), 'scr', 'cap'))
        for b in range(5 if kind == 'hbss' else 3):
            rows.append(('%s_beam%d' % (kind, b), '%s_beam%d' % (pre, b), 'scr', 'body'))
    return rows


def swizzle(i):
    return i ^ ((((i >> 3) ^ (i >> 4)) & 1) * 0x18)


def spr1(img):
    """RGBA PIL image -> SPR1 bytes with its own 256-colour palette (index 0 = transparent)."""
    from PIL import Image
    import numpy as np
    a = np.asarray(img.convert('RGBA'))
    h, w = a.shape[:2]
    opaque = a[:, :, 3] >= 8
    rgb = Image.fromarray(a[:, :, :3])
    q = rgb.quantize(colors=255, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    idx = np.asarray(q).astype(np.int32) + 1
    pal = np.array(q.getpalette()[:255 * 3], dtype=np.int32).reshape(-1, 3)
    alpha = np.zeros(256, np.int64)
    count = np.zeros(256, np.int64)
    np.add.at(alpha, idx[opaque], a[:, :, 3][opaque])
    np.add.at(count, idx[opaque], 1)
    idx[~opaque] = 0
    colours = np.zeros((256, 4), np.uint8)
    colours[1:len(pal) + 1, :3] = pal
    with np.errstate(invalid='ignore', divide='ignore'):
        mean = np.where(count > 0, alpha / np.maximum(count, 1), 0)
    colours[:, 3] = np.clip(np.round(mean * 0x80 / 255), 0, 0x80).astype(np.uint8)
    colours[0] = 0
    table = np.zeros((256, 4), np.uint8)
    for i in range(256):
        table[swizzle(i)] = colours[i]
    return b'SPR1' + struct.pack('<HHHBB', w, h, 0xffff, 0, 0) + idx.astype(np.uint8).tobytes() + table.tobytes()


def decode(spr):
    """SPR1 with own palette -> RGBA (alpha 0..255) PIL image, as the launcher reads it."""
    from PIL import Image
    import numpy as np
    w, h, sheet, clear, _ = struct.unpack_from('<HHHBB', spr, 4)
    if spr[:4] != b'SPR1' or sheet != 0xffff:
        raise ValueError('not an own-palette SPR1')
    px = np.frombuffer(spr, np.uint8, w * h, 12).reshape(h, w)
    table = np.frombuffer(spr, np.uint8, 0x400, 12 + w * h).reshape(256, 4)
    colours = table[[swizzle(i) for i in range(256)]].astype(np.int32)
    colours[:, 3] = np.minimum(255, colours[:, 3] * 255 // 0x80)
    return Image.fromarray(colours[px].astype(np.uint8), 'RGBA')


def scaled(src, kind, part):
    from PIL import Image
    w = WIDTH[kind]
    return src.resize((w, max(2, round(src.height * w / src.width * ROWS_PER_PX))), Image.LANCZOS)


def main(argv=None):
    from PIL import Image
    argv = sys.argv[1:] if argv is None else argv
    if len(argv) != 3:
        sys.exit(__doc__)
    short, full, out = (Path(p) for p in argv)
    out.mkdir(parents=True, exist_ok=True)
    manifest = {}
    previews = []
    for name, arcade, kind, part in catalogue():
        src = short / (arcade + '.png')
        if not src.is_file():
            src = full / (arcade + '.png')
        img = scaled(Image.open(src).convert('RGBA'), kind, part)
        data = spr1(img)
        (out / (name + '.spr')).write_bytes(data)
        manifest[name] = {'from': arcade, 'lane': kind, 'part': part, 'w': img.width, 'h': img.height,
                          'pivot': [img.width // 2, img.height if part == 'cap' else 0]}
        previews.append((name, decode(data)))
    (out / 'sprites.json').write_text(json.dumps(manifest, indent=1) + '\n')
    cell = 40
    sheet = Image.new('RGBA', (cell * 8, cell * ((len(previews) + 7) // 8)), (24, 24, 28, 255))
    for i, (name, img) in enumerate(previews):
        big = img.resize((img.width * 2, img.height * 2), Image.NEAREST)
        sheet.alpha_composite(big, ((i % 8) * cell + (cell - big.width) // 2, (i // 8) * cell + 4))
    sheet.save(out / 'preview.png')
    print('%d sprites -> %s' % (len(previews), out))


if __name__ == '__main__':
    main()
