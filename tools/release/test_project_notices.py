"""Original-project notices must accompany source-built tools and base installs."""
from pathlib import Path
import hashlib,json,tempfile,unittest,zipfile
from contextlib import redirect_stdout
from io import StringIO
from pack_release import pack
from megamix import validate_manifest
ROOT=Path(__file__).resolve().parents[2]

class ProjectNotices(unittest.TestCase):
    def test_base_recipes_include_the_project_licence_and_notices(self):
        for target in ('console','host'):
            r=json.loads((ROOT/'release-inputs/recipes'/('id-'+target+'.json')).read_bytes());validate_manifest(r['package'])
            for source,destination in [('LICENSE','licenses/MEGAMIX-MIT.txt'),('NOTICE.txt','licenses/MEGAMIX-NOTICES.txt')]:
                b=(ROOT/source).read_bytes();step=r['files'][destination]
                self.assertEqual(step['op'],'blob');self.assertEqual((ROOT/'release-inputs/blobs'/step['blob']).read_bytes(),b)
                self.assertEqual(r['package']['files'][destination],{'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest()})
    def test_download_includes_project_notice_texts(self):
        with tempfile.TemporaryDirectory()as tmp:
            tmp=Path(tmp);capsule=tmp/'fixture.mmr';out=tmp/'builder.zip'
            release={'schema':1,'discs':[{'kind':'base','recipes':{'console':'recipe.json'}}]*16}
            recipe={'package':{'files':{'MEGAMIX.ELF':{}}},'files':{}}
            with zipfile.ZipFile(capsule,'w')as z:z.writestr('release.json',json.dumps(release));z.writestr('recipe.json',json.dumps(recipe))
            with redirect_stdout(StringIO()):pack(capsule,out,ROOT/'tools/release/README.txt')
            with zipfile.ZipFile(out)as z:
                self.assertEqual(z.read('LICENSE'),(ROOT/'LICENSE').read_bytes());self.assertEqual(z.read('NOTICE.txt'),(ROOT/'NOTICE.txt').read_bytes())
                receipt=json.loads(z.read('release-files.json'))
                self.assertEqual(receipt['LICENSE'],hashlib.sha256(z.read('LICENSE')).hexdigest())

if __name__=='__main__':unittest.main()
