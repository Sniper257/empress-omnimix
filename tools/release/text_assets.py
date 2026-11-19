"""Source-bounded text asset operations used by the release authoring pipeline."""
import hashlib
import json
from pathlib import PurePosixPath
from text_native import Container,extract,render_sprite,title_card
from text_raster import Atlas


def digest(data):return hashlib.sha256(data).hexdigest()


def resource(spec,read):
    if set(spec)!={'input','offset','bytes','sha256'} or spec['input'] not in ('iso','base'):
        raise ValueError('Invalid text source reference')
    at,n=spec['offset'],spec['bytes']
    if type(at)is not int or type(n)is not int or at<0 or not 0<n<=16*1024*1024:
        raise ValueError('Invalid text source bounds')
    data=read(spec['input'],at,n)
    if len(data)!=n or digest(data)!=spec['sha256']:raise ValueError('Text source hash mismatch')
    return data


class Generator:
    def __init__(self,atlas,read):
        self.atlas=atlas;self.read=read;self.containers={}

    def container(self,spec):
        key=json.dumps(spec,sort_keys=True)
        if key not in self.containers:self.containers[key]=Container(resource(spec,self.read))
        return self.containers[key]

    def generate(self,spec):
        kind=spec.get('kind')
        if kind=='card':return title_card(self.atlas,spec['texts'])
        if kind=='launcher':
            from text_launcher import launcher_font
            return (json.dumps(launcher_font(self.atlas),sort_keys=True,separators=(',',':'))+'\n').encode('utf-8')
        target=self.container(spec['palette'])
        if kind=='folder':
            from folder_text import generate
            return generate(self.atlas,spec,target)
        if kind=='native':return extract(self.container(spec['source']),spec['sprite'],target,spec['sheet'],spec.get('grey',False))
        if kind=='sprite':return render_sprite(self.atlas,spec['text'],spec['role'],target,spec['reference'],spec['max_width'],spec.get('bold',0),spec.get('xscale',1.0))
        raise ValueError('Unsupported text asset operation')


def output_name(name):
    p=PurePosixPath(name)
    if not isinstance(name,str) or not name or '\\' in name or ':' in name or p.is_absolute() or any(c in ('','.','..') for c in name.split('/')):
        raise ValueError('Unsafe text output path')
    return p
