from pathlib import Path
import re,shlex,sys
O=Path(sys.argv[1]).resolve();SDK='/usr/local/ps2dev/ps2sdk';lines=['set -eu','mkdir -p /src/out']
q=lambda args:' '.join(shlex.quote(str(a))for a in args)
configs=[
 ('iomanx','iop/system/iomanx',['exports','iomanX','ioman_sbv','imports'],['-DIOMANX_OLD_NAME_ADDDELDRV=0','-DIOMANX_OLD_NAME_COMPATIBILITY=0','-DIOMANX_ENABLE_LEGACY_IOMAN_HOOK','-DIOMAN_USE_FILE_STRUCT_TEMP_STACK']),
 ('filexio','iop/fs/filexio',['fileXio_iop','imports'],['-DIOMANX_OLD_NAME_COMPATIBILITY=0','-DIOMANX_OLD_NAME_ADDDELDRV=0']),
 ('libbdm','iop/fs/libbdm',['bd_defrag','bd_cache'],[]),
 ('bdm','iop/fs/bdm',['exports','main','bdm','part_driver','part_driver_mbr','part_driver_gpt','imports'],[]),
 ('fatfs','iop/fs/bdmfs_fatfs',['main','imports','fs_driver','diskio','ff','ffsystem','ffunicode'],[]),
 ('usbmass','iop/usb/usbmass_bd',['main','usb_mass','scsi','imports'],['-DMINI_DRIVER']),
 ('sio2man','iop/sio/sio2man',['exports','sio2man','log','imports'],[])]
for name,folder,objects,defs in configs:
 src=O/'ps2sdk'/folder/'src';base='/src/ps2sdk/'+folder;cwd='/src/build-'+name;lines+=['mkdir -p '+cwd,'cd '+cwd]
 make=(src.parent/'Makefile').read_text();match=re.search(r'IOP_IMPORT_INCS \+=\s*\\\n((?:[^\n]*\\?\n)+?)\n',make)
 deps=re.findall(r'(?:system|fs|sio|usb|cdvd)/[A-Za-z0-9_]+',match[1])if match else[]
 inc=['-I'+base+'/src','-I'+base+'/src/include','-I'+base+'/include','-I/src/ps2sdk/iop/kernel/include','-I/src/ps2sdk/common/include']+['-I/src/ps2sdk/iop/'+d+'/include'for d in deps]
 flags=['-D_IOP','-fno-builtin','-G0','-Os','-Wall','-Werror','-gdwarf-2','-gz',*inc,*defs,'-msoft-float','-mno-explicit-relocs']
 if name=='fatfs':flags+=['-I/src/ps2sdk/common/external_deps/fatfs/source/include']
 for obj in objects:
  extra=[]
  if obj in ('imports','exports'):
   ext='.lst'if obj=='imports'else'.tab';header='irx_imports.h'if obj=='imports'else'irx.h';lines.append(q(['cp',base+'/src/'+obj+ext,obj+ext]));extra=['-include',header,'-fno-toplevel-reorder','-x','c'];source=obj+ext
  else:
   actual=src/(obj+'.c');source=base+'/src/'+obj+'.c'
   if not actual.exists():source='/src/ps2sdk/common/external_deps/fatfs/source/'+obj+'.c'
  lines.append(q(['mipsel-none-elf-gcc',*flags,*extra,'-c',source,'-o',obj+'.o']))
 if name=='libbdm':lines.append(q(['mipsel-none-elf-ar','cru','/src/out/libbdm.a',*[o+'.o'for o in objects]]));continue
 libraries=['/src/out/libbdm.a']if name=='bdm'else[]
 lines.append(q(['mipsel-none-elf-gcc',*flags,'-T/src/ps2sdk/iop/startup/src/linkfile','-o',name+'.elf',*[o+'.o'for o in objects],'-nostdlib','-dc','-r',*libraries,'-lgcc']))
 lines.append(q(['mipsel-none-elf-strip','--strip-unneeded','--remove-section=.pdr','--remove-section=.comment','--remove-section=.mdebug.abi32','--remove-section=.gnu.attributes','-o','stripped.elf',name+'.elf']))
 lines.append(q(['iopfixup','--rb','--irx1',*(['--allow-zero-text']if'exports'not in objects else[]),'-o','/src/out/'+name+'.irx','stripped.elf']))
# Qualified generic USBD EP0 fix, both variants compiled from the newer pinned source.
for name,mini in [('usbd-mini',True),('usbd-full',False)]:
 base='/src/ps2sdk-usbd/iop/usb/usbd';cwd='/src/build-'+name;lines+=['mkdir -p '+cwd,'cd '+cwd]
 flags=['-D_IOP',*(['-DMINI_DRIVER']if mini else[]),'-fno-builtin','-G0','-Os','-I'+SDK+'/iop/include','-I'+SDK+'/common/include','-I'+base+'/src','-I'+base+'/include','-std=gnu99','-msoft-float','-mno-explicit-relocs']
 objects='exports hub mem timer endpoint io_request hub_resets td_queue hcd usbd_sys usbd_api usbd_main report_descriptor_init device_driver device imports'.split()
 for obj in objects:
  ext='.tab'if obj=='exports'else'.lst'if obj=='imports'else'.c';extra=[]
  if obj in ('exports','imports'):extra=['-include','irx.h'if obj=='exports'else'irx_imports.h','-fno-toplevel-reorder','-x','c']
  if obj=='exports':extra+=['-include','usbd.h']
  lines.append(q(['mipsel-none-elf-gcc',*flags,*extra,'-c',base+'/src/'+obj+ext,'-o',obj+'.o']))
 lines.append(q(['mipsel-none-elf-gcc',*flags,'-T'+SDK+'/iop/startup/linkfile','-o','usbd.elf',*[o+'.o'for o in objects],'-nostdlib','-dc','-r','-lgcc']))
 lines.append(q(['mipsel-none-elf-strip','--strip-unneeded','--remove-section=.pdr','--remove-section=.comment','--remove-section=.mdebug.abi32','--remove-section=.gnu.attributes','-o','stripped.elf','usbd.elf']))
 lines.append(q(['iopfixup','--rb','--irx1','--allow-zero-text','-o','/src/out/'+name+'.irx','stripped.elf']))
# Project HID and MMCEMAN use the qualified installed SDK's ordinary IRX ABI.
for name,base,objects,defs in [('hid','/src/hid','driver hid_report bindings scratch imports'.split(),[]),('mmceman','/src/mmceman/mmceman','ioplib sio2man_hook mmce_sio2 mmce_cmds mmce_fs imports mmceman megamix_diag'.split(),['-DIOMANX_OLD_NAME_COMPATIBILITY=0','-DIOMANX_OLD_NAME_ADDDELDRV=0'])]:
 cwd='/src/build-'+name;lines+=['mkdir -p '+cwd,'cd '+cwd];sourcebase=base if name=='hid'else base+'/src'
 flags=['-D_IOP','-fno-builtin','-G0','-Os','-I'+SDK+'/iop/include','-I'+SDK+'/common/include','-I'+sourcebase,'-I'+base+'/include',*defs,*(['-include',base+'/include/megamix_diag.h']if name=='mmceman'else['-std=gnu99']),'-Wall','-gdwarf-2','-gz','-msoft-float','-mno-explicit-relocs']
 for obj in objects:
  ext='.lst'if obj=='imports'else'.c';extra=['-include','irx_imports.h','-fno-toplevel-reorder','-x','c']if obj=='imports'else[]
  lines.append(q(['mipsel-none-elf-gcc',*flags,*extra,'-c',sourcebase+'/'+obj+ext,'-o',obj+'.o']))
 lines.append(q(['mipsel-none-elf-gcc',*flags,'-T'+SDK+'/iop/startup/linkfile','-o',name+'.elf',*[o+'.o'for o in objects],'-nostdlib','-dc','-r','-lgcc']))
 lines.append(q(['mipsel-none-elf-strip','--strip-unneeded','--remove-section=.pdr','--remove-section=.comment','--remove-section=.mdebug.abi32','--remove-section=.gnu.attributes','-o','stripped.elf',name+'.elf']))
 lines.append(q(['iopfixup','--rb','--irx1','--allow-zero-text','-o','/src/out/'+name+'.irx','stripped.elf']))
(O/'build.sh').write_text('\n'.join(lines)+'\n');print(O/'build.sh')
