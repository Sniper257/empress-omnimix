"""Meaningful metadata migration/install boundaries with a tiny retail fixture."""
import copy,json,tempfile,unittest
from pathlib import Path
from unittest.mock import patch
import megamix as M,source_versions as V,apply_catalog_hotfix as H

class CoalesceUpdateTests(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory();self.top=Path(self.tmp.name)
  self.patch=patch.object(V,'ROWS',{('id',1):dict(style='red'),('red',1):dict(style='red'),('red',2):dict(style='red')});self.patch.start()
  self.base=self.manifest('id',[(1,'empress')]);self.red=self.manifest('red',[(1,'happy_sky'),(2,'red')])
  self.root=self.top/'MEGAMIX';self.root.mkdir();self.pack=self.top/'pack';self.pack.mkdir()
  self.put(self.root,self.base);self.put(self.pack,self.red)
  M.finalize_base(self.root,self.base);self.save(self.root/'megamix-package.json',self.base)
  self.bundle=self.top/'hotfix';self.bundle.mkdir()
  self.info=dict(hotfix=H.HOTFIX,song_versions=V.POLICY,origins={p['content_id']:p for p in(self.base,self.red)},presentation={'id-console':self.base},payload={},targets={},style_order=self.base['style_order'])
  for p,root in((self.base,self.root),(self.red,self.pack)):
   pre=copy.deepcopy(p)
   if p['source']=='id':pre['runtime_revision']='R165'
   else:pre['generator_revision']='R165'
   pre['content_id']=M.content_id(pre);q,_,_=V.normalize(pre,lambda n:(root/n).read_bytes());self.info['targets'][p['source']+'-'+p['target']]=q
  self.save(self.bundle/'hotfix.json',self.info)
  for n,b in [('launcher.cfg',b'personal'),('controllers.0',b'binds'),('scores.dat',b'scores')]:(self.root/n).write_bytes(b)
 def tearDown(self):self.patch.stop();self.tmp.cleanup()
 def save(self,p,v):p.write_text(json.dumps(v))
 def manifest(self,src,rows):
  files={};self.data=getattr(self,'data',{})
  def add(n,b):files[n]=dict(bytes=len(b),sha256=M.hash_bytes(b));self.data[src,n]=b
  for sid,st in rows:
   folder='songs/'+st+'/'+src+'_'+str(sid)
   text='name TEST\n'+('id'if src=='id'else src)+' '+str(sid)+'\n'
   if src!='id':
    rec=bytearray(range(256))+bytearray(0x34);rec[0x54]=12 if sid==1 else 11;add(folder+'/record.bin',bytes(rec));text+='record record.bin\n'
    if sid==2:text+='variant happy_sky/red_1 replace\n'
   add(folder+'/song.txt',text.encode());add(folder+'/card.if',b'graphics')
  if src=='id':
   b=bytearray(0x178bf0+2*0x134);b[0x178bf0+0x134+0x54]=16;add(V.EXE,bytes(b))
  songs=[dict(source=src,sid=sid,style=st,dir=src+'_'+str(sid))for sid,st in rows]
  p=dict(schema=1,kind='base'if src=='id'else'append',source=src,target='console'if src=='id'else'append',contract='test',files=files,songs=songs,style_order=['red','happy_sky','empress'],payload_prefixes=[]if src=='id'else['a']+['songs/'+s['style']+'/'+s['dir']for s in songs])
  if src=='id':p.update(asset_map={},asset_map_hash=M.hash_bytes(M.canonical({})))
  p['content_id']=M.content_id(p);M.validate_manifest(p);return p
 def put(self,root,p):
  for n in p['files']:
   f=root/n;f.parent.mkdir(parents=True,exist_ok=True);f.write_bytes(self.data[p['source'],n])
  self.save(root/'megamix-package.json',p)
 def check(self):
  self.assertEqual(M.verify(self.root),[])
  for n,b in [('launcher.cfg',b'personal'),('controllers.0',b'binds'),('scores.dat',b'scores')]:self.assertEqual((self.root/n).read_bytes(),b)
 def test_normalization_changes_only_version_and_owned_paths(self):
  p,routes,changed=V.normalize(self.red,lambda n:(self.pack/n).read_bytes())
  self.assertEqual(p['songs'][0]['style'],'red');self.assertEqual(p['songs'][1],self.red['songs'][1])
  old=(self.pack/'songs/happy_sky/red_1/record.bin').read_bytes();new=changed['songs/red/red_1/record.bin'];self.assertEqual([i for i,(a,b)in enumerate(zip(old,new))if a!=b],[0x54])
  self.assertIn(b'variant red/red_1 replace',changed['songs/red/red_2/song.txt'])
  q,_,c=V.normalize(p,lambda n:changed[n]if n in changed else(self.pack/routes[n]).read_bytes());self.assertEqual(p,q);self.assertEqual(c,{})
 def test_updater_and_old_append_install_are_idempotent(self):
  H.run(self.root,self.bundle);M.install(self.pack,self.root,progress=lambda _:None);self.check()
  before=(self.root/'.megamix/installed.json').read_bytes();M.install(self.pack,self.root,progress=lambda _:None);self.check();self.assertEqual(before,(self.root/'.megamix/installed.json').read_bytes())
  self.assertIn('red_1',(self.root/'songs/red/index.txt').read_text());self.assertFalse((self.root/'songs/happy_sky/red_1/song.txt').exists())
 def test_existing_append_is_relocated_and_rollback_restores_exact_metadata(self):
  M.install(self.pack,self.root,progress=lambda _:None);before=(self.root/'.megamix/installed.json').read_bytes()
  # Keep an unrelated file in the old directory.
  stray=self.root/'songs/happy_sky/red_1/personal.txt';stray.write_bytes(b'keep')
  H.run(self.root,self.bundle);self.check();self.assertEqual(stray.read_bytes(),b'keep')
  H.run(self.root,self.bundle,rollback=True);self.assertEqual(before,(self.root/'.megamix/installed.json').read_bytes());self.check()
 def test_new_pack_requires_new_base_policy(self):
  p,routes,changed=V.normalize(self.red,lambda n:(self.pack/n).read_bytes());new=self.top/'new-pack';new.mkdir()
  for n,old in routes.items():
   f=new/n;f.parent.mkdir(parents=True,exist_ok=True);f.write_bytes(changed[n]if n in changed else(self.pack/old).read_bytes())
  self.save(new/'megamix-package.json',p)
  with self.assertRaisesRegex(ValueError,'Update the base'):M.install(new,self.root,progress=lambda _:None)
 def test_interruption_resume_and_rollback(self):
  M.install(self.pack,self.root,progress=lambda _:None)
  def fail(i,n):
   if i==2:raise OSError('power loss')
  with self.assertRaises(OSError):H.run(self.root,self.bundle,after_write=fail)
  self.assertFalse((self.root/'launcher/source.id').exists());H.run(self.root,self.bundle);self.check();H.run(self.root,self.bundle,rollback=True);self.check()
 def test_conflicting_destination_is_preserved(self):
  n=self.root/'songs/red/id_1/song.txt';n.parent.mkdir(parents=True);n.write_bytes(b'other')
  with self.assertRaisesRegex(ValueError,'destination'):H.run(self.root,self.bundle)
  self.assertEqual(n.read_bytes(),b'other');self.assertFalse((self.root/'.megamix/hotfix-r165/journal.json').exists())
 def test_changed_source_is_preserved(self):
  n=self.root/'EMPRESS/SLPM_552.21';n.write_bytes(b'changed')
  with self.assertRaisesRegex(ValueError,'differs'):H.run(self.root,self.bundle)
  self.assertEqual(n.read_bytes(),b'changed')
 def test_changed_after_update_refuses_rollback(self):
  H.run(self.root,self.bundle);(self.root/'songs/red/id_1/song.txt').write_bytes(b'modified')
  with self.assertRaisesRegex(ValueError,'changed since staging'):H.run(self.root,self.bundle,rollback=True)
 def test_lock_and_symlink_refused(self):
  lock=self.root/'.megamix/install.lock';lock.write_bytes(b'active')
  with self.assertRaisesRegex(ValueError,'lock'):H.run(self.root,self.bundle)
  lock.unlink();p=self.root/'songs/red';p.mkdir(exist_ok=True);(p/'id_1').symlink_to(self.top/'outside',target_is_directory=True)
  with self.assertRaises(ValueError):H.run(self.root,self.bundle)
 def test_incomplete_source_and_wrong_policy_refused(self):
  p=copy.deepcopy(self.red);p['songs'].pop();p['content_id']=M.content_id(p)
  with self.assertRaises(ValueError):V.normalize(p,lambda n:(self.pack/n).read_bytes())
  p=copy.deepcopy(self.red);p['song_versions']={'schema':9};p['content_id']=M.content_id(p)
  with self.assertRaisesRegex(ValueError,'version table'):M.validate_manifest(p)
 def test_paired_base_payload_is_applied_and_rolled_back(self):
  pre=copy.deepcopy(self.base);pre['alphabet_policy']=1;pre['runtime_revision']='R165'
  for n,b in [('catalog/sort.txt',b'V 1\nS id 1 TEST\n'),('MEGAMIX.ELF',b'paired launcher')]:
   pre['files'][n]=dict(bytes=len(b),sha256=M.hash_bytes(b));self.info['payload']['console/'+n]=pre['files'][n]
   f=self.bundle/'payload/console'/n;f.parent.mkdir(parents=True,exist_ok=True);f.write_bytes(b)
  pre['content_id']=M.content_id(pre);self.info['presentation']['id-console']=pre
  q,_,_=V.normalize(pre,lambda n:self.data['id',n]);self.info['targets']['id-console']=q;self.save(self.bundle/'hotfix.json',self.info)
  H.run(self.root,self.bundle);self.check();self.assertEqual((self.root/'MEGAMIX.ELF').read_bytes(),b'paired launcher')
  H.run(self.root,self.bundle,rollback=True);self.assertFalse((self.root/'catalog/sort.txt').exists());self.assertFalse((self.root/'MEGAMIX.ELF').exists())
 def test_append_provenance_does_not_change_supported_identity(self):
  M.install(self.pack,self.root,progress=lambda _:None);state=json.loads((self.root/'.megamix/installed.json').read_text())
  state['packages']['red']['generator_revision']='earlier provenance';self.save(self.root/'.megamix/installed.json',state)
  H.run(self.root,self.bundle);self.check()
 def test_interrupted_preceding_update_is_refused(self):
  p=self.root/'.megamix/hotfix-r163/journal.json';p.parent.mkdir();self.save(p,dict(phase='applying'))
  with self.assertRaisesRegex(ValueError,'R163'):H.run(self.root,self.bundle)
 def test_interrupted_r164_update_is_preserved(self):
  p=self.root/'.megamix/hotfix-r164/journal.json';p.parent.mkdir();self.save(p,dict(phase='applying'))
  with self.assertRaisesRegex(ValueError,'R164'):H.run(self.root,self.bundle)
  self.assertFalse((self.root/'.megamix/hotfix-r165/journal.json').exists())
if __name__=='__main__':unittest.main()
