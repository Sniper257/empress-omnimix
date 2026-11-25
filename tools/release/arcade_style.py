"""A music-select folder (style) for converted songs: songs/<style>/style.txt, label.spr, caption.spr, index.txt.

  python3 arcade_style.py MEGAMIX_BASE OUT sirius SIRIUS SIRIUS song_folder...
  python3 arcade_style.py MEGAMIX_BASE OUT copula COPULA copula song_folder...

A style with "folder <label.spr>" in its style.txt gets its own music-select folder (launcher/main.c
folders(): label from container 0, caption from container 303, VERSION bar 0x1c; with no "voice"
line the folder uses the EMPRESS folder's voice, as there is no narration for newer styles). The
folder lists the songs in its index.txt (release launchers read only indexed songs); the style must
also be a line of songs/styles.txt, in menu order.

The label and caption are made the way the release makes its own (folder_text.py, folder_recipes.json):
  label    native label glyphs (container a/33/07), upper case: layout "label"
  caption  native caption glyphs (container a/84/41) for upper-case text ("caption"), or the open font
           in the native caption style for mixed case ("pb_caption", as substream / DistorteD)
Both containers come from a MEGAMIX base and are checked by SHA-256 (folder_recipes.json "bases").
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import folder_text as FT  # noqa: E402
import text_native as T  # noqa: E402

LABEL_BYTES, CAPTION_BYTES = 131072, 1603584
CAPTION_H = 38


def base_containers(base):
    refs = {b['ref']['bytes']: (b['path'], b['ref']['sha256'])
            for b in json.loads((HERE / 'folder_recipes.json').read_text())['bases']}
    out = {}
    for size, (rel, sha) in refs.items():
        for root in (Path(base), Path(base) / 'EMPRESS'):
            p = root / rel
            if p.is_file() and p.stat().st_size == size and hashlib.sha256(p.read_bytes()).hexdigest() == sha:
                out[size] = T.Container(p.read_bytes())
                break
    return out


def label(containers, text):
    if any(c not in FT.GLYPHS['label'] and c != ' ' for c in text):
        raise ValueError('folder label %r: the native label glyphs are 0-9 and A-Z without Q' % text)
    return FT.generate(None, {'layout': 'label', 'text': text}, containers[LABEL_BYTES])


def caption(containers, text, atlas=None):
    if all(c in FT.GLYPHS['caption'] or c == ' ' for c in text):
        return FT.generate(None, {'layout': 'caption', 'text': text}, containers[CAPTION_BYTES])
    import text_raster as R
    atlas = atlas or R.Atlas((HERE / 'text-fonts' / 'open-v1.mga').read_bytes())
    natural = atlas.coverage(text, 'large_title', (CAPTION_H - 4) * 4, 0, 512).bbox()
    width = min(640, (natural[2] - natural[0]) // 4 + 16 if natural else 64)
    return FT.generate(atlas, {'layout': 'pb_caption', 'text': text, 'size': [width, CAPTION_H]}, containers[CAPTION_BYTES])


def write_style(out, style, name, label_text, caption_text, songs, containers, custom=False):
    d = Path(out) / 'songs' / style
    d.mkdir(parents=True, exist_ok=True)
    (d / 'label.spr').write_bytes(label(containers, label_text))
    (d / 'caption.spr').write_bytes(caption(containers, caption_text))
    (d / 'style.txt').write_text('name %s\nfolder label.spr\ncaption caption.spr\n%s' % (name, 'custom\n' if custom else ''))
    (d / 'index.txt').write_text(''.join(s + '\n' for s in songs))
    return d


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('base', type=Path, help='MEGAMIX base folder (holds a/33/07 and a/84/41)')
    p.add_argument('output', type=Path, help='writes OUTPUT/songs/<style>/')
    p.add_argument('style', help='style folder name, e.g. copula')
    p.add_argument('label', help='folder label text (upper case), e.g. COPULA')
    p.add_argument('caption', help='folder caption text, e.g. copula')
    p.add_argument('songs', nargs='*', help='song folder names for index.txt')
    p.add_argument('--name', help='style name (default: the caption)')
    p.add_argument('--custom', action='store_true',
                   help='take the CUSTOM folder\'s slot (folder 59, slot 0) with this style\'s own label and caption. '
                        'Launchers before NFOLD 45 had 19 slots (CUSTOM + 16 PB styles + beatmania USA + one spare), so a '
                        'second added style fitted only this way; with 45 there are 44 style slots besides CUSTOM')
    a = p.parse_args(argv)
    containers = base_containers(a.base)
    if set(containers) != {LABEL_BYTES, CAPTION_BYTES}:
        sys.exit('%s: the native folder containers a/33/07 and a/84/41 were not found (or differ)' % a.base)
    d = write_style(a.output, a.style, a.name or a.caption, a.label, a.caption, a.songs, containers, a.custom)
    print('%s: folder %s, %d songs; add "%s" to songs/styles.txt' % (d, a.label, len(a.songs), a.style))


if __name__ == '__main__':
    main()
