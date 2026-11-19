import base64,copy,json,struct,tempfile,unittest,zlib
from pathlib import Path
from unittest.mock import Mock,patch
import megamix as M,font_folder as F,deferred_text as D
from text_assets import Generator
from text_raster import Atlas
import test_deferred_text as T

class FontFolders(unittest.TestCase):
 def setUp(self):self.tmp=tempfile.TemporaryDirectory();self.root=Path(self.tmp.name)
 def tearDown(self):self.tmp.cleanup()
 def test_empty_auto_does_not_require_libraries(self):
  self.assertEqual(F.scan(self.root,False),{})
  with self.assertRaisesRegex(ValueError,'No fonts found'):F.scan(self.root)
  self.assertEqual(F.scan(self.root/'missing',False),{})
 def test_family_layout_and_case(self):
  p=self.root/'Compacta';p.mkdir();(p/'Any Name.TTF').write_bytes(b'font');(p/'README.txt').write_text('hint')
  self.assertEqual(list(F.scan(self.root)),['compacta'])
  (self.root/'loose.otf').write_bytes(b'font')
  with self.assertRaisesRegex(ValueError,'named family'):F.scan(self.root,False)
 def test_unknown_folder_and_duplicate_case(self):
  p=self.root/'comapcta';p.mkdir();(p/'x.ttf').write_bytes(b'x')
  with self.assertRaisesRegex(ValueError,'Unrecognized'):F.scan(self.root)
  # Model both directory names without requiring a case-sensitive filesystem.
  # Keep real font-file probes so only the directory listing is simulated.
  directories=[]
  for name in('compacta','Compacta'):
   directory=Mock(spec=Path);directory.name=name
   directory.is_dir.return_value=True;directory.rglob.return_value=[p/'x.ttf']
   directories.append(directory)
  with patch.object(Path,'iterdir',return_value=directories):
   with self.assertRaisesRegex(ValueError,'Duplicate'):F.scan(self.root)
 def test_normal_width_upright_then_matching_weight(self):
  def face(name,**kw):return dict(name=name,italic=False,width=5,weight=400,sha256='a'*64,face=0,**kw)
  regular=face('Regular');bold=dict(regular,name='Bold',weight=700);extended=dict(bold,width=7);italic=dict(bold,italic=True)
  self.assertEqual(min([extended,italic,regular,bold],key=lambda x:F.score(x,700)),bold)
  self.assertEqual(min([bold,regular],key=lambda x:F.score(x,400)),regular)
 def test_family_priorities_and_roles(self):
  def inspect(paths):return [dict(file=str(paths[0]),face=0,sha256='a'*64,name=paths[0].stem,weight=400,width=5,italic=False,axes=[],variable_weight=False)]
  families={k:[self.root/(k+'.ttf')]for k in F.FAMILIES}
  with patch.object(F,'inspect',side_effect=inspect):p,selected=F.folder_profile(families,lambda _:None)
  self.assertTrue(p['roles']['wheel']['latin'][0].startswith('handel'))
  self.assertTrue(p['roles']['large_title']['latin'][0].startswith('compacta'))
  self.assertTrue(p['roles']['genre_artist']['latin'][0].startswith('helvetica'))
  self.assertTrue(p['roles']['large_title']['japanese'][0].startswith('mincho'))
  self.assertTrue(p['roles']['wheel']['japanese'][0].startswith('gothic'))
  self.assertNotIn('eurostile',{v['family']for v in selected.values()})
 def test_malformed_receipt_cannot_override_executable(self):
  p=dict(kind='append',files={'MEGAMIX.ELF':dict(bytes=1,sha256='a'*64)},font_rendered=dict(schema=1,id='a'*64,originals={'MEGAMIX.ELF':dict(bytes=1,sha256='b'*64)}))
  with self.assertRaisesRegex(ValueError,'original font-art'):F.validate(p)

 def test_source_owned_card_receipt_allowed_but_other_binaries_rejected(self):
  def receipt(name):return dict(kind='append',files={name:dict(bytes=10,sha256='a'*64)},font_rendered=dict(schema=1,id='a'*64,originals={name:dict(bytes=12,sha256='b'*64)}))
  F.validate(receipt('songs/9th_style/ninth_30/card.if'))
  for name in ('MEGAMIX.ELF','launcher/vdisc.bin','launcher/card.if','songs/9th_style/ninth_30/record.bin','songs/9th_style/ninth_30/other.if','songs/card.if','songs/too/many/parts/card.if'):
   with self.subTest(name=name),self.assertRaisesRegex(ValueError,'original font-art'):F.validate(receipt(name))

 def test_legacy_cmap_is_bounded(self):
  from prepare_text_fonts import legacy_coverage
  # Final live range A..D has only two glyph IDs, as in old truncated DF maps.
  body=struct.pack('>7H',4,36,0,4,4,1,0)+struct.pack('>11H',68,65535,0,65,65535,0,1,4,0,1,2)
  cmap=struct.pack('>2H2HI',0,1,3,1,12)+body
  self.assertEqual(legacy_coverage(cmap,3),{65,66})
  bad=bytearray(cmap);struct.pack_into('>H',bad,12+28,64)
  with self.assertRaises(ValueError):legacy_coverage(bad,3)

class FontInstall(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  T.DeferredTextTests.setUpClass();cls.palette=T.DeferredTextTests.palette;cls.default=T.DeferredTextTests.font
  raw=copy.deepcopy(Atlas(cls.default).info);key=raw['roles']['wheel']['glyphs']['T'];g=raw['glyphs'][key];g['pixels']=base64.b64encode(bytes(255-v for v in base64.b64decode(g['pixels']))).decode();raw['profile']='test-custom'
  cls.custom=b'MGA1'+zlib.compress(json.dumps(raw,sort_keys=True,separators=(',',':')).encode(),9)
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory();self.root=Path(self.tmp.name);self.base=self.root/'base';self.pack=self.root/'pack';self.base.mkdir();self.pack.mkdir()
  self.ref=dict(input='base',offset=100,bytes=len(self.palette),sha256=F.sha(self.palette));self.spec=dict(op='text',kind='sprite',text='TEST',role='wheel',reference=0,max_width=32,palette=self.ref,atlas=F.sha(self.default),atlas_bytes=len(self.default))
  self.old=Generator(Atlas(self.default),lambda *a:self.palette).generate(self.spec);self.new=Generator(Atlas(self.custom),lambda *a:self.palette).generate(self.spec);assert self.old!=self.new
  self.n=M.asset_path(F.sha(self.old));s=dict(self.spec,atlas=F.sha(self.custom),atlas_bytes=len(self.custom));self.jobs={self.n:dict(step=s,default=F.record(self.old))}
  self.doc=dict(schema=1,id=F.sha(self.custom),contract='test',sources={'red':self.jobs},selected={});doc=M.canonical(self.doc)
  self.b=dict(schema=1,kind='base',source='id',contract='test',files={'a/00/palette':F.record(self.palette),F.PROFILE_PATH:F.record(doc),F.ATLAS_PATH:F.record(self.custom)},songs=[],style_order=['red'],payload_prefixes=[],asset_map={'a/00/palette':F.sha(self.palette),self.n:F.sha(self.old)},text_bases={D.resource_key(self.ref):dict(ref=self.ref,path='a/00/palette')},font_profile=dict(schema=1,id=F.sha(self.custom),profile=F.PROFILE_PATH,atlas=F.ATLAS_PATH))
  self.b['asset_map_hash']=M.hash_bytes(M.canonical(self.b['asset_map']));self.b['content_id']=M.content_id(self.b);M.validate_manifest(self.b)
  for n,b in [('a/00/palette',self.palette),(F.PROFILE_PATH,doc),(F.ATLAS_PATH,self.custom)]:M.write_atomic(self.base/n,b)
  M.save_json(self.base/'megamix-package.json',self.b);M.finalize_base(self.base,self.b)
  self.m=dict(schema=1,kind='append',source='red',contract='test',files={self.n:F.record(self.old),'songs/red/red_1/unchanged.bin':F.record(b'chart')},songs=[dict(source='red',sid=1,dir='red_1',style='red')],style_order=['red'],payload_prefixes=['a','songs/red/red_1'],deferred_text=dict(schema=1,jobs={self.n:self.spec}))
  self.m['content_id']=M.content_id(self.m);self.save_pack();M.write_atomic(self.pack/'songs/red/red_1/unchanged.bin',b'chart');M.write_atomic(self.pack/D.input_path(F.sha(self.default)),self.default)
 def save_pack(self):self.m['content_id']=M.content_id(self.m);M.save_json(self.pack/'megamix-package.json',self.m)
 def tearDown(self):self.tmp.cleanup()
 def test_inherit_fonts_without_raw_fonts_or_libraries(self):
  # Install only needs serialized coverage; no local font files/compiler.
  with patch.dict('sys.modules',{'PIL':None,'fontTools':None}):M.install(self.pack,self.base,progress=lambda _:None)
  self.assertEqual((self.base/self.n).read_bytes(),self.new);self.assertEqual(M.verify(self.base),[])
  self.assertEqual((self.base/'songs/red/red_1/unchanged.bin').read_bytes(),b'chart')
  self.assertEqual(M.verify(self.pack),[]);self.assertFalse((self.pack/self.n).exists())
  seal=(self.base/'launcher/source.id').read_bytes();cfg=(self.base/'launcher.cfg').read_bytes();M.install(self.pack,self.base,progress=lambda _:None)
  self.assertEqual((self.base/'launcher/source.id').read_bytes(),seal);self.assertEqual((self.base/'launcher.cfg').read_bytes(),cfg)

 def test_nondeferred_card_rendered_without_changing_pack(self):
  step=dict(op='text',kind='card',texts=dict(genre='TEST',title='TEST',artist='TEST'),atlas=F.sha(self.custom),atlas_bytes=len(self.custom))
  before=Generator(Atlas(self.default),lambda *a:self.fail('Card borrowed input')).generate(step)
  after=Generator(Atlas(self.custom),lambda *a:self.fail('Card borrowed input')).generate(step)
  self.assertNotEqual(before,after);name=M.asset_path(F.sha(before));self.doc['sources']['red'][name]=dict(step=step,default=F.record(before));doc=M.canonical(self.doc)
  M.write_atomic(self.base/F.PROFILE_PATH,doc);self.b['files'][F.PROFILE_PATH]=F.record(doc);self.b['asset_map'][name]=F.sha(before);self.b['asset_map_hash']=M.hash_bytes(M.canonical(self.b['asset_map']));self.b['content_id']=M.content_id(self.b);M.finalize_base(self.base,self.b)
  self.m['files'][name]=F.record(before);self.save_pack();M.write_atomic(self.pack/name,before)
  M.install(self.pack,self.base,progress=lambda _:None)
  self.assertEqual((self.base/name).read_bytes(),after);self.assertEqual((self.pack/name).read_bytes(),before);self.assertEqual(M.verify(self.base),[])
 def test_loose_source_owned_card_installs_and_reinstalls_with_fonts(self):
  step=dict(op='text',kind='card',texts=dict(genre='TEST',title='TEST',artist='TEST'),atlas=F.sha(self.custom),atlas_bytes=len(self.custom))
  before=Generator(Atlas(self.default),lambda *a:self.fail('Card borrowed input')).generate(step)
  after=Generator(Atlas(self.custom),lambda *a:self.fail('Card borrowed input')).generate(step)
  self.assertNotEqual(before,after);name='songs/red/red_1/card.if'
  self.doc['sources']['red'][name]=dict(step=step,default=F.record(before));doc=M.canonical(self.doc)
  M.write_atomic(self.base/F.PROFILE_PATH,doc);self.b['files'][F.PROFILE_PATH]=F.record(doc);self.b['content_id']=M.content_id(self.b);M.finalize_base(self.base,self.b)
  self.m['files'][name]=F.record(before);self.save_pack();M.write_atomic(self.pack/name,before)
  M.install(self.pack,self.base,progress=lambda _:None)
  self.assertEqual((self.base/name).read_bytes(),after);self.assertEqual((self.pack/name).read_bytes(),before);self.assertEqual(M.verify(self.base),[])
  installed=copy.deepcopy(M.read_manifest(self.base));seal=(self.base/'launcher/source.id').read_bytes()
  M.install(self.pack,self.base,progress=lambda _:None)
  self.assertEqual((self.base/name).read_bytes(),after);self.assertEqual((self.base/'launcher/source.id').read_bytes(),seal);self.assertEqual(M.read_manifest(self.base),installed)

 def test_wrong_requested_profile_preserves_base(self):
  self.m['font_request']=dict(schema=1,id='0'*64);self.save_pack();seal=(self.base/'launcher/source.id').read_bytes()
  with self.assertRaisesRegex(ValueError,'different fonts'):M.install(self.pack,self.base,progress=lambda _:None)
  self.assertEqual((self.base/'launcher/source.id').read_bytes(),seal);self.assertFalse((self.base/self.n).exists())
 def test_corrupt_transport_not_hidden_by_replacement(self):
  (self.pack/D.input_path(F.sha(self.default))).write_bytes(b'bad');seal=(self.base/'launcher/source.id').read_bytes()
  with self.assertRaisesRegex(ValueError,'Damaged append'):M.install(self.pack,self.base,progress=lambda _:None)
  self.assertEqual((self.base/'launcher/source.id').read_bytes(),seal)
 def test_corrupt_profile_or_palette_before_writes(self):
  (self.base/'a/00/palette').write_bytes(b'bad');seal=(self.base/'launcher/source.id').read_bytes()
  with self.assertRaisesRegex(ValueError,'palette hash'):M.install(self.pack,self.base,progress=lambda _:None)
  self.assertEqual((self.base/'launcher/source.id').read_bytes(),seal)
 def test_resume_interrupted_activation(self):
  with patch.object(M,'activate_indexes',side_effect=OSError('power loss')):
   with self.assertRaises(OSError):M.install(self.pack,self.base,progress=lambda _:None)
  with self.assertRaisesRegex(ValueError,'interrupted'):M.verify(self.base)
  M.install(self.pack,self.base,progress=lambda _:None);self.assertEqual(M.verify(self.base),[])
 def test_cannot_claim_an_unrelated_original_hash(self):
  self.m['files'][self.n]=F.record(b'unrelated');self.save_pack()
  with self.assertRaisesRegex(ValueError,'does not match append'):M.install(self.pack,self.base,progress=lambda _:None)
 def test_requested_fonts_need_matching_base(self):
  self.b.pop('font_profile');self.b['content_id']=M.content_id(self.b);M.finalize_base(self.base,self.b)
  self.m['font_request']=dict(schema=1,id=F.sha(self.custom));self.save_pack()
  with self.assertRaisesRegex(ValueError,'same --fonts'):M.install(self.pack,self.base,progress=lambda _:None)


class ActualFontCompiler(unittest.TestCase):
 def test_authored_ttf_metadata_and_compilation(self):
  try:
   from fontTools.fontBuilder import FontBuilder
   from fontTools.pens.ttGlyphPen import TTGlyphPen
   import PIL
  except ImportError:self.skipTest('Optional font compiler libraries are not installed')
  from prepare_text_fonts import compile_profile
  with tempfile.TemporaryDirectory()as tmp:
   root=Path(tmp)
   for family,weight in [('compacta',400),('eurostile',700)]:
    d=root/family;d.mkdir();fb=FontBuilder(1000,isTTF=True);order=['.notdef','space','H','T','E','S'];fb.setupGlyphOrder(order);fb.setupCharacterMap({ord(c):c for c in 'HTES'}|{32:'space'});glyphs={}
    for i,n in enumerate(order):
     pen=TTGlyphPen(None)
     if n!='space':pen.moveTo((50,0));pen.lineTo((450+i*10,0));pen.lineTo((450+i*10,700));pen.lineTo((50,700));pen.closePath()
     glyphs[n]=pen.glyph()
    fb.setupGlyf(glyphs);fb.setupHorizontalMetrics({n:(600,0)for n in order});fb.setupHorizontalHeader(ascent=800,descent=-200);fb.setupNameTable(dict(familyName='Authored Test '+family,styleName='Regular'if weight==400 else'Bold',uniqueFontIdentifier='test-'+family,fullName='Authored Test '+family,psName='Test-'+family));fb.setupOS2(sTypoAscender=800,sTypoDescender=-200,usWinAscent=800,usWinDescent=200,usWeightClass=weight);fb.setupPost();fb.setupMaxp();fb.save(d/'arbitrary filename.ttf')
   profile,_=F.folder_profile(F.scan(root),lambda _:None);(root/'profile.json').write_bytes(M.canonical(profile));(root/'corpus.json').write_bytes(M.canonical(['TEST']))
   compile_profile(root/'profile.json',root/'corpus.json',root/'custom.mga',Path(__file__).parent/'text-fonts/open-v1.mga')
   custom=Atlas((root/'custom.mga').read_bytes())
   self.assertTrue(custom.info['glyphs'][custom.roles['wheel']['glyphs']['T']]['font'].startswith('eurostile'))
   self.assertTrue(custom.info['glyphs'][custom.roles['large_title']['glyphs']['T']]['font'].startswith('compacta'))
   # A missing character must retain the bundled role's fallback glyph.
   fallback=Atlas((Path(__file__).parent/'text-fonts/open-v1.mga').read_bytes())
   self.assertEqual(custom.roles['wheel']['glyphs']['A'],fallback.roles['wheel']['glyphs']['A'])

if __name__=='__main__':unittest.main()
