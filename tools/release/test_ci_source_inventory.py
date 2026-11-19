"""The CI source audit must survive its own generated downloads and ZIP output."""
from pathlib import Path
import hashlib,json,sys,tempfile,unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'ci'))
from audit import audit
from source_tree import source_files

class SourceInventory(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.root=Path(self.tmp.name)
        inputs=self.root/'release-inputs';(inputs/'recipes').mkdir(parents=True);(inputs/'blobs').mkdir()
        data=b'authored notice\n';h=hashlib.sha256(data).hexdigest();(inputs/'blobs'/h).write_bytes(data)
        (inputs/'release.json').write_text(json.dumps({'discs':[{}]*16}))
        recipe={'files':{'README.txt':{'op':'blob','blob':h}},'package':{'files':{'README.txt':{'bytes':len(data),'sha256':h}}}}
        for i in range(17):(inputs/'recipes'/(str(i)+'.json')).write_text(json.dumps(recipe))
        (self.root/'.github').mkdir();(self.root/'.github/workflow.yml').write_text('workflow')
        self.seal()
    def tearDown(self):self.tmp.cleanup()
    def seal(self):
        rows={p.relative_to(self.root).as_posix():hashlib.sha256(p.read_bytes()).hexdigest()for p in source_files(self.root)if p.name!='source-files.json'}
        (self.root/'source-files.json').write_text(json.dumps(rows))
    def test_build_products_and_dependency_cache_do_not_break_final_audit(self):
        audit(self.root)
        for name in ['dist/builder.zip','.build-dependencies/toolchain/private.ttf','build/sdk/output.elf','work/user.iso','.venv/lib/generated.py','.git/config','tools/__pycache__/generated.pyc']:
            p=self.root/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(b'local output')
        self.assertEqual(audit(self.root)['recipes'],17)
        self.assertIn('.github/workflow.yml',json.loads((self.root/'source-files.json').read_text()))
    def test_unaccounted_source_file_still_rejected(self):
        (self.root/'unexpected.txt').write_text('not reviewed')
        with self.assertRaisesRegex(ValueError,'untracked'):audit(self.root)
    def test_private_font_in_source_still_rejected(self):
        (self.root/'private.ttf').write_bytes(b'font')
        with self.assertRaisesRegex(ValueError,'Private font'):audit(self.root)
    def test_source_symlink_rejected(self):
        try:(self.root/'linked-source').symlink_to(self.root/'.github',target_is_directory=True)
        except (OSError,NotImplementedError):self.skipTest('Symlinks unavailable')
        with self.assertRaisesRegex(ValueError,'Symlink'):audit(self.root)

if __name__=='__main__':unittest.main()
