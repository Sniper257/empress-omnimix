import unittest,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'offdisc'))
import chart_catalog as C
class CoalescePolicyTests(unittest.TestCase):
 def setUp(self):self.data=C.load(Path(__file__).with_name('published_catalog.json'))
 def charts(self,source,sid,charts):return {f'{source}:{sid}:{ch}':1 for ch in charts}
 def test_combined_entry_has_every_noncolliding_chart(self):
  usa=self.charts('usa',68,['SPN','SPH','SPB','DPN','DPH']);red=self.charts('red',88,['SPN','SPH','SPA','DPN','DPH','DPA'])
  self.assertEqual(C.resolve(self.data,usa|red),set(self.charts('usa',68,['SPN','SPH','SPA','SPB','DPN','DPH','DPA'])))
 def test_only_one_source_never_manufactures_missing_charts(self):
  for source,sid,charts in [('usa',68,['SPN','SPH','SPB','DPN','DPH']),('red',88,['SPN','SPH','SPA','DPN','DPH','DPA'])]:
   available=self.charts(source,sid,charts);self.assertEqual(C.resolve(self.data,available),set(available))
 def test_existing_score_ids_stay_separate(self):
  for ch in ['SPN','SPH','DPN','DPH']:
   self.assertEqual(C.canonical(self.data,'usa:68:'+ch),'usa:68:'+ch);self.assertEqual(C.canonical(self.data,'red:88:'+ch),'red:88:'+ch)
  for ch in ['SPA','DPA']:self.assertEqual(C.canonical(self.data,'usa:68:'+ch),'red:88:'+ch)
  self.assertEqual(C.canonical(self.data,'usa:68:SPB'),'usa:68:SPB')
 def test_explicit_rule_and_shared_display_groups(self):
  self.assertIn('M usa 68 red 88 84 63',C.emit(self.data))
  for ch in ['SPN','SPH','DPN','DPH']:self.assertEqual(C.listing_key(self.data,'red:88:'+ch),C.listing_key(self.data,'usa:68:'+ch))
 def test_murmur_timing_variants_now_share_display_group(self):
  self.assertEqual(C.listing_key(self.data,'eighth:32:SPA'),C.listing_key(self.data,'pb:53:SPA'))
if __name__=='__main__':unittest.main()
