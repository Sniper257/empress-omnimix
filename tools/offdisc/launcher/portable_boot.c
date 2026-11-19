/* R144: self-contained bootstrap. Executable origin never chooses game media.
 * Startup mounts both providers for discovery. Handoff loads only providers
 * needed by game data and persisted config. One USB host, one fileXio service.
 * Every handle closes before reset; no SDK callback survives ExecPS2. */
#ifndef PORTABLE_BOOT_TEST
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <iopcontrol.h>
#include <iopcontrol_special.h>
#include <iopheap.h>
#define NEWLIB_PORT_AWARE
#include <fileio.h>
#include <fileXio_rpc.h>
#include <delaythread.h>
#include <sbv_patches.h>
#endif
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "deployment.h"
#include "portable_boot.h"
#include "usb_memory.h"
char dl_game[DL_ROOT],dl_config[DL_ROOT],dl_boot[DL_ROOT],dl_cfg[80];
unsigned dl_persistent,dl_usb_active,dl_mmce_active;
typedef struct{const char *name;void *data;unsigned bytes;} module;
extern module usb_modules[],mmce_modules[];
extern unsigned usb_module_count,mmce_module_count,usb_ioprp_bytes;
extern unsigned char usb_ioprp[];
static char status[256];static void (*reporter)(const char *);
static char gameid[11]="PIZD-00000";
const char *portable_boot_status(void){return status;}
void portable_boot_set_reporter(void (*fn)(const char *)){reporter=fn;}
void portable_boot_note(const char *fmt,...){(void)fmt;} /* external opt-in logs stay in launcher */
int portable_boot_flush_log(void){return 0;}
static void stage(const char *s){snprintf(status,sizeof status,"%s",s);if(reporter)reporter(status);}
int portable_game_id(const char *id){
 if(!id||strlen(id)>10)return -1;strcpy(gameid,id);return 0;
}
static int load(module *m){
 int result=-1;static const char args[]=USB_MEMORY_HOST_ARGS;
 int usb=!strcmp(m->name,"usbd_mini");
 snprintf(status,sizeof status,"starting %s...",m->name);if(reporter)reporter(status);
 int id=SifExecModuleBuffer(m->data,m->bytes,usb?sizeof args:0,usb?args:0,&result);
 if(id<0||(result!=0&&result!=2)){snprintf(status,sizeof status,"%s failed: %d / %d",m->name,id,result);return -1;}return 0;
}
static int gamecard(void){
 if(!gameid[0]||!dl_mmce_active)return 0;
 const char *dev=!strncmp(dl_game,"mmce1:",6)||!strncmp(dl_config,"mmce1:",6)?"mmce1:":"mmce0:";
 int ping=fileXioDevctl(dev,1,0,0,0,0);if(ping<0){stage("MMCE adapter is not responding");return -1;}
 if(fileXioDevctl(dev,8,gameid,11,0,0)<0){stage("Could not select MEGAMIX memory card");return -1;}
 unsigned stable=0;char seen[256];
 for(unsigned i=0;i<150;i++){
  DelayThread(100000);int s=fileXioDevctl(dev,2,0,0,0,0);memset(seen,0,sizeof seen);
  int r=s>=0&&!(s&0xff01)?fileXioDevctl(dev,7,0,0,seen,sizeof seen):-1;seen[255]=0;
  if(r>=0&&!strcmp(seen,gameid)){if(++stable==2)return 0;}else stable=0;
 }stage("Memory-card Game ID switch did not complete");return -1;
}
/* Existing retail heap RPC only. Necessary capacity check, not a reservation
 * or a promise about future fragmentation. All calls finish before Exec. */
static SifRpcClientData_t capacity_client __attribute__((aligned(64)));
static unsigned capacity_reply[16] __attribute__((aligned(64)));
static int capacity_check(void){
 memset(&capacity_client,0,sizeof capacity_client);
 if(SifBindRpc(&capacity_client,0x80000003,0)<0||!capacity_client.server){stage("Could not query game IOP capacity");return -1;}
 unsigned values[2];
 for(unsigned i=0;i<2;i++){
  capacity_reply[0]=~0u;
  if(SifCallRpc(&capacity_client,6+i,0,0,0,capacity_reply,4,0,0)<0){stage("IOP capacity query failed");return -1;}
  values[i]=capacity_reply[0];
 }
 if(values[0]>0x200000||values[1]>0x200000||values[0]>values[1]){stage("IOP capacity reply is invalid");return -1;}
 if(values[1]<0xe8000||values[0]<0xbe000){
  snprintf(status,sizeof status,"Not enough IOP memory for this storage combination\nFree %u KiB; largest %u KiB\nMove the settings to the game device",values[1]/1024,values[0]/1024);return -1;
 }return 0;
}
/* USB module start only registers the driver; device enumeration continues
 * asynchronously. R143 verified this read before any native module preload.
 * Always close the probe, including short/error reads, before retry or LOADFILE. */
static int wait_game_storage(void){
 stage("waiting for game storage after IOP restore...");
 for(unsigned attempt=0;attempt<150;attempt++){
  FILE *f=fopen(game_path("EMPRESS/base.txt"),"rb");
  if(f){
   char b[2];size_t n=fread(b,1,2,f);int closed=fclose(f);
   if(n==2&&!closed){
    if(b[0]=='E'&&b[1]==' ')return attempt+1;
    stage("Selected game storage has an invalid base index");return -1;
   }
  }
  if(attempt<149)DelayThread(100000);
 }
 snprintf(status,sizeof status,"Game storage unavailable after IOP restore\n%s\nNo native modules were started",dl_game);return -1;
}
int portable_boot_prepare(int game){
 unsigned needusb=!game||dl_device(dl_game)==1||dl_device(dl_config)==1;
 unsigned needmmce=!game||dl_device(dl_game)==2||dl_device(dl_config)==2;
 fileXioExit();fioExit();SifLoadFileExit();SifExitIopHeap();dl_usb_active=dl_mmce_active=0;
 stage("restoring storage services...");
 if(SifIopRebootBuffer(usb_ioprp,usb_ioprp_bytes)!=1){stage("Could not reset IOP");return -1;}
 while(!SifIopSync()){}SifInitRpc(0);
 if(SifLoadFileInit()<0||SifInitIopHeap()<0||sbv_patch_disable_prefix_check()){stage("Could not bind IOP services");return -1;}
 /* Common binaries are the pinned retail-compatible versions used by both
  * accepted benches. Startup discovery needs SDK SIO2; final USB-only
  * handoff restores native SIO2 instead. Every native load is preflighted. */
 for(unsigned i=0;i<usb_module_count;i++){
  module *m=usb_modules+i;if(strcmp(m->name,"iomanX")&&strcmp(m->name,"fileXio"))continue;
  if(load(m))return -1;
 }
 if(needmmce)for(unsigned i=0;i<mmce_module_count;i++)if(!strcmp(mmce_modules[i].name,"sio2man"))if(load(mmce_modules+i))return -1;
 if(needusb){
  for(unsigned i=0;i<usb_module_count;i++)if(strcmp(usb_modules[i].name,"iomanX")&&strcmp(usb_modules[i].name,"fileXio"))if(load(usb_modules+i))return -1;
  dl_usb_active=1;
 }
 if(needmmce){
  for(unsigned i=0;i<mmce_module_count;i++)if(!strcmp(mmce_modules[i].name,"mmceman"))if(load(mmce_modules+i))return -1;
  dl_mmce_active=1;
 }
 if(fileXioInit()<0){stage("Could not bind storage client");return -1;}
 if(game&&fileXioSetRWBufferSize(2048)<0){stage("Could not release launcher staging memory");return -1;}
 if(!game){
  /* Source may be a real memory card. These are launcher-only services and
   * disappear on the final reset; native copies are used after handoff. */
  if(dl_device(dl_config)==3){
   if(SifLoadModule("rom0:MCMAN",0,0)<0||SifLoadModule("rom0:MCSERV",0,0)<0){stage("Could not open boot memory card");return -1;}
  }return 0;
 }
 if(gamecard())return -1;
 /* R143-qualified order: preflight exact native words, then load all original
  * required modules before Exec. Never proceed after an allocation/load error.
  * SDK SIO2 is retained only when MMCE is needed. Pure USB keeps R143
  * original native SIO2, including physical-pad behavior. */
 static const struct{unsigned site;const char *name;} native[]={
  {0x12f834,"LIBSD.IRX"},{0x12f854,"SDDRVIOP.IRX"},{0x12f874,"SIO2MAN.IRX"},
  {0x12f894,"MCMAN.IRX"},{0x12f8b4,"MCSERV.IRX"},{0x12f8d4,"PADMAN.IRX"},{0x12f8f4,"CDVDSTM.IRX"}};
 for(unsigned i=0;i<7;i++)if(*(volatile unsigned *)native[i].site!=0x24020001){stage("Portable runtime mismatch; game not started");return -1;}
 if(*(volatile unsigned*)0x14d300!=0x102d){stage("Portable streaming patch mismatch");return -1;}
 int ready=wait_game_storage();if(ready<0)return -1;
 for(unsigned i=0;i<7;i++){
  if(i==2&&dl_mmce_active)continue;
  char path[256];snprintf(path,sizeof path,"%sEMPRESS/MODULES/%s",dl_game,native[i].name);
  int result=-1,id=SifLoadStartModule(path,0,0,&result);
  if(id<0||(result!=0&&result!=2)){snprintf(status,sizeof status,"%s failed: %d / %d\n%.112s\nStorage verified after %d attempts",native[i].name,id,result,path,ready);return -1;}
 }
 FILE *f=fopen(game_path("EMPRESS/base.txt"),"rb");char b[2];int ok=f&&fread(b,1,2,f)==2&&b[0]=='E'&&b[1]==' ';if(f&&fclose(f))ok=0;
 if(!ok){stage("Game location is unavailable after IOP restore");return -1;}
 /* Settings can be on a second USB device even when game data is on MMCE.
  * Native MCMAN/MCSERV are now resident, so real-card settings work here too. */
 for(unsigned attempt=0;attempt<150;attempt++){
  f=fopen(dl_cfg,"rb");if(f)break;if(attempt<149)DelayThread(100000);
 }
 if(!f){stage("Settings location unavailable after restore");return -1;}
 int bad=fseek(f,0,SEEK_END);long length=bad?-1:ftell(f);if(fclose(f))bad=1;
 if(bad||length<=0||length>=8192){stage("Could not verify settings after restore");return -1;}
 return capacity_check();
}
