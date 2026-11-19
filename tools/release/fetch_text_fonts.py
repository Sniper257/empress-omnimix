#!/usr/bin/env python3
"""Maintainer: fetch explicitly pinned OFL font sources for atlas regeneration.

Normal users render with the bundled coverage atlas and do not run this script.
"""
import argparse,hashlib,json,os,tempfile,urllib.request
from pathlib import Path,PurePosixPath


def fetch(profile,output):
    profile=Path(profile);spec=json.loads(profile.read_text(encoding='utf-8'));output=Path(output);output.mkdir(parents=True,exist_ok=True)
    for key,row in spec['fonts'].items():
        name=row['file'];path=PurePosixPath(name)
        if path.is_absolute()or any(p in ('','.','..')for p in name.split('/'))or '\\'in name or ':'in name:raise ValueError('Unsafe font destination')
        target=output/path;target.parent.mkdir(parents=True,exist_ok=True)
        if target.is_symlink():raise ValueError('Font destination is a symlink')
        parent=target.parent
        while parent!=output:
            if parent.is_symlink():raise ValueError('Font destination traverses a symlink')
            parent=parent.parent
        expected=row['sha256'];url=row['source']
        if not url.startswith('https://raw.githubusercontent.com/'):raise ValueError('Only pinned upstream font sources are accepted')
        if target.exists():
            if hashlib.sha256(target.read_bytes()).hexdigest()!=expected:raise ValueError('Existing font differs: '+name)
            continue
        fd,temporary=tempfile.mkstemp(prefix=target.name+'.',dir=target.parent)
        try:
            h=hashlib.sha256();size=0
            with os.fdopen(fd,'wb')as out,urllib.request.urlopen(url,timeout=120)as response:
                while data:=response.read(1024*1024):
                    size+=len(data)
                    if size>64*1024*1024:raise ValueError('Font download exceeds64MiB')
                    h.update(data);out.write(data)
            if h.hexdigest()!=expected:raise ValueError('Downloaded font hash mismatch: '+name)
            os.replace(temporary,target)
        finally:Path(temporary).unlink(missing_ok=True)
        print('Verified',key,flush=True)
    (output/'open-fonts.json').write_text(json.dumps(spec,indent=2)+'\n',encoding='utf-8')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('profile',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
    try:fetch(a.profile,a.output)
    except (ValueError,OSError)as e:p.exit(2,str(e)+'\n')
