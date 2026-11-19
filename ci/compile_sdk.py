"""Fresh IOP/launcher/stub builds in the pinned, offline SDK image."""
from pathlib import Path
import json,os,re,shutil,subprocess,sys,zipfile
from dependencies import archive,unpack
from patch_mmce import patch as patch_mmce
HERE=Path(__file__).resolve().parent

def run_sdk(folder,work,script):
 image=json.loads((HERE/'dependencies.json').read_text())['sdk_image']
 with(folder/(script.replace('/','-')+'.log')).open('w')as f:
  subprocess.run(['docker','run','--rm','--pull=never','--network=none','--user',str(os.getuid())+':'+str(os.getgid()),'-v',str(folder)+':/src','-w','/src/'+work,'--entrypoint','sh',image,script],stdout=f,stderr=subprocess.STDOUT,check=True)

def array(name,data):return 'unsigned char '+name+'[] __attribute__((aligned(64)))={\n'+''.join(','.join(str(v)for v in data[i:i+24])+',\n'for i in range(0,len(data),24))+'};\n'

def build(root,out,deps,offline=False):
 out.mkdir(parents=True,exist_ok=False)
 for key in('ps2sdk','ps2sdk-usbd','fatfs','mmceman'):unpack(archive(deps,key,offline),out/key)
 shutil.move(out/'fatfs',out/'ps2sdk/common/external_deps/fatfs')
 sys.path.insert(0,str(root/'tools/offdisc'));import retail_ioman;retail_ioman.patch(out/'ps2sdk')
 fs=out/'ps2sdk/iop/fs/bdmfs_fatfs/src/fs_driver.c';s=fs.read_text();a=s.index('    // translate mode\n');b=s.index('    ret = f_open(',a);s=s[:a]+'    f_mode = megamix_fatfs_mode(flags);\n\n'+s[b:];a=s.index('static int fs_open(');s=s[:a]+'#include "megamix_usb_fatfs_mode.h"\n'+s[a:];fs.write_text(s);shutil.copy2(root/'tools/offdisc/usb_fatfs_mode.h',fs.parent/'megamix_usb_fatfs_mode.h')
 patch=(root/'tools/offdisc/usb_enum_diag/ep0_config.c.inc').read_text().replace('\t\tud_note(11, dev, readLen, capacity, 0);','\t\tdbg_printf("USBD: configuration descriptors exceed static storage\\n");');assert'ud_note'not in patch
 p=out/'ps2sdk-usbd/iop/usb/usbd/src/device.c';s=p.read_text();a=s.index('static void fetchConfigDescriptors(UsbdIoRequest_t *req)\n{');b=s.index('\nstatic void requestDeviceDescrptorCB',a);p.write_text(s[:a]+patch+'\n'+s[b:])
 patch_mmce(out/'mmceman/mmceman')
 shutil.copytree(root/'tools/offdisc/usb_hid',out/'hid',ignore=shutil.ignore_patterns('*.o','*.irx','*.elf','obj','irx','test*','__pycache__'))
 subprocess.run([sys.executable,str(HERE/'generate_iop_build.py'),str(out)],check=True);run_sdk(out,'','build.sh')
 print('Built IOP modules from source',flush=True)
 ee=out/'ee';ee.mkdir()
 for p in(root/'tools/offdisc/launcher').iterdir():
  if p.suffix in('.c','.h')or p.name=='linkfile':shutil.copy2(p,ee/p.name)
 for p in(root/'tools/offdisc').glob('*.h'):shutil.copy2(p,out/p.name)
 for n in('songdb.h','input_probe.h','save_probe.h','menu_settings.h','customize_settings.h','profile_location.h','profile_format.h','profile_schema.h','profile_wire.h','profile_format.c','profile_schema.c'):shutil.copy2(root/'tools/offdisc'/n,ee/n)
 for n in('bindings.h','hid_report.h'):shutil.copy2(root/'tools/offdisc/usb_hid'/n,ee/n)
 shutil.copytree(root/'tools/offdisc/usb_hid',ee/'usb_hid',ignore=shutil.ignore_patterns('__pycache__'))
 shutil.copytree(root/'tools/offdisc/select_profiler',ee/'select_profiler',ignore=shutil.ignore_patterns('__pycache__'))
 for n,src in[('hid_bindings.c','bindings.c'),('hid_report.c','hid_report.c')]:shutil.copy2(root/'tools/offdisc/usb_hid'/src,ee/n)
 sys.path.insert(0,str(root/'tools/release'));from text_launcher import font_header
 font=json.loads((root/'tools/release/text-fonts/open-launcher-font.json').read_text());gfx=font_header(font)+'\n#define LOGO_W 440\n#define LOGO_H 215\nstatic const u32 logo_px[440*215] __attribute__((aligned(128),section(".rodata.iso_logo"),used)) = {0};\n';(ee/'gfx.h').write_text(gfx)
 for name,keys in [('usb_modules',dict(zip(['usb_mod_'+str(i)for i in range(6)],['iomanx','filexio','bdm','fatfs','usbd-mini','usbmass']))),('mmce_modules',dict(zip(['mmce_module_'+str(i)for i in range(4)],['iomanx','sio2man','filexio','mmceman']))),('usb_input_modules',dict(mui_usbd='usbd-full',mui_hid='hid'))]:
  s=(HERE/'module-templates'/(name+'.c.in')).read_text()
  for symbol,key in keys.items():s=s.replace('@ARRAY('+symbol+')@',array(symbol,(out/'out'/(key+'.irx')).read_bytes()))
  if '@ARRAY('in s:raise ValueError('Missing IOP embedding')
  (ee/(name+'.c')).write_text(s)
 shutil.copy2(HERE/'build_ee.sh',ee/'build.sh');run_sdk(out,'ee','build.sh')
 with zipfile.ZipFile(root/'tools/offdisc/licenses/ps2-packer-source.zip')as z:z.extractall(out/'packer-source')
 shutil.copy2(HERE/'build_stub.sh',out/'build-stub.sh');run_sdk(out,'','build-stub.sh')
 print('Built empty launcher templates and unpacker from source',flush=True)
