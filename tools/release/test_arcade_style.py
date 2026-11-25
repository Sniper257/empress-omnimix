"""Music-select folders for converted songs (arcade_style.py): text checks that need no game data."""
import unittest

import arcade_style as S


class ArcadeStyle(unittest.TestCase):
    def test_label_glyphs(self):
        with self.assertRaises(ValueError):
            S.label({}, 'QUEEN')                     # no Q among the native label glyphs
        with self.assertRaises(ValueError):
            S.label({}, 'copula')                    # labels are upper case

    def test_sizes_match_the_recipes(self):
        import json
        sizes = {b['ref']['bytes'] for b in json.loads((S.HERE / 'folder_recipes.json').read_text())['bases']}
        self.assertEqual(sizes, {S.LABEL_BYTES, S.CAPTION_BYTES})


if __name__ == '__main__':
    unittest.main()
