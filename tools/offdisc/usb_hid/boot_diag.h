/* Optional startup-only observer. No EE progress bridge, hooks or waits.
 * Explicit external debug enables it via RPC after the final MMCE reset.
 * Stops on first game input arm, 10s without progress, 60s total, or any error.
 * Normal builds omit this code, its storage imports and static scratch. */
#ifdef MU_BOOT_DIAG_BUILD
#ifndef MU_DIAG_TEST
#include <sysmem.h>
#include <iomanX.h>
#include <io_common.h>
#endif
#define MD_MAX_MODULES 64
static struct { char name[24]; unsigned id,version,base,text,data,bss; } md_modules[MD_MAX_MODULES];
static char md_buffer[8192];
static char md_path[]="mmce0:/MEGAMIX/logs/usb-boot-0000.txt";
static unsigned md_used,md_enabled,md_serial,md_records,md_ticks,md_idle,md_hash;
static unsigned md_min_free,md_min_max;
static void md_text(const char *s){while(*s && md_used<sizeof md_buffer-1)md_buffer[md_used++]=*s++;}
static void md_hex(unsigned x){for(int shift=28;shift>=0;shift-=4){char c="0123456789abcdef"[(x>>shift)&15];if(md_used<sizeof md_buffer-1)md_buffer[md_used++]=c;}}
static void md_field(const char *s,unsigned x){md_text(s);md_hex(x);md_text("\n");}
static unsigned md_inventory(unsigned *hash,unsigned *truncated){
 unsigned count=0,h=2166136261u;int state;CpuSuspendIntr(&state);
 ModuleInfo_t *m=GetLoadcoreInternalData()->image_info;
 while(m && count<MD_MAX_MODULES){
  md_modules[count].id=m->id;md_modules[count].version=m->version;
  md_modules[count].base=m->text_start;md_modules[count].text=m->text_size;
  md_modules[count].data=m->data_size;md_modules[count].bss=m->bss_size;
  unsigned j=0;for(;j<23 && m->name && m->name[j];j++)md_modules[count].name[j]=m->name[j];
  md_modules[count].name[j]=0;h=(h^m->id)*16777619u;h=(h^m->text_start)*16777619u;
  m=m->next;count++;
 }
 *truncated=m!=0;CpuResumeIntr(state);*hash=h;return count;
}
static int md_snapshot(const char *reason,unsigned hash,unsigned count,unsigned truncated){
 unsigned free=QueryTotalFreeMemSize(),max=QueryMaxFreeMemSize();
 if(free<md_min_free)md_min_free=free;
 if(max<md_min_max)md_min_max=max;
 md_used=0;md_text("R32 USB STARTUP SNAPSHOT\n");md_text(reason);md_text("\n");
 md_field("record ",md_records);md_field("tick_100ms ",md_ticks);
 md_field("free_total ",free);md_field("max_free ",max);
 md_field("min_free_total ",md_min_free);md_field("min_max_free ",md_min_max);
 md_field("game_mode ",game_mode);md_field("destination ",destination);
 md_field("expected_gate ",game_token);md_field("received_gate ",game_gate[0]);
 md_field("armed ",game_gate[1]);md_field("publisher_sequence ",sequence);
#ifndef MU_DIAG_TEST
 volatile const unsigned *ohci=(void *)0xbf801600;
 md_field("ohci_control ",ohci[1]);md_field("ohci_frame ",ohci[15]);
 md_field("usb_port0 ",ohci[21]);md_field("usb_port1 ",ohci[22]);
#endif
 for(unsigned i=0;i<MU_DEVICES;i++){
  md_field("hid_slot ",i);md_field("connected ",devices[i].info.connected);
  md_field("reports ",devices[i].info.reports);md_field("errors ",devices[i].info.errors);
 }
 md_field("module_count ",count);md_field("module_hash ",hash);md_field("truncated ",truncated);
 md_text("id version base text data bss name\n");
 for(unsigned i=0;i<count;i++){
  md_hex(md_modules[i].id);md_text(" ");md_hex(md_modules[i].version);md_text(" ");
  md_hex(md_modules[i].base);md_text(" ");md_hex(md_modules[i].text);md_text(" ");
  md_hex(md_modules[i].data);md_text(" ");md_hex(md_modules[i].bss);md_text(" ");
  md_text(md_modules[i].name);md_text("\n");
 }
 md_text("END SNAPSHOT\n");
 int fd=iomanX_open(md_path,FIO_O_WRONLY|FIO_O_APPEND,0);if(fd<0)return -1;
 /* Some card firmware accepts O_APPEND but starts writes at zero. */
 if(iomanX_lseek(fd,0,FIO_SEEK_END)<0){iomanX_close(fd);return -1;}
 int failed=iomanX_write(fd,md_buffer,md_used)!=(int)md_used;
 if(iomanX_close(fd)<0)failed=1;
 if(failed)return -1;
 md_records++;return 0;
}
static int md_start(void){
 if(md_enabled)return 0;
 if(md_serial)return -1; /* One finite capture per IOP lifetime. */
 for(unsigned n=1;n<=9999;n++){
  unsigned k=n;for(int j=3;j>=0;j--){md_path[sizeof("mmce0:/MEGAMIX/logs/usb-boot-")-1+j]='0'+k%10;k/=10;}
  int fd=iomanX_open(md_path,FIO_O_RDONLY,0);
  if(fd>=0){if(iomanX_close(fd)<0)return -1;continue;}
  fd=iomanX_open(md_path,FIO_O_WRONLY|FIO_O_CREAT|FIO_O_EXCL,0666);
  if(fd<0)return -1;
  if(iomanX_close(fd)<0)return -1;
  md_serial=n;break;
 }
 if(!md_serial)return -1;
 unsigned hash,truncated,count=md_inventory(&hash,&truncated);
 md_min_free=md_min_max=~0u;md_hash=hash;
 if(md_snapshot("observer-start",hash,count,truncated))return -1;
 md_enabled=1;return 0;
}
static void md_tick(void){
 if(!md_enabled)return;
 md_ticks++;md_idle++;
 unsigned free=QueryTotalFreeMemSize(),max=QueryMaxFreeMemSize();
 if(free<md_min_free)md_min_free=free;
 if(max<md_min_max)md_min_max=max;
 unsigned hash,truncated,count=md_inventory(&hash,&truncated);
 int armed=game_mode && game_gate[0]==game_token && game_gate[1]==1;
 const char *reason=armed?"native-input-armed":md_ticks>=600?"deadline":md_idle>=100?"idle":hash!=md_hash?"module-change":0;
 if(!reason)return;
 int stop=armed || md_ticks>=600 || md_idle>=100 || md_records>=31;
 if(md_snapshot(reason,hash,count,truncated))stop=1;
 md_hash=hash;md_idle=0;if(stop)md_enabled=0;
}
#endif
