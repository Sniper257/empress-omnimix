"""R171 user-reviewed chart coverage, slots, preferences and source fallbacks."""
import json,sys,unittest,hashlib
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'offdisc'))
import chart_catalog as C
class ReviewedCatalog(unittest.TestCase):
 @classmethod
 def setUpClass(cls):cls.data=C.load(Path(__file__).with_name('published_catalog.json'))
 def visible(self,*sources):
  rows=self.data['sources'];keys={r['source']+':'+str(r['sid'])+':'+ch:1 for r in rows if r['source']in sources for ch in r['charts']if ch not in r.get('borrowed_charts',{})};return C.resolve(self.data,keys)
 def test_preserve_four_unique_nonbeginner_patterns(self):
  v=self.visible('id','pb','usa','third','fourth')
  for a,b in [('pb:54:SPH','usa:52:SPH'),('fourth:2:SPA','third:24:SPA'),('fourth:48:SPH','usa:62:SPH')]:
   self.assertNotEqual(C.listing_key(self.data,a),C.listing_key(self.data,b))
  self.assertTrue({'pb:54:SPH','usa:52:SPH','fourth:2:SPA','third:24:SPA','id:84:SPH','fourth:48:SPH','id:84:SPN','usa:62:SPN'}<=v)
 def test_beginner_newest_even_when_patterns_differ(self):
  v=self.visible('pb','usa','seventh');self.assertIn('pb:88:SPB',v)
  self.assertNotIn('usa:44:SPB',v);self.assertNotIn('seventh:48:SPB',v)
  self.assertIn('usa:44:SPB',self.visible('usa','seventh'));self.assertIn('seventh:48:SPB',self.visible('seventh'))
 def test_beginner_policy_does_not_merge_homonymous_songs(self):
  rows=[r for r in self.data['sources']if r['name'].casefold()=='switch'];groups={C.listing_key(self.data,r['source']+':'+str(r['sid'])+':SPB')for r in rows if 'SPB'in r['charts']};self.assertEqual(len(groups),2)
 def test_beginner_does_not_inherit_older_source_preference(self):
  v=self.visible('usa','seventh');self.assertIn('usa:45:SPB',v);self.assertNotIn('seventh:73:SPB',v);self.assertIn('seventh:73:SPH',v);self.assertNotIn('usa:45:SPH',v)
 def test_preference_is_conditional_on_disc_availability(self):
  self.assertIn('tenth:59:SPA',self.visible('tenth','usa'));self.assertNotIn('usa:29:SPA',self.visible('tenth','usa'));self.assertIn('usa:29:SPA',self.visible('usa'));self.assertIn('tenth:59:SPA',self.visible('tenth'))
 def test_cross_slot_remix_fallbacks(self):
  for old,new,a,b in [('red','distorted',80,74),('happysky','gold',68,57)]:
   v=self.visible(old,new)
   for ch in ('SPA','DPA'):
    self.assertNotIn(f'{old}:{a}:{ch}',v);self.assertIn(f'{new}:{b}:{ch}',v);self.assertIn(f'{old}:{a}:{ch}',self.visible(old))
 def test_era_sp_mix_patterns_stay_separate(self):
  v=self.visible('pb');self.assertIn('pb:81:SPA',v);self.assertIn('pb:83:SPA',v)
 def test_native_difficulty_slots_are_not_collapsed_by_alias_labels(self):
  v=self.visible('pb');self.assertTrue({'pb:90:SPA','pb:90:SPX','pb:88:DPN','pb:88:DPH','pb:96:DPN','pb:96:DPH','pb:96:DPA','pb:91:SPN','pb:91:SPH'}<=v)
 def test_two_slow_variants_stay_available(self):
  v=self.visible('id','third','fourth','gold')
  self.assertTrue({'third:36:SPN','third:36:SPH','third:36:DPN','third:36:DPH','fourth:12:SPN','fourth:12:SPH','fourth:12:SPA','fourth:12:DPN','fourth:12:DPH','fourth:12:DPA'}<=v)
 def test_specific_timing_false_positives_group(self):
  for a,b in [('red:54:DPH','fourth:42:DPH'),('id:67:DPN','red:25:DPN')]:self.assertEqual(C.listing_key(self.data,a),C.listing_key(self.data,b))
 def test_genuine_old_dp_lane_changes_remain(self):
  for a,b in [('distorted:65:DPA','seventh:29:DPA'),('distorted:65:DPH','seventh:29:DPH'),('red:49:DPH','eighth:86:DPH')]:self.assertNotEqual(C.listing_key(self.data,a),C.listing_key(self.data,b))
 def test_old_snow_uses_cs9_when_present(self):
  v=self.visible('ninth','usa','djt')
  for ch in ('SPA','SPH','DPA'):self.assertIn('ninth:30:'+ch,v);self.assertNotIn('usa:58:'+ch,v);self.assertIn('djt:63:'+ch,v)
 def test_emitted_preferences_fit_existing_byte_without_changing_score_id(self):
  data=self.data;rows=[l.split()for l in C.emit(data).splitlines()if l.startswith('C tenth 59 ')];self.assertTrue(all(int(r[4])<=255 for r in rows));self.assertTrue(any(int(r[4])>1 for r in rows))
  for r in rows:
   k='tenth:59:'+data['chart_order'][int(r[3])];self.assertEqual(r[5],C.uid('chart',C.canonical(data,k)).hex())
if __name__=='__main__':unittest.main()
