#!/usr/bin/env python3
"""Apply or roll back the R161b USB enumeration hotfix to an R158 console base.
Python 3.10+, standard library only. Does not access ISOs, songs, settings or saves.
"""
import argparse,copy,hashlib,json,os,sys
from pathlib import Path

HOTFIX='R161b-USB-ENUM'
FILES={'MEGAMIX.ELF','megamix-package.json','.megamix/installed.json','launcher/source.id'}

def canonical(v):return json.dumps(v,sort_keys=True,separators=(',',':'),ensure_ascii=True).encode()
def digest(b):return hashlib.sha256(b).hexdigest()
def ident(v):
    v=dict(v);v.pop('content_id',None)
    if v.get('kind')=='append':v.pop('generator_revision',None);v.pop('asset_map_hash',None)
    return digest(canonical(v))
def encoded(v):return (json.dumps(v,indent=2,sort_keys=True)+'\n').encode()
def checked(root,name):
    parts=name.split('/')
    if not name or any(p in ('','.','..') or ':' in p or '\\' in p for p in parts):raise ValueError('Invalid path')
    p=root
    if p.is_symlink():raise ValueError('Symlink destination refused: '+str(p))
    for part in parts:
        p=p/part
        if p.is_symlink():raise ValueError('Symlink destination refused: '+str(p))
    return p

def sync_dir(p):
    if os.name=='posix':
        try:
            fd=os.open(p,os.O_RDONLY)
            try:os.fsync(fd)
            finally:os.close(fd)
        except OSError:pass

def atomic(p,b):
    tmp=p.with_name(p.name+'.r161b-writing')
    if tmp.is_symlink():raise ValueError('Symlink temporary refused')
    with tmp.open('wb') as f:f.write(b);f.flush();os.fsync(f.fileno())
    os.replace(tmp,p);sync_dir(p.parent)

def load(p):return json.loads(p.read_text(encoding='utf-8'))
def record(b):return {'bytes':len(b),'sha256':digest(b)}
def check_bytes(b,r):return record(b)==r

def prepare(root,bundle,info,tx):
    state_path=checked(root,'.megamix/installed.json');state=load(state_path)
    if state.get('schema')!=1:raise ValueError('Unsupported installed manifest')
    base=state['packages']['id']
    origins=info.get('origins',[{'base_content_id':info['base_content_id'],'old_elf':info['old_elf']}])
    origin=next((o for o in origins if o['base_content_id']==base.get('content_id')),None)
    if base.get('target')!='console' or origin is None or ident(base)!=base['content_id']:
        raise ValueError('This hotfix requires the supported R158 console base; nothing was changed')
    for p in state['packages'].values():
        if ident(p)!=p.get('content_id'):raise ValueError('Installed package identity mismatch')
    meta=checked(root,'megamix-package.json')
    if load(meta)!=base:raise ValueError('Base package and installed state disagree')
    elf=checked(root,'MEGAMIX.ELF').read_bytes()
    if not check_bytes(elf,origin['old_elf']) or base['files']['MEGAMIX.ELF']!=origin['old_elf']:
        raise ValueError('Installed MEGAMIX.ELF is not the supported original; preserved without replacement')
    source_path=checked(root,'launcher/source.id');source=source_path.read_bytes()
    if not check_bytes(source,state['generated']['launcher/source.id']):raise ValueError('Source identity differs from installed state')
    newelf=checked(bundle,'payload/MEGAMIX.ELF').read_bytes()
    if not check_bytes(newelf,info['new_elf']):raise ValueError('Hotfix ELF is damaged')
    updated=copy.deepcopy(state);newbase=updated['packages']['id']
    newbase['files']['MEGAMIX.ELF']=info['new_elf'];newbase['content_id']=ident(newbase)
    seal=digest(canonical({'activation_policy':2,'contract':updated['contract'],'packages':{k:v['content_id'] for k,v in updated['packages'].items()}})).encode()
    updated['generated']['launcher/source.id']=record(seal)
    new={'MEGAMIX.ELF':newelf,'megamix-package.json':encoded(newbase),'.megamix/installed.json':encoded(updated),'launcher/source.id':seal}
    tx.mkdir(exist_ok=True)
    journal={'hotfix':HOTFIX,'phase':'prepared','files':{}}
    for i,name in enumerate(sorted(FILES)):
        old=checked(root,name).read_bytes();slot=str(i)
        atomic(checked(tx,slot+'.old'),old);atomic(checked(tx,slot+'.new'),new[name])
        journal['files'][name]={'slot':slot,'old':record(old),'new':record(new[name])}
    atomic(checked(tx,'journal.json'),encoded(journal));return journal

def validate_tx(root,tx,journal,info):
    if journal.get('hotfix')!=HOTFIX or set(journal.get('files',{}))!=FILES:raise ValueError('Invalid hotfix journal')
    slots=set()
    for name,row in journal['files'].items():
        slot=row['slot']
        if slot not in ('0','1','2','3') or slot in slots:raise ValueError('Invalid backup slot')
        slots.add(slot)
        for side in ('old','new'):
            if not check_bytes(checked(tx,slot+'.'+side).read_bytes(),row[side]):raise ValueError('Damaged hotfix backup/stage: '+name)
        if record(checked(root,name).read_bytes()) not in (row['old'],row['new']):
            raise ValueError('File changed since hotfix staging; preserved: '+name)
    row=journal['files']['MEGAMIX.ELF']
    accepted_old=[o['old_elf'] for o in info.get('origins',[{'old_elf':info['old_elf']}])]
    if row['old'] not in accepted_old or row['new']!=info['new_elf']:raise ValueError('Journal belongs to another hotfix payload')

def run(root,bundle,rollback=False,after_write=None):
    root=Path(root).absolute();bundle=Path(bundle).absolute();info=load(checked(bundle,'hotfix.json'))
    if info.get('hotfix')!=HOTFIX:raise ValueError('Incorrect hotfix metadata')
    # Validate the supplied payload even on resumption/rollback.
    if not check_bytes(checked(bundle,'payload/MEGAMIX.ELF').read_bytes(),info['new_elf']):raise ValueError('Hotfix ELF is damaged')
    admin=checked(root,'.megamix')
    if not admin.is_dir():raise ValueError('Choose the generated MEGAMIX game-data directory')
    if checked(root,'.megamix/install-journal.json').exists():raise ValueError('Finish the interrupted append installation first')
    tx=checked(root,'.megamix/hotfix-r161b');jp=checked(root,'.megamix/hotfix-r161b/journal.json')
    if rollback and not jp.exists():raise ValueError('No R161b backup exists')
    lock=checked(root,'.megamix/install.lock')
    try:fd=os.open(lock,os.O_WRONLY|os.O_CREAT|os.O_EXCL,0o600)
    except FileExistsError:raise ValueError('An installer lock exists. Check no installer is running before removing .megamix/install.lock') from None
    try:
        with os.fdopen(fd,'w') as f:f.write(HOTFIX+' '+str(os.getpid())+'\n');f.flush();os.fsync(f.fileno())
        journal=load(jp) if jp.exists() else prepare(root,bundle,info,tx)
        validate_tx(root,tx,journal,info)
        side='old' if rollback else 'new';phase='rolled-back' if rollback else 'installed'
        journal['phase']='rolling-back' if rollback else 'applying';atomic(jp,encoded(journal))
        # Metadata is published last. Any interrupted transaction can be resumed
        # with this same command while the console is powered off.
        order=('MEGAMIX.ELF','megamix-package.json','launcher/source.id','.megamix/installed.json')
        for i,name in enumerate(order):
            row=journal['files'][name];p=checked(root,name);b=checked(tx,row['slot']+'.'+side).read_bytes()
            if p.read_bytes()!=b:atomic(p,b)
            if not check_bytes(p.read_bytes(),row[side]):raise ValueError('Readback failed: '+name)
            if after_write:after_write(i,name)
        journal['phase']=phase;atomic(jp,encoded(journal));sync_dir(root)
        return phase
    finally:lock.unlink(missing_ok=True);sync_dir(admin)

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('directory',type=Path);p.add_argument('--rollback',action='store_true');a=p.parse_args()
    try:status=run(a.directory,Path(__file__).absolute().parent,a.rollback)
    except (OSError,ValueError,KeyError,TypeError) as e:
        print('HOTFIX NOT COMPLETE: '+str(e),file=sys.stderr)
        print('Keep the console powered off. Correct the issue and repeat this command, or use --rollback if staging completed.',file=sys.stderr);return 1
    print('R161b '+status+'; all changed files read back and verified.')
    print('Settings, bindings, saves, songs and append packs were not changed.')
    print('Backups: .megamix/hotfix-r161b. A launcher cache rebuild may occur once.')
    if not a.rollback:print('If you boot an ELF from another location, back up that copy and replace it with payload/MEGAMIX.ELF too.')
    return 0
if __name__=='__main__':raise SystemExit(main())
