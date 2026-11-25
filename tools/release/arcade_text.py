"""Song text graphics for converted arcade songs: title card and the four title sprites.

Arcade IIDX draws song text with fonts, so nothing is converted: the title, genre and artist from
music_data / music_omni (UTF-16) are rendered with the release's open font atlas
(text-fonts/arcade-v1.mga, see ATLAS) through the same text_native functions the release uses for its own
text, in the native styles:

  card.if      title card (data1 container, 384x384), text_native.title_card   song.txt: card card.if
  wheel.spr    music wheel   container 0x12a, record 0xb8 (24 rows)          S 12a b8 wheel.spr
  title.spr    big title     container 0x12b, record 0xb0 (48 rows)          S 12b b0 title.spr
  genre.spr    genre         container 0x12c, record 0xac (16 rows)          S 12c ac genre.spr
  artist.spr   artist        container 0x12d, record 0xb4 (24 rows)          S 12d b4 artist.spr

(record fields: songdb.c spr_wheel / spr_big / spr_genre / spr_artist.)

The sprites are 8-bit, in the palette of a sheet of their container, so they need the four native
containers from a MEGAMIX base (text_sprites.json "bases": a/40/29, a/21/41, a/37/27, a/57/34),
checked by SHA-256. Each style comes from a reference sprite of that container and the render
settings text_sprites.json uses for the release's own songs.
"""
import hashlib
import json
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import arcade_data as A  # noqa: E402
import text_native as T  # noqa: E402
import text_raster as R  # noqa: E402

# the release's open atlas plus the characters of converted arcade songs (text-fonts/arcade-corpus.json:
# corpus.json + their titles, genres and artists), built the same way from the same pinned fonts:
#   python3 fetch_text_fonts.py text-fonts/open-fonts.sources.json SRC
#   cd SRC && python3 prepare_text_fonts.py open-fonts.json text-fonts/arcade-corpus.json text-fonts/arcade-v1.mga
# (the release's own glyphs come out identical). Add a song's text to arcade-corpus.json when a glyph is missing.
ATLAS = HERE / 'text-fonts' / 'arcade-v1.mga'
# name: (container bytes, role, reference sprite, max width, bold, xscale), from text_sprites.json
SPRITES = {
    'wheel': (139264, 'wheel', 76, 255, 0, 0.81),
    'title': (505856, 'large_title', 84, 266, 1, 1.2),
    'genre': (96256, 'genre_artist', 22, 218, 1, 1.0),
    'artist': (86016, 'genre_artist', 81, 262, 1, 1.0),
}
LINES = {'wheel': ('12a', 'b8'), 'title': ('12b', 'b0'), 'genre': ('12c', 'ac'), 'artist': ('12d', 'b4')}


def song_texts(music_data, song_id):
    """{'title', 'genre', 'artist'} of an arcade song (music_data v32 entry: UTF-16 at 0x00, 0x140, 0x1c0)."""
    m = A.read_music_data(music_data)
    n = m['index'][song_id] if song_id < len(m['index']) else -1
    if n < 0 or n >= len(m['entries']):
        raise ValueError('song %d is not in the music data' % song_id)
    e = m['entries'][n]
    u16 = lambda b: b.decode('utf-16-le', 'replace').split('\0')[0].strip()
    title, ascii_title = A.entry_title(e)
    return {'title': title or ascii_title, 'ascii_title': ascii_title,
            'genre': u16(e[0x140:0x1c0]), 'artist': u16(e[0x1c0:0x240])}


def base_containers(base):
    """{size: bytes} of the four native text containers found under a MEGAMIX base folder."""
    refs = {b['ref']['bytes']: (b['path'], b['ref']['sha256'])
            for b in json.loads((HERE / 'text_sprites.json').read_text())['bases']}
    out = {}
    for size, (rel, sha) in refs.items():
        for root in (Path(base), Path(base) / 'EMPRESS'):
            p = root / rel
            if p.is_file() and p.stat().st_size == size and hashlib.sha256(p.read_bytes()).hexdigest() == sha:
                out[size] = p.read_bytes()
                break
    return out


def atlas():
    return R.Atlas(ATLAS.read_bytes())


def missing_glyphs(a, text, role):
    """Characters of text the atlas has no glyph for in role."""
    out = []
    for ch in dict.fromkeys(text):
        try:
            a.glyph(role, ch)
        except (KeyError, ValueError):
            out.append(ch)
    return out


def render(texts, containers=None, a=None):
    """{file name: bytes}: card.if always, the four .spr files when containers ({size: bytes}) are given.
    A title the atlas cannot draw falls back to the ASCII title."""
    a = a or atlas()
    texts = dict(texts)
    if missing_glyphs(a, texts['title'], 'large_title') and texts.get('ascii_title'):
        texts['title'] = texts['ascii_title']
    for key, role in (('title', 'large_title'), ('genre', 'card_genre'), ('artist', 'card_artist')):
        gone = missing_glyphs(a, texts[key], role)
        if gone:
            raise ValueError('%s %r: no glyph for %s in the font atlas (add the text to text-fonts/arcade-corpus.json and rebuild, see ATLAS)' % (key, texts[key], ''.join(gone)))
    out = {'card.if': T.title_card(a, {k: texts[k] for k in ('title', 'genre', 'artist')})}
    for name, (size, role, ref, width, bold, xs) in SPRITES.items():
        if containers and size in containers:
            out[name + '.spr'] = T.render_sprite(a, texts['artist' if name == 'artist' else 'genre' if name == 'genre' else 'title'],
                                                 role, T.Container(containers[size]), ref, width, bold, xs)
    return out


def song_lines(files):
    """song.txt lines for the rendered files."""
    lines = ['card card.if'] if 'card.if' in files else []
    lines += ['S %s %s %s.spr' % (c, f, n) for n, (c, f) in LINES.items() if n + '.spr' in files]
    return lines
