"""Checked source-owned layout edits. No complete retail target is distributed."""
import hashlib

def apply(spec,read):
    source=spec['source'];at,n=source['offset'],source['bytes'];length=spec['length']
    if type(at)is not int or at<0 or type(n)is not int or not 0<n<=16*1024*1024 or type(length)is not int or not 0<length<=16*1024*1024:raise ValueError('Invalid source edit bounds')
    raw=read(at,n)
    if len(raw)!=n or hashlib.sha256(raw).hexdigest()!=source['sha256']:raise ValueError('Source edit input hash mismatch')
    out=bytearray(raw[:length]);out.extend(bytes(max(0,length-len(out))));end=0
    for edit in spec['edits']:
        a=edit['at'];before=bytes.fromhex(edit['before']);after=bytes.fromhex(edit['after'])
        if type(a)is not int or a<end or not before or len(before)!=len(after) or a+len(before)>length:raise ValueError('Invalid or overlapping source edit')
        if out[a:a+len(before)]!=before:raise ValueError('Source edit original-byte mismatch')
        out[a:a+len(before)]=after;end=a+len(before)
    return bytes(out)

def describe(raw,target,offset):
    """Maintainer: fixed, minimal contiguous original-byte-checked edits."""
    padded=raw[:len(target)]+bytes(max(0,len(target)-len(raw)));edits=[];i=0
    while i<len(target):
        if padded[i]==target[i]:i+=1;continue
        start=i
        while i<len(target)and padded[i]!=target[i]:i+=1
        edits.append(dict(at=start,before=padded[start:i].hex(),after=target[start:i].hex()))
    return dict(op='edit',source=dict(offset=offset,bytes=len(raw),sha256=hashlib.sha256(raw).hexdigest()),length=len(target),edits=edits)


def normalize_container(source):
    """Expand the old outer header, reading every graphics byte from the ISO."""
    import struct
    if not 4<=len(source)<=16*1024*1024:raise ValueError('Invalid original container size')
    offset,count=struct.unpack_from('<HH',source)
    if offset-(count+1)*8!=4 or offset>len(source) or offset>65531:raise ValueError('Unexpected original container layout')
    return struct.pack('<HHI',offset+4,count,0)+source[4:]
