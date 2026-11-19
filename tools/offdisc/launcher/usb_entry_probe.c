/* Private USB startup isolation. No IOP calls, heap or files after ExecPS2.
 * SDK text screen is the fixed-NTSC/no-ROM R121b implementation. The launcher
 * and its private data remain above a00000 ONLY until native arena creation.
 * Restore every hook and tail-call the arena BEFORE it may own that memory.
 * Never put a post-arena callback into this launcher-owned diagnostic. */
#ifndef USB_ENTRY_PROBE_TEST
#include <kernel.h>
#include <debug.h>
#endif
#include "usb_boot.h"
#ifndef USB_ENTRY_PROBE
#error Build only the explicit USB entry diagnostic
#endif
extern void usb_entry_probe(void);
extern void usb_probe_kernel(void),usb_probe_semas(void),usb_probe_kernel_patch(void);
extern void usb_probe_timer_patch(void),usb_probe_timer(void),usb_probe_alarm(void);
extern void usb_probe_threads(void),usb_probe_osd(void),usb_probe_exec_patch(void);
extern void usb_probe_constructors(void),usb_probe_arena(void);
static const struct {unsigned site,target;void (*hook)(void);} points[]={
 {0x1001e8,0x119158,usb_probe_kernel},
 {0x119160,0x118f08,usb_probe_semas},
 {0x119168,0x119048,usb_probe_kernel_patch},
 {0x119170,0x119638,usb_probe_timer_patch},
 {0x119178,0x1197b0,usb_probe_timer},
 {0x119180,0x1199f0,usb_probe_alarm},
 {0x119188,0x10fa38,usb_probe_threads},
 {0x119190,0x119280,usb_probe_osd},
 {0x119198,0x118778,usb_probe_exec_patch},
 {0x151888,0x14c8a0,usb_probe_constructors},
 {0x151894,0x1530c0,usb_probe_arena}
};
#ifndef USB_ENTRY_PROBE_TEST
static unsigned count_now(void){unsigned r;__asm__ volatile("mfc0 %0,$9":"=r"(r));return r;}
#else
extern unsigned count_now(void);
#endif
static void pause_count(unsigned ticks){unsigned start=count_now();while((unsigned)(count_now()-start)<ticks)__asm__ volatile("nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop":::"memory");}
static void line_at(int row,const char *s){
 for(int i=0;i<76;i++)scr_putchar(16+i*7,16+row*12,0xffffff,' ');
 for(int i=0;s[i]&&i<76;i++)scr_putchar(16+i*7,16+row*12,0xffffff,(unsigned char)s[i]);
}
static const char *const names[]={
 "00 EXEC ENTRY REACHED - before native CRT",
 "01 native kernel: ENTER", "02 native kernel: RETURNED",
 "03 libc semaphores: ENTER", "04 libc semaphores: RETURNED",
 "05 libc kernel patch: ENTER", "06 libc kernel patch: RETURNED",
 "07 timer patch: ENTER", "08 timer patch: RETURNED",
 "09 timer setup: ENTER", "10 timer setup: RETURNED",
 "11 alarm setup: ENTER", "12 alarm setup: RETURNED",
 "13 kernel threads: ENTER", "14 kernel threads: RETURNED",
 "15 OSD patch: ENTER", "16 OSD patch: RETURNED",
 "17 Exec patch: ENTER", "18 Exec patch: RETURNED",
 "19 constructors: ENTER", "20 constructors: RETURNED",
 "21 native arena next - removing all startup probes"
};
void usb_entry_probe_begin(void){
 init_scr();scr_setCursor(0);
 line_at(0,"MEGAMIX USB R135 - early native startup");
 line_at(2,"Diagnostic NTSC60; game video setting is preserved.");
 line_at(4,names[0]);
 line_at(6,"This screen proves the post-Exec entry was reached.");
 line_at(7,"Next: unmodified native CRT / kernel / constructors.");
 /* Long enough for capture/scaler to reacquire the kernel's NTSC reset. */
 pause_count(0x60000000u);
}
void usb_entry_probe_stage(unsigned stage){
 if(stage>=sizeof names/sizeof names[0])return;
 line_at(4,names[stage]);pause_count(0x04000000u);
 if(stage==21){
  for(unsigned i=0;i<sizeof points/sizeof points[0];i++)
   *(volatile unsigned *)points[i].site=0x0c000000u|(points[i].target>>2);
  FlushCache(0);FlushCache(2);
  /* No SDK or diagnostic callback after returning to the arena tail stub. */
 }
}
int usb_entry_probe_install(unsigned *entry){
 if(*entry!=0x100008)return -1;
 for(unsigned i=0;i<sizeof points/sizeof points[0];i++)
  if(*(volatile unsigned *)points[i].site!=(0x0c000000u|(points[i].target>>2)))return -1;
 for(unsigned i=0;i<sizeof points/sizeof points[0];i++)
  *(volatile unsigned *)points[i].site=0x0c000000u|((unsigned)points[i].hook>>2);
 *entry=(unsigned)usb_entry_probe;
 usb_boot_note("E01 paired post-Exec entry %08x; eleven pre-arena native sites",*entry);
 return usb_boot_flush_log();
}
