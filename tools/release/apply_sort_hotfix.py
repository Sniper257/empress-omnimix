#!/usr/bin/env python3
"""Correct alphabetical ordering in an existing retail build.
Python 3.10+, standard library only. Keep the PS2 powered off.
"""
import argparse,copy,json,os,sys
from pathlib import Path
import megamix as M,source_versions as V,folder_presentation as F
from apply_presentation_hotfix import record,encoded,read,atomic
HOTFIX='R164-ALPHABET'
META={'megamix-package.json','.megamix/installed.json','launcher/source.id'}

def load(root,name):return json.loads(read(root,name))

def allowed(info):
    # Only the union of signed-off old/new manifest paths and generated metadata.
    names=set(META)
    for p in list(info['origins'].values())+list(info['targets'].values()):names.update(p['files'])
    names.update(M.indexes({'schema':1,'contract':'','style_order':info['style_order'],'packages':{'id':{'songs':[],'content_id':''}}}))
    names|={'songs/'+s+'/'+k for s in F.STYLES for k in ('style.txt','pb-label.spr','pb-caption.spr')}
    # Explicitly exclude configuration even though it occurs in base receipts.
    return names-M.MUTABLE-{'launcher.cfg.pending'}

def prepare(root,bundle,info,tx):
    state=load(root,'.megamix/installed.json');base=state['packages']['id'];target=base.get('target')
    if target not in ('console','host'):raise ValueError('Unsupported base target')
    if load(root,'megamix-package.json')!=base:raise ValueError('Base and installed metadata disagree')
    if record(read(root,'launcher/source.id'))!=state['generated']['launcher/source.id']:raise ValueError('Source seal differs from installed state')
    updated=copy.deepcopy(state);new={};verified=set()
    def checked(n,expected):
        b=read(root,n)
        if record(b)!=expected:raise ValueError('Existing source file differs; preserved: '+n)
        verified.add(n);return b
    for src,old in state['packages'].items():
        M.validate_manifest(old)
        if src!=old['source'] or old['content_id'] not in info['origins']:raise ValueError('Unsupported or modified source revision: '+src)
        expected=info['targets'][src+'-'+(target if src=='id'else'append')]
        pre=copy.deepcopy(old)
        # Paired R164 base includes all preceding presentation/USB corrections.
        if src=='id':pre=copy.deepcopy(info['presentation']['id-'+target])
        if src=='pb' and not old.get('song_versions'):pre=copy.deepcopy(info['presentation']['pb-append'])
        def payload(n):
            expected=pre['files'][n]
            if old['files'].get(n)!=expected:
                b=read(bundle,'payload/'+(target+'/' if src=='id' else '')+n)
                if record(b)!=expected:raise ValueError('Damaged compatibility payload: '+n)
                before=read(root,n)
                if before is not None and record(before)!=old['files'].get(n):raise ValueError('Destination already exists; preserved: '+n)
                return b
            return checked(n,expected)
        if src=='id':pre['runtime_revision']='R164'
        else:
            pre['generator_revision']='R164'
            if 'asset_map_hash' in expected:pre['asset_map_hash']=expected['asset_map_hash']
        pre['content_id']=M.content_id(pre)
        result,routes,changed=V.normalize(pre,payload)
        expected=info['targets'][src+'-'+(target if src=='id'else'append')]
        if result!=expected:raise ValueError('Normalized source differs from frozen release: '+src)
        updated['packages'][src]=result
        for n,origin in routes.items():
            if n!=origin or old['files'].get(origin)!=result['files'][n]:
                b=changed[n]if n in changed else payload(origin)
                if record(b)!=result['files'][n]:raise ValueError('Invalid transformed payload: '+n)
                present=read(root,n)
                if n not in old['files'] and present is not None and present!=b:raise ValueError('Relocation destination already exists; preserved: '+n)
                new[n]=b
        # Only delete old receipt-owned files after verifying and backing them up.
        # Empty folders and all unrelated files within them are preserved.
        for n in set(old['files'])-set(result['files']):
            checked(n,old['files'][n]);new[n]=None
    generated={n:b.encode()for n,b in M.indexes(updated).items()}
    generated.update(F.generated(updated,lambda n:new[n]if n in new else read(root,n)))
    updated['generated']={n:record(b)for n,b in generated.items()}
    for n,b in generated.items():
        before=read(root,n);expected=state['generated'].get(n,base['files'].get(n))
        if before is not None and record(before)!=expected:raise ValueError('Generated metadata changed externally; preserved: '+n)
        new[n]=b
    new['megamix-package.json']=encoded(updated['packages']['id']);new['.megamix/installed.json']=encoded(updated)
    if not set(new)<=allowed(info):raise ValueError('Updater attempted an unrelated write')
    tx.mkdir(exist_ok=True);j={'hotfix':HOTFIX,'bundle':M.hash_bytes(M.canonical(info)),'phase':'prepared','files':{}}
    for i,(n,b)in enumerate(sorted(new.items())):
        before=read(root,n);slot=str(i)
        atomic(tx,slot+'.old',before);atomic(tx,slot+'.new',b)
        j['files'][n]={'slot':slot,'old':record(before),'new':record(b)}
    atomic(tx,'journal.json',encoded(j));return j

def validate_tx(root,tx,j,info):
    if j.get('hotfix')!=HOTFIX or j.get('bundle')!=M.hash_bytes(M.canonical(info)) or not META<=set(j['files']) or not set(j['files'])<=allowed(info):raise ValueError('Invalid version-update journal')
    slots=set()
    for n,row in j['files'].items():
        slot=row['slot']
        if not isinstance(slot,str) or not slot.isdecimal() or slot in slots or int(slot)>=len(j['files']):raise ValueError('Invalid journal slot')
        slots.add(slot)
        for side in ('old','new'):
            if record(read(tx,slot+'.'+side))!=row[side]:raise ValueError('Damaged backup/stage: '+n)
        current=record(read(root,n))
        if current not in(row['old'],row['new']) and not(n=='launcher/source.id'and current is None and j['phase']in('applying','rolling-back')):raise ValueError('File changed since staging; preserved: '+n)

def run(root,bundle,rollback=False,after_write=None):
    root=Path(root).absolute();bundle=Path(bundle).absolute();info=load(bundle,'hotfix.json')
    if info.get('hotfix')!=HOTFIX or info.get('song_versions')!=V.POLICY:raise ValueError('Incorrect song-version update/table')
    for n,row in info['payload'].items():
        if record(read(bundle,'payload/'+n))!=row:raise ValueError('Damaged update payload: '+n)
    admin=M.path_in(root,'.megamix')
    if not admin.is_dir():raise ValueError('Choose a generated MEGAMIX data directory')
    if M.path_in(root,'.megamix/install-journal.json').exists():raise ValueError('Finish the interrupted append installation first')
    r162=M.path_in(root,'.megamix/hotfix-r162/journal.json')
    if r162.exists() and load(root,'.megamix/hotfix-r162/journal.json')['phase']not in('installed','rolled-back'):raise ValueError('Finish or roll back the interrupted R162 update first')
    r163=M.path_in(root,'.megamix/hotfix-r163/journal.json')
    if r163.exists() and load(root,'.megamix/hotfix-r163/journal.json')['phase']not in('installed','rolled-back'):raise ValueError('Finish or roll back the interrupted R163 update first')
    lock=M.path_in(root,'.megamix/install.lock');tx=M.path_in(root,'.megamix/hotfix-r164')
    if rollback and not(tx/'journal.json').exists():raise ValueError('No R164 backup exists')
    try:fd=os.open(lock,os.O_WRONLY|os.O_CREAT|os.O_EXCL,0o600)
    except FileExistsError:raise ValueError('Installer lock exists; check no installer is running before removing it')from None
    try:
        with os.fdopen(fd,'w')as f:f.write(HOTFIX+' '+str(os.getpid())+'\n');f.flush();os.fsync(f.fileno())
        j=load(tx,'journal.json')if(tx/'journal.json').exists()else prepare(root,bundle,info,tx)
        validate_tx(root,tx,j,info);side='old'if rollback else'new';j['phase']='rolling-back'if rollback else'applying';atomic(tx,'journal.json',encoded(j))
        # Remove the seal first: an interrupted update must never be treated as a
        # prepared launcher cache. Receipts/source seal are committed last.
        M.path_in(root,'launcher/source.id').unlink(missing_ok=True)
        names=sorted(n for n in j['files']if n not in META)+['megamix-package.json','.megamix/installed.json','launcher/source.id']
        for i,n in enumerate(names):
            row=j['files'][n];b=read(tx,row['slot']+'.'+side)
            if read(root,n)!=b:atomic(root,n,b)
            if record(read(root,n))!=row[side]:raise ValueError('Readback failed: '+n)
            if after_write:after_write(i,n)
        j['phase']='rolled-back'if rollback else'installed';atomic(tx,'journal.json',encoded(j));return j['phase']
    finally:lock.unlink(missing_ok=True);M.sync_directory(admin)

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('directory',type=Path);p.add_argument('--rollback',action='store_true');a=p.parse_args()
    try:status=run(a.directory,Path(__file__).absolute().parent,a.rollback)
    except(OSError,ValueError,KeyError,TypeError)as e:
        print('UPDATE NOT COMPLETE: '+str(e),file=sys.stderr);print('Keep PS2 off; correct the issue and repeat, or add --rollback if staging completed.',file=sys.stderr);return 1
    print('R164 '+status+'; changed files verified. Settings, bindings, scores and chart/audio data preserved.')
    print('Backup: .megamix/hotfix-r164. Replace separate boot ELF copies too; one cache rebuild is expected.')
    return 0
if __name__=='__main__':raise SystemExit(main())
