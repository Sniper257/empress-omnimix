"""Synthetic resources only; no retail fixture is needed for these checks."""
import copy,hashlib,lzma,struct,unittest
import iso_launcher as L

class Launcher(unittest.TestCase):
 def setUp(self):
  ident=b'\x7fELF\x01\x01\x01'+bytes(9)
  head=struct.pack('<16sHHIIIIIHHHHHH',ident,2,8,1,0xa00000,52,0,0,52,32,1,0,0,0)
  ph=struct.pack('<8I',1,128,0xa00000,0xa00000,128,160,7,128)
  self.template=head+ph+bytes(128-len(head)-len(ph))+bytes(128)
  raw=b'TEST';out=bytearray(self.template);out[160:164]=raw
  self.output=bytes(out);self.spec=dict(format='megamix/iso-launcher-template-v1',template_sha256=L.digest(self.template),unpacked_sha256=L.digest(out),spans=[dict(at=160,bytes=4,iso_offset=32,source_bytes=4,source_sha256=L.digest(raw),sha256=L.digest(raw),transform='copy')]);self.read=lambda a,n:raw
 def test_exact_fill(self):self.assertEqual(L.fill(self.template,self.spec,self.read),self.output)
 def test_wrong_resource(self):
  with self.assertRaisesRegex(ValueError,'resource hash'):L.fill(self.template,self.spec,lambda a,n:b'FAIL')
 def test_wrong_template(self):
  with self.assertRaisesRegex(ValueError,'template hash'):L.fill(self.template[:-1],self.spec,self.read)
 def test_overlap(self):
  s=copy.deepcopy(self.spec);s['spans']*=2
  with self.assertRaisesRegex(ValueError,'overlapping'):L.fill(self.template,s,self.read)
 def test_outside_loaded_image(self):
  s=copy.deepcopy(self.spec);s['spans'][0]['at']=90
  with self.assertRaisesRegex(ValueError,'range'):L.fill(self.template,s,self.read)
 def test_packed_memory_and_bss(self):
  stub=b'LZMA'+bytes(60);spec=dict(stub_sha256=L.digest(stub),stub_base=0x1d00000,stub_entry=0x1d0001c,stub_zero=32)
  b=L.pack(self.output,stub,spec);header,ph=L.load_segments(b);self.assertEqual(header[4],spec['stub_entry'])
  entry,count,n,zero,destination,size=struct.unpack_from('<6I',b,ph[1]);stream=b[ph[1]+24:ph[1]+24+size];raw=lzma.decompress(stream[5:],format=lzma.FORMAT_RAW,filters=[dict(id=lzma.FILTER_LZMA1,dict_size=1<<21,lc=3,lp=0,pb=2)])
  self.assertEqual(count,1);self.assertEqual(destination,0xa00000);self.assertEqual(entry,0xa00000)
  self.assertEqual(raw+bytes(zero),self.output[128:]+bytes(32))
 def test_loaded_memory_and_decoder_checks(self):
  stub=b'LZMA'+bytes(60);spec=dict(stub_sha256=L.digest(stub),stub_base=0x1d00000,stub_entry=0x1d0001c,stub_zero=32,stub_pointer_before='00000000')
  memory=dict(base=0xa00000,entry=0xa00000,bytes=160,sha256=L.digest(self.output[128:]+bytes(32)))
  b=L.pack(self.output,stub,spec);self.assertTrue(L.verify_packed(b,memory,spec))
  wrong=dict(memory,sha256='0'*64)
  with self.assertRaisesRegex(ValueError,'memory hash'):L.verify_packed(b,wrong,spec)
  broken=bytearray(b);broken[-1]^=1
  with self.assertRaisesRegex(ValueError,'Unpacker code'):L.verify_packed(broken,memory,spec)
 def test_wrong_unpacker(self):
  with self.assertRaisesRegex(ValueError,'Wrong launcher'):L.pack(self.output,b'FAIL',dict(stub_sha256='0'*64))

if __name__=='__main__':unittest.main()
