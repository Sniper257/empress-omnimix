"""Release boundaries: real xdelta encoder, independent Python consumer, recovery.
Run python3 -m unittest discover -s tools/release -p test_release.py -v
"""
import copy,hashlib,io,json,os,random,shutil,subprocess,sys,tempfile,unittest,zipfile,struct
from pathlib import Path
from unittest.mock import patch
import megamix as M
from vcdiff import decode
from freeze_release import freeze
from resume_freeze import resume


class ReleaseTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.root=Path(self.tmp.name)
        self.iso=self.root/'disc.iso';self.original=random.Random(717).randbytes(100000)
        self.iso.write_bytes(self.original)

    def tearDown(self):self.tmp.cleanup()

    def bundle(self, sources=('id','red','third'), runtime=b'homebrew', tag='', contract='test-v1'):
        discs=[]
        for i,source in enumerate(sources):
            iso=self.root/(source+'.iso');iso.write_bytes(self.original+bytes([i]))
            tree=self.root/(source+tag+'-author');tree.mkdir()
            songs=[];paths={}
            data=tree/'music';data.write_bytes(self.original[123:90123])
            name=M.asset_path(M.hash_file(data))
            paths[name]={'path':str(data),'origin':[123,90000]}
            song=tree/'song.txt';song.write_text('name TEST\n'+source+' 1\n')
            style='red'if source!='third'else'3rd_style';folder=source+'_1'
            songs=[{'source':source,'sid':1,'style':style,'dir':folder}]
            paths[f'songs/{style}/{folder}/song.txt']={'path':str(song),'generated':'reviewed source metadata'}
            changed=tree/'modified';changed.write_bytes(self.original[:400]+b'generated title graphic'+self.original[417:])
            paths[M.asset_path(M.hash_file(changed))]={'path':str(changed),'origin':[0,len(self.original)]}
            if source=='id':
                audio=bytearray(0x8020+64);audio[:4]=b'wp\x02\x00';struct.pack_into('<2I',audio,0x8000,1,64);audio[0x8020:]=bytes(range(64))
                original_audio=bytes(audio);offset=iso.stat().st_size
                with iso.open('ab')as f:f.write(original_audio)
                audio[2]=3;struct.pack_into('<I',audio,0x8008,1)
                for frame in range(0x8020,len(audio),16):audio[frame]^=255;audio[frame+2]=(audio[frame+2]-36)&255
                out=tree/'audio';out.write_bytes(audio);paths[M.asset_path(M.hash_file(out))]={'path':str(out),'origin':[offset,len(original_audio)]}
                old=bytearray(0x4040);struct.pack_into('<3I',old,0,0x4000,0,0);struct.pack_into('<HHBBBB6I',old,32,1,0,0,127,64,3,48000,0,0,0,64,0);old[0x4000:]=bytes(range(64))
                offset=iso.stat().st_size
                with iso.open('ab')as f:f.write(old)
                repacked=bytearray(0x8020+64);repacked[:4]=b'wp\x03\x00';repacked[8:10]=bytes([63,63]);struct.pack_into('<4I',repacked,0x8000,1,64,1,0);data=bytearray(range(64))
                for frame in range(0,64,16):data[frame]^=255;data[frame+2]=(data[frame+2]-36)&255
                repacked[0x8020:]=data;out=tree/'old-audio';out.write_bytes(repacked);paths[M.asset_path(M.hash_file(out))]={'path':str(out),'origin':[offset,len(old)]}
                runtime_file=tree/'runtime';runtime_file.write_bytes(runtime)
                paths['launcher/vdisc-portable.bin']={'path':str(runtime_file),'generated':'homebrew runtime'}
            target='console'if source=='id'else'append'
            discs.append({'source':source,'name':source,'kind':'base'if source=='id'else'append','image':str(iso),'iso':{'sha256':M.hash_file(iso),'bytes':iso.stat().st_size},'targets':{target:{'files':paths,'songs':songs,'payload_prefixes':(['a',f'songs/{style}/{folder}'] if source!='id' else [])}}})
        plan={'version':'test'+tag,'contract':contract,'style_order':['red','3rd_style'],'discs':discs}
        p=self.root/('plan'+tag+'.json');p.write_text(json.dumps(plan));bundle=self.root/('release'+tag+'.mmr');freeze(p,bundle)
        return bundle

    def test_freezer_fsync_accepts_windows_descriptor_rules(self):
        native_open=Path.open;native_fsync=os.fsync;handles={};synced=[]
        def tracked_open(path,*args,**kwargs):
            f=native_open(path,*args,**kwargs);handles[f.fileno()]=f
            return f
        def windows_fsync(fd):
            f=handles[fd]
            if not f.writable():raise OSError(9,'Bad file descriptor')
            synced.append(f.name);native_fsync(fd)
        with patch.object(Path,'open',autospec=True,side_effect=tracked_open),patch('os.fsync',side_effect=windows_fsync):
            bundle=self.bundle(('id',));resumed=self.root/'resumed.mmr'
            plan_path=self.root/'plan.json';plan=json.loads(plan_path.read_bytes())
            with zipfile.ZipFile(bundle)as archive:plan['asset_map']=json.loads(archive.read('recipes/id-console.json'))['package']['asset_map']
            plan_path.write_bytes(M.canonical(plan))
            resume(plan_path,bundle,resumed)
        self.assertTrue(bundle.is_file());self.assertTrue(resumed.is_file())
        self.assertEqual(len(synced),2)
        self.assertFalse(bundle.with_name(bundle.name+'.incomplete').exists())
        self.assertFalse(resumed.with_suffix('.merged.incomplete').exists())

    def test_no_external_user_dependencies_and_idempotent_install(self):
        bundle=self.bundle();base=self.root/'MEGAMIX';red=self.root/'red-pack'
        # The consumer needs no PATH executables, site packages or font files.
        with patch.dict(os.environ,{'PATH':''}):
            M.build(self.root/'id.iso',base,bundle,progress=lambda _:None)
            M.build(self.root/'red.iso',red,bundle,progress=lambda _:None)
            M.install(red,base,progress=lambda _:None)
        self.assertEqual(M.verify(base),[])
        cfg=(base/'launcher.cfg').read_bytes();seal=(base/'launcher/source.id').read_bytes()
        (base/'launcher.cfg').write_bytes(cfg+b'gn1 290\n');(base/'saved.bin').write_bytes(b'my score')
        M.install(red,base,progress=lambda _:None)
        self.assertEqual((base/'launcher/source.id').read_bytes(),seal)
        self.assertEqual((base/'saved.bin').read_bytes(),b'my score')
        self.assertEqual(M.verify(base),[])
        self.assertIn('red_1',(base/'songs/red/index.txt').read_text())

    def test_standalone_cli_without_site_packages_or_external_tools(self):
        bundle=self.bundle(('id','red'))
        clean=self.root/'clean-release';clean.mkdir()
        for name in ['megamix.py','vcdiff.py','xor_delta.py','audio_transform.py','folder_presentation.py','source_versions.py','source_versions.json']:
            shutil.copy2(Path(__file__).parent/name,clean/name)
        shutil.copy2(bundle,clean/'release.mmr')
        for encoding in ('utf-8','cp1252','ascii'):
            with self.subTest(output_encoding=encoding):
                env=dict(os.environ,PATH='',PYTHONPATH='',PYTHONNOUSERSITE='1',PYTHONUTF8='0',PYTHONIOENCODING=encoding+':strict')
                dest=self.root/('standalone-base-'+encoding)
                result=subprocess.run([sys.executable,'-S',str(clean/'megamix.py'),str(self.root/'id.iso'),str(dest)],env=env,cwd=clean,capture_output=True,text=True,encoding=encoding)
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                result=subprocess.run([sys.executable,'-S',str(clean/'megamix.py'),'verify',str(dest)],env=env,cwd=clean,capture_output=True,text=True,encoding=encoding)
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        with zipfile.ZipFile(bundle)as archive:
            recipe=json.loads(archive.read('recipes/id-console.json'))
            self.assertTrue(any(r['op']=='xor'for r in recipe['files'].values()))
            self.assertTrue(any(r['op']=='audio'for r in recipe['files'].values()))
            self.assertTrue(any(r['op']=='bank'for r in recipe['files'].values()))

    def test_reversed_append_order_same_indexes_and_seal(self):
        bundle=self.bundle();bases=[];packs={}
        for source in ('red','third'):
            out=self.root/(source+'-pack');M.build(self.root/(source+'.iso'),out,bundle,progress=lambda _:None);packs[source]=out
        for n,order in enumerate([('red','third'),('third','red')]):
            out=self.root/('base'+str(n));M.build(self.root/'id.iso',out,bundle,progress=lambda _:None)
            for source in order:M.install(packs[source],out,progress=lambda _:None)
            self.assertEqual(M.verify(out),[]);bases.append(out)
        for name in ['songs/styles.txt','songs/red/index.txt','songs/3rd_style/index.txt','launcher/source.id']:
            self.assertEqual((bases[0]/name).read_bytes(),(bases[1]/name).read_bytes())

    def test_hand_added_styles_are_in_the_coverage_index(self):
        root=self.root/'cov';(root/'songs/copula/mz').mkdir(parents=True);(root/'songs/copula/mz/song.txt').write_text('x')
        (root/'songs/copula/index.txt').write_text('mz\n');(root/'songs/styles.txt').write_text('copula\nred\n')
        state={'schema':1,'contract':'c','style_order':['red','3rd_style'],'generated':{},
               'packages':{'id':{'source':'id','songs':[{'source':'id','sid':1,'style':'red','dir':'id_1'}],'selection':{'id:1':{'charts':[],'rank':0,'source_id':'0'}},'content_id':'x'}}}
        M.activate_indexes(root,state)
        self.assertEqual((root/'songs/styles.txt').read_text(),'copula\nred\n')
        self.assertEqual((root/'catalog/coverage.txt').read_text(),'V 1\nS 0 copula\nS 1 red\n')

    def test_hand_added_song_folders_survive_install_update_and_index(self):
        bundle=self.bundle(('id','red'));base=self.root/'base';pack=self.root/'pack'
        M.build(self.root/'id.iso',base,bundle,progress=lambda _:None);M.build(self.root/'red.iso',pack,bundle,progress=lambda _:None)
        for style,d in (('copula','mesmerizer'),('red','elisha_cn')):
            (base/'songs'/style/d).mkdir(parents=True);(base/'songs'/style/d/'song.txt').write_text('name X\n')
        (base/'songs/copula/index.txt').write_text('mesmerizer\nmissing_folder\n')
        (base/'songs/red/index.txt').write_text('elisha_cn\n')
        (base/'songs/styles.txt').write_text('copula\n'+(base/'songs/styles.txt').read_text()+'red\n')
        log=[];M.reindex(base,progress=log.append);self.assertIn('2 hand-added',log[-1])
        self.assertEqual(M.verify(base),[])
        M.install(pack,base,progress=lambda _:None)
        self.assertEqual((base/'songs/red/index.txt').read_text(),'id_1\nred_1\nelisha_cn\n')
        self.assertEqual((base/'songs/copula/index.txt').read_text(),'mesmerizer\n')
        self.assertEqual((base/'songs/styles.txt').read_text().splitlines()[0],'copula')
        styles=(base/'songs/styles.txt').read_text().split()
        M.update(self.root/'id.iso',base,self.bundle(('id',),runtime=b'v2',tag='-v2'),progress=lambda _:None)
        self.assertIn('copula',(base/'songs/styles.txt').read_text().split())
        self.assertIn('elisha_cn',(base/'songs/red/index.txt').read_text().split())
        self.assertEqual(M.verify(base),[])

    def test_update_replaces_runtime_and_keeps_packs(self):
        bundle=self.bundle(('id','red'));base=self.root/'base';pack=self.root/'pack'
        M.build(self.root/'id.iso',base,bundle,progress=lambda _:None);M.build(self.root/'red.iso',pack,bundle,progress=lambda _:None)
        M.install(pack,base,progress=lambda _:None)
        (base/'launcher.cfg').write_bytes(b'gn1 290\n');(base/'saved.bin').write_bytes(b'my score')
        seal=(base/'launcher/source.id').read_bytes()
        newer=self.bundle(('id',),runtime=b'homebrew v2',tag='-v2')
        log=[];M.update(self.root/'id.iso',base,newer,progress=log.append)
        self.assertEqual((base/'launcher/vdisc-portable.bin').read_bytes(),b'homebrew v2')
        self.assertEqual(M.verify(base),[])
        self.assertNotEqual((base/'launcher/source.id').read_bytes(),seal)
        self.assertEqual((base/'launcher.cfg').read_bytes(),b'gn1 290\n');self.assertEqual((base/'saved.bin').read_bytes(),b'my score')
        self.assertIn('red_1',(base/'songs/red/index.txt').read_text())
        self.assertFalse((base/'.megamix/update-journal.json').exists())
        log=[];M.update(self.root/'id.iso',base,newer,progress=log.append);self.assertIn('nothing changed',log[-1])
        other=self.bundle(('id',),runtime=b'homebrew v3',tag='-v3',contract='test-v2')
        with self.assertRaisesRegex(ValueError,'different data contract'):M.update(self.root/'id.iso',base,other,progress=lambda _:None)
        self.assertEqual((base/'launcher/vdisc-portable.bin').read_bytes(),b'homebrew v2')

    def test_newer_compatible_base_accepts_older_data_pack(self):
        bundle=self.bundle(('id','red'));base=self.root/'base';pack=self.root/'pack'
        M.build(self.root/'id.iso',base,bundle,progress=lambda _:None);M.build(self.root/'red.iso',pack,bundle,progress=lambda _:None)
        state=json.loads((base/'.megamix/installed.json').read_text());b=state['packages']['id']
        # Model an append-only asset-table extension in a compatible newer base.
        b['asset_map']['a/fe/01']='f'*64;b['asset_map_hash']=M.hash_bytes(M.canonical(b['asset_map']));b['runtime_revision']='newer-compatible';b['content_id']=M.content_id(b)
        M.activate_indexes(base,state)
        old=M.read_manifest(pack);old_id=old['content_id'];old['generator_revision']='different-maintainer-build'
        self.assertEqual(M.content_id(old),old_id)
        M.save_json(pack/'megamix-package.json',old)
        cfg=(base/'launcher.cfg').read_bytes();M.install(pack,base,progress=lambda _:None)
        self.assertEqual(M.verify(base),[]);self.assertEqual((base/'launcher.cfg').read_bytes(),cfg)

    def test_reinstall_repairs_missing_owned_payload(self):
        bundle=self.bundle(('id','red'));base=self.root/'base';pack=self.root/'pack'
        M.build(self.root/'id.iso',base,bundle,progress=lambda _:None);M.build(self.root/'red.iso',pack,bundle,progress=lambda _:None);M.install(pack,base,progress=lambda _:None)
        cfg=(base/'launcher.cfg').read_bytes();missing=base/'songs/red/red_1/song.txt';missing.unlink()
        M.install(pack,base,progress=lambda _:None);self.assertTrue(missing.is_file());self.assertEqual(M.verify(base),[]);self.assertEqual((base/'launcher.cfg').read_bytes(),cfg)

    def test_maintainer_completed_target_checkpoint_resume(self):
        bundle=self.bundle();plan=json.loads((self.root/'plan.json').read_text())
        with zipfile.ZipFile(bundle)as archive:plan['asset_map']=json.loads(archive.read('recipes/id-console.json'))['package']['asset_map']
        (self.root/'plan.json').write_text(json.dumps(plan));output=self.root/'resumed.mmr'
        resume(self.root/'plan.json',bundle,output)
        receipt=json.loads((self.root/'freeze-resume.json').read_text());self.assertEqual(len(receipt['reused_targets']),3);self.assertEqual(receipt['remaining_sources'],[])
        base=self.root/'resumed-base';M.build(self.root/'id.iso',base,output,progress=lambda _:None);self.assertEqual(M.verify(base),[])

    def test_concurrent_build_lock_preserves_staging(self):
        bundle=self.bundle(('id',));out=self.root/'same-output';lock=self.root/'.same-output.megamix-build.lock'
        lock.write_text('another-process\n')
        with self.assertRaisesRegex(ValueError,'build is active'):M.build(self.root/'id.iso',out,bundle,progress=lambda _:None)
        self.assertEqual(lock.read_text(),'another-process\n');self.assertFalse(out.exists())
        lock.unlink();M.build(self.root/'id.iso',out,bundle,progress=lambda _:None)
        self.assertFalse(lock.exists());self.assertEqual(M.verify(out),[])

    def test_changed_iso_and_existing_output_preserved(self):
        bundle=self.bundle(('id',));out=self.root/'out';out.mkdir();(out/'mine').write_text('keep')
        with self.assertRaises(ValueError):M.build(self.root/'id.iso',out,bundle,progress=lambda _:None)
        self.assertEqual((out/'mine').read_text(),'keep')
        damaged=self.root/'damaged.iso';data=bytearray((self.root/'id.iso').read_bytes());data[900]=0;damaged.write_bytes(data)
        with self.assertRaises(ValueError):M.build(damaged,self.root/'new',bundle,progress=lambda _:None)
        self.assertFalse((self.root/'new').exists())

    def test_interrupted_build_resumes_and_rechecks_files(self):
        bundle=self.bundle(('id',));out=self.root/'base'
        old=M.check_files
        with patch.object(M,'check_files',side_effect=OSError('simulated read failure')):
            with self.assertRaises(OSError):M.build(self.root/'id.iso',out,bundle,progress=lambda _:None)
        self.assertFalse(out.exists());stage=out.with_name('.base.megamix-incomplete')
        (stage/M.asset_path(M.hash_bytes(self.original[123:90123]))).write_bytes(b'corrupt staged file')
        M.build(self.root/'id.iso',out,bundle,progress=lambda _:None)
        self.assertEqual(old(out,json.loads((out/'megamix-package.json').read_text())['files']),[])
        self.assertEqual(M.verify(out),[])

    def test_interrupted_install_resume_preserves_settings(self):
        bundle=self.bundle(('id','red'));base=self.root/'base';pack=self.root/'pack'
        M.build(self.root/'id.iso',base,bundle,progress=lambda _:None);M.build(self.root/'red.iso',pack,bundle,progress=lambda _:None)
        cfg=(base/'launcher.cfg').read_bytes()
        with patch.object(M,'activate_indexes',side_effect=OSError('simulated interrupted activation')):
            with self.assertRaises(OSError):M.install(pack,base,progress=lambda _:None)
        with self.assertRaises(ValueError):M.verify(base)
        M.install(pack,base,progress=lambda _:None)
        self.assertEqual(M.verify(base),[]);self.assertEqual(cfg,(base/'launcher.cfg').read_bytes())

    def test_corrupt_pack_and_destination_symlink(self):
        bundle=self.bundle(('id','red'));base=self.root/'base';pack=self.root/'pack'
        M.build(self.root/'id.iso',base,bundle,progress=lambda _:None);M.build(self.root/'red.iso',pack,bundle,progress=lambda _:None)
        outside=self.root/'outside';outside.mkdir();bucket=base/M.asset_path(M.hash_bytes(self.original[123:90123]));bucket.unlink();bucket.parent.rmdir();bucket.parent.symlink_to(outside,target_is_directory=True)
        with self.assertRaises(ValueError):M.install(pack,base,progress=lambda _:None)
        self.assertFalse(list(outside.iterdir()))
        bucket.parent.unlink();(pack/M.asset_path(M.hash_bytes(self.original[123:90123]))).write_bytes(b'broken')
        with self.assertRaises(ValueError):M.install(pack,base,progress=lambda _:None)
        self.assertFalse((base/'.megamix/install-journal.json').exists())

    @unittest.skipUnless(shutil.which('xdelta3'), 'Optional native xdelta interoperability oracle')
    def test_decoder_rejects_truncated_corrupt_and_oversized(self):
        src=self.root/'s';target=self.root/'t';patchfile=self.root/'p';src.write_bytes(self.original);target.write_bytes(self.original+b'hello'*500)
        subprocess.run(['xdelta3','-S','none','-A','-e','-s',str(src),str(target),str(patchfile)],check=True)
        p=patchfile.read_bytes()
        self.assertEqual(b''.join(decode(p,io.BytesIO(self.original),target.stat().st_size)),target.read_bytes())
        for n in [0,1,4,len(p)-1]:
            with self.assertRaises(ValueError):b''.join(decode(p[:n],io.BytesIO(self.original),target.stat().st_size))
        bad=bytearray(p);bad[-1]^=64
        with self.assertRaises(ValueError):b''.join(decode(bad,io.BytesIO(self.original),target.stat().st_size))
        with self.assertRaises(ValueError):b''.join(decode(p,io.BytesIO(self.original),2))


if __name__=='__main__':unittest.main()


class HoldSetTests(unittest.TestCase):
    def test_unify_keeps_the_17px_set(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            def put(folder, w, tail):
                d = root / 'songs' / folder / 'holds'
                d.mkdir(parents=True)
                for n in ('cn_white_start', 'cn_white_end'):
                    (d / (n + '.spr')).write_bytes(b'SPR1' + w.to_bytes(2, 'little') + b'\0' * 6 + tail)
            put('sirius/a', 17, b'x'); put('sirius/b', 17, b'x'); put('mine/c', 16, b'y'); put('mine/d', 17, b'z')
            self.assertEqual(len(M.hold_sets(root)), 3)
            self.assertEqual(M.unify_holds(root, progress=lambda *_: None), 2)
            sets = M.hold_sets(root)
            self.assertEqual(len(sets), 1)
            self.assertEqual((root / 'songs/mine/c/holds/cn_white_start.spr').read_bytes()[-1:], b'x')
            self.assertEqual(M.unify_holds(root, progress=lambda *_: None), 0)
            # a set without a file the others have never wins (songs would name a sprite that is gone)
            (root / 'songs/sirius/a/holds/bss_start.spr').write_bytes(b'SPR1' + (17).to_bytes(2, 'little') + b'\0' * 6 + b'q')
            (root / 'songs/mine/d/holds/bss_start.spr').write_bytes(b'SPR1' + (17).to_bytes(2, 'little') + b'\0' * 6 + b'q')
            self.assertEqual(M.unify_holds(root, progress=lambda *_: None), 2)
            for f in ('sirius/b', 'mine/c'):
                self.assertTrue((root / 'songs' / f / 'holds/bss_start.spr').is_file())
