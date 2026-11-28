#!/usr/bin/env python3
"""Arcade note skins (data/graphic notesNN.ifs) -> launcher note skins (MEGAMIX/skins/notes/<dir>/).

  python3 tools/holds/note_skins.py OUT/ notes06.ifs [notes00.ifs ...]
  then copy OUT/* into MEGAMIX/skins/notes/ (list.txt is merged: existing lines are kept)

Each skin is the four sprites the arcade draws, notes_white / notes_black / notes_turn / notes_auto_turn, as the
launcher's note skin takes them (launcher/main.c note_skin_build): <dir>/sp_<kind>.spr and wd_<kind>.spr for the
SP and wide note containers (#109 / #146), kinds white, black, turn, auto, plus skin.txt (name, rows). The
launcher adds them to the containers and points the game's note tables at them; the runtime draws them at the
Skin page's note height (note_zoom_skin = height * 60 / rows), so rows only sets the detail kept.

Widths are the charge-note sprites' (hold_sprites.WIDTH: 17 / 13 / 29 frame px, which fill EMPRESS's lanes);
height ROWS (12, twice the game's 6-row notes, so the arcade's edge highlight survives). The four sprites share
one palette: cont_add takes a call's sprites only with byte-identical palettes.

Skin names: the arcade's customize list order is not in the DLL; NAMES has the ones known by their look.
"""
import argparse
import re
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / 'release'))

import hold_sprites as H  # noqa: E402

KINDS = (('white', 'notes_white', 'white'), ('black', 'notes_black', 'black'),
         ('turn', 'notes_turn', 'scr'), ('auto', 'notes_auto_turn', 'scr'))
ROWS = 12
NAMES = {6: 'Simple Color'}


def skin_images(pngs):
    from PIL import Image
    out = {}
    for kind, arcade, lane in KINDS:
        src = Image.open(pngs / (arcade + '.png')).convert('RGBA')
        # the textures carry transparent padding rows (notes06: 4 on top): scaled with the note they blend
        # into a half-transparent dark row, so the note is cut to its opaque part first and stays opaque
        box = src.getchannel('A').point(lambda a: 255 if a >= 128 else 0).getbbox()
        if box:
            src = src.crop(box)
        img = src.resize((H.WIDTH[lane], ROWS), Image.LANCZOS)
        img.putalpha(img.getchannel('A').point(lambda a: 255 if a >= 128 else 0))
        out[kind] = img
    return out


def write_skin(pngs, dest, name):
    imgs = skin_images(pngs)
    pal = H.shared_palette(list(imgs.values()))
    dest.mkdir(parents=True, exist_ok=True)
    for kind, img in imgs.items():
        data = H.spr1(img, pal)
        for side in ('sp', 'wd'):
            (dest / ('%s_%s.spr' % (side, kind))).write_bytes(data)
    (dest / 'skin.txt').write_text('name %s\nrows %d\n' % (name, ROWS))
    return imgs


def main(argv=None):
    import konami_tex
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('out', type=Path, help='folder that becomes MEGAMIX/skins/notes')
    p.add_argument('ifs', type=Path, nargs='+', help='notesNN.ifs files (length variants such as _short are skipped)')
    a = p.parse_args(argv)
    a.out.mkdir(parents=True, exist_ok=True)
    listed = (a.out / 'list.txt').read_text().split() if (a.out / 'list.txt').is_file() else []
    made = []
    for f in sorted(a.ifs, key=lambda f: f.name[-12:]):
        m = re.search(r'notes(\d\d)\.ifs$', f.name)
        if not m:
            print('%s: skipped (not notesNN.ifs)' % f.name, file=sys.stderr)
            continue
        n = int(m.group(1))
        with tempfile.TemporaryDirectory() as tmp:
            konami_tex.main([str(f), tmp])
            d = 'arcade%02d' % n
            write_skin(Path(tmp), a.out / d, NAMES.get(n, 'Arcade %02d' % n))
        made.append(d)
        if d not in listed:
            listed.append(d)
    (a.out / 'list.txt').write_text(''.join(l + '\n' for l in listed))
    print('%d skins: %s' % (len(made), ', '.join(made)))


if __name__ == '__main__':
    main()
