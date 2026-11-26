"""arcade_batch: song discovery, folder names, versions, and --install into a built base."""
import json, unittest
from pathlib import Path
import arcade_batch as B
import megamix as M
import test_release as TR


class Batch(unittest.TestCase):
    setUp, tearDown, bundle = TR.ReleaseTests.setUp, TR.ReleaseTests.tearDown, TR.ReleaseTests.bundle

    def test_find_sources_and_versions(self):
        r = self.root / 'ac'
        (r / 'data' / 'sound' / '32083').mkdir(parents=True)
        (r / 'data' / 'sound' / '32083' / '32083.1').write_bytes(b'x')
        (r / 'data' / 'sound' / '32083_pre').mkdir()
        (r / 'ifs').mkdir()
        (r / 'ifs' / '29025.ifs').write_bytes(b'x')
        (r / 'ifs' / '29025_pre.ifs').write_bytes(b'x')
        found = B.find_sources(r)
        self.assertEqual(sorted(found), [29025, 32083])
        self.assertEqual(B.parse_versions('17-19,32'), {17, 18, 19, 32})
        used = set()
        self.assertEqual([B.slug(t, used) for t in ('Kailua (HCN Ver.)', 'kailua hcn ver', '!!!')],
                         ['kailua_hcn_ver', 'kailua_hcn_ver_2', 'song'])

    def test_install_lists_songs_and_styles(self):
        bundle = self.bundle(('id',)); base = self.root / 'MEGAMIX'
        M.build(self.root / 'id.iso', base, bundle, progress=lambda _: None)
        out = self.root / 'batch'
        for style, folder in (('pinky_crush', 'mesmerizer'), ('sirius', 'elisha'), ('resort_anthem', 'kailua')):
            d = out / 'songs' / style / folder; d.mkdir(parents=True)
            (d / 'song.txt').write_text('name X\n')
            (out / 'songs' / style / 'style.txt').write_text('name %s\n' % style)
        (base / 'songs' / 'styles.txt').write_text('sirius\n' + (base / 'songs' / 'styles.txt').read_text())
        (base / 'songs' / 'sirius').mkdir(exist_ok=True)
        done = [{'style': 'pinky_crush', 'folder': 'mesmerizer'}, {'style': 'sirius', 'folder': 'elisha'},
                {'style': 'resort_anthem', 'folder': 'kailua'}]
        B.install(base, out, done)
        styles = (base / 'songs' / 'styles.txt').read_text().split()
        self.assertEqual(styles[:3], ['sirius', 'resort_anthem', 'pinky_crush'])    # arcade styles in version order
        self.assertIn('elisha', (base / 'songs' / 'sirius' / 'index.txt').read_text().split())
        self.assertTrue((base / 'songs' / 'pinky_crush' / 'mesmerizer' / 'song.txt').is_file())
        self.assertEqual(M.verify(base), [])
        B.install(base, out, done)                                                    # again: nothing doubled
        self.assertEqual((base / 'songs' / 'sirius' / 'index.txt').read_text().split().count('elisha'), 1)


if __name__ == '__main__':
    unittest.main()
