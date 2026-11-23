"""holds.bin format, conversion, and the C rules core (built with the host compiler when present)."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

import bms
import holds_format as H

ROOT = Path(__file__).resolve().parents[2] / 'tools' / 'holds'


class Format(unittest.TestCase):
    def test_from_events_lanes_and_kinds(self):
        ev = [(100, 0, 0, 500), (100, 0, 7, 300), (200, 1, 7, 50), (300, 1, 2, 0), (0, 7, 0, 3)]
        self.assertEqual(H.from_events(ev), [(100, 400, 7, H.BSS, 0), (100, 600, 0, H.CN, 0), (200, 250, 15, H.BSS, 0)])
        self.assertEqual([h[3] for h in H.from_events(ev, hell=True)], [H.HBSS, H.HCN, H.HBSS])

    def test_hcn_tick_is_a_16th_at_the_heads_bpm(self):
        # BPM 146 (as in the measured Kailua chart): a 16th is 102.7 ms = 6.16 EMPRESS frames
        ev = [(0, 4, 100, 14600), (0, 0, 2, 1000), (5000, 4, 1, 200), (6000, 0, 3, 500)]
        holds = H.from_events(ev, hell=True, time=lambda t: round(t * 1000 / 16683), ticks_per_ms=1000 / 16683)
        self.assertEqual([h[4] for h in holds], [round(15000 / 146 * 1000 / 16683 * 64), round(75 * 1000 / 16683 * 64)])
        self.assertAlmostEqual(holds[0][4] / 64, 6.16, places=2)
        self.assertEqual(H.unpack(H.pack(holds)), holds)

    def test_time_mapping_and_round_trip(self):
        holds = H.from_events([(1000, 0, 3, 1000)], time=lambda t: round(t * 1000 / 16683))
        self.assertEqual(holds, [(60, 120, 3, H.CN, 0)])
        self.assertEqual(H.unpack(H.pack(holds)), holds)
        self.assertEqual(len(H.pack(holds)), 8 + 12)

    def test_rejects_overlap_and_bad_records(self):
        with self.assertRaisesRegex(ValueError, 'Overlapping'):
            H.from_events([(0, 0, 1, 100), (50, 0, 1, 100)])
        bad = bytearray(H.pack([(0, 10, 0, H.CN, 0)]))
        bad[17] = H.BSS          # header 8 + start 4 + end 4 + lane 1
        with self.assertRaisesRegex(ValueError, 'Invalid hold'):
            H.unpack(bytes(bad))

    def test_native_events_and_totals(self):
        ev = [(0, 0, 0, 500), (10, 0, 1, 0), (0, 4, 100, 15000)]
        holds = H.from_events(ev)
        self.assertEqual(H.strip_lengths(ev), [(0, 0, 0, 0), (10, 0, 1, 0), (0, 4, 100, 15000)])
        self.assertEqual(H.note_total(ev, holds), 3)

    def test_bms_long_notes_and_lnmode(self):
        chart = bms.parse('#BPM 120\n#LNMODE 3\n#WAV01 a.wav\n#00051:01000100\n#00056:0001\n#00156:01\n')
        holds = H.from_events(chart.events, hell=chart.header.get('LNMODE') == '3')
        # BPM 120: a 16th is 125 ms
        self.assertEqual(holds, [(0, 1000, 0, H.HCN, 125 * 64), (1000, 2000, 7, H.HBSS, 125 * 64)])
        with self.assertRaisesRegex(ValueError, 'tick64'):
            H.unpack(H.pack([(0, 10, 0, H.HCN, 0)]))


@unittest.skipUnless(shutil.which('cc') or shutil.which('gcc') or shutil.which('clang'), 'no C compiler')
class RulesCore(unittest.TestCase):
    def test_c_core(self):
        cc = shutil.which('cc') or shutil.which('gcc') or shutil.which('clang')
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / 'test_holds'
            subprocess.run([cc, '-std=c99', '-Wall', '-Wextra', '-Werror', '-o', str(exe),
                            str(ROOT / 'test_holds.c'), str(ROOT / 'holds.c')], check=True, capture_output=True)
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)


if __name__ == '__main__':
    unittest.main()
