"""Song text graphics for arcade songs: texts from music data, title card, song.txt lines."""
import struct
import unittest

import arcade_text as X
import text_native as T


class ArcadeText(unittest.TestCase):
    def test_card_and_lines(self):
        a = X.atlas()
        files = X.render({'title': 'Elisha', 'ascii_title': 'Elisha', 'genre': 'TRANCE CORE', 'artist': 'DJ YOSHITAKA'}, None, a)
        self.assertEqual(set(files), {'card.if'})                        # sprites need the native containers
        w, h, px = T.Container(files['card.if']).sprite(0)
        self.assertEqual((w, h), (298, 349))
        self.assertTrue(any(p[3] for p in px))                            # text drawn
        self.assertEqual(X.song_lines(files), ['card card.if'])
        self.assertEqual(X.song_lines({'card.if': b'', 'wheel.spr': b'', 'artist.spr': b''}),
                         ['card card.if', 'S 12a b8 wheel.spr', 'S 12d b4 artist.spr'])

    def test_missing_glyphs(self):
        a = X.atlas()
        self.assertEqual(X.missing_glyphs(a, 'Elisha', 'large_title'), [])
        self.assertEqual(X.missing_glyphs(a, 'A☃', 'large_title'), ['☃'])
        # a title the atlas cannot draw falls back to the ASCII title
        f = X.render({'title': '☃☃', 'ascii_title': 'Snow', 'genre': 'G', 'artist': 'A'}, None, a)
        self.assertIn('card.if', f)
        with self.assertRaises(ValueError):
            X.render({'title': 'T', 'ascii_title': 'T', 'genre': '☃', 'artist': 'A'}, None, a)

    def test_lines_match_the_runtime_getters(self):
        # songdb.c: spr_wheel 0xb8, spr_big 0xb0, spr_genre 0xac, spr_artist 0xb4; containers 0x12a.. (main.c SPR_CONT)
        self.assertEqual(X.LINES, {'wheel': ('12a', 'b8'), 'title': ('12b', 'b0'), 'genre': ('12c', 'ac'), 'artist': ('12d', 'b4')})


if __name__ == '__main__':
    unittest.main()
