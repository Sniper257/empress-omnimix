"""Arcade charge-note textures (konami_tex.py PNGs) -> EMPRESS-size SPR1 sprites for the hold renderer.

  python3 tools/release/konami_tex.py notes_cn00.ifs GFX/full
  python3 tools/holds/hold_sprites.py GFX/full GFX/full OUT/
  (the first folder may be a note-height variant, e.g. notes_cn00_short.ifs; missing names come from the second)

Writes OUT/<name>.spr (SPR1, 8-bit, own palette), OUT/sprites.json (sizes, pivots) and OUT/preview.png.

Sizes, measured on an EMPRESS capture (PCSX2, 640x360 of the 640x448 frame): white-key lanes
18 px, blue-key lanes 14 px, turntable 30 px; notes 16 px wide, 6 frame rows tall (the game's note
sprites are 6 rows: launcher/main.c note_hook). Caps keep the arcade texture's aspect (a 54 x 26
cap is about 2x a normal note's height in the arcade, as on an arcade capture of a held HCN), so they
are 17 x 10, 13 x 9 and 30 x 10 frame rows (widths: the lanes' inside, see WIDTH); frame rows = capture px * 448 / 360. Bodies are one tile
of the arcade beam scaled the same way, drawn stretched (zoomY) between the caps.

All 36 sprites share one palette: cont_add (launcher/cont.c) rejects sprites added to a container
together unless their own palettes are byte-identical ("sprites use different palettes").

SPR1 (launcher/main.c spr_file / spr_tex): "SPR1", u16 w, u16 h, u16 sheet (0xffff = own palette),
u8 clear index, u8 0, w*h index bytes, then 256 RGBA palette entries in GS CSM1 order (entry
i ^ (((i >> 3) ^ (i >> 4)) & 1) * 0x18 holds colour i), alpha 0..0x80.
"""
import json
import struct
import sys
from pathlib import Path

# the lanes' inside widths, measured on a PCSX2 640x480 capture (white 120..136, blue 74..86, turntable
# 25..54): sprites drawn from their centre then fill the lane edge to edge (16 / 12 / 28 left a 1-2 px gap)
WIDTH = {'white': 17, 'black': 13, 'scr': 30}
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


def shared_palette(images):
    """One 256-entry RGBA palette (index 0 transparent) for all the sprites: the launcher's cont_add
    takes the sprites added to a container in one call only if their palettes are identical."""
    from PIL import Image
    import numpy as np
    px = np.concatenate([np.asarray(i.convert('RGBA')).reshape(-1, 4) for i in images])
    px = px[px[:, 3] >= 8]
    strip = Image.fromarray(px.reshape(1, -1, 4), 'RGBA')
    q = strip.quantize(colors=255, method=Image.Quantize.FASTOCTREE, dither=Image.Dither.NONE)
    pal = np.array(q.getpalette(rawmode='RGBA')[:255 * 4], dtype=np.uint8).reshape(-1, 4)
    colours = np.zeros((256, 4), np.uint8)
    colours[1:len(pal) + 1] = pal
    return colours


def spr1(img, colours=None):
    """RGBA PIL image -> SPR1 bytes with its own palette (index 0 = transparent). `colours`: a shared
    256 x RGBA palette (alpha 0..255) from shared_palette; by default one is made for this image."""
    import numpy as np
    a = np.asarray(img.convert('RGBA')).astype(np.int32)
    h, w = a.shape[:2]
    if colours is None:
        colours = shared_palette([img])
    c = colours.astype(np.int32)
    flat = a.reshape(-1, 4)
    opaque = flat[:, 3] >= 8
    idx = np.zeros(len(flat), np.int32)
    if opaque.any():
        d = ((flat[opaque][:, None, :] - c[None, 1:, :]) ** 2).sum(2)
        idx[opaque] = d.argmin(1) + 1
    table = np.zeros((256, 4), np.uint8)
    ps2 = colours.copy()
    ps2[:, 3] = np.clip(np.round(colours[:, 3].astype(np.int32) * 0x80 / 255), 0, 0x80).astype(np.uint8)
    ps2[0] = 0
    for i in range(256):
        table[swizzle(i)] = ps2[i]
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
    rows = []
    for name, arcade, kind, part in catalogue():
        src = short / (arcade + '.png')
        if not src.is_file():
            src = full / (arcade + '.png')
        rows.append((name, arcade, kind, part, scaled(Image.open(src).convert('RGBA'), kind, part)))
    colours = shared_palette([r[4] for r in rows])
    for name, arcade, kind, part, img in rows:
        data = spr1(img, colours)
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
