#!/usr/bin/env python3
"""Song database for arcade_batch.py, from one or more arcade music data files.

  python3 arcade_songdb.py music_data.bin music_omni.bin -o arcade-songs.json

Every song id found in the files, with its version, style folder (arcade_batch.STYLES), titles, genre,
artist, levels and its whole music-data entry (so a batch needs no music data file). When a song is in
more than one file, the first file given wins: put the newest music_data.bin first and an omnimix
music_omni.bin after it to fill in the songs it lacks.

  python3 arcade_songdb.py --list arcade-songs.json      songs per style
"""
import argparse
import base64
import json
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import arcade_data as A  # noqa: E402

GAME_VERSION = 988          # music_data v32 entry: u16 game version (0 1st style, 1 substream, 2 2nd style, ...)
SCHEMA = 1


def _u16(b):
    return b.decode('utf-16-le', 'replace').split('\0')[0].strip()


def entry_texts(e):
    """{'title', 'ascii_title', 'genre', 'artist'} of a music_data v32 entry (as arcade_text.song_texts)."""
    title, ascii_title = A.entry_title(e)
    return {'title': title or ascii_title, 'ascii_title': ascii_title, 'genre': _u16(e[0x140:0x1c0]),
            'artist': _u16(e[0x1c0:0x240])}


def build(paths):
    from arcade_batch import STYLES
    import arcade_songfolder as F
    songs, sources = {}, []
    for path in paths:
        m = A.read_music_data(Path(path).read_bytes())
        n, taken = 0, set()
        for sid, at in enumerate(m['index']):
            # unused ids of a version's range point at entry 0 (5.1.1.): an entry belongs to its lowest id
            if at < 0 or at in taken:
                continue
            taken.add(at)
            if str(sid) in songs:
                continue
            e = m['entries'][at]
            version = struct.unpack_from('<H', e, GAME_VERSION)[0]
            t = entry_texts(e)
            if not (t['title'] or t['ascii_title']):
                continue
            songs[str(sid)] = dict(t, version=version, style=STYLES.get(version, (None,))[0], levels=F.levels(e),
                                   music=str(Path(path).name), entry=base64.b64encode(e).decode())
            n += 1
        sources.append({'file': Path(path).name, 'format': m['version'], 'songs': n})
    return {'schema': SCHEMA, 'sources': sources, 'songs': songs}


def load(path):
    db = json.loads(Path(path).read_text(encoding='utf-8'))
    if db.get('schema') != SCHEMA:
        raise ValueError('%s is not an arcade song database (arcade_songdb.py)' % path)
    return db


def entry(row):
    return base64.b64decode(row['entry'])


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('music', type=Path, nargs='*', help='music_data.bin files, the preferred first')
    p.add_argument('-o', '--output', type=Path, help='database to write')
    p.add_argument('--list', type=Path, metavar='DB', help='print the songs per style of a database')
    a = p.parse_args(argv)
    if a.list:
        db = load(a.list)
        by = {}
        for sid, r in db['songs'].items():
            by.setdefault((r['version'], r['style'] or '(CS-era: version %d)' % r['version']), []).append(int(sid))
        for (v, style), ids in sorted(by.items()):
            print('%-28s %4d songs  (ids %d..%d)' % (style, len(ids), min(ids), max(ids)))
        return
    if not a.music or not a.output:
        p.error('give the music data files and -o DB (or --list DB)')
    db = build(a.music)
    a.output.write_text(json.dumps(db, ensure_ascii=False))
    for s in db['sources']:
        print('%s (format %d): %d songs' % (s['file'], s['format'], s['songs']))
    print('%s: %d songs' % (a.output, len(db['songs'])))


if __name__ == '__main__':
    main()
