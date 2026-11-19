"""Bounded native text extraction and EMPRESS palette mapping, stdlib only."""
import struct
from text_raster import Mask


def lzss(data,size):
    if not 0<size<=16*1024*1024:raise ValueError('Invalid native chunk size')
    ring=bytearray(4096);out=bytearray();at=0;pos=0xfee
    while len(out)<size:
        if at>=len(data):raise ValueError('Truncated native flags')
        flags=data[at];at+=1
        for bit in range(8):
            if len(out)==size:break
            if flags>>bit&1:
                if at>=len(data):raise ValueError('Truncated native literal')
                v=data[at];at+=1;out.append(v);ring[pos]=v;pos=(pos+1)&4095
            else:
                if at+2>len(data):raise ValueError('Truncated native match')
                lo,hi=data[at:at+2];at+=2;start=lo|((hi&240)<<4);n=(hi&15)+3
                if len(out)+n>size:raise ValueError('Native match overrun')
                for j in range(n):v=ring[(start+j)&4095];out.append(v);ring[pos]=v;pos=(pos+1)&4095
    return bytes(out)


def swizzle(i):return i^((((i>>3)^(i>>4))&1)*24)

def offset(x,y,w):
    xr=x^(4*(((y>>1)^(y>>2))&1))
    return (y>>2)*4*w+(y&1)*2*w+(x>>4)*32+4*(xr&7)+2*((xr>>3)&1)+((y&3)>>1)


class Container:
    def __init__(self,data):
        self.data=data;self.cache={}
        if len(data)<12:raise ValueError('Truncated native container')
        off,n=struct.unpack_from('<HH',data);table=off-(n+1)*8
        if not 1<=n<=48 or not 4<=table<=off<=len(data):raise ValueError('Invalid native chunk table')
        self.chunks=[]
        for i in range(n+1):
            raw,packed=struct.unpack_from('<II',data,table+i*8)
            if not 0<raw<=16*1024*1024 or not 0<packed or off+packed>len(data):raise ValueError('Invalid native chunk extent')
            self.chunks.append((raw,off,packed));off+=packed
        self.meta=self.chunk(0)
        if len(self.meta)<16:raise ValueError('Truncated native metadata')
        _,sheets,_,sprites,rect,_=struct.unpack_from('<4H2I',self.meta)
        if sheets!=n or 16+4*sheets>len(self.meta) or rect<16+4*sheets or rect+sprites*8>len(self.meta):raise ValueError('Invalid native rectangles')
        self.sizes=[struct.unpack_from('<2H',self.meta,16+4*i) for i in range(sheets)]
        if any(not 0<w<=1024 or not 0<h<=1024 or w%16 or h%4 for w,h in self.sizes):raise ValueError('Unsupported PSMT8 sheet dimensions')
        self.rects=[struct.unpack_from('<4H',self.meta,rect+8*i) for i in range(sprites)]

    def chunk(self,i):
        if i not in self.cache:
            size,at,n=self.chunks[i];self.cache[i]=lzss(self.data[at:at+n],size)
        return self.cache[i]

    def palette(self,sheet):
        if not 0<=sheet<len(self.sizes):raise ValueError('Native sheet absent')
        w,h=self.sizes[sheet];raw=self.chunk(sheet+1)
        if len(raw)!=1024+w*h:raise ValueError('Unexpected native texture format')
        return [tuple(raw[swizzle(i)*4:swizzle(i)*4+4]) for i in range(256)]

    def sprite(self,number):
        if not 0<=number<len(self.rects):raise ValueError('Native sprite absent')
        x,sy,w,h=self.rects[number];sheet,y=divmod(sy,1024)
        if not 0<=sheet<len(self.sizes):raise ValueError('Native sprite sheet absent')
        sw,sh=self.sizes[sheet]
        if not 0<w<=640 or not 0<h<=384 or x+w>sw or y+h>sh:raise ValueError('Native sprite exceeds texture')
        raw=self.chunk(sheet+1);pal=self.palette(sheet)
        return w,h,[pal[raw[1024+offset(xx,yy,sw)]] for yy in range(y,y+h) for xx in range(x,x+w)]


def map_pixels(w,h,pixels,palette,sheet,grey=False):
    if len(pixels)!=w*h or not 0<w<=640 or not 0<h<=48 or not 0<=sheet<48:raise ValueError('Text exceeds EMPRESS runtime slot')
    clear=next((i for i,p in enumerate(palette) if p[3]==0),None)
    opaque=[i for i,p in enumerate(palette) if p[3]>=128]
    if clear is None or not opaque:raise ValueError('Palette lacks clear/opaque entries')
    cache={};out=bytearray()
    for r,g,b,a in pixels:
        if not a:out.append(clear);continue
        if grey=='peak':r=g=b=max(r,g,b)
        elif grey is True:r=g=b=round(r*.299+g*.587+b*.114)
        rgb=r,g,b
        if rgb not in cache:cache[rgb]=min(opaque,key=lambda i:sum((palette[i][k]-rgb[k])**2 for k in range(3)))
        out.append(cache[rgb])
    return b'SPR1'+struct.pack('<HHHBB',w,h,sheet,clear,0)+out


def extract(source,number,target,sheet,grey=False):
    if grey not in (False,True,'peak'):raise ValueError('Unknown grayscale conversion')
    w,h,pixels=source.sprite(number)
    return map_pixels(w,h,pixels,target.palette(sheet),sheet,grey)


def style(target,number):
    w,h,pixels=target.sprite(number);rows=[];gradient=[]
    for y in range(h):
        fill=[p for p in pixels[y*w:(y+1)*w] if p[3] and sum(p[:3])>330]
        if fill:rows.append(y);gradient.append(tuple(sum(p[k] for p in fill)/len(fill) for k in range(3)))
        else:gradient.append(None)
    if not rows:raise ValueError('Reference sprite has no text fill')
    for y in range(h):
        if gradient[y] is None:gradient[y]=gradient[min(rows,key=lambda i:abs(i-y))]
    return gradient,rows[0],rows[-1],h


def render_sprite(atlas,text,role,target,reference,max_width=640,bold=0,xscale=1.0):
    from text_raster import fitted
    grad,top,bottom,h=style(target,reference);sheet=target.rects[reference][1]>>10
    m=fitted(atlas,text,role,top,bottom,h,max_width,bold,xscale)
    core=Mask(m.w,h,bytes(255 if v>38 else 0 for v in m.p));ring=core.dilate()
    pixels=[]
    for y in range(h):
        for x in range(m.w):
            i=y*m.w+x
            if core.p[i]:pixels.append((*[int(c*min(m.p[i]*1.25/255,1)) for c in grad[y]],255))
            else:pixels.append((0,0,0,255 if ring.p[i] else 0))
    return map_pixels(m.w,h,pixels,target.palette(sheet),sheet)


def card_metadata():
    """Authored clean form of the native static-card schema, not a donor copy.

    A title card has an empty animation list; the game draws sprite0 directly.
    Retain the native640x480 canvas, sentinel fields, offsets and320-byte size.
    """
    out=bytearray(320)
    struct.pack_into('<4H2I',out,0,0,1,1,1,208,304)
    struct.pack_into('<2H',out,16,384,384)
    struct.pack_into('<4H',out,208,0,0,298,349)
    struct.pack_into('<HHHHHHIhhIIII',out,224,65535,65535,0,65535,65535,0,0,320,240,0,0,0,0)
    struct.pack_into('<I',out,304,224)
    return bytes(out)


def title_card(atlas,texts):
    from text_raster import card_image
    from bga_codec import lzss_encode
    im=card_image(atlas,texts)
    meta=card_metadata()
    raw=bytearray(1024+384*384)
    for i in range(256):raw[swizzle(i)*4:swizzle(i)*4+4]=bytes((i,i,i,128))
    for y in range(384):
        for x in range(384):raw[1024+offset(x,y,384)]=im.p[y*384+x]
    packed=[lzss_encode(meta,48),lzss_encode(bytes(raw),48)]
    header=struct.pack('<HH',20,1)+b''.join(struct.pack('<II',len(r),len(c)) for r,c in zip((meta,raw),packed))
    return header+b''.join(packed)
