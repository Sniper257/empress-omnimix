"""BMS reader timing, notes and probe pairing on synthetic charts."""
import io
import json
import struct
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path

import arcade_data as A
import bms

BASE = '''#PLAYER 1
#TITLE Test [A]
#BPM 120
#WAV01 a.wav
#WAV02 b.wav
#BPM0A 240
#STOP01 48
'''


def notes(chart):
    return [e for e in chart.events if e[1] in (0, 1)]


class Timing(unittest.TestCase):
    def test_bpm_stop_and_measure_length(self):
        c = bms.parse(BASE + '#00011:0101\n#00008:000A\n#00109:01\n#00111:01\n#00116:0001\n#00202:0.5\n#00211:01\n#00301:01\n')
        self.assertEqual([e[0] for e in notes(c)], [0, 1000, 1500, 2250, 2750])
        self.assertIn((1000, 4, 100, 24000), c.events)
        self.assertIn((2750, 5, 2, 1), c.events)
        self.assertEqual([e for e in c.events if e[1] == 6], [(3250, 6, 0, 0)])
        self.assertEqual((c.notes, c.difficulty), ((5, 0), 'SPA'))

    def test_keysound_change_follows_previous_note(self):
        c = bms.parse(BASE + '#00011:01020100\n')
        changes = [e for e in c.events if e[1] == 2]
        self.assertEqual(changes, [(0, 2, 0, 1), (1, 2, 0, 2), (501, 2, 0, 1)])

    def test_long_notes_lntype_and_lnobj(self):
        c = bms.parse(BASE + '#00051:01000100\n')
        self.assertEqual(notes(c), [(0, 0, 0, 1000)])
        c = bms.parse(BASE + '#LNOBJ 02\n#00012:0102\n')
        self.assertEqual(notes(c), [(0, 0, 1, 1000)])
        with self.assertRaisesRegex(A.FormatError, 'Unterminated'):
            bms.parse(BASE + '#00051:01\n')

    def test_random_takes_first_branch_and_dp_columns(self):
        c = bms.parse(BASE + '#RANDOM 2\n#IF 1\n#00011:01\n#ENDIF\n#IF 2\n#00012:01\n#ENDIF\n#ENDRANDOM\n#00026:01\n#00029:01\n')
        self.assertTrue(c.random)
        self.assertEqual(sorted((e[1], e[2]) for e in notes(c)), [(0, 0), (1, 6), (1, 7)])
        self.assertEqual(c.difficulty, 'DPA')

    def test_difficulty_header_and_ignored_channels(self):
        c = bms.parse('#DIFFICULTY 3\n#TITLE x\n#00031:01\n#000D1:01\n#00011:01\n')
        self.assertEqual((c.difficulty, c.ignored), ('SPH', {'31': 1, 'D1': 1}))


class Audio(unittest.TestCase):
    def wav(self, tag, bits, data, ch=1):
        fmt = struct.pack('<HHIIHH', tag, ch, 44100, 0, ch * bits // 8, bits)
        return b'RIFF' + struct.pack('<I', 36 + len(data)) + b'WAVEfmt ' + struct.pack('<I', 16) + fmt + b'data' + struct.pack('<I', len(data)) + data

    def test_pcm_variants(self):
        self.assertEqual(A.decode_wav(self.wav(1, 8, bytes([128, 255, 0])))[1], [[0, 127 << 8, -32768]])
        self.assertEqual(A.decode_wav(self.wav(1, 24, b'\x00\x00\x80\xff\xff\x7f'))[1], [[-32768, 32767]])
        self.assertEqual(A.decode_wav(self.wav(3, 32, struct.pack('<2f', 1.0, -0.5)))[1], [[32767, -16384]])

    def test_find_audio_other_extension_and_case(self):
        with tempfile.TemporaryDirectory() as tmp:
            (Path(tmp) / 'Kick.OGG').write_bytes(b'x')
            self.assertEqual(bms.find_audio(tmp, 'kick.wav').name, 'Kick.OGG')
            self.assertIsNone(bms.find_audio(tmp, 'snare.wav'))


class ProbeBms(unittest.TestCase):
    def test_pairs_bms_song_with_empress_by_note_counts(self):
        from chart_lz import lz_encode_opt
        from chart_transform import crypt
        import sirius_probe
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            song = tmp / 'bms/Bad Maniacs'
            song.mkdir(parents=True)
            (song / 'a.wav').write_bytes(Audio().wav(1, 16, bytes(8)))
            hyper = ''.join('#%03d11:01010101\n' % m for m in range(5))      # 20 notes
            another = ''.join('#%03d11:0101010101010101\n' % m for m in range(5))  # 40 notes
            (song / 'h.bme').write_text('#TITLE Bad Maniacs [H]\n#BPM 150\n#WAV01 a.wav\n' + hyper, encoding='cp932')
            (song / 'a.bme').write_text('#TITLE Bad Maniacs [A]\n#BPM 150\n#WAV01 a.wav\n' + another, encoding='cp932')
            mm = tmp / 'mm'
            (mm / 'a/00').mkdir(parents=True)
            lines = []
            for i, chart in enumerate(('h.bme', 'a.bme')):
                ev = [e for e in bms.read(song / chart).events if e[1] == 0]
                ply = struct.pack('<II', 8, 16683) + b''.join(struct.pack('<IBBH', round(t * 1000 / 16683), k, p, v) for t, k, p, v in ev) + struct.pack('<I', 0x7fff)
                (mm / ('a/00/0%d' % i)).write_bytes(crypt(lz_encode_opt(ply), 'empress'))
                lines.append('E 1 1 9 a/00/0%d' % i)
            folder = mm / 'songs/empress/id_97'
            folder.mkdir(parents=True)
            (folder / 'song.txt').write_text('name Bad Maniacs\nid 97\n' + '\n'.join(lines) + '\n')
            out = tmp / 'r.json'
            with redirect_stdout(io.StringIO()):
                sirius_probe.main(['--bms', str(tmp / 'bms'), str(mm), '-o', str(out)])
            r = json.loads(out.read_text(encoding='utf-8'))
            self.assertEqual(r['difficulties'], {'SPA': 1, 'SPH': 1})
            (pair,) = r['pairs']
            self.assertEqual((pair['empress_folder'], pair['bms_folder']), ('empress/id_97', 'Bad Maniacs'))
            self.assertEqual(sorted(m['notes'] for m in pair['matches']), [20, 40])
            for m in pair['matches']:
                self.assertAlmostEqual(m['fit']['scale'], 1000 / 16683, places=3)
            self.assertEqual(pair['bms']['keysounds']['missing'], 0)


if __name__ == '__main__':
    unittest.main()
