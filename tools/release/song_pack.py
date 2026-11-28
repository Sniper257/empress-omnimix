#!/usr/bin/env python3
"""Pack each song folder's charts and keysound banks into one file, pack.bin, for the launcher's extent table.

The launcher gives every file a song.txt `F` line names one disc extent (16 bytes) and one path in the reserved
block; with about 12 files a song, 2,000 songs fill its 16,384 extents ("too many extents"). A packed song has one
extent for all of them: its `F` lines name members, `F <field> <bytes> pack.bin#<sector>+<size>`, at 2,048-byte
aligned offsets, and the game reads each member as a file (launcher main.c add_song; vdisc reads inside an extent).

Packed: chart fields 0xc0..0xe7 and the bank of each keysound group (0xe8, 0xf0, 0xf8, 0x100). Not packed: the
group streams (+4: 0xec, 0xf4, 0xfc, 0x104), which the streamer reads past their end (the launcher puts zero
sectors after each file), paths starting with "/" (shared between songs) and every other line.

  python3 song_pack.py MEGAMIX            every songs/<style>/<folder>/song.txt
  python3 song_pack.py MEGAMIX/songs/x/y  one folder

Idempotent: a packed folder is left alone. Each folder is changed in one step (pack.bin.tmp and song.txt.tmp are
written, then renamed), and the packed files are removed afterwards.
"""
import argparse
import sys
from pathlib import Path

SECTOR = 2048
PACK = 'pack.bin'


def packable(field):
    return 0xc0 <= field < 0xe8 or field in (0xe8, 0xf0, 0xf8, 0x100)


def pack_folder(folder):
    """Pack one song folder. Returns the number of files packed (0: nothing to do)."""
    txt = folder / 'song.txt'
    lines = txt.read_text(encoding='utf-8').splitlines()
    members, order, out = {}, [], []
    for line in lines:
        parts = line.split(' ', 3)
        if len(parts) == 4 and parts[0] == 'F':
            try:
                field = int(parts[1], 16)
            except ValueError:
                field = -1
            name = parts[3].strip()
            if packable(field) and not name.startswith('/') and '#' not in name and (folder / name).is_file():
                if name not in members:
                    members[name] = None
                    order.append(name)
                out.append((parts[:3], name))
                continue
        out.append((None, line))
    if not order:
        return 0
    tmp = folder / (PACK + '.tmp')
    at = 0
    with open(tmp, 'wb') as f:
        for name in order:
            data = (folder / name).read_bytes()
            members[name] = (at // SECTOR, len(data))
            f.write(data)
            pad = -len(data) % SECTOR
            f.write(b'\0' * pad)
            at += len(data) + pad
    text = []
    for head, rest in out:
        if head is None:
            text.append(rest)
        else:
            sector, size = members[rest]
            text.append('%s %s %s %s#%d+%d' % (head[0], head[1], head[2], PACK, sector, size))
    ttmp = folder / 'song.txt.tmp'
    ttmp.write_text('\n'.join(text) + '\n', encoding='utf-8')
    tmp.replace(folder / PACK)
    ttmp.replace(txt)
    for name in order:
        (folder / name).unlink()
    return len(order)


def unpack_folder(folder):
    """Undo pack_folder: members back to their own files, F lines naming them. Returns files written."""
    txt = folder / 'song.txt'
    lines = txt.read_text(encoding='utf-8').splitlines()
    pack = folder / PACK
    if not pack.is_file() or not any('#' in l for l in lines if l.startswith('F ')):
        return 0
    data = pack.read_bytes()
    names, out = {}, []
    for line in lines:
        parts = line.split(' ', 3)
        if len(parts) == 4 and parts[0] == 'F' and parts[3].startswith(PACK + '#'):
            sector, size = map(int, parts[3].split('#', 1)[1].split('+'))
            key = (sector, size)
            if key not in names:
                names[key] = 'member_%d_%s.bin' % (len(names), parts[1])
                (folder / names[key]).write_bytes(data[sector * SECTOR:sector * SECTOR + size])
            out.append(' '.join(parts[:3] + [names[key]]))
        else:
            out.append(line)
    ttmp = folder / 'song.txt.tmp'
    ttmp.write_text('\n'.join(out) + '\n', encoding='utf-8')
    ttmp.replace(txt)
    pack.unlink()
    return len(names)


def song_folders(path):
    if (path / 'song.txt').is_file():
        return [path]
    return sorted(p.parent for p in (path / 'songs').glob('*/*/song.txt'))


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('path', type=Path, help='MEGAMIX folder, or one song folder')
    p.add_argument('--unpack', action='store_true', help='undo packing (members back to files)')
    a = p.parse_args(argv)
    folders = song_folders(a.path)
    if not folders:
        sys.exit('%s: no song folders (songs/<style>/<folder>/song.txt)' % a.path)
    packed = files = 0
    for n, folder in enumerate(folders, 1):
        try:
            k = unpack_folder(folder) if a.unpack else pack_folder(folder)
        except OSError as e:
            print('%s: %s' % (folder, e), file=sys.stderr)
            continue
        if k:
            packed += 1
            files += k
        if n % 100 == 0:
            print('%d/%d folders' % (n, len(folders)), flush=True)
    if a.unpack:
        print('unpacked %d song folders (%d files)' % (packed, files))
        return
    print('packed %d song folders (%d files into %d packs); %d were already packed or had nothing to pack'
          % (packed, files, packed, len(folders) - packed))


if __name__ == '__main__':
    main()
