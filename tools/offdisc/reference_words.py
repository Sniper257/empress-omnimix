"""Patch preconditions for a fixed retail executable, without distributing it.

Scan results contain only addresses/instruction checks that become patches.
Large native code scans are performed once by the ISO-owning maintainer. CI reads
these reviewed preconditions; end-user ISO fingerprints and patch checks stay on.
"""
import hashlib,json,struct
from pathlib import Path

class Reference:
 def __init__(self,exe=None,words=None):
  self.raw=Path(exe).read_bytes()if words is None else None;self.record=True
  if self.raw is not None:self.info=dict(schema=1,source_sha256=hashlib.sha256(self.raw).hexdigest(),words={},strings={},scans={})
  else:
   self.info=json.loads(Path(words).read_text(encoding='utf-8'))
   if self.info.get('schema')!=1:raise ValueError('Unsupported executable precondition table')
 @property
 def digest(self):return bytes.fromhex(self.info['source_sha256'])
 def word(self,va):
  key=format(va,'x')
  if self.raw is None:
   if key not in self.info['words']:raise ValueError('Missing executable precondition: '+key)
   return int(self.info['words'][key],16)
  w=struct.unpack_from('<I',self.raw,va-0x100000+0x80)[0]
  if self.record:self.info['words'][key]=format(w,'08x')
  return w
 def cstring(self,va):
  key=format(va,'x')
  if self.raw is None:return self.info['strings'][key]
  at=va-0x100000+0x80;text=self.raw[at:self.raw.index(0,at)].decode('ascii');self.info['strings'][key]=text;return text
 def scan(self,key,operation):
  if key in self.info['scans']:return self.info['scans'][key]
  if self.raw is None:raise ValueError('Missing reviewed native scan result: '+key)
  previous=self.record;self.record=False
  try:value=operation()
  finally:self.record=previous
  self.info['scans'][key]=value;return value
 def export(self,path):
  path=Path(path)
  if path.exists():
   old=json.loads(path.read_text());assert old['source_sha256']==self.info['source_sha256']
   for key in ('words','strings','scans'):
    for name,value in old[key].items():
     if name in self.info[key]and self.info[key][name]!=value:raise ValueError('Conflicting executable precondition')
     self.info[key][name]=value
  path.parent.mkdir(parents=True,exist_ok=True);path.write_text(json.dumps(self.info,indent=2,sort_keys=True)+'\n')
