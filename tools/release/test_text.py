import unittest,struct,zlib,json,hashlib
from pathlib import Path
from text_raster import Mask,Atlas,fitted,split_title
from text_native import Container,lzss,map_pixels,title_card,card_metadata
from text_assets import Generator,resource,output_name
O=Path(__file__).parent
FONT=O/'text-fonts/open-v1.mga'
class TextTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):cls.a=Atlas.load(FONT)
 def test_bbox_preserved(self):
  import random
  rng=random.Random(731)
  for _ in range(100):
   w,h=rng.randrange(1,70),rng.randrange(1,70);data=bytes(rng.randrange(256)if rng.random()<.01 else 0 for _ in range(w*h));m=Mask(w,h,data)
   points=[(i%w,i//w)for i,v in enumerate(data)if v]
   expected=(min(x for x,y in points),min(y for x,y in points),max(x for x,y in points)+1,max(y for x,y in points)+1)if points else None
   self.assertEqual(m.bbox(),expected)
 def test_mask_area(self):
  self.assertEqual(Mask(2,2,bytes((0,100,100,200))).resize(1,1).p,bytes((100,)))
  self.assertEqual(Mask(1,1,b'\x80').resize(3,3).p,bytes((128,))*9)
 def test_descenders_and_japanese(self):
  for text in ('ygjpq','éöñßłæ','蠍火','水上の提督','Scripted Connection⇒'):
   m=fitted(self.a,text,'wheel',2,17,21,180,0,.81);box=m.bbox();self.assertIsNotNone(box)
   self.assertGreaterEqual(box[0],1);self.assertGreaterEqual(box[1],1);self.assertLessEqual(box[2],m.w-1);self.assertLessEqual(box[3],20)
 def test_long_text(self):
  m=fitted(self.a,'W'*200,'wheel',2,17,21,180,0,.81);self.assertLessEqual(m.w,180);self.assertTrue(m.bbox())
 def test_missing_is_error(self):
  with self.assertRaisesRegex(ValueError,'U\\+1F984'):self.a.coverage('🦄','wheel',32,0,80)
 def test_corrupt_atlas(self):
  for data in (b'MGA1bad',(FONT).read_bytes()+b'tail',b'MGA1'+zlib.compress(b'{}')):
   with self.assertRaises((ValueError,KeyError)):Atlas(data)
 def test_bounds(self):
  for args in ((-1,1),(65537,2),(8192,2048+1)):
   with self.assertRaises(ValueError):Mask(*args)
 def test_palette_tie_and_clear(self):
  p=[(0,0,0,0),(10,10,10,128),(20,20,20,128)]+[(255,0,255,128)]*253
  data=map_pixels(2,1,[(15,15,15,255),(1,2,3,0)],p,0)
  self.assertEqual(data[12:],bytes((1,0)))
 def test_bad_native(self):
  for b in (b'',b'\x14\0\x01\0'+bytes(16)):
   with self.assertRaises(ValueError):Container(b)
  with self.assertRaises(ValueError):lzss(b'\0\0\0',1)
 def test_source_checks(self):
  ref=dict(input='iso',offset=4,bytes=4,sha256=hashlib.sha256(b'abcd').hexdigest())
  self.assertEqual(resource(ref,lambda *args:b'abcd'),b'abcd')
  with self.assertRaisesRegex(ValueError,'hash'):resource(ref,lambda *args:b'abce')
  with self.assertRaises(ValueError):resource(dict(ref,bytes=-1),lambda *args:b'')
 def test_output_paths(self):
  for n in ('../x','/x','a//b','a/./b','C:/x','x\\y'):
   with self.assertRaises(ValueError):output_name(n)
 def test_native_static_card_schema(self):
  m=card_metadata();self.assertEqual(len(m),320);self.assertEqual(struct.unpack_from('<4H2I',m),(0,1,1,1,208,304));self.assertEqual(struct.unpack_from('<HHHHHHIhhIIII',m,224),(65535,65535,0,65535,65535,0,0,320,240,0,0,0,0))
 def test_launcher_atlas(self):
  from text_launcher import launcher_font
  f=launcher_font(self.a);self.assertEqual((f['width'],f['height']),(512,64));self.assertEqual(len(f['glyphs']),95)
  self.assertTrue(all(g[0]+g[2]<=512 and g[1]+32<=f['height'] for g in f['glyphs']))
 def test_runtime_slot(self):
  p=[(0,0,0,0),(255,255,255,128)]*128
  with self.assertRaisesRegex(ValueError,'slot'):map_pixels(641,1,[(0,0,0,0)]*641,p,0)
 def test_no_font_dependencies(self):
  import ast
  for name in ('text_raster.py','text_assets.py','text_native.py'):
   for n in ast.walk(ast.parse((O/name).read_text())):
    if isinstance(n,ast.Import):self.assertFalse(any(a.name.startswith(('PIL','fontTools','numpy'))for a in n.names))
    if isinstance(n,ast.ImportFrom):self.assertFalse((n.module or '').startswith(('PIL','fontTools','numpy')))
if __name__=='__main__':unittest.main()
