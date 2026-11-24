"""Konami texture archives (graphic .ifs): texturelist.xml + per-image avslz pixel data.

  python3 konami_tex.py notes_cn00.ifs OUT/      writes OUT/<image name>.png and OUT/images.json

Layout (Sparkle Shower notes_cn00.ifs / lnote00.ifs):
  tex/texturelist.xml   kbinxml: texture(format, size) > image(name) > uvrect, imgrect
  tex/<md5(image name)> u32 BE uncompressed size, u32 BE compressed size, avslz (LZ77) data
Rects are in half pixels: [x0, x1, y0, y1] / 2. imgrect is the stored image including its padding;
uvrect is the part that is drawn (imgrect inset by 1 px on each side).
"""
import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ifs  # noqa: E402


class FormatError(ValueError):
    pass


def lz77(data):
    """Konami avslz: flag byte (bit set = literal), else BE u16 = offset << 4 | (length - 3)."""
    out = bytearray()
    i = 0
    while i < len(data):
        flags = data[i]
        i += 1
        for bit in range(8):
            if flags >> bit & 1:
                if i >= len(data):
                    return bytes(out)
                out.append(data[i])
                i += 1
                continue
            if i + 1 >= len(data):
                return bytes(out)
            word = data[i] << 8 | data[i + 1]
            i += 2
            if word == 0:
                return bytes(out)
            back, length = word >> 4, (word & 15) + 3
            for _ in range(length):
                out.append(out[-back] if back <= len(out) else 0)
    return bytes(out)


def unpack(blob):
    if len(blob) < 8:
        raise FormatError('texture entry too short')
    size, packed = struct.unpack_from('>II', blob)
    if packed + 8 == len(blob) and packed != size:
        raw = lz77(blob[8:])
    else:
        raw = blob[8:]
    if len(raw) < size:
        raise FormatError('texture entry decompressed to %d of %d bytes' % (len(raw), size))
    return raw[:size]


def images(archive):
    """[{name, texture, format, imgrect, uvrect, size, data}] from a graphic .ifs (bytes)."""
    files = ifs.read(archive)
    if 'tex/texturelist.xml' not in files:
        raise FormatError('no tex/texturelist.xml in this archive')
    root = ifs.kbinxml(files['tex/texturelist.xml'])
    out = []
    for tex in root.children:
        if tex.name != 'texture':
            continue
        fmt = tex.attrs.get('format', '')
        for img in tex.children:
            if img.name != 'image':
                continue
            rect = {c.name: list(c.value) for c in img.children if c.name in ('uvrect', 'imgrect')}
            name = img.attrs['name']
            blob = files.get('tex/' + hashlib.md5(name.encode()).hexdigest())
            x0, x1, y0, y1 = rect['imgrect']
            w, h = (x1 - x0) // 2, (y1 - y0) // 2
            out.append({'name': name, 'texture': tex.attrs.get('name'), 'format': fmt,
                        'imgrect': rect['imgrect'], 'uvrect': rect.get('uvrect'), 'size': [w, h],
                        'data': unpack(blob) if blob else None})
    return out


def to_rgba(img):
    """RGBA bytes for argb8888rev (stored B, G, R, A)."""
    w, h = img['size']
    data = img['data']
    if img['format'] != 'argb8888rev':
        raise FormatError('%s: texture format %s is not handled yet' % (img['name'], img['format']))
    need = w * h * 4
    if data is None or len(data) < need:
        raise FormatError('%s: %s bytes of pixels for %dx%d' % (img['name'], None if data is None else len(data), w, h))
    px = bytearray(data[:need])
    px[0::4], px[2::4] = data[2:need:4], data[0:need:4]
    return bytes(px)


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('archive', type=Path)
    p.add_argument('output', type=Path)
    a = p.parse_args(argv)
    from PIL import Image
    a.output.mkdir(parents=True, exist_ok=True)
    report = []
    for img in images(a.archive.read_bytes()):
        row = {k: img[k] for k in ('name', 'texture', 'format', 'size', 'imgrect', 'uvrect')}
        try:
            w, h = img['size']
            Image.frombytes('RGBA', (w, h), to_rgba(img)).save(a.output / (img['name'] + '.png'))
            row['png'] = img['name'] + '.png'
        except FormatError as e:
            row['error'] = str(e)
            print(e, file=sys.stderr)
        report.append(row)
    (a.output / 'images.json').write_text(json.dumps(report, indent=1) + '\n')
    print('%s: %d images, %d written' % (a.archive.name, len(report), sum('png' in r for r in report)))


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError) as e:
        print('konami_tex: ' + str(e), file=sys.stderr)
        sys.exit(1)
