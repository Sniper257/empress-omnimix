import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import konami_tex as K  # noqa: E402


class Lz77(unittest.TestCase):
    def test_literals_and_back_reference(self):
        # flags 0x07: three literals, then a reference (offset 3, length 3), then the 0 end word
        self.assertEqual(K.lz77(bytes([0x07]) + b'abc' + b'\x00\x30' + b'\x00\x00'), b'abcabc')

    def test_unpack_stored_and_packed(self):
        raw = b'abcabc'
        packed = bytes([0x07]) + b'abc' + b'\x00\x30\x00\x00'
        self.assertEqual(K.unpack(struct.pack('>II', 6, len(packed)) + packed), raw)
        self.assertEqual(K.unpack(struct.pack('>II', 6, 6) + raw), raw)

    def test_argb8888rev_to_rgba(self):
        img = {'name': 't', 'format': 'argb8888rev', 'size': [1, 1], 'data': bytes([1, 2, 3, 4])}
        self.assertEqual(K.to_rgba(img), bytes([3, 2, 1, 4]))


if __name__ == '__main__':
    unittest.main()
