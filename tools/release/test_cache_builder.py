"""Offline cache boundaries; a valid tiny receipt exercises publishing safely."""
import copy,json,tempfile,unittest
from pathlib import Path
from unittest.mock import patch
import cache_builder as C,megamix as M
class CacheTests(unittest.TestCase):
 def test_logical_device_paths(self):
  for raw,want in [('mass:/MEGAMIX','mass:/MEGAMIX/'),('mmce1:\\Games\\IIDX','mmce1:/Games/IIDX/'),('mass0:','mass0:/')]:self.assertEqual(C.logical_path(raw),want)
  self.assertEqual(C.logical_path('mc0:/APPS/MM/',False),'mc0:/APPS/MM/')
  self.assertEqual(C.logical_path('host:',target='host'),'host:')
 def test_invalid_paths_rejected(self):
  for v in [None,'','/tmp/a','mc0:/MEGAMIX/','mmce2:/x/','mass:/../x/','mass:/./x/','mass://x','mass:/x:bad','mass:/'+70*'a','mass:/\nx/']:
   with self.subTest(v=v),self.assertRaises(ValueError):C.logical_path(v)
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory();self.top=Path(self.tmp.name);self.root=self.top/'MEGAMIX';(self.root/'.megamix').mkdir(parents=True)
  for n,b in [('launcher.cfg',b'video 0\n'),('launcher.cfg.pending',b''),('private-save.bin',b'scores'),('launcher/source.id',b'a'*64),('cache/old.bin',b'prior cache')]:
   p=self.root/n;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(b)
  self.outcome=dict(boot_saved=1,menu_saved=1,boot_hit=1,menu_hit=1)
  self.boot='boot_'+16*'a'+'.bin';self.menu='menu_'+16*'b'+'.bin'
 def tearDown(self):self.tmp.cleanup()
 def preflight(self,*args):return 'console',{'wasm':{'sha256':'test'}},self.top/'engine.wasm',{'launcher/source.id':C._record(self.root/'launcher/source.id')},{'launcher.cfg':b'video 0\n','launcher.cfg.pending':b'','debug.cfg':None},'a'*64
 def execute(self,wasm,root,config,out,*args):
  (out/'cache'/self.boot).write_bytes(b'boot');(out/'cache'/self.menu).write_bytes(b'menu');(out/'cache'/'sprites_abc.bin').write_bytes(b'pixels');(out/'cache'/'sprites_build.tmp').write_bytes(b'temporary')
  return self.outcome
 def generate(self,**kw):
  with patch.object(C,'_preflight',side_effect=self.preflight),patch.object(C,'_execute',side_effect=self.execute):return C.generate(self.root,'mass:/DATA/',progress=lambda _:None,**kw)
 def assert_preserved(self):
  self.assertEqual((self.root/'launcher.cfg').read_bytes(),b'video 0\n');self.assertEqual((self.root/'private-save.bin').read_bytes(),b'scores');self.assertEqual((self.root/'cache/old.bin').read_bytes(),b'prior cache');self.assertFalse((self.root/'.megamix/install.lock').exists())
 def test_publication_only_caches_and_last_boot(self):
  writes=[];real=M.write_atomic
  def write(p,b):writes.append(p.name);real(p,b)
  with patch.object(M,'write_atomic',side_effect=write):r=self.generate()
  self.assertEqual(r['bytes'],14);self.assertEqual(writes[-2:], [self.boot,'pc-build.json']);self.assertFalse((self.root/'cache/sprites_build.tmp').exists());self.assert_preserved()
 def test_lock_does_not_remove_other_lock(self):
  p=self.root/'.megamix/install.lock';p.write_text('someone else')
  with self.assertRaisesRegex(ValueError,'lock'):self.generate()
  self.assertEqual(p.read_text(),'someone else')
 def test_unverified_engine_output_not_published(self):
  self.outcome['boot_hit']=0
  with self.assertRaisesRegex(ValueError,'restore check'):self.generate()
  self.assertFalse((self.root/'cache'/self.boot).exists());self.assert_preserved()
 def test_changed_source_aborts_before_publication(self):
  execute=self.execute
  def change(*args):r=execute(*args);(self.root/'launcher/source.id').write_bytes(b'b'*64);return r
  with patch.object(C,'_preflight',side_effect=self.preflight),patch.object(C,'_execute',side_effect=change),self.assertRaisesRegex(ValueError,'Installation changed'):C.generate(self.root,'mass:/DATA/',progress=lambda _:None)
  self.assertFalse((self.root/'cache'/self.boot).exists());self.assert_preserved()
 def test_changed_settings_abort_before_publication(self):
  execute=self.execute
  def change(*args):r=execute(*args);(self.root/'launcher.cfg').write_bytes(b'new settings');return r
  with patch.object(C,'_preflight',side_effect=self.preflight),patch.object(C,'_execute',side_effect=change),self.assertRaisesRegex(ValueError,'Settings changed'):C.generate(self.root,'mass:/DATA/',progress=lambda _:None)
  self.assertFalse((self.root/'cache'/self.boot).exists());self.assertEqual((self.root/'private-save.bin').read_bytes(),b'scores')
 def test_symlink_destination_rejected_before_other_writes(self):
  outside=self.top/'outside';outside.write_bytes(b'keep');(self.root/'cache'/self.boot).symlink_to(outside)
  with self.assertRaisesRegex(ValueError,'Symlink'):self.generate()
  self.assertEqual(outside.read_bytes(),b'keep');self.assertFalse((self.root/'cache'/self.menu).exists());self.assert_preserved()
 def test_missing_dependency_is_optional(self):
  import builtins
  real=builtins.__import__
  def blocked(name,*a,**k):
   if name=='wasmtime':raise ImportError('not installed')
   return real(name,*a,**k)
  with patch('builtins.__import__',side_effect=blocked),self.assertRaisesRegex(ValueError,'Optional cache runtime'):C._execute(self.top/'missing',self.root,self.root,self.root,'mass:/DATA/','mass:/DATA/')
 def test_partial_publication_preserves_user_data_and_retry_works(self):
  real=M.write_atomic
  def fail(p,b):
   if p.name==self.boot:raise OSError('device disconnected')
   real(p,b)
  with patch.object(M,'write_atomic',side_effect=fail),self.assertRaises(OSError):self.generate()
  self.assertFalse((self.root/'cache'/self.boot).exists());self.assert_preserved();self.generate();self.assert_preserved()
