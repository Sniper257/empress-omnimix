"""IFS reading and arcade -> EMPRESS chart/stream conversion on synthetic data."""
import struct
import unittest

import arcade_convert as C
import holds_format as H
import ifs

try:
    import numpy  # noqa: F401
    HAVE_NUMPY = True
except ImportError:
    HAVE_NUMPY = False


def sixbit(name):
    bits = 0
    for ch in name:
        bits = bits << 6 | ifs.SIXBIT.index(ch)
    n = (len(name) * 6 + 7) // 8
    return bytes([len(name)]) + (bits << (n * 8 - len(name) * 6)).to_bytes(n, 'big')


def tiny_ifs(files):
    """IFS v1 with one directory node holding files {escaped name: bytes}."""
    nodes = bytearray([1]) + sixbit('imgfs') + bytes([1]) + sixbit('song')
    data = bytearray()
    blobs = b''
    for name, body in files.items():
        nodes += bytes([31]) + sixbit(name) + bytes([190])
        data += struct.pack('>III', len(blobs), len(body), 0)
        blobs += body
    nodes += bytes([190, 190, 191])
    while len(nodes) % 4:
        nodes.append(0)
    kbin = bytes([0xa0, 0x42, 0x80, 0x7f]) + struct.pack('>I', len(nodes)) + nodes + struct.pack('>I', len(data)) + data
    start = 20 + len(kbin)
    return struct.pack('>IHHIII', ifs.MAGIC, 1, 0xfffe, 0, len(kbin), start) + kbin + blobs


class Ifs(unittest.TestCase):
    def test_manifest_and_names(self):
        files = ifs.read(tiny_ifs({'_23069_E1': b'chart', '_23069__pre_E2dx': b'pre'}))
        self.assertEqual(files, {'song/23069.1': b'chart', 'song/23069_pre.2dx': b'pre'})

    def test_rejects_other_files(self):
        with self.assertRaises(ifs.FormatError):
            ifs.read(b'\0' * 64)
        raw = bytearray(tiny_ifs({'_1_E1': b'x' * 8}))
        raw = raw[:-4]
        with self.assertRaisesRegex(ifs.FormatError, 'exceeds'):
            ifs.read(bytes(raw))


def parse(ply):
    off, clock = struct.unpack_from('<II', ply)
    ev = []
    at = off
    while True:
        e = struct.unpack_from('<IBBH', ply, at)
        if e[0] == C.END:
            return clock, ev, ply[at + 8:]
        ev.append(e)
        at += 8


class Chart(unittest.TestCase):
    def test_event_encoding(self):
        events = [(0, 4, 1, 300), (0, 5, 4, 4), (0, 7, 0, 5), (500, 7, 0, 6), (0, 16, 0, 2), (0, 8, 2, 255),
                  (1000, 2, 3, 12), (1000, 0, 3, 0), (2000, 1, 7, 0), (0, 12, 1, 0), (3000, 6, 0, 0)]
        ply, warnings = C.empress_events(events)
        clock, ev, rest = parse(ply)
        self.assertEqual((clock, rest, warnings), (16683, b'', []))
        self.assertIn((0, 4 | 1 << 4, 300 & 0xff, 0), ev)          # BPM 300 = 0x12c: high nibble 1, c2 0x2c
        self.assertEqual([e for e in ev if e[1] & 15 == 7], [(0, 7, 0, 1)])   # one stream start, BGM dropped
        self.assertIn((60, 0 | 3 << 4, 0, 0), ev)                  # 1000 ms = 60 ticks, column 3
        self.assertIn((60, 2 | 3 << 4, 0, 0), ev)                  # keysound slot 0 until banks exist
        self.assertIn((120, 1 | 7 << 4, 0, 0), ev)                 # P2 scratch
        self.assertIn((0, 8 | 2 << 4, 255, 0), ev)
        self.assertEqual(ev[-1][1] & 15, 6)
        self.assertFalse(any(e[1] & 15 == 0 and e[1] >> 4 == 0 and e[0] == 0 for e in ev))
        self.assertEqual(sum(1 for e in ev if e[1] == 16), 0)

    def test_hold_trailer_follows_terminator(self):
        events = [(0, 4, 1, 120), (1000, 0, 2, 500), (2000, 6, 0, 0)]
        holds = H.from_events(events, hell=True, time=C.tick, ticks_per_ms=1000 / C.CLOCK)
        ply, _ = C.empress_events(events, holds)
        _, ev, rest = parse(ply)
        self.assertEqual(H.unpack(rest), holds)
        self.assertEqual([e for e in ev if e[1] & 15 == 0], [(60, 2 << 4, 0, 0), (90, 2 << 4, 0, 0)])   # head + tail note
        self.assertEqual(holds[0][:4], (60, 90, 2, H.HCN))
        _, out, _ = parse(ply)
        self.assertIn((90, 0x20, 0, 0), out)                    # the tail note, P1 lane 2
        ply, _ = C.empress_events(events, holds, tail_notes=False)
        self.assertNotIn((90, 0x20, 0, 0), parse(ply)[1])

    def test_keysound_slots_and_bgm_only(self):
        import keysound_bank as KB
        ev = [(0, 2, 1, 7), (0, 2, 3, 9), (500, 0, 1, 0), (900, 2, 1, 9), (1000, 0, 1, 0), (0, 7, 0, 5)]
        self.assertEqual(KB.player_sounds(ev), [7, 9])
        slots = KB.slot_map(KB.player_sounds(ev))
        self.assertEqual(slots, {7: 1, 9: 2})
        ply, _ = C.empress_events(KB.remap(ev, slots), [], keysounds=True)
        _, out, _ = parse(ply)
        self.assertEqual(sorted(e[3] for e in out if e[1] & 15 == 2), [1, 2, 2])
        ply, _ = C.empress_events(ev, [])
        self.assertEqual({e[3] for e in parse(ply)[1] if e[1] & 15 == 2}, {0})

    def test_bgm_from_bank(self):
        import keysound_bank as KB
        ev = [(0, 7, 0, 5), (0, 2, 1, 7), (500, 7, 0, 7), (1000, 0, 1, 0)]
        sounds = KB.player_sounds(ev) + [v for v in KB.bgm_sounds(ev) if v not in KB.player_sounds(ev)]
        slots = KB.slot_map(sounds)
        self.assertEqual(slots, {7: 1, 5: 2})
        ply, _ = C.empress_events(KB.remap(ev, slots, bgm=True), [], keysounds=True, bgm_slots=True)
        bgm = [e for e in parse(ply)[1] if e[1] & 15 == 7]
        self.assertEqual([(e[1], e[3]) for e in bgm], [(0x87, 2), (0x87, 1)])

    def test_bank_round_trip(self):
        import io, wave, math
        import keysound_bank as KB
        def wav(rate, n):
            b = io.BytesIO()
            with wave.open(b, 'wb') as w:
                w.setnchannels(2); w.setsampwidth(2); w.setframerate(rate)
                w.writeframes(b''.join(struct.pack('<hh', int(9000 * math.sin(i / 5)), int(-9000 * math.sin(i / 5))) for i in range(n)))
            return b.getvalue()
        keys = [{'wav': wav(44100, 4410)}, {'wav': wav(24000, 100)}, {'wav': wav(22050, 2205)}]
        data, slots, info = KB.build('SP', [2, 1, 3], keys)
        self.assertEqual(slots, {2: 1, 1: 2, 3: 3})
        self.assertEqual(data[:16], b'wp\x03\x00\x10\x00\x00\x00SP' + bytes(6))
        b = KB.read(data)
        self.assertEqual(b['name'], 'SP')
        self.assertEqual([b['slots'][n]['sample'] for n in (1, 2, 3)], [0, 1, 2])
        self.assertEqual(b['slots'][2]['ms'], 100)                       # 4410 samples at 44.1 kHz
        self.assertEqual([x['rate'] for x in b['samples']], [24000, 44100, 44100])   # 22050 resampled
        for x in b['samples']:
            for ch in x['frames']:
                self.assertEqual(ch[:16], bytes(16))
                self.assertEqual(ch[-16:], bytes([0, 7]) + b'\x77' * 14)
                self.assertEqual(ch[-31], 1)                              # flag 1 on the last audio frame
        self.assertEqual(b['slots'][1]['tail'], 76)

    def test_hcn_flag_event(self):
        # 18102 Kailua (HCN Ver.) = 18021's notes plus (0, 13, 0, 0) in every chart
        plain = [(0, 4, 1, 146), (1000, 0, 2, 500), (2000, 6, 0, 0)]
        self.assertFalse(C.is_hell(plain))
        self.assertTrue(C.is_hell([(0, 13, 0, 0)] + plain))
        _, warnings = C.empress_events([(0, 13, 0, 0)] + plain, [])
        self.assertEqual(warnings, [])

    def test_fractional_bpm_warns_and_long_chart_fails(self):
        _, warnings = C.empress_events([(0, 4, 100, 15050)])
        self.assertTrue(warnings)
        with self.assertRaisesRegex(ValueError, 'longer'):
            C.empress_events([(10 * 60 * 1000, 0, 0, 0)])

    @unittest.skipUnless(HAVE_NUMPY, 'numpy not installed')
    def test_render_places_note_and_bgm_sounds(self):
        import arcade_data as A
        from test_sirius_import import pcm_wav
        keys = [{'wav': pcm_wav([1000] * 10)}, {'wav': pcm_wav([-500] * 5)}]
        events = [(0, 2, 0, 1), (100, 0, 0, 0), (200, 7, 0, 2)]
        rate, (left, right), info = C.render(events, keys)
        at = 100 * 44100 // 1000
        self.assertEqual((left[at], left[at + 9], left[at + 10]), (1000, 1000, 0))
        self.assertEqual(left[200 * 44100 // 1000], -500)
        self.assertEqual((left, info['keysounds_played']), (right, 2))
        self.assertTrue(A)


if __name__ == '__main__':
    unittest.main()
