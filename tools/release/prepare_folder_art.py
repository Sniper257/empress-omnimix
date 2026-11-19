"""Maintainer-only PB extraction and clean fallback authoring.

Requires the existing authoring venv (Pillow/numpy), extracted Disc1 base, PB
ISO/ELF, and Compacta fonts. Consumers receive only frozen SPR1 files.
"""
import argparse
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--project',type=__import__('pathlib').Path)
parser.add_argument('--base',required=True,type=__import__('pathlib').Path)
parser.add_argument('--pb-iso',required=True,type=__import__('pathlib').Path)
parser.add_argument('--pb-exe',required=True,type=__import__('pathlib').Path)
parser.add_argument('--fonts',required=True,type=__import__('pathlib').Path)
parser.add_argument('--output',required=True,type=__import__('pathlib').Path)
args=parser.parse_args()
from pathlib import Path
import sys,struct,json,numpy as np,hashlib
from PIL import Image,ImageDraw,ImageFont,ImageFilter
ROOT=args.project or Path(__file__).resolve().parents[2];sys.path[:0]=[str(ROOT/'tools/offdisc'),str(ROOT/'tools/e16'),str(ROOT/'tools'),str(ROOT/'iidx-ps2tools')]
import mk_version_folders as V,songfolder,title_gfx as T,mk_label as M
OUT=args.output;OUT.mkdir(exist_ok=True)
emp=[V.Cont(songfolder.data1(args.base,c)) for c in (0,303)]
exe=args.pb_exe.read_bytes();rd=lambda va:struct.unpack_from('<17I',exe,va-0x100000+0x80);labels,captions,voices=map(rd,(V.LABEL,V.CAPTION,V.VOICE))
disc=V.Disc(str(args.pb_iso));pb={}
for c in (0,0x525):
 e=struct.unpack_from('<4I',exe,V.DATA1_TABLE+c*16);pb[c]=V.Cont(disc.read(V.DATA1_LBA+e[1],e[2]*2048))
names={st:((str(n)+('st' if n==1 else 'nd' if n==2 else 'rd' if n==3 else 'th')+' STYLE'),(str(n)+('st' if n==1 else 'nd' if n==2 else 'rd' if n==3 else 'th')+' style')) for n,st in enumerate(['1st_style','2nd_style','3rd_style','4th_style','5th_style','6th_style','7th_style','8th_style','9th_style','10th_style'],1)}
names.update(substream=('SUBSTREAM','substream'),red=('IIDX RED','IIDX RED'),happy_sky=('HAPPY SKY','HAPPY SKY'),distorted=('DISTORTED','DistorteD'),gold=('GOLD','GOLD'),dj_troopers=('DJ TROOPERS','DJ TROOPERS'))
fonts_caption=[str(args.fonts/n) for n in ('Compacta BT.TTF','Compacta D.ttf')]
if Path('/usr/share/fonts/TTF/impact.ttf').is_file():fonts_caption.insert(0,'/usr/share/fonts/TTF/impact.ttf')
def font_mask(text,font,size):
 f=ImageFont.truetype(font,256);bb=f.getbbox(text);im=Image.new('L',(bb[2]-bb[0],bb[3]-bb[1]));ImageDraw.Draw(im).text((-bb[0],-bb[1]),text,font=f,fill=255,stroke_width=0)
 return im.resize(size,Image.Resampling.LANCZOS)
def native_label(text,w,h):
 root=str(args.base)
 def core(word):
  idx,clear=M.label_var(root,word);mask=np.isin(idx,M.FILL);ys,xs=np.where(mask)
  return Image.fromarray(mask.astype(np.uint8)*255).crop((int(xs.min()),0,int(xs.max())+1,h))
 suffixes={
  't':["001000","001000","111110","001000","001000","001010","000100"],
  'h':["100000","100000","100000","111100","100010","100010","100010"],
  's':["000000","000000","011110","100000","011100","000010","111100"],
  'n':["000000","000000","111100","100010","100010","100010","100010"],
  'd':["000010","000010","000010","011110","100010","100010","011110"],
  'r':["000000","000000","101100","110010","100000","100000","100000"],
 }
 words=text.split();parts=[]
 if words[0][0].isdigit():
  digits=''.join(c for c in words[0] if c.isdigit());suffix=words[0][len(digits):]
  parts=[core(digits)]
  for c in suffix:
   sm=np.array([[ch=='1' for ch in row] for row in suffixes[c]],np.uint8)*255
   v=Image.new('L',(7,h));v.paste(Image.fromarray(sm).resize((7,9),Image.Resampling.NEAREST),(0,3));parts.append(v)
  spacer=Image.new('L',(7,h));parts.extend([spacer,core(words[1])])
 else:parts=[core(text)]
 mask=Image.new('L',(sum(p.width for p in parts)+max(0,len(parts)-1),h));at=0
 for part in parts:mask.paste(part,(at,0));at+=part.width+1
 # Only spacing is fitted to PB's dimensions; the mask contains no rectangle
 # backgrounds. A single final native-colour outline covers the complete word.
 if mask.width>w-2:mask=mask.resize((w-2,h),Image.Resampling.NEAREST)
 out=Image.new('L',(w,h));out.paste(mask,((w-mask.width)//2,0))
 fill=np.array(out)>0;ring=np.zeros_like(fill)
 for dy,dx in M.OUTLINE:ring|=np.roll(np.roll(np.pad(fill,2),dy,0),dx,1)[2:-2,2:-2]
 pal=emp[0].sheet(0)[0];clear=int(np.where(pal[:,3]==0)[0][0]);idx=np.full((h,w),clear,np.uint8);idx[ring]=M.NAVY
 # Original EMPRESS label fill uses white and one grey row.
 for y in range(h):idx[y,fill[y]]=255 if y<h-3 else 221
 return idx,clear
rows=[];facts=[]
for fid,st in enumerate(V.PB_STYLE,1):
 for k in (0,1):
  ref=pb[0].rgba(labels[fid]) if k==0 else pb[0x525].rgba(captions[fid]&65535)
  fill=(ref[:,:,:3].max(2)>110)&(ref[:,:,3]>0);ys,xs=np.where(fill);x0,x1,y0,y1=int(xs.min()),int(xs.max())+1,int(ys.min()),int(ys.max())+1;w,h=ref.shape[1],ref.shape[0]
  target=fill[y0:y1,x0:x1];scores=[]
  if k==0:
   score,font,shape=0,'native-EMPRESS-glyphs-clean-outline',Image.new('L',(x1-x0,y1-y0))
  else:
   for font in fonts_caption:
    shape=font_mask(names[st][k],font,(x1-x0,y1-y0));mask=np.array(shape)>127
    score=float((mask&target).sum()/max(1,(mask|target).sum()));scores.append((score,font,shape))
   score,font,shape=max(scores,key=lambda t:t[0])
  mask=Image.new('L',(w,h));mask.paste(shape,(x0,y0));a=np.array(mask)/255
  # One continuous mask/outline, with no cut rectangles or inter-glyph seams.
  outline=np.array(mask.filter(ImageFilter.MaxFilter(3 if k==0 else 5)))/255
  rgba=np.zeros((h,w,4),np.uint8);rgba[:,:,3]=np.rint(outline*255).astype(np.uint8)
  for y in range(h):
   # Native row gradient is a colour/style reference, never a copied glyph.
   cols=ref[y][fill[y]]
   colour=np.median(cols[:,:3],axis=0) if len(cols) else np.array([255,255,255])
   rgba[y,:,:3]=(a[y,:,None]*colour).astype(np.uint8)
  # Alpha-aware antialiasing is quantized into the original shared palettes.
  # Keep RGB coverage at boundaries; native sheet alpha is retained by indices.
  sheet=0 if k==0 else 12;idx,clear=T.to_indices(np.concatenate([rgba[:,:,:3],(outline[:,:,None]>.12)*255],2),emp[k].sheet(sheet)[0])
  if k==0:
   idx,clear=native_label(names[st][k],w,h);font='native-EMPRESS-glyphs-clean-outline';candidate=np.isin(idx,M.FILL);score=float((candidate&fill).sum()/max(1,(candidate|fill).sum()))
  fallback=songfolder.spr(idx,sheet,clear);native,err=V.to_spr(ref,emp[k],sheet)
  kind='label' if k==0 else 'caption'
  for variant,data in [('fallback',fallback),('pb',native)]:
   p=OUT/variant/st/(kind+'.spr');p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data)
  pal=emp[k].sheet(sheet)[0].copy();pal[clear,3]=0;shown=pal[idx].copy();shown[:,:,3]=np.where(shown[:,:,3],255,0)
  cell=Image.new('RGBA',(1200,110),(30,32,45,255));d=ImageDraw.Draw(cell);d.text((4,2),st+' '+kind+' / PB then fallback / '+Path(font).name+f' score={score:.3f}',fill='white')
  for x,pixels in [(30,ref),(650,shown)]:
   im=Image.fromarray(pixels.astype(np.uint8));im.putalpha(Image.fromarray((pixels[:,:,3]>0).astype(np.uint8)*255));cell.alpha_composite(im.resize((w*2,h*2),Image.Resampling.NEAREST),(x,20))
  rows.append(cell);facts.append(dict(style=st,kind=kind,text=names[st][k],font=font,iou=score,width=w,height=h,pb_palette_error=int(err),voice=voices[fid]))
im=Image.new('RGBA',(1200,len(rows)*110),(30,32,45,255))
for i,row in enumerate(rows):im.alpha_composite(row,(0,i*110))
im.convert('RGB').save(OUT/'fallback-comparison.png');(OUT/'art-facts.json').write_text(json.dumps(facts,indent=2)+'\n')
print([(f['style'],f['kind'],Path(f['font']).name,round(f['iou'],3)) for f in facts])
