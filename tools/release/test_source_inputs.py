import copy,hashlib,struct,unittest
from source_edit import apply,describe,normalize_container

class SourceEdits(unittest.TestCase):
 def test_roundtrip_resize(self):
  for raw,target in[(b'abcd',b'aZZd'),(b'abc',b'abcd\0'),(b'abcdef',b'ab')]:
   s=describe(raw,target,1024);self.assertEqual(apply(s,lambda a,n:raw),target)
 def test_wrong_source(self):
  s=describe(b'abc',b'def',0)
  with self.assertRaisesRegex(ValueError,'input hash'):apply(s,lambda a,n:b'abd')
 def test_original_byte_check(self):
  s=describe(b'abc',b'def',0);s['edits'][0]['before']='000000'
  with self.assertRaisesRegex(ValueError,'original-byte'):apply(s,lambda a,n:b'abc')
 def test_overlap_and_bounds(self):
  s=describe(b'abc',b'def',0);s['edits']*=2
  with self.assertRaises(ValueError):apply(s,lambda a,n:b'abc')
  s['edits']=[];s['length']=20*1024*1024
  with self.assertRaises(ValueError):apply(s,lambda a,n:b'abc')
 def test_header_conversion(self):
  raw=struct.pack('<HH',20,1)+b'1234567890abcdef';self.assertEqual(normalize_container(raw),struct.pack('<HHI',24,1,0)+raw[4:])
 def test_wrong_header(self):
  for raw in(b'abc',struct.pack('<HHI',24,1,0)+b'1234567890abcdef'):
   with self.assertRaises(ValueError):normalize_container(raw)

if __name__=='__main__':unittest.main()
