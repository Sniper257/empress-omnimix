"""ISO-derived folder labels and licensed-font fallback captions, stdlib only."""
import json,struct
from pathlib import Path
from text_native import offset,map_pixels,style
from text_raster import Mask,fitted

GLYPHS=json.loads(Path(__file__).with_name('folder_glyphs.json').read_text(encoding='utf-8'))
FILL=(255,221);NAVY=10
OUTLINE=[(dy,dx)for dy in range(-2,3)for dx in (-1,0,1)if not(abs(dy)==2 and dx)]
SUFFIX={
 't':['001000','001000','111110','001000','001000','001010','000100'],
 'h':['100000','100000','100000','111100','100010','100010','100010'],
 's':['000000','000000','011110','100000','011100','000010','111100'],
 'n':['000000','000000','111100','100010','100010','100010','100010'],
 'd':['000010','000010','000010','011110','100010','100010','011110'],
 'r':['000000','000000','101100','110010','100000','100000','100000']}

def indexed(cont,number):
    if not 0<=number<len(cont.rects):raise ValueError('Folder glyph sprite absent')
    x,sy,w,h=cont.rects[number];sheet,y=divmod(sy,1024);sw,sh=cont.sizes[sheet]
    if x+w>sw or y+h>sh:raise ValueError('Folder glyph rectangle exceeds sheet')
    raw=cont.chunk(sheet+1)
    return w,h,sheet,bytes(raw[1024+offset(xx,yy,sw)]for yy in range(y,y+h)for xx in range(x,x+w))

def cut(cont,number,a,b):
    w,h,sheet,data=indexed(cont,number)
    if not 0<=a<b<=w:raise ValueError('Invalid folder glyph columns')
    return b-a,h,bytes(data[y*w+a:y*w+b][x]for y in range(h)for x in range(b-a))

def join(parts,h,clear,gap=0):
    w=sum(p[0]for p in parts)+max(0,len(parts)-1)*gap;out=bytearray([clear])*(w*h);at=0
    for pw,ph,data in parts:
        if ph!=h:raise ValueError('Folder glyph height mismatch')
        for y in range(h):out[y*w+at:y*w+at+pw]=data[y*pw:(y+1)*pw]
        at+=pw+gap
    return w,h,out

def outline(mask,colour,clear):
    w,h=mask.w,mask.h;out=bytearray([clear])*(w*h)
    for y in range(h):
        for x in range(w):
            if mask.p[y*w+x]:
                for dy,dx in OUTLINE:
                    xx,yy=x+dx,y+dy
                    if 0<=xx<w and 0<=yy<h:out[yy*w+xx]=colour
    return out

def label(cont,text):
    pal=cont.palette(0);clear=next(i for i,p in enumerate(pal)if p[3]==0);parts=[]
    for ch in text:
        if ch==' ':parts.append((8,14,bytes([clear])*112));continue
        sp,a,b=GLYPHS['label'][ch];parts.append(cut(cont,sp,a,b+1))
    w,h,raw=join(parts,14,clear,2);w+=2;idx=bytearray([clear])*(w*h);mask=Mask(w,h);at=1
    for pw,ph,data in parts:
        for y in range(h):
            for x in range(pw):
                if data[y*pw+x]in FILL:idx[y*w+at+x]=data[y*pw+x];mask.p[y*w+at+x]=255
        at+=pw+2
    ring=outline(mask,NAVY,clear)
    for i,v in enumerate(mask.p):
        if not v:idx[i]=ring[i]
    return w,h,0,clear,bytes(idx)

def caption(cont,text):
    pal=cont.palette(12);clear=next(i for i,p in enumerate(pal)if p[3]==0)
    w,h,_,data=indexed(cont,0x6e);parts=[cut(cont,0x6e,0,1)]
    for ch in text:
        if ch==' ':parts.append((12,38,bytes([clear])*456));continue
        sp,a,b=GLYPHS['caption'][ch];parts.append(cut(cont,sp,a-1,b+2))
    parts.append(cut(cont,0x6e,w-1,w));w,h,out=join(parts,38,clear)
    return w,h,12,clear,bytes(out)

def pb_label(cont,text,w,h):
    if h!=14:raise ValueError('Unsupported folder label height')
    pal=cont.palette(0);clear=next(i for i,p in enumerate(pal)if p[3]==0)
    def core(word):
        nw,nh,_,_,data=label(cont,word);m=Mask(nw,nh,bytes(255 if i in FILL else 0 for i in data));l,t,r,b=m.bbox();return m.crop((l,0,r,nh))
    words=text.split();parts=[]
    if words[0][0].isdigit():
        digits=''.join(c for c in words[0]if c.isdigit());parts=[core(digits)]
        for c in words[0][len(digits):]:
            small=Mask(6,7,bytes(255 if c=='1'else 0 for row in SUFFIX[c]for c in row));m=Mask(7,h);m.paste(small.resize(7,9),0,3);parts.append(m)
        parts.extend((Mask(7,h),core(words[1])))
    else:parts=[core(text)]
    m=Mask(sum(p.w for p in parts)+len(parts)-1,h);at=0
    for p in parts:m.paste(p,at,0);at+=p.w+1
    if m.w>w-2:m=m.resize(w-2,h)
    placed=Mask(w,h);placed.paste(m,(w-m.w)//2,0);m=placed;out=outline(m,NAVY,clear)
    for y in range(h):
        for x in range(w):
            if m.p[y*w+x]>127:out[y*w+x]=255 if y<h-3 else 221
    return w,h,0,clear,bytes(out)

def pb_caption(atlas,cont,text,w,h):
    if not 1<w<=640 or not 1<h<=48:raise ValueError('Invalid folder caption bounds')
    grad,top,bottom,refh=style(cont,0x6e);m=fitted(atlas,text,'large_title',2,h-3,h,w,1,1.0);pad=(w-m.w)//2
    placed=Mask(w,h);placed.paste(m,pad,0);core=Mask(w,h,bytes(255 if v>38 else 0 for v in placed.p));ring=core.dilate();pixels=[]
    for y in range(h):
        row=grad[min(refh-1,y*refh//h)]
        for x in range(w):
            i=y*w+x
            if core.p[i]:pixels.append((*[int(c*min(placed.p[i]*1.25/255,1))for c in row],255))
            else:pixels.append((0,0,0,255 if ring.p[i]else 0))
    return map_pixels(w,h,pixels,cont.palette(12),12)

def generate(atlas,spec,cont):
    mode=spec['layout'];text=spec['text']
    if mode=='pb_caption':return pb_caption(atlas,cont,text,*spec['size'])
    if mode=='pb_label':result=pb_label(cont,text,*spec['size'])
    elif mode=='label':result=label(cont,text)
    elif mode=='caption':result=caption(cont,text)
    else:raise ValueError('Unknown folder text layout')
    w,h,sheet,clear,data=result
    if not 0<w<=640 or not 0<h<=48:raise ValueError('Folder text exceeds native slot')
    return b'SPR1'+struct.pack('<3H2B',w,h,sheet,clear,0)+data
