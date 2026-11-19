"""Research candidate: fixed-disc chart conversions, no precomputed chart deltas.

Legacy timing and compression reproduce the accepted importer exactly. Original
ISO bytes and output hashes remain authoritative. The vendored Blowfish module
is GPL-3.0-or-later; its source notice and license accompany this prototype.
"""
from functools import lru_cache
import struct
from chart_lz import lz_encode_opt
from vendor.blowfish import Cipher

MAX_BYTES=16*1024*1024
KEYS={"empress": "75596c466756774f7061516d58644952", "gold": "73456b324931587137627a30394d7476", "djt": "65716b42697078327767457855414947"}
NAMES=('encrypt-empress','rekey-gold-empress','rekey-djt-empress',*('legacy-'+str(i)+'-empress'for i in range(3,9)))

@lru_cache(None)
def cipher(name):return Cipher(bytes.fromhex(KEYS[name]),byte_order='little')

def crypt(data,name,decrypt=False):
    if len(data)<8 or len(data)>MAX_BYTES:raise ValueError('Invalid chart cipher input size')
    c=cipher(name)
    if len(data)%8:
        return b''.join(c.decrypt_cbc_cts(data,bytes(8))if decrypt else c.encrypt_cbc_cts(data,bytes(8)))
    out=bytearray();prev=bytes(8)
    for at in range(0,len(data),8):
        block=data[at:at+8]
        if decrypt:out.extend(a^b for a,b in zip(c.decrypt_block(block),prev));prev=block
        else:prev=c.encrypt_block(bytes(a^b for a,b in zip(block,prev)));out.extend(prev)
    return bytes(out)

def decode_lz(data):
    """Bounded chart-LZ reader; preserve all decompressed bytes through EOF token."""
    at=0;out=bytearray();control=0
    try:
        while True:
            control>>=1
            if not control&0x100:control=data[at]|0xff00;at+=1
            token=data[at];at+=1
            if not control&1:out.append(token)
            elif token==255:return bytes(out)
            elif token<0xc0:
                if token<0x80:distance=((token&3)<<8)|data[at];at+=1;length=(token>>2)+3
                else:distance=(token&15)+1;length=(token>>4)-6
                if not 0<distance<=len(out):raise ValueError('Invalid chart LZ back reference')
                for _ in range(length):out.append(out[-distance])
            else:
                length=token-0xb9+1
                if at+length>len(data):raise ValueError('Truncated chart LZ literals')
                out.extend(data[at:at+length]);at+=length
            if len(out)>MAX_BYTES:raise ValueError('Chart LZ output exceeds limit')
    except IndexError as e:raise ValueError('Truncated chart LZ data')from e

def frame(value):
    if value<0 or value==0x7fff:raise ValueError('Invalid source chart timestamp')
    result=(value*526+262)//525
    if result==0x7fff:raise ValueError('Converted timestamp collides with terminator')
    return result

def normalize(raw,version):
    ply=decode_lz(raw)
    if version==8:
        out=bytearray(ply)
        if len(out)<8:raise ValueError('Short chart header')
        off,clock=struct.unpack_from('<II',out)
        if off!=8 or clock not in(16716,16722):raise ValueError('Unsupported eighth-style chart header')
        struct.pack_into('<I',out,4,16683)
        for at in range(off,len(out)-7,8):
            timestamp=struct.unpack_from('<I',out,at)[0]
            if timestamp==0x7fff:break
            struct.pack_into('<I',out,at,frame(timestamp))
            if out[at+4]&15==7 and struct.unpack_from('<H',out,at+6)[0]==2:struct.pack_into('<H',out,at+6,0)
        return bytes(out)
    if version<7:
        off,clock=struct.unpack_from('<II',ply)if version>=5 else(0,16716)
        if off==4:clock=16716
        if off not in(0,4,8):raise ValueError('Invalid old chart header')
        expanded=bytearray(struct.pack('<II',8,clock))
        for at in range(off,len(ply)-3,4):
            t,cmd,value=struct.unpack_from('<HBB',ply,at)
            if t==0x7fff:break
            low=cmd&15;c2=value if low in(0,1,4,5,8)else 0;param=value if low in(2,3,5,7)else 0
            expanded.extend(struct.pack('<IBBH',t,cmd,c2,param))
        expanded.extend(struct.pack('<I',0x7fff));ply=bytes(expanded)
    if len(ply)<8:raise ValueError('Short legacy chart')
    off,clock=struct.unpack_from('<II',ply)
    if off not in(8,12,16)or clock not in(16716,16722):raise ValueError('Unsupported legacy chart header')
    out=bytearray(struct.pack('<II',8,16683));started=False;trigger=False
    for at in range(off,len(ply)-7,8):
        timestamp,cmd,c2,value=struct.unpack_from('<IBBH',ply,at)
        if timestamp==0x7fff:break
        if cmd&15==7 and value in(1,2):value=0 if started else 1;started=True;trigger=True
        out.extend(struct.pack('<IBBH',frame(timestamp),cmd,c2,value))
    if not trigger:raise ValueError('Legacy chart has no backing-stream trigger')
    out.extend(struct.pack('<I',0x7fff));return bytes(out)

def convert(kind,raw,target_size):
    if kind not in NAMES or not 0<target_size<=MAX_BYTES:raise ValueError('Invalid chart transformation')
    if kind=='encrypt-empress':decode_lz(raw);out=crypt(raw,'empress')
    elif kind.startswith('rekey-'):
        plain=crypt(raw,kind.split('-')[1],True);decode_lz(plain);out=crypt(plain,'empress')
    else:out=crypt(lz_encode_opt(normalize(raw,int(kind.split('-')[1]))),'empress')
    if len(out)!=target_size:raise ValueError('Chart conversion size mismatch')
    return out
