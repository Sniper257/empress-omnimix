"""Synthetic-data tests for the SIRIUS arcade readers, SPU-ADPCM codec and probe."""
import io
import json
import math
import struct
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path

import arcade_data as A
import spu_adpcm as S


def one_file(charts):
    """Build a .1 container from {slot: [(time, type, param, value)]}."""
    head = bytearray(A.CHART_SLOTS * 8)
    body = bytearray()
    for slot, events in sorted(charts.items()):
        data = b''.join(struct.pack('<iBBh', *e) for e in events) + struct.pack('<iBBh', A.END_TIME, 0, 0, 0)
        struct.pack_into('<II', head, slot * 8, len(head) + len(body), len(data))
        body += data
    return bytes(head + body)


def pcm_wav(samples, rate=44100, channels=1):
    data = struct.pack('<%dh' % len(samples), *samples)
    fmt = struct.pack('<HHIIHH', 1, channels, rate, rate * 2 * channels, 2 * channels, 16)
    return b'RIFF' + struct.pack('<I', 4 + 8 + len(fmt) + 8 + len(data)) + b'WAVE' + b'fmt ' + struct.pack('<I', len(fmt)) + fmt + b'data' + struct.pack('<I', len(data)) + data


def msadpcm_wav(blocks, per_block):
    coefs = [(256, 0), (512, -256), (0, 0), (192, 64), (240, 0), (460, -208), (392, -232)]
    extra = struct.pack('<HH', per_block, len(coefs)) + b''.join(struct.pack('<hh', *c) for c in coefs)
    fmt = struct.pack('<HHIIHHH', 2, 1, 44100, 22050, len(blocks[0]), 4, len(extra)) + extra
    data = b''.join(blocks)
    return b'RIFF' + struct.pack('<I', 4 + 8 + len(fmt) + 8 + len(data)) + b'WAVE' + b'fmt ' + struct.pack('<I', len(fmt)) + fmt + b'data' + struct.pack('<I', len(data)) + data


def twodx(wavs):
    head = bytearray(b'test'.ljust(16, b'\0')) + struct.pack('<II', 0x48 + 4 * len(wavs), len(wavs)) + bytes(48)
    offsets = []
    body = bytearray()
    base = 0x48 + 4 * len(wavs)
    for w in wavs:
        offsets.append(base + len(body))
        body += b'2DX9' + struct.pack('<IIHhhhi', 24, len(w), 0x3231, -1, 64, 1, 0) + w
    return bytes(head + struct.pack('<%dI' % len(wavs), *offsets) + body)


class ArcadeReaders(unittest.TestCase):
    def test_chart_container_round_trip(self):
        raw = one_file({0: [(0, 4, 1, 150), (100, 0, 3, 0), (250, 1, 7, 40)], 6: [(5, 7, 0, 2)]})
        self.assertEqual([r[0] for r in A.chart_directory(raw)], [0, 6])
        charts = A.read_charts(raw)
        self.assertEqual(charts[0][2], (250, 1, 7, 40))
        self.assertEqual(A.note_count(charts[0]), (1, 1))

    def test_chart_without_terminator_rejected(self):
        raw = bytearray(one_file({0: [(0, 0, 0, 0)]}))
        struct.pack_into('<i', raw, len(raw) - 8, 9)
        with self.assertRaises(A.FormatError):
            A.read_charts(bytes(raw))

    def test_2dx_entries_and_pcm(self):
        name, entries = A.read_2dx(twodx([pcm_wav([1, -2, 3]), pcm_wav([7] * 4, 22050)]))
        self.assertEqual((name, len(entries), entries[1]['attenuation']), ('test', 2, 1))
        self.assertEqual(A.decode_wav(entries[0]['wav']), (44100, [[1, -2, 3]]))
        self.assertEqual(A.decode_wav(entries[1]['wav'])[0], 22050)

    def test_encrypted_2dx_reports_format(self):
        raw = bytearray(twodx([pcm_wav([0])]))
        raw[0x4c:0x50] = b'XXXX'
        with self.assertRaisesRegex(A.FormatError, '2DX9'):
            A.read_2dx(bytes(raw))

    def test_msadpcm_follows_adaptation(self):
        # predictor 0 = (256, 0): each +1 nibble adds the current delta (floor 16)
        block = bytes([0]) + struct.pack('<hhh', 16, 100, 90) + bytes([0x11] * 9)
        rate, (ch,) = A.decode_wav(msadpcm_wav([block], 20))
        self.assertEqual(ch[:4], [90, 100, 116, 132])
        self.assertEqual(len(ch), 20)

    def test_music_data_layout(self):
        entry = lambda title, sid: title.encode('cp932').ljust(64, b'\0') + b'ASCII'.ljust(64, b'\0') + struct.pack('<I', sid) + bytes(60)
        index = [-1] * 17003
        index[17000], index[17002] = 0, 1
        raw = b'IIDX' + struct.pack('<IHHI', 17, 2, len(index), 0) + struct.pack('<%dh' % len(index), *index) + entry('曲', 17000) + entry('B', 17002)
        md = A.read_music_data(raw)
        self.assertEqual((md['entry_size'], md['index'][17002]), (192, 1))
        self.assertEqual(A.entry_strings(md['entries'][0])[:2], ['曲', 'ASCII'])
        self.assertEqual(A.find_id_fields(md['entries'], [17000, 17002])[0], ((128, 4), 2))


class Adpcm(unittest.TestCase):
    def test_encode_decode_quality(self):
        x = [int(9000 * math.sin(i / 5) + 3000 * math.sin(i / 1.7)) for i in range(28 * 60)]
        coded = S.encode(x)
        self.assertEqual(coded[-15], S.FLAG_END)
        y = S.decode(coded)[:len(x)]
        rms = math.sqrt(sum((a - b) ** 2 for a, b in zip(x, y)) / len(x))
        self.assertLess(rms, 120)

    def test_silence_is_exact(self):
        self.assertEqual(S.decode(S.encode([0] * 56, end_flags=0)), [0] * 56)

    def test_disguise_matches_audio_transform_rule(self):
        from audio_transform import frames
        raw = bytes(range(64))
        self.assertEqual(S.disguise(raw, S.BANK_DELTA), bytes(frames(raw, 0, 64, 36)))
        self.assertEqual(S.disguise(S.disguise(raw, 2), 2, reverse=True), raw)

    def test_stream_header_matches_native_layout(self):
        out = S.stream([[0] * 56, [1000] * 56], 44100)
        self.assertEqual(struct.unpack_from('<12I', out)[:8], (S.STREAM_MAGIC, 0, 0x800, 64, 0, 0, 44100, 2))
        self.assertEqual(S.plausibility(out[0x800:], S.STREAM_DELTA)[0], 1.0)

    def test_resample_length(self):
        self.assertEqual(len(S.resample(list(range(441)), 44100, 22050)), 221)


class Probe(unittest.TestCase):
    def test_probe_recovers_time_scale_and_slot(self):
        from chart_lz import lz_encode_opt
        from chart_transform import crypt
        import sirius_probe
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            arc = [(1000 + 250 * i, i % 2, i % 8, 0) for i in range(40)] + [(0, 7, 0, 1)]
            arc.sort()
            sound = tmp / 'data/sound/17010'
            sound.mkdir(parents=True)
            (sound / '17010.1').write_bytes(one_file({1: arc, 2: arc[:10]}))
            (sound / '17010.2dx').write_bytes(twodx([pcm_wav([0] * 10)]))
            info = tmp / 'data/info'
            info.mkdir()
            index = [-1] * 17011
            index[17010] = 0
            entry = b'TEST SONG'.ljust(64, b'\0') * 2 + struct.pack('<I', 17010) + bytes(60)
            (info / 'music_data.bin').write_bytes(b'IIDX' + struct.pack('<IHHI', 17, 1, len(index), 0) + struct.pack('<%dh' % len(index), *index) + entry)
            # EMPRESS: same notes at 60 ticks per arcade ms unit / 1000 -> frames
            ply = struct.pack('<II', 8, 16683) + b''.join(struct.pack('<IBBH', round(t * 1000 / 16683), k, p, v) for t, k, p, v in arc if k != 7) + struct.pack('<IBBH', 0, 7, 0, 1) + struct.pack('<I', 0x7fff)
            mm = tmp / 'mm'
            (mm / 'a/00').mkdir(parents=True)
            (mm / 'a/00/01').write_bytes(crypt(lz_encode_opt(ply), 'empress'))
            short = struct.pack('<II', 8, 16683) + b''.join(struct.pack('<IBBH', round(t * 1000 / 16683), k, p, v) for t, k, p, v in arc[:10] if k != 7) + struct.pack('<I', 0x7fff)
            (mm / 'a/00/03').write_bytes(crypt(lz_encode_opt(short), 'empress'))
            bank = bytearray(0x8030)
            bank[:4] = b'wp\x03\x00'
            struct.pack_into('<4I', bank, 0x8000, 1, 32, 1, 0)
            bank[0x8020:] = S.disguise(S.encode([5] * 56, end_flags=0), S.BANK_DELTA)
            (mm / 'a/00/02').write_bytes(bank)
            song = mm / 'songs/empress/id_97'
            song.mkdir(parents=True)
            (song / 'song.txt').write_text('name TEST SONG\nid 97\nE 1 1 9 a/00/01\nE 2 1 9 a/00/02\nE 3 1 9 a/00/03\n')
            out = tmp / 'r.json'
            with redirect_stdout(io.StringIO()):
                sirius_probe.main([str(tmp / 'data'), str(mm), '-o', str(out)])
            r = json.loads(out.read_text(encoding='utf-8'))
            self.assertEqual(r['music_data']['entry_size'], 192)
            pair = r['pairs'][0]
            self.assertEqual((pair['empress_folder'], pair['sirius_id']), ('empress/id_97', 17010))
            m = next(x for x in pair['matches'] if x['notes'] == 40)
            self.assertEqual(sorted(x['arcade_slot'] for x in pair['matches']), [1, 2])
            self.assertEqual((m['arcade_slot'], m['notes'], m['same_lanes']), (1, 40, 40))
            self.assertAlmostEqual(m['fit']['scale'], 1000 / 16683, places=4)
            bank_row = next(f for f in pair['empress']['files'] if f['kind'] == 'bank')
            self.assertEqual(bank_row['summary']['adpcm_bank_delta']['legal'], 1.0)
            # Without a song list, songs are paired by their chart note counts.
            (info / 'music_data.bin').unlink()
            with redirect_stdout(io.StringIO()):
                sirius_probe.main([str(tmp / 'data'), str(mm), '-o', str(out)])
            r = json.loads(out.read_text(encoding='utf-8'))
            self.assertEqual(r['pairs'][0]['sirius_id'], 17010)
            self.assertEqual(r['count_pairing'], {'empress_songs_with_charts': 1, 'matched': 1})
            self.assertIn('layout', r)

    def test_locate_flat_sd_data_and_preview(self):
        import sirius_probe
        with tempfile.TemporaryDirectory() as tmp:
            data = Path(tmp)
            (data / 'sd_data').mkdir()
            (data / 'info').mkdir()
            for name in ('17010.1', '17010.2dx', '17010_pre.2dx', 'readme.txt'):
                (data / 'sd_data' / name).write_bytes(b'x')
            (data / 'info' / 'music_data.bin').write_bytes(b'x')
            files, music = sirius_probe.locate(data)
            self.assertEqual(sorted(files[17010]), ['.1', '.2dx', '_pre.2dx'])
            self.assertEqual(music.name, 'music_data.bin')

    def test_encrypted_2dx_and_decrypted_override(self):
        import sirius_probe
        with tempfile.TemporaryDirectory() as tmp:
            data = Path(tmp) / 'data'
            (data / 'sd_data/0100').mkdir(parents=True)
            (data / 'sd_data/0100/0100.1').write_bytes(one_file({0: [(0, 0, 0, 0)]}))
            (data / 'sd_data/0100/0100.2dx').write_bytes(b'%hid' + bytes(60))
            files, _ = sirius_probe.locate(data)
            self.assertIn('encrypted', sirius_probe.sirius_song(files, 100)[0]['keysounds']['error'])
            plain = Path(tmp) / 'plain'
            plain.mkdir()
            (plain / '0100.2dx').write_bytes(twodx([pcm_wav([0] * 4)]))
            files, _ = sirius_probe.locate(data, keysounds=plain)
            self.assertEqual(sirius_probe.sirius_song(files, 100)[0]['keysounds']['count'], 1)
            mm = Path(tmp) / 'mm'
            mm.mkdir()
            out = Path(tmp) / 'r.json'
            with redirect_stdout(io.StringIO()):
                sirius_probe.main([str(data), str(mm), '-o', str(out)])
            r = json.loads(out.read_text(encoding='utf-8'))
            self.assertEqual(r['megamix_diagnostics']['song_txt_found'], 0)
            self.assertEqual(r['survey']['keysound_errors'], {'encrypted .2dx (%hid)': 1})

    def test_no_song_files_prints_layout(self):
        import sirius_probe
        with tempfile.TemporaryDirectory() as tmp:
            (Path(tmp) / 'other').mkdir()
            (Path(tmp) / 'other' / 'a.ifs').write_bytes(b'x')
            buf = io.StringIO()
            with redirect_stdout(buf), self.assertRaises(SystemExit):
                sirius_probe.main([tmp, tmp])
            self.assertIn('.ifs', buf.getvalue())


if __name__ == '__main__':
    unittest.main()
