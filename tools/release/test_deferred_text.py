import copy,hashlib,json,tempfile,unittest,zipfile,struct
from pathlib import Path
from unittest.mock import patch
import megamix as M
from text_native import title_card,Container
from text_raster import Atlas
from text_assets import Generator
import deferred_text as D

class DeferredTextTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.font=(Path(__file__).parent/'text-fonts/open-v1.mga').read_bytes()
  cls.atlas=Atlas(cls.font)
  from bga_codec import lzss_encode
  from text_native import card_metadata,swizzle
  meta=bytearray(card_metadata());struct.pack_into('<2H',meta,16,32,16);struct.pack_into('<4H',meta,208,0,0,32,16)
  sheet=bytearray(1024+32*16)
  for i in range(256):sheet[swizzle(i)*4:swizzle(i)*4+4]=bytes((i,i,i,128 if i else 0))
  sheet[1024:]=bytes([255])*(32*16)
  raw=[bytes(meta),bytes(sheet)];packed=[lzss_encode(r,48)for r in raw]
  cls.palette=struct.pack('<HH',20,1)+b''.join(struct.pack('<II',len(r),len(p))for r,p in zip(raw,packed))+b''.join(packed)

 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory();self.root=Path(self.tmp.name)
  self.base=self.root/'base';self.pack=self.root/'pack';self.base.mkdir();self.pack.mkdir()
  self.ref=dict(input='base',offset=100,bytes=len(self.palette),sha256=M.hash_bytes(self.palette))
  self.source=dict(self.ref,input='iso',offset=80)
  self.step=dict(op='text',kind='native',source=self.source,palette=self.ref,sprite=0,sheet=0)
  self.data=Generator(None,lambda *args:self.palette).generate(self.step)
  self.name='songs/red/red_1/title.spr'
  self.package=dict(schema=1,kind='append',source='red',contract='test',files={self.name:dict(bytes=len(self.data),sha256=M.hash_bytes(self.data))},songs=[dict(source='red',sid=1,dir='red_1',style='red')],style_order=['red'],payload_prefixes=['a','songs/red/red_1'],deferred_text=dict(schema=1,jobs={self.name:self.step}))
  self.package['content_id']=M.content_id(self.package)
  self.b=dict(schema=1,kind='base',source='id',contract='test',files={'a/00/palette':dict(bytes=len(self.palette),sha256=M.hash_bytes(self.palette))},songs=[],style_order=['red'],payload_prefixes=[],asset_map={'a/00/palette':M.hash_bytes(self.palette)},text_bases={D.resource_key(self.ref):dict(ref=self.ref,path='a/00/palette')})
  self.b['asset_map_hash']=M.hash_bytes(M.canonical(self.b['asset_map']));self.b['content_id']=M.content_id(self.b)
  M.write_atomic(self.base/'a/00/palette',self.palette);M.finalize_base(self.base,self.b)
  M.write_atomic(self.pack/D.input_path(M.hash_bytes(self.palette)),self.palette);M.save_json(self.pack/'megamix-package.json',self.package)
 def tearDown(self):self.tmp.cleanup()
 def test_independent_pack_then_install(self):
  self.assertFalse((self.pack/self.name).exists());self.assertEqual(M.verify(self.pack),[])
  M.install(self.pack,self.base,progress=lambda _:None)
  self.assertEqual((self.base/self.name).read_bytes(),self.data);self.assertEqual(M.verify(self.base),[])
  self.assertFalse((self.base/'.megamix-inputs').exists());cfg=(self.base/'launcher.cfg').read_bytes()
  M.install(self.pack,self.base,progress=lambda _:None);self.assertEqual((self.base/'launcher.cfg').read_bytes(),cfg)
 def test_damaged_source_is_rejected_before_activation(self):
  seal=(self.base/'launcher/source.id').read_bytes();(self.pack/D.input_path(M.hash_bytes(self.palette))).write_bytes(b'bad')
  with self.assertRaisesRegex(ValueError,'Damaged append'):M.install(self.pack,self.base,progress=lambda _:None)
  self.assertEqual((self.base/'launcher/source.id').read_bytes(),seal);self.assertFalse((self.base/self.name).exists())
 def test_wrong_base_palette_is_rejected_before_activation(self):
  seal=(self.base/'launcher/source.id').read_bytes();(self.base/'a/00/palette').write_bytes(b'bad')
  with self.assertRaisesRegex(ValueError,'palette hash'):M.install(self.pack,self.base,progress=lambda _:None)
  self.assertEqual((self.base/'launcher/source.id').read_bytes(),seal);self.assertFalse((self.base/'.megamix/install-journal.json').exists())
 def test_missing_base_resource(self):
  self.b.pop('text_bases');self.b['content_id']=M.content_id(self.b)
  state=json.loads((self.base/'.megamix/installed.json').read_text());state['packages']['id']=self.b;M.save_json(self.base/'.megamix/installed.json',state)
  with self.assertRaisesRegex(ValueError,'palette is missing'):M.install(self.pack,self.base,progress=lambda _:None)
 def test_wrong_output_is_rejected(self):
  self.package['files'][self.name]['sha256']='0'*64;self.package['content_id']=M.content_id(self.package);M.save_json(self.pack/'megamix-package.json',self.package)
  with self.assertRaisesRegex(ValueError,'Finalized text hash'):M.install(self.pack,self.base,progress=lambda _:None)
  self.assertFalse((self.base/self.name).exists());self.assertTrue((self.base/'launcher/source.id').exists())
 def test_resume_activation_and_materialization(self):
  with patch.object(M,'activate_indexes',side_effect=OSError('interrupted')):
   with self.assertRaises(OSError):M.install(self.pack,self.base,progress=lambda _:None)
  with self.assertRaisesRegex(ValueError,'interrupted'):M.verify(self.base)
  stage=self.base/'.megamix/materialized'/self.package['content_id']/self.name;stage.write_bytes(b'bad')
  M.install(self.pack,self.base,progress=lambda _:None);self.assertEqual(M.verify(self.base),[]);self.assertFalse(stage.exists())
 def test_conflicting_extent_hashes(self):
  step=copy.deepcopy(self.step);step['source']['sha256']='0'*64
  self.package['deferred_text']['jobs']['songs/red/red_1/b.spr']=step
  self.package['files']['songs/red/red_1/b.spr']=self.package['files'][self.name]
  M.write_atomic(self.pack/D.input_path('0'*64),self.palette)
  with self.assertRaisesRegex(ValueError,'Damaged deferred'):D.materialize(self.pack,self.base,self.package,self.b)
 def test_unsafe_output_and_invalid_hash(self):
  for name in ('../x','launcher.cfg','/a/x'):
   p=copy.deepcopy(self.package);p['deferred_text']['jobs']={name:self.step}
   with self.assertRaises(ValueError):D.transport(p)
  p=copy.deepcopy(self.package);p['deferred_text']['jobs'][self.name]['source']['sha256']='x'*64
  with self.assertRaises(ValueError):D.transport(p)
 def test_base_resource_requires_owned_exact_file(self):
  D.validate_base(self.b)
  p=copy.deepcopy(self.b);next(iter(p['text_bases'].values()))['path']='launcher.cfg'
  with self.assertRaises(ValueError):D.validate_base(p)
 def test_capture_bounded_source_and_no_palette_on_pack(self):
  dest=self.root/'captured';recipe={'files':{self.name:self.step}}
  with zipfile.ZipFile(self.root/'capsule.zip','w')as z:pass
  with zipfile.ZipFile(self.root/'capsule.zip')as z:
   reads=[]
   def read(at,n):reads.append((at,n));return self.palette
   D.capture(dest,self.package,recipe,z,read)
   self.assertEqual(reads,[(80,len(self.palette))]);self.assertEqual(M.check_files(dest,D.pack_files(self.package)),[])
   D.capture(dest,self.package,recipe,z,lambda *a:self.fail('read despite verified resume'))
 def test_full_build_freeze_and_resume_without_base_iso_for_append(self):
  from freeze_release import freeze
  from resume_freeze import resume
  def image(name):
   p=self.root/name;p.write_bytes(bytes(100)+self.palette);return p
  base_iso=image('base.iso');append_iso=image('append.iso');append_iso.write_bytes(append_iso.read_bytes()+b'append')
  ref=dict(self.ref,offset=100);source=dict(self.source,offset=100)
  native=dict(self.step,palette=ref,source=source)
  generated=dict(op='text',kind='sprite',palette=ref,text='TEST',role='wheel',reference=0,max_width=32,bold=0,xscale=1)
  data=Generator(self.atlas,lambda *args:self.palette).generate(generated)
  native_file=self.root/'native.spr';native_file.write_bytes(self.data)
  generated_file=self.root/'generated.spr';generated_file.write_bytes(data)
  palette_file=self.base/'a/00/palette'
  plan=dict(version='fixture',contract='test',style_order=['red'],text_base_image=str(base_iso),discs=[])
  plan['discs']=[dict(source='id',name='base',kind='base',image=str(base_iso),iso=dict(bytes=base_iso.stat().st_size,sha256=M.hash_file(base_iso)),targets=dict(console=dict(files={'a/00/palette':dict(path=str(palette_file),origin=[100,len(self.palette)])},songs=[],text_bases={D.resource_key(ref):dict(ref=ref,path='a/00/palette')}))),dict(source='red',name='append',kind='append',image=str(append_iso),iso=dict(bytes=append_iso.stat().st_size,sha256=M.hash_file(append_iso)),targets=dict(append=dict(files={self.name:dict(path=str(native_file),text=dict(spec=native)), 'songs/red/red_1/other.spr':dict(path=str(generated_file),text=dict(spec=generated,atlas=str(Path(__file__).parent/'text-fonts/open-v1.mga')))},songs=self.package['songs'],payload_prefixes=self.package['payload_prefixes'])))]
  plan['discs'][0]['targets']['console']['files']['base-native.spr']=dict(path=str(native_file),text=dict(spec=native))
  pp=self.root/'plan.json';M.save_json(pp,plan);bundle=self.root/'fixture.mmr';freeze(pp,bundle)
  base=self.root/'fresh-base';pack=self.root/'fresh-pack';M.build(base_iso,base,bundle,progress=lambda _:None);self.assertEqual((base/'base-native.spr').read_bytes(),self.data)
  base_iso.unlink() # Consumer append generation must need only its own ISO.
  with patch.object(M,'check_files',side_effect=OSError('build interrupted')):
   with self.assertRaises(OSError):M.build(append_iso,pack,bundle,progress=lambda _:None)
  stage=pack.with_name('.'+pack.name+'.megamix-incomplete')
  (stage/D.input_path(M.hash_bytes(self.palette))).write_bytes(b'damaged staged transport')
  M.build(append_iso,pack,bundle,progress=lambda _:None);self.assertEqual(M.verify(pack),[])
  self.assertFalse((pack/self.name).exists());M.install(pack,base,progress=lambda _:None)
  self.assertEqual((base/self.name).read_bytes(),self.data);self.assertEqual(M.verify(base),[])
  # Strict checkpoint re-use checks annotation, palette and font identity.
  with zipfile.ZipFile(bundle)as z:plan['asset_map']=json.loads(z.read('recipes/id-console.json'))['package']['asset_map']
  M.save_json(pp,plan);out=self.root/'resumed.mmr';resume(pp,bundle,out)
  self.assertEqual(len(json.loads((self.root/'freeze-resume.json').read_text())['reused_targets']),2)
if __name__=='__main__':unittest.main()
