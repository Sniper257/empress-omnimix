import copy,json,tempfile,unittest,sys
from pathlib import Path
import apply_presentation_hotfix as H,folder_presentation as F,megamix as M
from test_folder_presentation import PresentationTests
class HotfixTests(unittest.TestCase):
 def setUp(self):
  self.fx=PresentationTests();self.fx.setUp();self.root=self.fx.root;self.top=self.fx.top
  self.old=copy.deepcopy(self.fx.base);self.old.pop('folder_presentation');self.old['content_id']=M.content_id(self.old)
  for n,b in self.fx.data.items():(self.root/n).write_bytes(b)
  M.finalize_base(self.root,self.old);(self.root/'megamix-package.json').write_text(json.dumps(self.old))
  self.new=copy.deepcopy(self.old);self.new['folder_presentation']=1;self.new['content_id']=M.content_id(self.new)
  self.bundle=self.top/'hotfix';self.bundle.mkdir();pb=M.read_manifest(self.fx.pb)
  self.info=dict(hotfix=H.HOTFIX,origins={self.old['content_id']:self.old},base=self.new,pb=pb,base_payload={})
  for r in pb['folder_art'].values():
   for k in ('label','caption'):
    n=r[k];p=self.bundle/'payload'/n;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes((self.fx.pb/n).read_bytes())
  self.write_info();(self.root/'launcher.cfg').write_bytes(b'personal settings');(self.root/'controllers.0').write_bytes(b'bindings');(self.root/'scores.bin').write_bytes(b'scores')
 def write_info(self):(self.bundle/'hotfix.json').write_text(json.dumps(self.info))
 def tearDown(self):self.fx.tearDown()
 def check(self):
  self.assertEqual(M.verify(self.root),[])
  self.assertEqual((self.root/'launcher.cfg').read_bytes(),b'personal settings');self.assertEqual((self.root/'controllers.0').read_bytes(),b'bindings');self.assertEqual((self.root/'scores.bin').read_bytes(),b'scores')
 def old_pb(self):
  p=self.fx.old_pb();M.install(p,self.root,progress=lambda _:None)
 def test_base_only_update_rollback_reapply(self):
  before=(self.root/'.megamix/installed.json').read_bytes();self.assertEqual(H.run(self.root,self.bundle),'installed');self.check()
  H.run(self.root,self.bundle,rollback=True);self.assertEqual(before,(self.root/'.megamix/installed.json').read_bytes());self.assertEqual(M.verify(self.root),[])
  H.run(self.root,self.bundle);self.check()
 def test_pb_migration_and_old_pack_reimport(self):
  self.old_pb();before=(self.root/'.megamix/installed.json').read_bytes();H.run(self.root,self.bundle);self.check()
  M.install(self.top/'pb-old',self.root,progress=lambda _:None);self.check()
  self.assertIn('pb-label.spr',(self.root/'songs/red/style.txt').read_text())
  H.run(self.root,self.bundle,rollback=True);self.assertEqual(before,(self.root/'.megamix/installed.json').read_bytes());self.assertEqual(M.verify(self.root),[])
  self.assertFalse((self.root/'songs/red/pb-label.spr').exists())
 def test_interruption_resume(self):
  self.old_pb()
  def fail(i,n):
   if i==20:raise OSError('simulated interruption')
  with self.assertRaises(OSError):H.run(self.root,self.bundle,after_write=fail)
  H.run(self.root,self.bundle);self.check()
 def test_interruption_rollback(self):
  self.old_pb();before=(self.root/'.megamix/installed.json').read_bytes()
  def fail(i,n):
   if n=='launcher/source.id':raise OSError('simulated interruption')
  with self.assertRaises(OSError):H.run(self.root,self.bundle,after_write=fail)
  H.run(self.root,self.bundle,rollback=True);self.assertEqual(before,(self.root/'.megamix/installed.json').read_bytes());self.assertEqual(M.verify(self.root),[])
 def test_unknown_revision_refused(self):
  s=json.loads((self.root/'.megamix/installed.json').read_text());s['packages']['id']['content_id']='x';(self.root/'.megamix/installed.json').write_text(json.dumps(s))
  with self.assertRaises(ValueError):H.run(self.root,self.bundle)
  self.assertFalse((self.root/'.megamix/hotfix-r162/journal.json').exists())
 def test_changed_source_seal_refused(self):
  (self.root/'launcher/source.id').write_bytes(b'unknown')
  with self.assertRaises(ValueError):H.run(self.root,self.bundle)
 def test_corrupt_native_art_refused(self):
  self.old_pb();n=self.info['pb']['folder_art']['red']['label'];(self.bundle/'payload'/n).write_bytes(b'wrong')
  with self.assertRaises(ValueError):H.run(self.root,self.bundle)
  self.assertEqual(M.read_manifest(self.root),self.old)
 def test_external_change_after_staging_refused(self):
  H.run(self.root,self.bundle);(self.root/'songs/red/style.txt').write_bytes(b'changed later')
  with self.assertRaises(ValueError):H.run(self.root,self.bundle,rollback=True)
  self.assertEqual((self.root/'songs/red/style.txt').read_bytes(),b'changed later')
 def test_active_installer_refused(self):
  (self.root/'.megamix/install.lock').write_text('other')
  with self.assertRaises(ValueError):H.run(self.root,self.bundle)
 def test_symlink_destination_refused(self):
  dest=self.root/'songs/red/style.txt';dest.unlink();dest.symlink_to(self.top/'unrelated');(self.top/'unrelated').write_bytes(b'keep')
  with self.assertRaises(ValueError):H.run(self.root,self.bundle)
  self.assertEqual((self.top/'unrelated').read_bytes(),b'keep')
 def test_append_after_hotfix(self):
  H.run(self.root,self.bundle);M.install(self.fx.red,self.root,progress=lambda _:None);self.check()
if __name__=='__main__':unittest.main(verbosity=2)
