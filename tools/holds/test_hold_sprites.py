import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import hold_sprites as S  # noqa: E402


class Spr1(unittest.TestCase):
    def test_round_trip_and_layout(self):
        from PIL import Image
        img = Image.new('RGBA', (4, 2), (0, 0, 0, 0))
        img.putpixel((1, 0), (255, 0, 0, 255))
        img.putpixel((2, 1), (0, 0, 255, 128))
        data = S.spr1(img)
        self.assertEqual(data[:4], b'SPR1')
        self.assertEqual(struct.unpack_from('<HHHBB', data, 4), (4, 2, 0xffff, 0, 0))
        self.assertEqual(len(data), 12 + 8 + 0x400)
        back = S.decode(data)
        self.assertEqual(back.getpixel((0, 0))[3], 0)
        self.assertEqual(back.getpixel((1, 0)), (255, 0, 0, 255))
        r, g, b, a = back.getpixel((2, 1))
        self.assertEqual((r, g, b), (0, 0, 255))
        self.assertTrue(125 <= a <= 130)
        top = max(struct.unpack_from('256B', data[12 + 8 + 3::4][:256]))
        self.assertEqual(top, 0x80)                                    # PS2 alpha: 0x80 = opaque

    def test_shared_palette_is_identical(self):
        from PIL import Image
        a = Image.new('RGBA', (3, 1), (200, 10, 10, 255)); b = Image.new('RGBA', (2, 2), (10, 10, 200, 100))
        pal = S.shared_palette([a, b])
        da, db = S.spr1(a, pal), S.spr1(b, pal)
        self.assertEqual(da[-0x400:], db[-0x400:])
        self.assertEqual(S.decode(da).getpixel((0, 0))[:3], (200, 10, 10))
        self.assertEqual(S.decode(db).getpixel((1, 1))[:3], (10, 10, 200))

    def test_swizzle_is_csm1(self):
        self.assertEqual([S.swizzle(i) for i in (0, 8, 16, 24, 31, 40)], [0, 16, 8, 24, 31, 48])

    def test_catalogue(self):
        names = [r[0] for r in S.catalogue()]
        self.assertEqual(len(names), 36)
        self.assertIn('hcn_white_beam4', names)
        self.assertIn('hbss_end', names)


if __name__ == '__main__':
    unittest.main()
