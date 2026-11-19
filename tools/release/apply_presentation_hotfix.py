#!/usr/bin/env python3
"""Update a supported generated installation's folder graphics, without rebuilding ISOs.
Python 3.10+, standard library only. Keep the PS2 powered off during the update.
"""
import argparse,copy,hashlib,json,os,sys
from pathlib import Path
import megamix as M,folder_presentation as F
HOTFIX='R162-FOLDERS'
META={'megamix-package.json','.megamix/installed.json','launcher/source.id'}
def record(b):return None if b is None else {'bytes':len(b),'sha256':M.hash_bytes(b)}
def encoded(x):return (json.dumps(x,sort_keys=True,indent=2)+'\n').encode()
def read(root,name):
 p=M.path_in(root,name)
 return p.read_bytes() if p.exists() else None
def atomic(root,name,b):
 p=M.path_in(root,name)
 if b is None:p.unlink(missing_ok=True);M.sync_directory(p.parent);return
 p.parent.mkdir(parents=True,exist_ok=True);tmp=p.with_name(p.name+'.r162-writing')
 if tmp.is_symlink():raise ValueError('Symlink temporary refused')
 with tmp.open('wb')as f:f.write(b);f.flush();os.fsync(f.fileno())
 os.replace(tmp,p);M.sync_directory(p.parent)
def load(root,name):return json.loads(read(root,name))
def allowed(info):
 names=set(info['base_payload'])|META|{'songs/'+s+'/'+k for s in F.STYLES for k in ('style.txt','pb-label.spr','pb-caption.spr')}
 if info.get('pb'):
  names|={r[k] for r in info['pb']['folder_art'].values() for k in ('label','caption')}
 return names
def prepare(root,bundle,info,tx):
 state=load(root,'.megamix/installed.json');base=state['packages']['id'];origin=info['origins'].get(base['content_id'])
 if origin is None or M.content_id(base)!=base['content_id'] or base.get('target')!='console':raise ValueError('Unsupported base revision; nothing changed')
 if load(root,'megamix-package.json')!=base:raise ValueError('Base and installed metadata disagree')
 for p in state['packages'].values():M.validate_manifest(p)
 source=read(root,'launcher/source.id')
 if record(source)!=state['generated']['launcher/source.id']:raise ValueError('Source identity differs from installed state')
 if base!=origin:raise ValueError('Base metadata differs from supported revision')
 updated=copy.deepcopy(state);updated['packages']['id']=copy.deepcopy(info['base']);new={}
 # The explicit base revisions pin every old file this update will replace.
 for n,row in info['base_payload'].items():
  if record(read(root,n))!=base['files'].get(n):raise ValueError('Existing base file differs; preserved: '+n)
  b=read(bundle,'payload/'+n)
  if record(b)!=row:raise ValueError('Damaged hotfix payload: '+n)
  new[n]=b
 pb=state['packages'].get('pb')
 if pb:
  if not F.same_song_payload(pb,info['pb']):raise ValueError('Unsupported PB revision')
  updated['packages']['pb']=copy.deepcopy(info['pb'])
  for row in info['pb']['folder_art'].values():
   for kind in ('label','caption'):
    n=row[kind];b=read(bundle,'payload/'+n)
    if record(b)!=info['pb']['files'][n]:raise ValueError('Damaged PB artwork payload')
    old=read(root,n)
    if old is not None and old!=b:raise ValueError('Existing PB artwork differs; preserved: '+n)
    new[n]=b
 generated=F.generated(updated,lambda n:new[n] if n in new else read(root,n))
 seal=M.indexes(updated)['launcher/source.id'].encode();generated['launcher/source.id']=seal
 for n,b in generated.items():
  old=read(root,n);expected=state['generated'].get(n,base['files'].get(n))
  if old is not None and record(old)!=expected:raise ValueError('Existing generated file differs; preserved: '+n)
  new[n]=b;updated['generated'][n]=record(b)
 new['megamix-package.json']=encoded(updated['packages']['id']);new['.megamix/installed.json']=encoded(updated)
 if not set(new)<=allowed(info):raise ValueError('Hotfix attempted an unrelated write')
 tx.mkdir(exist_ok=True);journal={'hotfix':HOTFIX,'bundle':M.hash_bytes(M.canonical(info)),'phase':'prepared','files':{}}
 for i,(name,b)in enumerate(sorted(new.items())):
  old=read(root,name);slot=str(i)
  if old is not None:atomic(tx,slot+'.old',old)
  atomic(tx,slot+'.new',b)
  journal['files'][name]={'slot':slot,'old':record(old),'new':record(b)}
 atomic(tx,'journal.json',encoded(journal));return journal

def validate_tx(root,tx,j,info):
 if j.get('hotfix')!=HOTFIX or j.get('bundle')!=M.hash_bytes(M.canonical(info)) or not META<=set(j['files']) or not set(j['files'])<=allowed(info):raise ValueError('Invalid hotfix journal')
 slots=set()
 for n,row in j['files'].items():
  slot=row['slot']
  if not isinstance(slot,str) or not slot.isdecimal() or slot in slots or int(slot)>=len(j['files']):raise ValueError('Invalid journal slot')
  slots.add(slot)
  for side in ('old','new'):
   if record(read(tx,slot+'.'+side))!=row[side]:raise ValueError('Damaged hotfix backup/stage: '+n)
  if record(read(root,n))not in(row['old'],row['new']):raise ValueError('File changed since staging; preserved: '+n)

def run(root,bundle,rollback=False,after_write=None):
 root=Path(root).absolute();bundle=Path(bundle).absolute();info=load(bundle,'hotfix.json')
 if info.get('hotfix')!=HOTFIX:raise ValueError('Incorrect hotfix metadata')
 for n,row in info['base_payload'].items():
  if record(read(bundle,'payload/'+n))!=row:raise ValueError('Damaged hotfix payload: '+n)
 admin=M.path_in(root,'.megamix')
 if not admin.is_dir():raise ValueError('Choose a generated MEGAMIX data directory')
 if M.path_in(root,'.megamix/install-journal.json').exists():raise ValueError('Finish the interrupted append installation first')
 lock=M.path_in(root,'.megamix/install.lock');tx=M.path_in(root,'.megamix/hotfix-r162')
 if rollback and not (tx/'journal.json').exists():raise ValueError('No R162 backup exists')
 try:fd=os.open(lock,os.O_WRONLY|os.O_CREAT|os.O_EXCL,0o600)
 except FileExistsError:raise ValueError('Installer lock exists; check no installer is running before removing it')from None
 try:
  with os.fdopen(fd,'w')as f:f.write(HOTFIX+' '+str(os.getpid())+'\n');f.flush();os.fsync(f.fileno())
  j=load(tx,'journal.json')if(tx/'journal.json').exists()else prepare(root,bundle,info,tx)
  validate_tx(root,tx,j,info);side='old'if rollback else'new';j['phase']='rolling-back'if rollback else'applying';atomic(tx,'journal.json',encoded(j))
  order=sorted(n for n in j['files'] if n not in META)+['megamix-package.json','launcher/source.id','.megamix/installed.json']
  for i,n in enumerate(order):
   row=j['files'][n];b=read(tx,row['slot']+'.'+side)
   if read(root,n)!=b:atomic(root,n,b)
   if record(read(root,n))!=row[side]:raise ValueError('Readback failed: '+n)
   if after_write:after_write(i,n)
  j['phase']='rolled-back'if rollback else'installed';atomic(tx,'journal.json',encoded(j));return j['phase']
 finally:lock.unlink(missing_ok=True);M.sync_directory(admin)

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('directory',type=Path);p.add_argument('--rollback',action='store_true');a=p.parse_args()
 try:status=run(a.directory,Path(__file__).absolute().parent,a.rollback)
 except (OSError,ValueError,KeyError,TypeError)as e:
  print('HOTFIX NOT COMPLETE: '+str(e),file=sys.stderr);print('Keep PS2 off; correct the issue and repeat, or use --rollback if staging completed.',file=sys.stderr);return 1
 print('R162 '+status+'; changed files read back and verified. Settings, bindings, saves and chart/audio data preserved.')
 print('Backup: .megamix/hotfix-r162. One launcher cache rebuild is expected.')
 if not a.rollback:print('Replace any separate boot copy of MEGAMIX.ELF with payload/MEGAMIX.ELF too. Use the supplied megamix.py/helpers for future pack installs.')
 return 0
if __name__=='__main__':raise SystemExit(main())
