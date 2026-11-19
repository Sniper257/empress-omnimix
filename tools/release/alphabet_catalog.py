"""Maintainer reviewed alphabetical keys, independent of scores and source rank."""
import json
from pathlib import Path
import source_versions as V

def load(path=None):
    rows=json.loads(Path(path or Path(__file__).with_suffix('.json')).read_text(encoding='utf-8'))
    out={}
    for r in rows:
        identity=r['source'],r['sid'];key=r['key']
        if identity in out or identity not in V.ROWS or not isinstance(key,str)or not 0<len(key)<=255 or key!=key.strip()or any(ord(c)<32 or ord(c)>126 for c in key):raise ValueError('Invalid alphabetical source/key')
        out[identity]=key
    if set(out)!=set(V.ROWS):raise ValueError('Incomplete retail alphabetical catalog')
    return out

def render(path=None):
    rows=load(path)
    return ('V 1\n'+''.join('S '+source+' '+str(sid)+' '+rows[source,sid]+'\n'for source,sid in sorted(rows))).encode('ascii')

if __name__=='__main__':
    import argparse
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('output',type=Path);a=p.parse_args();a.output.write_bytes(render())
