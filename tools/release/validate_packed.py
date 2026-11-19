"""Maintainer: independently reconstruct a ps2-packer LZMA ELF's PT_LOAD bytes."""
from pathlib import Path
import argparse,hashlib,json,lzma,struct

def segments(data):
    h=struct.unpack_from('<16sHHIIIIIHHHHHH',data)
    assert h[0][:7]==b'\x7fELF\x01\x01\x01'
    return h[4],[struct.unpack_from('<8I',data,h[5]+i*h[9])for i in range(h[10])]

def validate(original,packed):
    raw,p=Path(original).read_bytes(),Path(packed).read_bytes();entry,heads=segments(raw);_,ph=segments(p)
    originals={h[2]:raw[h[1]:h[1]+h[4]]+bytes(h[5]-h[4])for h in heads if h[0]==1 and h[4]}
    loaded=[h for h in ph if h[0]==1];assert len(loaded)==1
    block=loaded[0];at=block[1];pc,count=struct.unpack_from('<2I',p,at);at+=8
    assert pc==entry and count==len(originals);restored=set()
    for _ in range(count):
        size,zero,address,n=struct.unpack_from('<4I',p,at);at+=16;stream=p[at:at+n];assert len(stream)==n
        prop=stream[0];filters=[dict(id=lzma.FILTER_LZMA1,dict_size=int.from_bytes(stream[1:5],'little'),lc=prop%9,lp=prop//9%5,pb=prop//45)]
        data=lzma.decompress(stream[5:],format=lzma.FORMAT_RAW,filters=filters);assert len(data)==size
        assert data+bytes(zero)==originals[address];restored.add(address);at=(at+n+3)&~3
        assert block[2]+block[5]<=address or address+size+zero<=block[2]
    assert restored==set(originals) and block[1]+block[4]==len(p)
    return dict(raw_sha256=hashlib.sha256(raw).hexdigest(),packed_sha256=hashlib.sha256(p).hexdigest(),raw_bytes=len(raw),packed_bytes=len(p),entry=hex(entry),exact_loaded_memory_and_zero_fill=True,packed_region_disjoint=True)

if __name__=='__main__':
    a=argparse.ArgumentParser(description=__doc__);a.add_argument('original');a.add_argument('packed');a.add_argument('receipt');v=a.parse_args();r=validate(v.original,v.packed);Path(v.receipt).write_text(json.dumps(r,indent=2)+'\n');print('Packed ELF load image PASS',r['packed_bytes'])
