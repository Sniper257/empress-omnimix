import unittest,json,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'offdisc'))
import chart_catalog as C
class Cleanup(unittest.TestCase):
 def setUp(self):self.c=C.load(Path(__file__).with_name('published_catalog.json'))
 def available(self,*sources):return {r['source']+':'+str(r['sid'])+':'+ch:1 for r in self.c['sources']if r['source']in sources for ch in r['charts']if ch not in r.get('borrowed_charts',{})}
 def test_remix_fallbacks(self):
  for old,new,a,b in [('red','distorted',80,74),('happysky','gold',68,57)]:
   one=C.resolve(self.c,self.available(old));both=C.resolve(self.c,self.available(old,new))
   for ch in ('SPA','DPA'):
    self.assertIn(f'{old}:{a}:{ch}',one);self.assertNotIn(f'{old}:{a}:{ch}',both);self.assertIn(f'{new}:{b}:{ch}',both)
   for ch in ('SPN','SPH','DPN','DPH'):self.assertIn(f'{old}:{a}:{ch}',both)
 def test_snow_unique_hyper_retained(self):
  v=C.resolve(self.c,self.available('ninth','usa','djt'))
  self.assertIn('ninth:30:SPH',v);self.assertNotIn('usa:58:SPH',v);self.assertIn('djt:63:SPH',v)
  for ch in ('SPA','DPA'):
   self.assertIn('ninth:30:'+ch,v);self.assertNotIn('usa:58:'+ch,v);self.assertIn('djt:63:'+ch,v)
  self.assertIn('djt:63:SPB',v);self.assertNotIn('usa:58:SPB',v)
 def test_dissolve_old_variants_retained(self):
  v=C.resolve(self.c,self.available('sixth','eighth','pb'))
  for ch in ('SPN','SPH','SPA','DPH'):
   self.assertNotIn('sixth:63:'+ch,v);self.assertNotIn('eighth:46:'+ch,v);self.assertIn('pb:51:'+ch,v)
  self.assertNotIn('sixth:63:SPB',v);self.assertIn('pb:51:SPB',v);self.assertIn('eighth:46:DPN',v);self.assertIn('pb:51:DPN',v)
 def test_display_bridge_never_changes_song_family(self):
  rows={r['source']+':'+str(r['sid']):r for r in self.c['sources']}
  self.assertNotEqual(rows['red:80']['family'],rows['distorted:74']['family'])
  self.assertNotEqual(C.canonical(self.c,'red:80:SPA'),C.canonical(self.c,'distorted:74:SPA'))
 def test_attitude_reviewed_cs10_preference_and_solo(self):
  v=C.resolve(self.c,self.available('eighth','tenth','usa'))
  for ch in ('SPN','SPH','SPA','DPN','DPH','DPA'):
   self.assertIn('tenth:59:'+ch,v);self.assertNotIn('usa:29:'+ch,v)
  self.assertIn('usa:29:SPB',v)
  self.assertIn('tenth:59:SPA',C.resolve(self.c,self.available('tenth')))
