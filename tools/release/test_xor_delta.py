"""Byte-exact sparse audio masks, including shifted/padded edges and corruption."""
import io,random,unittest,zlib
from xor_delta import difference,decode


class XorDelta(unittest.TestCase):
    def test_shift_edges_and_large_blocks(self):
        r=random.Random(57)
        for source_size,target_size in [(0,40),(100,20),(20,100),(1300000,1200000)]:
            source=r.randbytes(source_size);target=r.randbytes(target_size)
            for shift in [-101,-1,0,1,104]:
                p=difference(target,source,shift)
                self.assertEqual(b''.join(decode(p,io.BytesIO(source),len(source),len(target),shift)),target)
    def test_adpcm_header_mask(self):
        source=bytearray(random.Random(7).randbytes(1024*1024));target=bytearray(source)
        for i in range(0,len(target),16):target[i]^=255;target[i+2]=(target[i+2]-0x24)&255
        p=difference(target,source);self.assertLess(len(p),len(source)//10)
        self.assertEqual(b''.join(decode(p,io.BytesIO(source),len(source),len(target))),target)
        for broken in [p[:-1],p+b'trailing']:
            with self.assertRaises(ValueError):b''.join(decode(broken,io.BytesIO(source),len(source),len(target)))
        with self.assertRaises(ValueError):b''.join(decode(p,io.BytesIO(source),len(source),1))


if __name__=='__main__':unittest.main()
