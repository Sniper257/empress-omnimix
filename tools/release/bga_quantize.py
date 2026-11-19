"""RGB octree reduction matching Pillow's QuantOctree.c.

Python translation of the RGB path. Copyright (c) 2010 Oliver Tonnhofer
<olt@bogosoft.com>, Omniscale. The MIT notice is in licenses/QuantOctree.txt.
Stable cube-index tie ordering makes the historical Linux result portable.
"""
from collections import Counter
import struct

def _f32(n):
    return struct.unpack('<f',struct.pack('<f',n))[0]

def _avg(b):
    if not b[0]:return (0,0,0)
    count=_f32(b[0])
    return tuple(min(255,int(_f32(_f32(v)/count)))for v in b[1:])

def _fine(r,g,b):return (r>>4)*256+(g>>4)*16+(b>>4)
def _coarse(r,g,b):return (r>>6)*16+(g>>6)*4+(b>>6)

def quantize(rgba):
    # Convert RGB with alpha ignored, as the existing im.convert('RGB') path.
    histogram=Counter(v[0]&0xffffff for v in struct.iter_unpack('<I',rgba))
    fine=[[0,0,0,0]for _ in range(4096)]
    for color,n in histogram.items():
        r,g,b=color&255,(color>>8)&255,color>>16
        q=fine[_fine(r,g,b)];q[0]+=n;q[1]+=r*n;q[2]+=g*n;q[3]+=b*n
    coarse=[[0,0,0,0]for _ in range(64)]
    for i,bucket in enumerate(fine):
        j=((i>>10)&3)*16+((i>>6)&3)*4+((i>>2)&3)
        for k,v in enumerate(bucket):coarse[j][k]+=v
    used=sum(bool(b[0])for b in coarse);nf=255-used
    fine=sorted(fine,key=lambda b:-b[0])
    def subtract(start,end):
        for b in fine[start:end]:
            if b[0]:
                q=coarse[_coarse(*_avg(b))]
                for k,v in enumerate(b):q[k]-=v
    subtract(0,nf)
    while used>sum(bool(b[0])for b in coarse):
        old=nf;used=sum(bool(b[0])for b in coarse);nf=255-used;subtract(old,nf)
    coarse=sorted(coarse,key=lambda b:-b[0]);palette=coarse[:used]+fine[:nf]
    rgb=[_avg(b)for b in palette];coarse_lut=[0]*64
    for i in range(used-1,-1,-1):coarse_lut[_coarse(*rgb[i])]=i
    lut=[coarse_lut[((i>>10)&3)*16+((i>>6)&3)*4+((i>>2)&3)]for i in range(4096)]
    for i in range(254,used-1,-1):lut[_fine(*rgb[i])]=i
    colors={c:lut[_fine(c&255,(c>>8)&255,c>>16)]for c in histogram}
    indices=bytes(colors[v[0]&0xffffff]if v[0]>>24 else 255 for v in struct.iter_unpack('<I',rgba))
    return indices,b''.join(bytes((*c,128))for c in rgb)+bytes(4)
