"""Lossless fixed-disc audio byte conversions; Python stdlib, no audio resampling."""
import struct

NAMES=('adpcm-bank-v3','adpcm-stream-v3','eighth-stream-v3','red-bank-v3','early-bank-v3')
XOR=bytes(i^255 for i in range(256))
SUB={n:bytes((i-n)&255 for i in range(256))for n in (2,36)}

def frames(data,start,size,delta):
    if start<0 or size<0 or size%16 or start+size>len(data):raise ValueError('Invalid ADPCM frame bounds')
    out=bytearray(data);end=start+size
    out[start:end:16]=bytes(out[start:end:16]).translate(XOR)
    out[start+2:end:16]=bytes(out[start+2:end:16]).translate(SUB[delta])
    return out

def convert(kind,source,target_size):
    if max(len(source),target_size)>64*1024*1024 or target_size<0:raise ValueError('Audio transform exceeds bounds')
    if kind in ('red-bank-v3','early-bank-v3'):
        expected=b'wp\x02\x00' if kind=='red-bank-v3' else b'ef\x01\x00'
        if len(source)<0x8010 or source[:4]!=expected:raise ValueError('Wrong source bank format')
        count,size=struct.unpack_from('<II',source,0x8000);start=0x8010+16*count
        if count>4096 or size%16 or start+size>len(source):raise ValueError('Invalid source bank bounds')
        if kind=='early-bank-v3':out=frames(source,start,size,36)
        else:
            out=bytearray(source)
            for delta,index in ((2,1),(-19,2),(-55,3)):
                out[start+index:start+size:16]=source[start+index:start+size:16].translate(bytes((i+delta)&255 for i in range(256)))
        out[:4]=b'wp\x03\x00';struct.pack_into('<I',out,0x8008,1)
    elif kind=='adpcm-bank-v3':
        if len(source)<0x8010 or source[:4]!=b'wp\x02\x00':raise ValueError('Not a version2 bank')
        count,size=struct.unpack_from('<2I',source,0x8000);start=0x8010+16*count
        out=frames(source,start,size,36);out[2]=3;struct.pack_into('<I',out,0x8008,1)
    elif kind=='adpcm-stream-v3':
        if len(source)<0x800 or struct.unpack_from('<I',source)[0]!=0x08640001:raise ValueError('Not a native stream')
        size=struct.unpack_from('<I',source,12)[0];out=frames(source,0x800,size,2);struct.pack_into('<I',out,0x20,1)
    elif kind=='eighth-stream-v3':
        if len(source)<0x800:raise ValueError('Short legacy stream')
        size,volume,rate,ch=struct.unpack_from('>IHHB',source)
        if ch not in (1,2)or size%16 or size+0x800>len(source)or target_size!=0x800+size:raise ValueError('Invalid legacy stream')
        raw=source[0x800:0x800+size]
        if ch==2:
            if size%0x1000:raise ValueError('Invalid stereo block alignment')
            raw=b''.join(raw[b+j:b+j+16]+raw[b+0x800+j:b+0x810+j]for b in range(0,size,0x1000)for j in range(0,0x800,16))
        out=bytearray(0x800);struct.pack_into('<12I',out,0,0x08640001,0,0x800,size,0,0,rate,ch,1,16,volume,1)
        out+=frames(raw,0,size,2)
    else:raise ValueError('Unsupported audio transformation')
    if target_size>len(out):raise ValueError('Audio output shorter than recipe')
    return bytes(out[:target_size])


def legacy_bank_recipe(source,target):
    """Maintainer: frozen header plus original sample spans, verified by caller."""
    if len(source)<0x4000 or len(target)<0x8010 or target[:4]!=b'wp\x03\x00':raise ValueError('Not an old bank conversion')
    count=struct.unpack_from('<I',target,0x8000)[0];prefix=0x8010+16*count
    if count>4096 or prefix>len(target):raise ValueError('Invalid target bank table')
    if source[:4]==b'wp\x02\x00'and len(source)>=0x8010:
        old_count,size=struct.unpack_from('<2I',source,0x8000);start=0x8010+16*old_count
        if old_count!=count or start+size>len(source)or prefix+size!=len(target):raise ValueError('Incompatible native bank spans')
        ordinary=bytearray(source[:start]);ordinary[2]=3;struct.pack_into('<I',ordinary,0x8008,1)
        if ordinary==target[:prefix]:raise ValueError('Use the smaller native bank recipe')
        return target[:prefix],[[start,size]]
    archive,_,tail=struct.unpack_from('<3I',source);section2=archive-tail;seen=set();spans=[]
    for i in range(1,512):
        eid,_,group,vol,pan,kind,rate,rate2,left,right,size,_=struct.unpack_from('<HHBBBB6I',source,i*32)
        kind&=7
        if not kind:continue
        if kind not in (2,3,4):raise ValueError('Unsupported old sample')
        if kind==3:at=section2+left;channels=1
        else:
            at=left-0xf020;channels=2 if kind==4 else 1
            if at<0x4000 or at+16>len(source):raise ValueError('Invalid old sample offset')
            if kind==4:
                size=right-left
                if rate!=rate2:raise ValueError('Stereo rate mismatch')
            else:
                end=next((p+16 for p in range(at+16,len(source)-15,16)if source[p+1]==7),None)
                if end is None:raise ValueError('Missing old sample end')
                size=end-at
        length=size*channels
        if at<0x4000 or size<=0 or size%16 or at+length>len(source):raise ValueError('Invalid old sample span')
        key=(at,size,channels,rate)
        if key not in seen:seen.add(key);spans.append([at,length])
    if len(spans)!=count or prefix+sum(n for _,n in spans)!=len(target):raise ValueError('Old bank layout does not match frozen output')
    return target[:prefix],spans


def assemble_bank(prefix,source,spans,target_size):
    if target_size>64*1024*1024 or len(prefix)+sum(n for _,n in spans)!=target_size:raise ValueError('Invalid bank recipe size')
    yield prefix
    for offset,n in spans:
        if n<=0 or n%16:raise ValueError('Invalid bank sample size')
        source.seek(offset);raw=source.read(n)
        if len(raw)!=n:raise ValueError('Short bank source')
        yield bytes(frames(raw,0,n,36))
