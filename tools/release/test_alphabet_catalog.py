import json,tempfile,unittest
from pathlib import Path
import alphabet_catalog as A
class AlphabetCatalogTest(unittest.TestCase):
 def test_complete_and_deterministic(self):
  rows=A.load();self.assertEqual(len(rows),1332);self.assertEqual(A.render(),A.render());self.assertTrue(A.render().startswith(b'V 1\n'))
 def test_confirmed_full_names(self):
  rows=A.load();self.assertEqual(rows['gold',2],'ANDROMEDA II');self.assertEqual(rows['djt',64],'SWEETEST SAVAGE');self.assertLess(rows['pb',35],rows['gold',2])
 def test_reject_invalid_catalogs(self):
  base=json.loads(Path(A.__file__).with_suffix('.json').read_bytes())
  for rows in (base[:-1],base+[base[0]],base+[dict(source='unknown',sid=0,key='X')],[dict(r,key='\t')if i==0 else r for i,r in enumerate(base)]):
   with tempfile.TemporaryDirectory()as d:
    p=Path(d)/'rows.json';p.write_text(json.dumps(rows))
    with self.assertRaises(ValueError):A.load(p)
if __name__=='__main__':unittest.main()
