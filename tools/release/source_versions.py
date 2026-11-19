"""Reviewed, fixed-retail song debut policy. Standard library only.

Source disc/SID is authoritative: never infer a debut from a title, filename,
import order or the source game's often inaccurate generation byte. Chart
identity, source preference and score keys are deliberately independent.
"""
import copy,hashlib,json,re
from pathlib import Path

_DATA=json.loads(Path(__file__).with_suffix('.json').read_text(encoding='utf-8'))
ROWS={ (r['source'],r['sid']):r for r in _DATA['songs'] }
TABLE_HASH=hashlib.sha256(json.dumps(_DATA,sort_keys=True,separators=(',',':'),ensure_ascii=True).encode()).hexdigest()
POLICY={'schema':1,'table_sha256':TABLE_HASH}
VERSIONS={'1st_style':1,'substream':1,'2nd_style':2,'3rd_style':3,'4th_style':4,'5th_style':5,'6th_style':6,'7th_style':7,'8th_style':8,'9th_style':9,'10th_style':10,'red':11,'happy_sky':12,'distorted':13,'gold':14,'dj_troopers':15,'empress':16,'sirius':17}
EXE='EMPRESS/SLPM_552.21'

def style(source,sid,fallback):
    row=ROWS.get((source,sid))
    return row['style'] if row else fallback

def record(source,sid,raw):
    row=ROWS.get((source,sid))
    if row is None or row['style']=='beatmania_usa':return bytes(raw)
    if len(raw)!=0x134:raise ValueError('Invalid converted song record: '+source+':'+str(sid))
    data=bytearray(raw);data[0x54]=VERSIONS[row['style']];return bytes(data)

def native(raw,songs):
    data=bytearray(raw)
    for s in songs:
        start=0x178bf0+s['sid']*0x134;end=start+0x134
        if end>len(data):raise ValueError('Truncated native song table')
        data[start:end]=record('id',s['sid'],data[start:end])
    return bytes(data)

def validate(package):
    policy=package.get('song_versions')
    if policy is None:return
    prior=policy=={'schema':1,'table_sha256':'7ce5a6c8bcd5413fc279e709c6464a4054981a7d0e5c7136d188dc8f32612063'}
    if policy!=POLICY and not prior:raise ValueError('Unsupported song-version table; use a matching builder/updater')
    expected={sid for src,sid in ROWS if src==package['source']}
    if {s['sid'] for s in package['songs']}!=expected or len(package['songs'])!=len(expected):raise ValueError('Song-version policy requires the complete retail source')
    for s in package['songs']:
        if s['style']!=('sirius' if prior and s['source']=='id' and s['sid'] in (97,98) else style(s['source'],s['sid'],None)):raise ValueError('Song folder differs from reviewed version table')

def normalize(package,read):
    """Return new manifest, new->old payload paths, changed small-file bytes.

    Only source-owned paths, descriptor references and version bytes change.
    Reads all small descriptors/records, never pooled audio/video/textures.
    Caller verifies the original receipt and activates the new one atomically.
    """
    import megamix as M
    M.validate_manifest(package)
    p=copy.deepcopy(package);source=p['source'];old_songs=package['songs']
    p['style_order']=[s for s in p['style_order']if s!='sirius']
    if {s['sid'] for s in old_songs}!={sid for src,sid in ROWS if src==source}:raise ValueError('Not a complete supported retail source: '+source)
    moves={};refs={}
    for old,s in zip(old_songs,p['songs']):
        before=old['style']+'/'+old['dir'];s['style']=style(source,s['sid'],None);after=s['style']+'/'+s['dir']
        refs[before]=after
        if before!=after:moves['songs/'+before+'/']='songs/'+after+'/'
    def path(n):
        for a,b in moves.items():
            if n.startswith(a):return b+n[len(a):]
        return n
    routes={path(n):n for n in package['files']}
    if len(routes)!=len(package['files']):raise ValueError('Song-version destination collision')
    p['files']={n:package['files'][old] for n,old in routes.items()};changed={}
    def save(n,b):
        old=routes[n]
        if len(b)!=package['files'][old]['bytes'] or M.hash_bytes(b)!=package['files'][old]['sha256']:
            changed[n]=b;p['files'][n]={'bytes':len(b),'sha256':M.hash_bytes(b)}
    for old,s in zip(old_songs,p['songs']):
        folder='songs/'+s['style']+'/'+s['dir'];n=folder+'/song.txt'
        text=read(routes[n]).decode('utf-8')
        # Whitespace and all unrelated descriptor bytes remain identical.
        def token(m):
            word=m.group();prefix='/'if word.startswith('/')else'';key=word[len(prefix):]
            if key in refs:return prefix+refs[key]
            return prefix+path(key)
        text=re.sub(r'[^\s]+',token,text);save(n,text.encode('utf-8'))
        if source!='id':
            n=folder+'/record.bin';save(n,record(source,s['sid'],read(routes[n])))
    if source=='id':save(EXE,native(read(EXE),p['songs']))
    else:p['payload_prefixes']=['a']+['songs/'+s['style']+'/'+s['dir']for s in p['songs']]
    for row in p.get('folder_art',{}).values():
        for k in ('label','caption'):row[k]=path(row[k])
    p['song_versions']=dict(POLICY);p['content_id']=M.content_id(p);M.validate_manifest(p)
    return p,routes,changed
