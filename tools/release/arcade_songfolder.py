"""Converted arcade song (arcade_convert.py --split output) -> MEGAMIX custom song folder.

  python3 arcade_songfolder.py CONVERTED TEMPLATE.bin OUT/kailua_hcn --music music_omni.bin --id 18102

TEMPLATE.bin is a native EMPRESS record (dump_record.py; All My Turn = id 2). Writes OUT/record.bin,
OUT/song.txt, copies the charts and banks, and writes OUT/catalog-lines.txt: the S/C lines to append
to the MEGAMIX catalog/charts.txt (the launcher rejects an added song with no catalog identity).
Install as songs/custom/<folder>.

Record fields (0x134 bytes; launcher/main.c add_song, song_resources.c, song_window.c, All My Turn):
  0x00  title (ASCII, upper case; also the A-Z folder sort)      0x42/0x44  s16 BPM min / max (-1 = constant)
  0x4a  levels SP N,H,A,B,X then DP N,H,A,B,X                    0x54  version (16 EMPRESS, 17 SIRIUS)
  0x58  file id kept from the template (All My Turn's 37 MB file, probably the BGA movie)
  0x94, 0x9a  title card (data1 index)    0xac..0xbb  title sprite indices     0xbd  large-bank flag
  0xc0..0xe4  u32 chart file ids, by level index (launcher/coalesce_catalog.h): SPN d0, SPH c0,
        SPA c8, SPB d8, SPX e0, DPN d4, DPH c4, DPA cc, DPB dc, DPX e4
  0xe8 / 0xec  u16 bank / stream file ids (0xea / 0xee 0xffff, as in every native record), more groups up to 0x107 unused
  0x108 u16 SP notes N,H,A,B,X   0x112 u16 pairs DP notes (P1, P2) N,H,A,B,X   0x126 u16 graph targets
  (SP N,H,A, DP N,H,A; All My Turn's are 1.33-1.52 x notes; 1.4 x notes is written)
  0x38 / 0x3c (the end of the title field)  hold sprites: the handle of the last of the 36 hold sprites
  appended to note container #109 / #146 (S lines; tools/holds/holds_draw.c); titles stop at 0x37.
Until title cards and sprites are converted, the card and sprites are the template's.

--hold-sprites DIR (tools/holds/hold_sprites.py output): charts with holds get the CN/HCN graphics.
"""
import argparse
import json
import shutil
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import arcade_data as A  # noqa: E402
import holds_format as H  # noqa: E402

ORDER = ['SPN', 'SPH', 'SPA', 'SPB', 'SPX', 'DPN', 'DPH', 'DPA', 'DPB', 'DPX']   # level / catalog order
# chart file id per difficulty: launcher/coalesce_catalog.h `fields` (level index ch -> field)
CHART_FIELD = {'SPN': 0xd0, 'SPH': 0xc0, 'SPA': 0xc8, 'SPB': 0xd8, 'SPX': 0xe0,
               'DPN': 0xd4, 'DPH': 0xc4, 'DPA': 0xcc, 'DPB': 0xdc, 'DPX': 0xe4}
ARCADE = {'SPB': 3, 'SPN': 1, 'SPH': 0, 'SPA': 2, 'SPX': 4, 'DPB': 9, 'DPN': 7, 'DPH': 6, 'DPA': 8, 'DPX': 10}
LEVELS = 0x3ec           # music_data v32 entry: SP B,N,H,A,L then DP B,N,H,A,L (u8)
GRAPH = 1.4


def ply_counts(raw):
    """(P1 notes, P2 notes) including hold tails, from a converted (encrypted) EMPRESS chart."""
    from sirius_probe import empress_chart
    from chart_transform import crypt, decode_lz
    c = empress_chart(raw)
    if not c:
        raise ValueError('not an EMPRESS chart')
    side = [0, 0]
    notes = set()
    for t, cmd, c2, v in c['events']:
        if cmd & 15 in (0, 1):
            side[cmd & 15] += 1
            notes.add((t, (cmd & 15) * 8 + (cmd >> 4)))
    ply = decode_lz(crypt(raw, 'empress', True))
    end = 8 + 8 * len(c['events']) + 8
    if ply[end:end + 4] == H.MAGIC:
        for s, e, lane, kind, tick in H.unpack(ply[end:]):
            if not any((e + o, lane) in notes for o in range(-2, 3)):   # charts from before tail notes: count the tail here
                side[lane >> 3] += 1
    return side


def has_holds(path):
    from chart_transform import crypt, decode_lz
    from sirius_probe import empress_chart
    raw = path.read_bytes()
    c = empress_chart(raw)
    ply = decode_lz(crypt(raw, 'empress', True))
    end = 8 + 8 * len(c['events']) + 8
    return ply[end:end + 4] == H.MAGIC


def levels(entry):
    sp, dp = entry[LEVELS:LEVELS + 5], entry[LEVELS + 5:LEVELS + 10]
    b, n, h, a, l = sp
    out = {'SPB': b, 'SPN': n, 'SPH': h, 'SPA': a, 'SPX': l}
    b, n, h, a, l = dp
    out.update({'DPB': b, 'DPN': n, 'DPH': h, 'DPA': a, 'DPX': l})
    return out


HOLD_FIELDS = ((0x6d, 0x38), (0x92, 0x3c))        # (note container #109 / #146, record field)


def hold_sprite_names():
    sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'holds'))
    import hold_sprites
    return [row[0] for row in hold_sprites.catalogue()]


def build(converted, template, out, title, lv, version=17, sprites=None):
    report = json.loads((converted / 'convert.json').read_text())
    rec = bytearray(template)
    if len(rec) != 0x134:
        raise ValueError('template record is %d bytes, not 0x134' % len(rec))
    rec[0:0x40] = title.upper().encode('ascii', 'replace')[:0x37].ljust(0x40, b'\0')
    bpms = [b for c in report['charts'].values() if c.get('bpm') for b in c['bpm']]
    lo, hi = int(round(min(bpms))), int(round(max(bpms)))
    struct.pack_into('<hh', rec, 0x42, lo, -1 if lo == hi else hi)
    rec[0x54] = version
    rec[0xbd] = 1
    for f in CHART_FIELD.values():
        struct.pack_into('<I', rec, f, 0)
    rec[0x108:0x134] = bytes(0x134 - 0x108)
    lines = ['name ' + title, 'record record.bin']
    charts = []
    out.mkdir(parents=True, exist_ok=True)
    for i, name in enumerate(ORDER):
        path = converted / (name + '.bin')
        present = path.is_file()
        rec[0x4a + i] = lv.get(name, 0) if present else 0
        if not present:
            continue
        charts.append(name)
        shutil.copyfile(path, out / path.name)
        lines.append('F %x 4 %s' % (CHART_FIELD[name], path.name))
        p1, p2 = ply_counts(path.read_bytes())
        d = i % 5
        if name.startswith('SP'):
            struct.pack_into('<H', rec, 0x108 + d * 2, p1 + p2)
            total = p1 + p2
        else:
            struct.pack_into('<HH', rec, 0x112 + d * 4, p1, p2)
            total = p1 + p2
        if d < 3:
            struct.pack_into('<H', rec, 0x126 + ((i >= 5) * 3 + d) * 2, round(total * GRAPH))
    # 0xe8 bank, 0xec stream (FUN_001e4050 / FUN_001e3f30 read +0 / +4 of the group): every native
    # song has one bank for SP and DP and a stream. Earlier conversions wrote DP.bank to 0xec, so the
    # game played the bank as a stream (noise in Kailua, a divide by zero in Elisha).
    # Per-difficulty banks (arcade_convert DIFFICULTY_FIELDS): FUN_001e4050 picks the group by
    # difficulty (0xe8 H/X, 0xf0 A, 0xf8 N, 0x100 B) and falls back to 0xe8 when a field is 0xffff.
    if any(converted.glob('keys_*.bank')):
        files = [('keys_%s.bank' % g, f) for g, f in (('H', 0xe8), ('A', 0xf0), ('N', 0xf8), ('B', 0x100))]
        if not (converted / 'keys_H.bank').is_file():          # 0xe8 is every group's fallback: never the template's
            files[0] = (next(n for n, f in files if (converted / n).is_file()), 0xe8)
        files = tuple(files) + (('stream.bin', 0xec),)
    elif (converted / 'keys.bank').is_file():
        files = (('keys.bank', 0xe8), ('stream.bin', 0xec))
    else:
        files = (('SP.bank', 0xe8), ('stream.bin', 0xec))
    for name, field in files:
        if (converted / name).is_file():
            shutil.copyfile(converted / name, out / name)
            lines.append('F %x 2 %s' % (field, name))
    if not (converted / 'stream.bin').is_file():
        print('%s: warning: no stream.bin; the record keeps the template\'s stream at 0xec' % out, file=sys.stderr)
    if sprites and any(has_holds(converted / (n + '.bin')) for n in charts):
        names = hold_sprite_names()
        (out / 'holds').mkdir(exist_ok=True)
        for n in names:
            shutil.copyfile(sprites / (n + '.spr'), out / 'holds' / (n + '.spr'))
        for cont, field in HOLD_FIELDS:
            lines += ['S %x %x holds/%s.spr' % (cont, field, n) for n in names]
    (out / 'record.bin').write_bytes(bytes(rec))
    (out / 'song.txt').write_text('\n'.join(lines) + '\n')
    (out / 'catalog-lines.txt').write_text(catalog_lines(out.name, charts))
    return {'title': title, 'charts': charts, 'bpm': [lo, hi]}


def catalog_lines(folder, charts):
    from importlib import util
    spec = util.spec_from_file_location('chart_catalog', Path(__file__).resolve().parents[1] / 'offdisc' / 'chart_catalog.py')
    cc = util.module_from_spec(spec)
    spec.loader.exec_module(cc)
    data = {'chart_order': ORDER, 'aliases': {}, 'sources': [
        {'source': 'custom/' + folder, 'sid': 0, 'rank': 2000, 'family': 'custom/' + folder, 'charts': charts}]}
    return ''.join(l + '\n' for l in cc.emit(data).splitlines()[1:])


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('converted', type=Path)
    p.add_argument('template', type=Path)
    p.add_argument('output', type=Path)
    p.add_argument('--music', type=Path, required=True, help='music_data.bin / music_omni.bin (title, levels)')
    p.add_argument('--id', type=int, required=True, help='arcade song id')
    p.add_argument('--version', type=int, default=17, help='record version byte (default 17, SIRIUS)')
    p.add_argument('--hold-sprites', type=Path, help='tools/holds/hold_sprites.py output (CN/HCN graphics)')
    a = p.parse_args(argv)
    md = A.read_music_data(a.music.read_bytes())
    at = md['index'][a.id] if a.id < len(md['index']) else -1
    if at < 0:
        sys.exit('song %d is not in %s' % (a.id, a.music))
    entry = md['entries'][at]
    title = A.entry_title(entry)[1] or A.entry_title(entry)[0]
    info = build(a.converted, a.template.read_bytes(), a.output, title, levels(entry), a.version, a.hold_sprites)
    print('%s: %s, BPM %s, charts %s' % (a.output, info['title'], info['bpm'], ' '.join(info['charts'])))


if __name__ == '__main__':
    main()
