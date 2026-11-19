import base64,hashlib,json,unittest,zlib
from startup_art import FORMAT,overlay,blend,from_iso,compose

class StartupArtwork(unittest.TestCase):
 def doc(self,raw):
  return dict(format=FORMAT,size=[640,480],rgba_zlib=base64.b64encode(zlib.compress(raw)).decode(),rgba_sha256=hashlib.sha256(raw).hexdigest())
 def test_authored_overlay_exact(self):
  raw=bytes((123,45,67,89))*(640*480);self.assertEqual(overlay(json.dumps(self.doc(raw)).encode()),raw)
 def test_overlay_bounded_and_sealed(self):
  for raw in(b'',bytes(640*480*4+1),bytes(640*480*4-1)):
   with self.assertRaises(ValueError):overlay(json.dumps(self.doc(raw)).encode())
  d=self.doc(bytes(640*480*4));d['rgba_sha256']='0'*64
  with self.assertRaisesRegex(ValueError,'hash'):overlay(json.dumps(d).encode())
 def test_overlay_rejects_trailing_and_wrong_geometry(self):
  d=self.doc(bytes(640*480*4));d['rgba_zlib']=base64.b64encode(zlib.compress(bytes(640*480*4))+b'extra').decode()
  with self.assertRaises(ValueError):overlay(json.dumps(d).encode())
  d['size']=[640,512]
  with self.assertRaises(ValueError):overlay(json.dumps(d).encode())
 def test_alpha_composition_preserves_transparent_background(self):
  p=10|(20<<8)|(30<<16)|(128<<24)
  self.assertEqual(blend([p,p,p],bytes((99,88,77,0,99,88,77,255,100,120,140,128))),bytes((10,20,30,255,99,88,77,255,55,70,85,255)))
  with self.assertRaises(ValueError):blend([p&0xffffff],bytes(4))
 def test_iso_inputs_reject_wrong_source(self):
  ref=dict(offset=0,bytes=3,sha256=hashlib.sha256(b'abc').hexdigest());spec=dict(original=ref,background=ref,overlay='0'*64,capacity=2048)
  with self.assertRaisesRegex(ValueError,'ISO source hash'):from_iso(spec,lambda a,n:b'abd',b'{}')
  with self.assertRaisesRegex(ValueError,'blob hash'):from_iso(spec,lambda a,n:b'abc',b'{}')
 def test_allocation_must_be_bounded_and_sector_aligned(self):
  for n in(0,1,2099200):
   with self.assertRaisesRegex(ValueError,'capacity'):compose(b'',b'',b'',n)

if __name__=='__main__':unittest.main()
