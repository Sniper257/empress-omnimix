"""Portable text rasterizer. Default profiles contain coverage/metrics, never fonts.

Python3.10+, standard library only. Font atlases are built by prepare_text_fonts.py.
"""
import base64
import json
import struct
import zlib
from pathlib import Path

MAX_PIXELS = 16 * 1024 * 1024
MAX_TEXT = 512


def inflate(data, limit):
    d = zlib.decompressobj()
    try:out = d.decompress(data, limit + 1)
    except zlib.error as e:raise ValueError('Invalid compressed glyph atlas') from e
    if len(out) > limit or not d.eof or d.unused_data or d.unconsumed_tail:
        raise ValueError('Invalid or oversized compressed glyph atlas')
    return out


class Mask:
    def __init__(self, w, h, pixels=None):
        if type(w) is not int or type(h) is not int or not 1 <= w <= 65536 or not 1 <= h <= 2048 or w*h > MAX_PIXELS:
            raise ValueError('Invalid text raster dimensions')
        self.w, self.h = w, h
        self.p = bytearray(w*h) if pixels is None else bytearray(pixels)
        if len(self.p) != w*h:
            raise ValueError('Invalid text raster length')

    def bbox(self):
        # Run zero scanning in the standard-library byte routines. This is the
        # same nonzero-pixel bounding box, without a Python loop over blank rows.
        binary=bytes(self.p).translate(bytes([0]+[1]*255))
        first,last=binary.find(b'\1'),binary.rfind(b'\1')
        if first<0:return None
        top,bottom=first//self.w,last//self.w+1
        left,right=self.w,0
        for y in range(top,bottom):
            lo,hi=y*self.w,(y+1)*self.w
            a,b=binary.find(b'\1',lo,hi),binary.rfind(b'\1',lo,hi)
            if a>=0:left=min(left,a-lo);right=max(right,b-lo+1)
        return left,top,right,bottom

    def crop(self, box):
        l,t,r,b = box
        if not 0 <= l < r <= self.w or not 0 <= t < b <= self.h:
            raise ValueError('Invalid text crop')
        return Mask(r-l,b-t,b''.join(self.p[y*self.w+l:y*self.w+r] for y in range(t,b)))

    def paste(self, other, x, y):
        for sy in range(max(0,-y),min(other.h,self.h-y)):
            for sx in range(max(0,-x),min(other.w,self.w-x)):
                a=(sy+y)*self.w+sx+x
                self.p[a]=max(self.p[a],other.p[sy*other.w+sx])

    def resize(self, w, h):
        """Separable integer area filter; stable across OS/Python builds."""
        if (w,h)==(self.w,self.h):return Mask(w,h,self.p)
        # Weights are integer overlaps of input/output cells on a common grid.
        def spans(src,dst):
            rows=[]
            for i in range(dst):
                lo,hi=i*src,(i+1)*src
                rows.append([(j,min(hi,(j+1)*dst)-max(lo,j*dst)) for j in range(lo//dst,(hi-1)//dst+1)])
            return rows
        xs,ys=spans(self.w,w),spans(self.h,h)
        out=Mask(w,h)
        # Do not round the horizontal pass: a single rounding at the end.
        rows={}
        for y,sp in enumerate(ys):
            for sy,_ in sp:
                if sy not in rows:
                    srcrow=self.p[sy*self.w:(sy+1)*self.w]
                    rows[sy]=[sum(srcrow[sx]*weight for sx,weight in row) for row in xs] if any(srcrow) else None
            active=[(rows[sy],weight)for sy,weight in sp if rows[sy] is not None]
            for x in range(w) if active else ():
                total=sum(row[x]*weight for row,weight in active)
                out.p[y*w+x]=(total+self.w*self.h//2)//(self.w*self.h)
            needed={sy for sy,_ in ys[y+1]} if y+1<h else set()
            rows={sy:r for sy,r in rows.items() if sy in needed}
        return out

    def dilate(self, radius=1):
        out=Mask(self.w,self.h)
        for y in range(self.h):
            for x in range(self.w):
                out.p[y*self.w+x]=max(self.p[sy*self.w+sx] for sy in range(max(0,y-radius),min(self.h,y+radius+1)) for sx in range(max(0,x-radius),min(self.w,x+radius+1)))
        return out


class Atlas:
    def __init__(self, data):
        if data[:4]!=b'MGA1' or len(data)>32*1024*1024:
            raise ValueError('Invalid font atlas')
        self.info=json.loads(inflate(data[4:],64*1024*1024))
        if self.info.get('format')!=1 or self.info.get('cap')!=192:
            raise ValueError('Unsupported font atlas')
        self.roles=self.info['roles'];self.cache={};self.scaled={}

    @classmethod
    def load(cls,path):return cls(Path(path).read_bytes())

    def glyph(self,role,ch):
        key=role,ch
        if key not in self.cache:
            try:g=self.info['glyphs'][self.roles[role]['glyphs'][ch]]
            except KeyError:raise ValueError(f'Missing glyph U+{ord(ch):04X} {ch!r} in role {role}') from None
            w,h=g['size'];raw=base64.b64decode(g['pixels'],validate=True)
            if len(raw)!=w*h or not 0<w<=1024 or not 0<h<=1024:raise ValueError('Invalid glyph geometry')
            self.cache[key]=(g,Mask(w,h,raw))
        return self.cache[key]

    def coverage(self,text,role,cap,top,height,bold=0,pad=16):
        if not isinstance(text,str) or len(text)>MAX_TEXT or any(ord(ch)<32 for ch in text):
            raise ValueError('Text must be a single printable line of at most512 characters')
        if not 1<=cap<=256 or not 0<=bold<=4:raise ValueError('Invalid cap height/stroke')
        r=self.roles[role];scale=cap/192;cursor=pad+bold;placed=[];previous=''
        for ch in text:
            g,m=self.glyph(role,ch)
            if previous:cursor+=r.get('kern',{}).get(previous+ch,0)*scale/64
            placed.append((g,m,cursor));cursor+=g['advance']*scale/64;previous=ch
        width=max(1,round(cursor+pad+bold))
        for g,m,x in placed:width=max(width,round(x+(g['offset'][0]+m.w)*scale)+pad+bold)
        canvas=Mask(width,height)
        for g,m,x in placed:
            # Glyph masks are immutable while compositing; reuse exact scaled bytes.
            key=(id(m),cap,bold)
            if key not in self.scaled:
                rendered=m.resize(max(1,round(m.w*scale)),max(1,round(m.h*scale)))
                self.scaled[key]=rendered.dilate(bold) if bold else rendered
            m=self.scaled[key]
            canvas.paste(m,round(x+g['offset'][0]*scale),round(top+g['offset'][1]*scale))
        return canvas


def split_title(text):
    cuts=[i for i,c in enumerate(text) if c in '（([〜~-' and i] or [i for i,c in enumerate(text) if c==' ' and i]
    if not cuts:cuts=[max(1,len(text)//2)]
    cut=min(cuts,key=lambda i:abs(i-len(text)/2))
    return text[:cut].strip(),text[cut:].strip()


def fitted(atlas,text,role,top,bottom,height,width,bold=0,xscale=1.0):
    """Supersample4x; fit actual ink to protect descenders, accents and outline."""
    if not 3<=width<=640 or not 3<=height<=384 or not 0<=top<=bottom<height:
        raise ValueError('Invalid text slot')
    cap=bottom-top+1
    im=atlas.coverage(text,role,cap*4-2*bold,top*4+bold,(height+cap*2)*4,bold)
    box=im.bbox()
    if box is None:return Mask(1,height)
    # Keep one full output pixel free for the outline at every edge.
    ink=im.crop(box)
    fit_h=min(ink.h,(height-2)*4)
    fit_w=min(max(1,round(ink.w*xscale)),(width-4)*4)
    ink=ink.resize(fit_w,fit_h)
    out=Mask(fit_w+8,height*4)
    y=box[1] if box[1]>=4 and box[3]<=(height-1)*4 else 4
    out.paste(ink,4,y)
    return out.resize(max(1,(out.w+3)//4),height)


def card_image(atlas,texts):
    if set(texts)!={'title','genre','artist'}:raise ValueError('Card requires title, genre and artist')
    out=Mask(384,384)
    for key,role,top,bottom,bold,xs in (
        ('genre','card_genre',52,67,0,1.0),('title','large_title',87,135,1,1.05),('artist','card_artist',229,242,1,1.0)):
        text=texts[key];lines=[(text,top,bottom)]
        # Width measured before fitting, including the chosen font's kerning.
        natural=atlas.coverage(text,role,(bottom-top+1)*4,0,512)
        box=natural.bbox()
        if key=='title' and box and (box[2]-box[0])*xs/4>1.8*290:
            a,b=split_title(text);half=(bottom-top+1-4)//2
            lines=[(a,top,top+half-1),(b,bottom-half+1,bottom)]
        for line,t,b in lines:
            m=fitted(atlas,line,role,t,b,384,290,bold,xs)
            out.paste(m,round(149-m.w/2),0)
    return out
