import copy,hashlib,json,os,tempfile,unittest,sys
from pathlib import Path
import folder_presentation as F,megamix as M
class PresentationTests(unittest.TestCase):
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory();self.top=Path(self.temp.name);self.root=self.top/'MEGAMIX';self.root.mkdir();self.art={s:{'label':'songs/red/pb_0/ui/'+s+'/label.spr','caption':'songs/red/pb_0/ui/'+s+'/caption.spr','voice':F.VOICES[s]} for s in F.STYLES}
  base={'schema':1,'kind':'base','source':'id','target':'console','contract':'test','songs':[dict(source='id',sid=0,style='red',dir='id_0')],'files':{},'asset_map':{},'asset_map_hash':M.hash_bytes(M.canonical({})),'style_order':list(F.STYLES),'folder_presentation':1}
  self.data={}
  for st in F.STYLES:
   for kind in ('style.txt','label.spr','caption.spr'):
    path='songs/'+st+'/'+kind;self.data[path]=(st+' old '+kind).encode()
  self.data['songs/red/id_0/song.txt']=b'base song'
  for n,b in self.data.items():p=self.root/n;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(b);base['files'][n]={'bytes':len(b),'sha256':M.hash_bytes(b)}
  base['content_id']=M.content_id(base);self.base=base;M.finalize_base(self.root,base)
  self.pbfiles={}
  for st,r in self.art.items():
   for k in ('label','caption'):self.pbfiles[r[k]]=(st+' original PB '+k).encode()
  self.pbfiles['songs/red/pb_0/song.txt']=b'PB song'
  self.pb=self.pack('pb',self.pbfiles,[dict(source='pb',sid=0,style='red',dir='pb_0')],self.art)
  self.red=self.pack('red',{'songs/red/red_0/song.txt':b'RED song'},[dict(source='red',sid=0,style='red',dir='red_0')])
 def tearDown(self):self.temp.cleanup()
 def pack(self,source,files,songs,art=None):
  p=self.top/source;p.mkdir();manifest=dict(schema=1,kind='append',source=source,contract='test',songs=songs,payload_prefixes=['a']+['songs/'+s['style']+'/'+s['dir'] for s in songs],files={n:dict(bytes=len(b),sha256=M.hash_bytes(b)) for n,b in files.items()})
  if art:manifest['folder_art']=art
  manifest['content_id']=M.content_id(manifest)
  for n,b in files.items():f=p/n;f.parent.mkdir(parents=True,exist_ok=True);f.write_bytes(b)
  (p/'megamix-package.json').write_text(json.dumps(manifest));return p
 def check(self,pb):
  self.assertEqual(M.verify(self.root),[])
  for st in F.STYLES:
   text=(self.root/'songs'/st/'style.txt').read_text();self.assertIn('name '+F.NAMES[st],text)
   self.assertIn('folder '+('pb-label.spr' if pb else'label.spr'),text)
   if pb:self.assertEqual((self.root/'songs'/st/'pb-label.spr').read_bytes(),self.pbfiles[self.art[st]['label']])
 def test_fallback_without_pb(self):self.check(False)
 def test_pb_first(self):M.install(self.pb,self.root,progress=lambda _:None);M.install(self.red,self.root,progress=lambda _:None);self.check(True)
 def test_pb_last(self):M.install(self.red,self.root,progress=lambda _:None);M.install(self.pb,self.root,progress=lambda _:None);self.check(True)
 def test_reimport_does_not_downgrade(self):
  M.install(self.pb,self.root,progress=lambda _:None);before=(self.root/'launcher/source.id').read_bytes();M.install(self.pb,self.root,progress=lambda _:None);self.check(True);self.assertEqual(before,(self.root/'launcher/source.id').read_bytes())
 def old_pb(self):
  p=M.read_manifest(self.pb);art=p.pop('folder_art')
  for r in art.values():
   for k in ('label','caption'):p['files'].pop(r[k])
  p['content_id']=M.content_id(p);old=self.top/'pb-old';old.mkdir()
  for n in p['files']:
   dst=old/n;dst.parent.mkdir(parents=True,exist_ok=True);dst.write_bytes((self.pb/n).read_bytes())
  (old/'megamix-package.json').write_text(json.dumps(p));return old
 def test_old_pack_reimport_preserves_native_art(self):
  old=self.old_pb();M.install(self.pb,self.root,progress=lambda _:None);before=(self.root/'.megamix/installed.json').read_bytes()
  M.install(old,self.root,progress=lambda _:None);self.check(True);self.assertEqual(before,(self.root/'.megamix/installed.json').read_bytes())
 def test_upgrade_old_pb_adds_only_presentation(self):
  old=self.old_pb();M.install(old,self.root,progress=lambda _:None);self.check(False)
  M.install(self.pb,self.root,progress=lambda _:None);self.check(True)
 def test_song_revision_still_requires_explicit_migration(self):
  M.install(self.pb,self.root,progress=lambda _:None);p=M.read_manifest(self.pb);p['songs'][0]['sid']=1;p['songs'][0]['dir']='pb_1';p['content_id']=M.content_id(p)
  with self.assertRaises(ValueError):F.same_song_payload(M.read_manifest(self.pb),p)
 def test_art_tampering_detected(self):
  M.install(self.pb,self.root,progress=lambda _:None);(self.root/'songs/red/pb-label.spr').write_bytes(b'wrong')
  self.assertTrue(M.verify(self.root))
 def test_payload_corruption_refused_before_activation(self):
  p=self.pb/self.art['red']['label'];p.write_bytes(b'wrong')
  with self.assertRaises(ValueError):M.install(self.pb,self.root,progress=lambda _:None)
  self.check(False)
 def test_art_owned_only_by_pb(self):
  p=M.read_manifest(self.red);p['folder_art']=self.art;p['content_id']=M.content_id(p)
  with self.assertRaises(ValueError):M.validate_manifest(p)
 def test_traversal_and_foreign_paths_refused(self):
  for path in ('../launcher.cfg','launcher.cfg','songs/red/id_0/ui/red/label.spr'):
   p=M.read_manifest(self.pb);p['folder_art']['red']['label']=path;p['content_id']=M.content_id(p)
   with self.assertRaises(ValueError):M.validate_manifest(p)
 def test_settings_scores_and_fallback_files_preserved(self):
  (self.root/'launcher.cfg').write_bytes(b'user settings');(self.root/'scores.bin').write_bytes(b'scores');original={n:(self.root/n).read_bytes() for n in self.data if n.endswith('.spr')}
  M.install(self.pb,self.root,progress=lambda _:None);self.check(True)
  self.assertEqual((self.root/'launcher.cfg').read_bytes(),b'user settings');self.assertEqual((self.root/'scores.bin').read_bytes(),b'scores')
  for n,b in original.items():self.assertEqual((self.root/n).read_bytes(),b)
if __name__=='__main__':unittest.main(verbosity=2)
