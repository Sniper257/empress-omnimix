"""Optional offline launcher cache generation. Cache writes only, never game code.

The engine is the matching production C launcher compiled to32-bit WASI. Its
input mounts are read-only and only a temporary output directory is writable.
Wasmtime is imported only by this optional command, not ISO generation/install.
"""
from pathlib import Path
import hashlib,json,os,re,stat,tempfile

HERE=Path(__file__).resolve().parent


def logical_path(value,game=True,target='console'):
    if target=='host':
        if value!='host:':raise ValueError('HOST cache paths must be host:')
        return value
    if not isinstance(value,str):raise ValueError('Supply the console game-data path with --game-path')
    value=value.replace('\\','/')
    devices=r'(?:mass[0-9]?|mmce[01])' if game else r'(?:mass[0-9]?|mmce[01]|mc[01])'
    if not re.fullmatch(devices+r':(?:/[^:\x00-\x1f]*)?',value):raise ValueError('Unsupported console '+('game'if game else'configuration')+' path')
    tail=value.split(':',1)[1]
    if any(p in ('.','..')for p in tail.split('/'))or'//'in tail:raise ValueError('Use an absolute console path without . or ..')
    if not value.endswith('/'):value+='/'
    if len(value.encode('utf-8'))>=64:raise ValueError('Console path exceeds the launcher63-byte limit')
    return value


def _record(path):
    try:
        b=path.read_bytes();return {'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest()}
    except FileNotFoundError:return None


def _preflight(root,config,engine_dir):
    import megamix as M
    state_path=M.path_in(root,'.megamix/installed.json')
    state=json.loads(state_path.read_bytes());base=state['packages']['id'];target=base.get('target')
    if target not in ('console','host'):raise ValueError('Unsupported cache target')
    if json.loads(M.path_in(root,'megamix-package.json').read_bytes())!=base:raise ValueError('Base/installed receipts disagree')
    for src,p in state['packages'].items():
        M.validate_manifest(p)
        if p['source']!=src:raise ValueError('Invalid installed source identity')
    if M.path_in(root,'.megamix/install-journal.json').exists():raise ValueError('Finish the interrupted append installation first')
    for p in M.path_in(root,'.megamix').glob('hotfix-*/journal.json'):
        if json.loads(M.path_in(root,p.relative_to(root).as_posix()).read_bytes()).get('phase')not in('installed','rolled-back'):raise ValueError('Finish or roll back the interrupted updater first')
    info=json.loads((engine_dir/'engine.json').read_bytes())
    if info.get('schema')!=1 or target not in info['targets']:raise ValueError('Unsupported offline cache engine')
    spec=info['targets'][target];wasm=engine_dir/(target+'.wasm')
    if _record(wasm)!=spec['wasm']:raise ValueError('Damaged offline cache engine; extract the complete tools package again')
    observed={}
    def check(n,row):
        actual=_record(M.path_in(root,n));observed[n]=actual
        if actual!=row:raise ValueError('Installation does not match this cache engine: '+n+'; use matching release tools')
    for n,row in spec['files'].items():
        if n=='MEGAMIX.ELF'and spec.get('launcher'):
            verified=spec['launcher']
            if base.get('derived_launcher')!=verified['identity']:raise ValueError('Installation uses a different launcher source build')
            check(n,base['files'][n])
            from iso_launcher import verify_packed
            verify_packed(M.path_in(root,n).read_bytes(),verified['memory'],verified['packing'])
        else:check(n,row)
    for n,row in state['generated'].items():check(n,row)
    token=M.path_in(root,'launcher/source.id').read_bytes()
    if not re.fullmatch(b'[0-9a-f]{64}',token):raise ValueError('Missing or invalid source seal; finish the installer/updater first')
    if not M.path_in(config,'launcher.cfg').is_file():raise ValueError('Configuration folder has no launcher.cfg')
    pending=M.path_in(config,'launcher.cfg.pending')
    if pending.exists()and pending.stat().st_size:raise ValueError('Pending settings transaction: finish it in the launcher before generating a cache')
    # Copy settings into a read-only snapshot; no settings recovery or save can
    # touch the actual configuration folder while the engine is running.
    config_bytes={n:(M.path_in(config,n).read_bytes()if M.path_in(config,n).exists()else None)for n in ('launcher.cfg','launcher.cfg.pending','debug.cfg')}
    # Bulk audio/video are sized rather than rehashed. Core executable/catalog
    # bytes and generated indexes receive full hash checks below. Missing or
    # partial source files must not silently shrink the workspace measurement.
    owned={}
    for package in state['packages'].values():
        for n,row in package['files'].items():
            if n in owned and owned[n]!=row:raise ValueError('Conflicting installed source file: '+n)
            owned[n]=row
    parents={}
    for n,row in owned.items():
        if n in M.MUTABLE or n in M.DYNAMIC or n in state['generated']:continue
        parent,_,leaf=n.rpartition('/')
        if parent not in parents:parents[parent]=M.path_in(root,parent)if parent else root
        try:st=(parents[parent]/leaf).lstat()
        except FileNotFoundError:raise ValueError('Missing installed source file: '+n)from None
        if stat.S_ISLNK(st.st_mode):raise ValueError('Symlink in installed source file: '+n)
        if not stat.S_ISREG(st.st_mode)or st.st_size!=row['bytes']:raise ValueError('Incomplete installed source file: '+n)
    observed.update({'.megamix/installed.json':_record(state_path),'megamix-package.json':_record(M.path_in(root,'megamix-package.json'))})
    return target,spec,wasm,observed,config_bytes,token.decode()


def _execute(wasm,root,config,out,game_path,settings_path):
    try:
        import wasmtime as W
        from importlib.metadata import version
        installed=tuple(int(v)for v in version('wasmtime').split('.')[:3])
        if not((50,0,1)<=installed<(51,0,0)):raise ValueError('Cache generation needs wasmtime50.0.1: python -m pip install "wasmtime>=50.0.1,<51"')
    except ImportError as e:raise ValueError('Optional cache runtime missing. Run: python -m pip install "wasmtime>=50.0.1,<51"')from e
    try:
        engine=W.Engine();module=W.Module.from_file(engine,str(wasm));store=W.Store(engine)
        store.set_limits(memory_size=268435456,instances=1,memories=1,tables=1,table_elements=65536)
        wasi=W.WasiConfig();wasi.argv=['megamix-cache',game_path,settings_path,'/game/','/config/']
        wasi.preopen_dir(str(root),'/game',False);wasi.preopen_dir(str(config),'/config',False);wasi.preopen_dir(str(out),'/output',True)
        wasi.stdout_file=str(out/'engine.stdout');wasi.stderr_file=str(out/'engine.stderr');store.set_wasi(wasi)
        linker=W.Linker(engine);linker.define_wasi();linker.instantiate(store,module).exports(store)['_start'](store)
    except (W.WasmtimeError,W.Trap)as e:
        log=out/'engine.stderr';text=log.read_text(errors='replace')[-4096:]if log.exists()else''
        raise ValueError('Offline cache build failed; existing files preserved. '+(text.strip()or str(e)))from e
    receipt={}
    for line in (out/'receipt.txt').read_text().splitlines():
        k,v=line.split();receipt[k]=int(v,16 if k=='entry'else 10)
    if receipt.get('cache_builder')!=1:raise ValueError('Invalid cache-engine receipt')
    return receipt


def generate(root,game_path,config_dir=None,settings_path=None,temp_dir=None,progress=print,engine_dir=None):
    import megamix as M
    root=Path(root).absolute();config=Path(config_dir).absolute()if config_dir else root
    engine_dir=Path(engine_dir)if engine_dir else HERE/'cache-engine'
    admin=M.path_in(root,'.megamix')
    if not admin.is_dir():raise ValueError('Choose an installed MEGAMIX data directory')
    lock=M.path_in(root,'.megamix/install.lock')
    try:fd=os.open(lock,os.O_WRONLY|os.O_CREAT|os.O_EXCL,0o600)
    except FileExistsError:raise ValueError('Installer/cache builder lock exists; finish that process first')from None
    try:
        with os.fdopen(fd,'w')as f:f.write('offline-cache '+str(os.getpid())+'\n');f.flush();os.fsync(f.fileno())
        target,spec,wasm,observed,config_bytes,token=_preflight(root,config,engine_dir)
        game_path=logical_path(game_path,target=target)
        if config_dir is not None and config!=root and settings_path is None:raise ValueError('Separate configuration folder needs its console --settings-path too')
        settings_path=logical_path(settings_path or game_path,False,target)
        progress('Using settings: '+str(config/'launcher.cfg'))
        progress('Building '+target+' cache for '+game_path+' using the selected launcher.cfg...')
        with tempfile.TemporaryDirectory(prefix='megamix-cache-',dir=temp_dir)as tmp:
            stage=Path(tmp);settings=stage/'settings';settings.mkdir();out=stage/'output';(out/'cache').mkdir(parents=True)
            for n,b in config_bytes.items():
                if b is not None:(settings/n).write_bytes(b)
            cold=_execute(wasm,root,settings,out,game_path,settings_path)
            if cold.get('boot_saved')!=1 or cold.get('menu_saved')!=1:raise ValueError('Engine did not produce both fresh caches')
            progress('Validating the generated cache and its dependent graphics...')
            warm=_execute(wasm,root,settings,out,game_path,settings_path)
            if warm.get('boot_hit')!=1 or warm.get('menu_hit')!=1:raise ValueError('Generated cache failed its launcher restore check')
            products={p.name:p for p in(out/'cache').iterdir()if p.is_file()and p.suffix=='.bin'}
            boots=[n for n in products if re.fullmatch(r'boot_[0-9a-f]{16}\.bin',n)];menus=[n for n in products if re.fullmatch(r'menu_[0-9a-f]{16}\.bin',n)]
            if len(boots)!=1 or len(menus)!=1 or len(products)>260 or not all(re.fullmatch(r'[A-Za-z0-9_-]+\.bin',n)for n in products):raise ValueError('Unexpected cache-engine output')
            rows={n:_record(p)for n,p in products.items()}
            for n,row in observed.items():
                if _record(M.path_in(root,n))!=row:raise ValueError('Installation changed during cache generation; run the command again')
            for n,b in config_bytes.items():
                p=M.path_in(config,n)
                if (p.read_bytes()if p.exists()else None)!=b:raise ValueError('Settings changed during cache generation; run the command again')
            # Prepare every destination before writing. Products first, boot
            # snapshot last. A partial copy is a normal launcher cache miss.
            destinations={n:M.path_in(root,'cache/'+n)for n in products}
            receipt=M.path_in(root,'cache/pc-build.json')
            order=sorted(n for n in products if n not in boots)+boots
            for n in order:
                if _record(destinations[n])!=rows[n]:M.write_atomic(destinations[n],products[n].read_bytes())
                if _record(destinations[n])!=rows[n]:raise OSError('Cache readback mismatch: '+n)
            result=dict(schema=1,engine=spec['wasm']['sha256'],game_path=game_path,settings_path=settings_path,source_seal=token,files=rows,boot=boots[0],menu=menus[0],bytes=sum(r['bytes']for r in rows.values()),launcher_restore_verified=True)
            M.save_json(receipt,result);M.sync_directory(root/'cache');progress('Cache ready: '+str(len(rows))+' files, '+str(result['bytes'])+' bytes. Settings and saves unchanged.')
            return result
    finally:lock.unlink(missing_ok=True);M.sync_directory(admin)
