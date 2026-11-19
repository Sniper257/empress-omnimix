"""Portable malformed-input and semantics checks, no retail fixture files."""
import copy,hashlib,struct,unittest
import bga_transform as B
from bga_source_lz import decode_lz
from bga_quantize import quantize

def literal_lz(data):
 out=bytearray()
 for i in range(0,len(data),8):
  block=data[i:i+8]
  if len(block)==8:out+=b'\0'+block
  else:out+=bytes([1<<len(block)])+block+b'\xff';return bytes(out)
 return bytes(out)+b'\x01\xff'

def source(version=8):
 va,pt,ct,ft,tt,partt=B.LAYOUTS[version];exe=bytearray(max(pt,ct,ft,tt,partt)+64)
 pos=len(exe);exe+=literal_lz(struct.pack('<16H',*([0,31]+[0]*14)))
 struct.pack_into('<I',exe,pt,va+pos);struct.pack_into('<HH',exe,ct,0,1)
 struct.pack_into('<HBBHHH',exe,ft,0,2,0,16,16,0);struct.pack_into('<HH',exe,partt,0,1)
 struct.pack_into('<hhhhHBBBBH',exe,tt,171,0,16,16,0,0,0,0,0,0)
 data=bytearray(20);struct.pack_into('<I',data,4,0);struct.pack_into('<2I',data,12,4,0xffffffff)
 data+=literal_lz(bytes([0x11])*8192)
 return bytes(exe),bytes(data)

class PortableBGA(unittest.TestCase):
 def test_lz_literals_and_end(self):
  for n in (0,1,7,8,9,32,8192):
   raw=bytes(i%256 for i in range(n));self.assertEqual(decode_lz(literal_lz(raw)),raw)
 def test_lz_invalid_backref(self):
  with self.assertRaises(ValueError):decode_lz(b'\x01\x80')
 def test_lz_truncation(self):
  for b in (b'',b'\0',b'\x01\x00',b'\x01\xc0'):
   with self.assertRaises(ValueError):decode_lz(b)
 def test_raster_binary_mask_and_clipping(self):
  canvas=B.Raster(2,2,[1,2,3,4]);tile=B.Raster(2,2,[0xff000011,0x22,0xff000033,0xff000044]);canvas.paste(tile,-1,0,mask=True)
  self.assertEqual(list(canvas.p),[1,2,0xff000044,4])
 def test_transparent_rgb_not_in_bbox(self):
  im=B.Raster(3,2,[0xffffff,0,0,0,0xff001100,0]);self.assertEqual(im.bbox(),(1,1,2,2))
 def test_outside_crop_is_transparent(self):
  self.assertEqual(list(B.Raster(1,1,[0xffffffff]).crop((-1,-1,1,1)).p),[0,0,0,0xffffffff])
 def test_tile_flip(self):
  self.assertEqual(list(B.Raster(2,2,[1,2,3,4]).flip(True,True).p),[4,3,2,1])
 def test_frames_original_window(self):
  exe,data=source();frames=B.frames(exe,data,0,[0],8);image=frames[0][0][1];self.assertEqual(image.bbox(),(0,0,16,16));self.assertEqual(image.p[0],0xff0000f8)
 def test_atlas_original_window(self):
  exe,data=source(3);frames=B.frames(exe,data,0,[0],3);x,y,full,rect=frames[0][0][1][0];self.assertEqual((x,y,full.w,full.h,rect),(0,0,128,128,(0,0,16,16)))
 def test_invalid_version_and_clip_limits(self):
  for version,clips in ((2,[0]),(8,[]),(8,[0]*9),(8,[-1])):
   with self.assertRaises(ValueError):B.convert(b'x',b'0'*20,version,0,clips)
 def test_invalid_palette_pointer(self):
  exe,data=source();exe=bytearray(exe);struct.pack_into('<I',exe,B.LAYOUTS[8][1],0)
  with self.assertRaisesRegex(ValueError,'Palette pointer'):B.convert(exe,data,8,0,[0])
 def test_invalid_tile_offset(self):
  exe,data=source();data=bytearray(data);struct.pack_into('<I',data,12,0xfffffffe)
  with self.assertRaisesRegex(ValueError,'tile offset'):B.convert(exe,data,8,0,[0])
 def test_invalid_rotation(self):
  exe,data=source();exe=bytearray(exe);exe[B.LAYOUTS[8][4]+13]=4
  with self.assertRaisesRegex(ValueError,'placement'):B.convert(exe,data,8,0,[0])
 def test_invalid_recipe_extent_before_read(self):
  recipe=dict(op='bga',format=1,executable=dict(offset=-1,bytes=1,sha256=''))
  with self.assertRaisesRegex(ValueError,'extent'):B.from_iso(recipe,lambda *a:self.fail('must reject before read'))
 def test_wrong_source_hash(self):
  recipe=dict(op='bga',format=1,executable=dict(offset=0,bytes=1,sha256='0'*64))
  with self.assertRaisesRegex(ValueError,'SHA256'):B.from_iso(recipe,lambda *a:b'x')
 def test_short_source_read(self):
  recipe=dict(op='bga',format=1,executable=dict(offset=0,bytes=2,sha256=hashlib.sha256(b'x').hexdigest()))
  with self.assertRaisesRegex(ValueError,'SHA256'):B.from_iso(recipe,lambda *a:b'x')
 def test_palette_reserved_transparency(self):
  idx,pal=quantize(bytes([248,0,0,255,248,0,0,0]));self.assertNotEqual(idx[0],255);self.assertEqual(idx[1],255);self.assertEqual(pal[-4:],bytes(4))
if __name__=='__main__':unittest.main()
