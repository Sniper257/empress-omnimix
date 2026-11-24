import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import arcade_convert as C  # noqa: E402
import arcade_songfolder as F  # noqa: E402
import holds_format as H  # noqa: E402


class SongFolder(unittest.TestCase):
    def test_record_and_song_txt(self):
        with tempfile.TemporaryDirectory() as tmp:
            conv, out = Path(tmp) / 'conv', Path(tmp) / 'songs' / 'test_song'
            conv.mkdir()
            ev = [(0, 4, 1, 150), (1000, 0, 0, 0), (1500, 1, 3, 500), (2000, 0, 7, 0), (3000, 6, 0, 0)]
            holds = H.from_events(ev, hell=True, time=C.tick, ticks_per_ms=1000 / C.CLOCK)
            ply, _ = C.empress_events(ev, holds)
            for name in ('SPN', 'DPA'):
                (conv / (name + '.bin')).write_bytes(C.encrypt(ply))
            (conv / 'SP.bank').write_bytes(b'wp\x03\x00')
            (conv / 'convert.json').write_text(json.dumps({'charts': {'SPN': {'bpm': [150, 150]}, 'DPA': {'bpm': [150, 150]}}}))
            template = bytearray(b'\xee' * 0x134)
            spr = Path(tmp) / 'spr'
            spr.mkdir()
            for n in F.hold_sprite_names():
                (spr / (n + '.spr')).write_bytes(b'SPR1')
            F.build(conv, bytes(template), out, 'Test Song ' + 'x' * 80, {'SPN': 4, 'DPA': 12, 'SPA': 9}, sprites=spr)
            rec = (out / 'record.bin').read_bytes()
            self.assertEqual(rec[:10], b'TEST SONG ')
            self.assertEqual(rec[0x37:0x40], bytes(9))                         # title stops before the hold fields
            self.assertEqual(struct.unpack_from('<hh', rec, 0x42), (150, -1))
            self.assertEqual(list(rec[0x4a:0x54]), [4, 0, 0, 0, 0, 0, 0, 12, 0, 0])
            self.assertEqual((rec[0x54], rec[0xbd]), (17, 1))
            self.assertEqual(struct.unpack_from('<H', rec, 0x108)[0], 4)            # 3 notes + 1 hold tail
            self.assertEqual(struct.unpack_from('<HH', rec, 0x112 + 2 * 4), (2, 2))  # DPA: P1 2 notes, P2 1 note + its tail
            self.assertEqual(struct.unpack_from('<I', rec, 0xc4)[0], 0)             # absent chart cleared
            txt = (out / 'song.txt').read_text().splitlines()
            self.assertEqual(txt[1:3], ['record record.bin', 'F c0 4 SPN.bin'])
            self.assertIn('F d4 4 DPA.bin', txt)
            self.assertIn('F e8 2 SP.bank', txt)
            s_lines = [l for l in txt if l.startswith('S ')]
            self.assertEqual(len(s_lines), 72)
            self.assertEqual(s_lines[0], 'S 6d 38 holds/cn_white_start.spr')
            self.assertEqual(s_lines[35], 'S 6d 38 holds/hbss_beam4.spr')
            self.assertEqual(s_lines[36], 'S 92 3c holds/cn_white_start.spr')
            self.assertTrue((out / 'holds' / 'hcn_black_beam3.spr').is_file())
            cat = (out / 'catalog-lines.txt').read_text().splitlines()
            self.assertTrue(cat[0].startswith('S custom/test_song 0 2000 '))
            self.assertEqual([l.split()[3] for l in cat[1:]], ['0', '7'])


if __name__ == '__main__':
    unittest.main()
