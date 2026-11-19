"""BGA ownership and plan annotation use generated graphics only."""
from pathlib import Path
import copy,hashlib,json,tempfile,unittest
from bga_catalog import attach

class CatalogOwnership(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory();self.root=Path(self.tmp.name);self.art=self.root/'art';self.art.write_bytes(b'reviewed animation');self.sha=hashlib.sha256(self.art.read_bytes()).hexdigest()
  self.spec=dict(op='bga',format=1,version=3,palette=1,clips=[1]);self.catalog=self.root/'catalog.json';self.catalog.write_text(json.dumps(dict(format=1,sources={'third':{self.sha:self.spec}})))
 def tearDown(self):self.tmp.cleanup()
 def plan(self,source='third',reason='Pregenerated converted game graphics, keyed to exact source disc'):
  return dict(discs=[dict(source=source,targets={'append':dict(files={'a/00/00':dict(path=str(self.art),generated=reason)})})])
 def test_correct_owner_and_independent_recipe(self):
  plan=self.plan();self.assertEqual(attach(plan,self.catalog),1);derived=plan['discs'][0]['targets']['append']['files']['a/00/00']['derive'];self.assertEqual(derived,self.spec)
  derived['clips'].append(2);self.assertEqual(self.spec['clips'],[1])
 def test_other_disc_does_not_supply_same_art(self):self.assertEqual(attach(self.plan('fourth'),self.catalog),0)
 def test_unrelated_generated_file_is_not_read(self):
  plan=self.plan(reason='Metadata');plan['discs'][0]['targets']['append']['files']['a/00/00']['path']=str(self.root/'absent');self.assertEqual(attach(plan,self.catalog),0)
 def test_direct_iso_payload_is_not_read(self):
  plan=self.plan();row=plan['discs'][0]['targets']['append']['files']['a/00/00'];row.pop('generated');row['origin']=[0,8];row['path']=str(self.root/'absent');self.assertEqual(attach(plan,self.catalog),0)
 def test_no_output_hash_match(self):self.art.write_bytes(b'other art');self.assertEqual(attach(self.plan(),self.catalog),0)
 def test_unknown_catalog_format(self):
  self.catalog.write_text('{"format":99}')
  with self.assertRaises(ValueError):attach(self.plan(),self.catalog)
if __name__=='__main__':unittest.main()
