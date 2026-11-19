"""Original CS3..CS8 tile animations -> exact EMPRESS containers.

Standard library only. Input executable, tile art, palettes and timelines are
read from the user's owning ISO. The recipe contains offsets/IDs, no pixels.
"""
from array import array
from functools import lru_cache
import struct
import sys

from bga_source_lz import decode_lz
from bga_codec import lzss_encode
from bga_metadata import metadata
from bga_quantize import quantize

LAYOUTS={
 3:(0xff000,0x13d128,0x7ac70,0x7df48,0x94ff8,0x130ac8),
 4:(0xff000,0x12b4f0,0x8e108,0x91640,0xa6a80,0x11ec50),
 5:(0xff000,0x174828,0xb44a8,0xb9608,0xd2c70,0x163190),
 6:(0xff000,0x171138,0xd1dc4,0xd63a8,0xeb3d8,0x15e298),
 7:(0xfff80,0xf66b0,0xf68d0,0xfa9a0,0x111610,0x1aec50),
 8:(0xfff80,0x124a20,0x124ba0,0x127480,0x136f10,0x191bd0),
}
MAX_BYTES=32*1024*1024

def _array_bytes(values):
    a=array('I',values)
    if a.itemsize!=4:raise ValueError('Expected32-bit unsigned integers')
    if sys.byteorder!='little':a.byteswap()
    return a.tobytes()

class Raster:
    """Binary-alpha RGBA image; precise crop/flip/paste semantics, no renderer."""
    def __init__(self,w,h,pixels=None):
        if not 0<w<=640 or not 0<h<=512:raise ValueError('Invalid image dimensions')
        self.w,self.h=w,h
        self.p=array('I',[0])*(w*h)if pixels is None else array('I',pixels)
        if len(self.p)!=w*h:raise ValueError('Raster pixel count mismatch')
    def raw(self):return _array_bytes(self.p)
    def crop(self,box):
        x,y,r,b=box;out=Raster(r-x,b-y)
        left,right=max(0,x),min(self.w,r)
        if left>=right:return out
        for sy in range(max(0,y),min(self.h,b)):
            dest=(sy-y)*out.w+left-x;out.p[dest:dest+right-left]=self.p[sy*self.w+left:sy*self.w+right]
        return out
    def flip(self,vertical=False,horizontal=False):
        out=Raster(self.w,self.h)
        for y in range(self.h):
            row=self.p[(self.h-1-y if vertical else y)*self.w:(self.h-y if vertical else y+1)*self.w]
            if horizontal:row.reverse()
            out.p[y*self.w:(y+1)*self.w]=row
        return out
    def paste(self,other,x,y,mask=False):
        l,r=max(0,x),min(self.w,x+other.w)
        if l>=r:return
        for dy in range(max(0,y),min(self.h,y+other.h)):
            src=(dy-y)*other.w+l-x;dest=dy*self.w+l;row=other.p[src:src+r-l]
            if mask:
                for i,c in enumerate(row):
                    if c>>24:self.p[dest+i]=c
            else:self.p[dest:dest+r-l]=row
    def bbox(self):
        left,right,top,bottom=self.w,0,self.h,0
        for i,c in enumerate(self.p):
            if c>>24:
                y,x=divmod(i,self.w);left=min(left,x);right=max(right,x+1);top=min(top,y);bottom=max(bottom,y+1)
        return (left,top,right,bottom)if right else None


def frames(exe,data,palette,clips,version):
    va,pt,ct,ft,tt,partt=LAYOUTS[version]
    ptr=struct.unpack_from('<I',exe,pt+palette*4)[0]-va
    if not 0<=ptr<len(exe):raise ValueError('Palette pointer outside executable')
    colors=decode_lz(exe[ptr:])
    if not colors or len(colors)%32 or len(colors)>65536:raise ValueError('Invalid source palette')
    pals=[]
    for at in range(0,len(colors),32):
        pal=[]
        for i,(v,)in enumerate(struct.iter_unpack('<H',colors[at:at+32])):
            r,g,b=(v&31)*8,((v>>5)&31)*8,((v>>10)&31)*8
            pal.append(r|(g<<8)|(b<<16)|((255 if i else 0)<<24))
        pals.append(pal)
    first=struct.unpack_from('<I',data,4)[0];images={};at=12
    while True:
        off=struct.unpack_from('<I',data,at)[0];at+=4
        if off==0xffffffff:break
        if len(images)>=2048 or not 0<=off*4+4<len(data):raise ValueError('Invalid source tile offset')
        raw=decode_lz(data[off*4+4:])
        if len(raw)!=8192:raise ValueError('Expected128x1284-bit source tile')
        indices=bytearray(16384);indices[::2]=bytes(v&15 for v in raw);indices[1::2]=bytes(v>>4 for v in raw)
        images[first+len(images)]=indices
    tiles={};fulls={};output=[];total_parts=0
    for clip in clips:
        ai,n=struct.unpack_from('<HH',exe,ct+clip*4);parts=[]
        if n>8192:raise ValueError('Animation has too many source frames')
        for i in range(n):
            ix,duration,flags,w,h,_=struct.unpack_from('<HBBHHH',exe,ft+(ai+i)*10)
            if duration==255:continue
            tile,nt=struct.unpack_from('<HH',exe,partt+ix*4);placements=[]
            total_parts+=nt
            if total_parts>262144:raise ValueError('Animation tile budget exceeded')
            canvas=Raster(512,512)if version==8 else None
            for j in range(nt):
                x,y,w,h,img,sx,sy,pal,rot,_=struct.unpack_from('<hhhhHBBBBH',exe,tt+(tile+j)*16)
                if rot not in (0,1,2,3)or not 0<w<=512 or not 0<h<=512 or img not in images or pal>=len(pals):raise ValueError('Invalid source tile placement')
                key=img,pal
                if key not in tiles:tiles[key]=Raster(128,128,(pals[pal][v]for v in images[img]))
                pixels=tiles[key];part=pixels.crop((sx,sy,sx+w,sy+h))
                if rot:part=part.flip(bool(rot&1),bool(rot&2))
                if canvas is not None:canvas.paste(part,x,y,mask=True)
                else:
                    left,top=max(x,171),max(y,0);right,bottom=min(x+w,469),min(y+h,219)
                    if left<right and top<bottom:
                        visible=part.crop((left-x,top-y,right-x,bottom-y));box=visible.bbox()
                        if box:
                            fullkey=img,pal,rot
                            if fullkey not in fulls:fulls[fullkey]=pixels.flip(bool(rot&1),bool(rot&2))if rot else pixels
                            ax=128-sx-w if rot&2 else sx;ay=128-sy-h if rot&1 else sy
                            placements.append((left-171+box[0],top+box[1],fulls[fullkey],(ax+left-x+box[0],ay+top-y+box[1],box[2]-box[0],box[3]-box[1])))
            parts.append((max(1,duration),canvas.crop((171,0,469,219))if canvas is not None else placements))
        output.append(parts)
    return output

@lru_cache(1)
def _permutation():
    # PSMT8 unswizzle equation, same mapping as base_assets.sprite().
    w,h=640,512;out=array('I')
    for y in range(h):
        for x in range(w):
            xr=x^(4*(((y>>1)^(y>>2))&1))
            out.append((y>>2)*4*w+(y&1)*2*w+(x>>4)*32+4*(xr&7)+2*((xr>>3)&1)+((y&3)>>1))
    return out

def texture(sheet):
    indices,palette=quantize(sheet.raw());out=bytearray(1024+640*512)
    for v in range(256):
        i=v^((((v>>3)^(v>>4))&1)*24);out[i*4:i*4+4]=palette[v*4:v*4+4]
    for at,v in zip(_permutation(),indices):out[1024+at]=v
    return bytes(out)

def convert(exe,data,version,palette,clips):
    if version not in LAYOUTS or not 0<len(exe)<=16*1024*1024 or not 12<=len(data)<=MAX_BYTES or type(palette)is not int or not 0<=palette<=65535 or not 1<=len(clips)<=8 or any(type(c)is not int or not 0<=c<=65535 for c in clips):raise ValueError('Invalid source animation recipe')
    try:return _convert(exe,data,version,palette,clips)
    except (struct.error,IndexError,KeyError)as e:raise ValueError('Source animation reference outside input')from e

def _convert(exe,data,version,palette,clips):
    source=frames(exe,data,palette,clips,version);unique=[];lookup={};sprites=[];sprite_lookup={};timelines=[]
    for clip in source:
        timeline=[]
        for duration,frame in clip:
            cells=[]
            if version<8:
                for x,y,full,rect in reversed(frame):
                    key=full.raw()
                    if key not in lookup:lookup[key]=len(unique);unique.append(full)
                    iid=lookup[key];key=iid,rect
                    if key not in sprite_lookup:sprite_lookup[key]=len(sprites);sprites.append(key)
                    cells.append((sprite_lookup[key],x,y))
            else:
                box=frame.bbox()
                if box:
                    crop=frame.crop(box);key=(crop.w,crop.h,crop.raw())
                    if key not in lookup:lookup[key]=len(unique);unique.append(crop)
                    cells.append((lookup[key],box[0],box[1]))
            timeline.append((duration,cells))
        timelines.append(timeline)
    if version<8 and not unique:unique=[Raster(1,1)];sprites=[(0,(0,0,1,1))]
    # Release the source frame images before encoding large animation sheets.
    del source,lookup
    sheets=[];positions=[];x=y=rowh=0;sheet=Raster(640,512)
    for im in unique:
        w,h=im.w,im.h
        if x+w>640:x=0;y+=rowh;rowh=0
        if y+h>512:sheets.append(sheet);sheet=Raster(640,512);x=y=rowh=0
        if len(sheets)>=32:raise ValueError('Too many EMPRESS animation sheets')
        positions.append((x,len(sheets)*1024+y));sheet.paste(im,x,y);x+=w+2;rowh=max(rowh,h+2)
    sheets.append(sheet)
    if version<8:
        rects=[]
        for iid,(sx,sy,w,h)in sprites:
            if not(0<=sx<128 and 0<=sy<128 and sx+w<=128 and sy+h<=128):raise ValueError('Invalid source sprite rectangle')
            x,y=positions[iid];rects.append((x+sx,y+sy,w,h))
    else:rects=[(x,y,im.w,im.h)for (x,y),im in zip(positions,unique)]
    anims=[]
    for timeline in timelines:
        els=[];t=0
        for duration,cells in timeline:
            if t+duration>65535:raise ValueError('Animation exceeds native timeline range')
            for sprite,x,y in cells:
                els.append(dict(sprite=sprite,start=t,end=t+duration,pivot=(0,0),pos=[(t,168+round(x*304/298),round(y*416/219))],zoom=[(t,102,190)]))
            t+=duration
        anims.append((t,(320,240),els))
    meta=metadata([(640,512)]*len(sheets),rects,anims)
    raw=[meta]+[texture(sheet)for sheet in sheets]
    packed=[lzss_encode(chunk,chain_limit=48)for chunk in raw]
    n=len(sheets);head=struct.pack('<HHI',8+(n+1)*8,n,0)+b''.join(struct.pack('<II',len(a),len(b))for a,b in zip(raw,packed))
    return head+b''.join(packed)

def from_iso(recipe,read):
    """Bounded, hash-checked reader adapter; read(offset, size) is ISO-local."""
    import hashlib
    if recipe.get('format')!=1 or recipe.get('op')!='bga':raise ValueError('Unsupported BGA recipe')
    inputs=[]
    for name,limit in [('executable',16*1024*1024),('tiles',MAX_BYTES)]:
        row=recipe[name];at,size=row['offset'],row['bytes']
        if type(at)is not int or type(size)is not int or at<0 or not 0<size<=limit:raise ValueError('Invalid BGA ISO extent')
        data=read(at,size)
        if len(data)!=size or hashlib.sha256(data).hexdigest()!=row['sha256']:raise ValueError('BGA source SHA256 mismatch: '+name)
        inputs.append(data)
    return convert(inputs[0],inputs[1],recipe['version'],recipe['palette'],recipe['clips'])
