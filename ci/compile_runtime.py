"""Source-only R5900 runtime build using reviewed original-word checks."""
from pathlib import Path
import os,subprocess,sys

def build(root,out,toolchain):
 env={k:v for k,v in os.environ.items()if k not in ('INPUT_LOG','FILE_IO_LOG','HOST_BENCH','PORTABLE_LAUNCHER','USB_HID','PREP_WATCH','ASSET_FRAME_ORDER','ASSET_SPLIT_TEXTURE','USB_BENCH','MMCE_BENCH','USB_NATIVE_PRELOAD','SAVE_PROFILE','LOAD_PROFILE','SELECT_PROFILE','PREVIEW_TRACE','USB_BOOT_TRACE','USB_BOOT_JOURNAL','EXIT_TRACE','AUDIO_PROBE','ASSET_GAP_TRACE','PREP_PROBE')}
 env['PATH']=str(toolchain/'llvm/bin')+os.pathsep+env['PATH'];out.mkdir(parents=True,exist_ok=False)
 for target in('console','host'):
  for mode in('quiet','prep','debug'):
   dest=out/(target+'-'+mode);e=dict(env,ASSET_FRAME_ORDER='1',ASSET_SPLIT_TEXTURE='1')
   e.update({'PORTABLE_LAUNCHER':'1','USB_HID':'1'}if target=='console'else{'HOST_BENCH':'1'})
   if mode=='prep':e['PREP_WATCH']='1'
   if mode=='debug':e.update(INPUT_LOG='1',FILE_IO_LOG='1')
   with(out/(dest.name+'.log')).open('w')as f:subprocess.run([sys.executable,str(root/'tools/offdisc/build_vdisc.py'),str(dest),'--reference-words',str(root/'tools/offdisc/reference-words.json')],env=e,stdout=f,stderr=subprocess.STDOUT,check=True)
   print('Built runtime:',dest.name,flush=True)
