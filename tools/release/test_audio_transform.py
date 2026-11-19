import struct,unittest,io
from audio_transform import convert,legacy_bank_recipe,assemble_bank

class AudioTests(unittest.TestCase):
    def test_bank_matches_independent_frame_loop(self):
        raw=bytearray(0x8020+64);raw[:4]=b'wp\x02\x00';struct.pack_into('<2I',raw,0x8000,1,64);raw[0x8020:]=bytes(range(64))
        wanted=bytearray(raw);wanted[2]=3;struct.pack_into('<I',wanted,0x8008,1)
        for at in range(0x8020,len(raw),16):wanted[at]^=255;wanted[at+2]=(wanted[at+2]-36)&255
        self.assertEqual(convert('adpcm-bank-v3',raw,len(raw)),wanted)
    def test_native_bank_with_frozen_metadata_override(self):
        raw=bytearray(0x8020+64);raw[:4]=b'wp\x02\x00';struct.pack_into('<2I',raw,0x8000,1,64);raw[0x8020:]=bytes(range(64))
        target=bytearray(convert('adpcm-bank-v3',raw,len(raw)));target[8:10]=bytes([63,63])
        prefix,spans=legacy_bank_recipe(raw,target);self.assertEqual(spans,[[0x8020,64]])
        self.assertEqual(b''.join(assemble_bank(prefix,io.BytesIO(raw),spans,len(target))),target)

    def test_stereo_stream_orders_left_right_per_frame(self):
        raw=bytearray(0x1800);struct.pack_into('>IHHB',raw,0,0x1000,0x1234,48000,2)
        for c in range(2):
            for i in range(128):raw[0x800+c*0x800+i*16:0x810+c*0x800+i*16]=bytes([i+c*128])*16
        out=convert('eighth-stream-v3',raw,len(raw));self.assertEqual(struct.unpack_from('<12I',out),(0x08640001,0,0x800,0x1000,0,0,48000,2,1,16,0x1234,1))
        for i in range(128):
            for c in range(2):
                at=0x800+i*32+c*16;v=i+c*128;self.assertEqual(out[at:at+16],bytes([v^255,v,(v-2)&255]+[v]*13))
    def test_old_bank_spans_reproduce_frozen_sample_bytes(self):
        raw=bytearray(0x4040);struct.pack_into('<3I',raw,0,0x4000,0,0)
        struct.pack_into('<HHBBBB6I',raw,32,1,0,0,127,64,3,48000,0,0,0,64,0);raw[0x4000:]=bytes(range(64))
        target=bytearray(0x8020+64);target[:4]=b'wp\x03\x00';struct.pack_into('<4I',target,0x8000,1,64,1,0)
        data=bytearray(range(64))
        for at in range(0,64,16):data[at]^=255;data[at+2]=(data[at+2]-36)&255
        target[0x8020:]=data;prefix,spans=legacy_bank_recipe(raw,target)
        self.assertEqual(spans,[[0x4000,64]]);self.assertEqual(b''.join(assemble_bank(prefix,io.BytesIO(raw),spans,len(target))),target)

    def test_native_stream_and_rejected_lengths(self):
        raw=bytearray(0x820);struct.pack_into('<I',raw,0,0x08640001);struct.pack_into('<I',raw,12,32)
        out=convert('adpcm-stream-v3',raw,len(raw));self.assertEqual(out[0x800:0x803],bytes([255,0,254]));self.assertEqual(out[0x20],1)
        struct.pack_into('<I',raw,12,64)
        with self.assertRaises(ValueError):convert('adpcm-stream-v3',raw,len(raw))
        with self.assertRaises(ValueError):convert('unknown',raw,len(raw))

if __name__=='__main__':unittest.main()
