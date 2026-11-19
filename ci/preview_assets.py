"""Author the silent scratch/stairs test chart; no retail chart/sample input."""
import math,struct,sys
from pathlib import Path

def build(root):
 sys.path.insert(0,str(root/'tools/release'));from chart_lz import lz_encode_opt;from chart_transform import crypt
 files={};end=10800
 for double in(False,True):
  e=[(0,0,0,c,0)for c in(250,250,204)]
  if double:e +=[(0,1,0,c,0)for c in(250,250,204)]
  e +=[(0,4,0,120,0),(0,5,0,4,4)]+[(0,8,i,v,0)for i,v in enumerate((240,250,255,3,8,18))]
  for cmd in((2,3)if double else(2,)):e +=[(0,cmd,lane,0,4)for lane in range(8)]
  for i in range(704):
   lane=(7,0,1,2,3,4,5,6)[i%8];frame=120+i*15;e.append((frame,0,lane,0,0))
   if double:e.append((frame,1,lane,0,0))
  e +=[(13,7,8,0,1)]+[(t,12,0,0,0)for t in range(0,end,120)]+[(end,6,0,0,0)];e.sort(key=lambda x:x[0]);raw=struct.pack('<II',8,16683)+b''.join(struct.pack('<IBBH',f,(cmd&15)|(hi<<4),c2,par)for f,cmd,hi,c2,par in e)+struct.pack('<I',0x7fff)
  files['chart-dp.bin'if double else'chart.bin']=crypt(lz_encode_opt(raw),'empress')
 sample=bytes([255,0,220])+bytes(13)+bytes([255,1,220])+bytes(13)+bytes([255,7,83])+bytes([119])*13
 bank=bytearray(34816);bank[:16]=bytes([119,112,3,0,16,0,0,0,100,64,0,0,0,0,0,0]);start=0x8010+8*16
 for i in range(8):
  struct.pack_into('<HHIBBHI',bank,0x10+i*16,0xff00,0x100,2,64,64,i,113);struct.pack_into('<IIHHI',bank,0x8010+i*16,i*len(sample),len(sample),1,0x3e44,0);bank[start+i*len(sample):start+(i+1)*len(sample)]=sample
 struct.pack_into('<4I',bank,0x8000,8,len(bank)-start,1,0);files['bank.wvb']=bytes(bank)
 frames=1+math.ceil(math.ceil(end/59.94+2)*44100/28);pair=(bytes([255,0,254])+bytes(13))*2;last=bytearray(pair);last[1]=last[17]=1;body=pair*(frames-1)+last;head=bytearray(2048);struct.pack_into('<12I',head,0,0x08640001,0,2048,len(body),0,0,44100,2,1,16,64,1);stream=head+body;stream+=bytes(-len(stream)%2048);files['silence.pcm']=bytes(stream)
 return files
