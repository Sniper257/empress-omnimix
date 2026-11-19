/* Launcher half of the diagnostic worker. Called only AFTER final USB reset.
 * The IOP block survives ExecPS2 and is published into the paired runtime. */
#ifndef USB_JOURNAL_TEST
#include <kernel.h>
#include <sifrpc.h>
#include <iopheap.h>
#include <loadfile.h>
#endif
#include <stdio.h>
#include <string.h>
#include "usb_boot.h"
#include "usb_journal_shared.h"
extern unsigned char usb_journal_irx[];
extern unsigned usb_journal_irx_bytes;
static unsigned config_at,build_at,publish_at,diag_at;
void usb_journal_configure(unsigned cfg,unsigned build,unsigned publish,unsigned diag){config_at=cfg;build_at=build;publish_at=publish;diag_at=diag;}
static void snapshot(unsigned addr,unsigned *dst)
{
#ifdef USB_JOURNAL_TEST
 extern void journal_snapshot(unsigned,unsigned *);journal_snapshot(addr,dst);
#else
 unsigned old;__asm__ volatile("mfc0 %0,$12\ndi\nsync.p":"=r"(old)::"memory");ee_kmode_enter();
 volatile unsigned *p=(volatile unsigned *)(0xbc000000u+addr);for(int i=0;i<BJ_WORDS;i++)dst[i]=p[i];
 __asm__ volatile("mtc0 %0,$12\nsync.p"::"r"(old):"memory");
#endif
}
int usb_journal_start(void)
{
 if(!config_at || !build_at || !publish_at || !diag_at){usb_boot_note("J00 no paired startup journal runtime");return -1;}
 unsigned *cfg=(unsigned *)config_at;
 if(cfg[0]!=BJ_ABI || cfg[1]){usb_boot_note("J00 invalid publisher config %08x/%08x",cfg[0],cfg[1]);return -1;}
 static unsigned initial[BJ_WORDS] __attribute__((aligned(64)));memset(initial,0,sizeof initial);
 initial[0]=BJ_MAGIC;initial[1]=BJ_ABI;memcpy(initial+BJ_BUILD,(void *)build_at,16);
 void *block=SifAllocIopHeap(sizeof initial);unsigned a=(unsigned)block;
 if(a<0x1000 || a>0x200000-sizeof initial || (a&15)){usb_boot_note("J01 IOP allocation failed");return -1;}
 SifDmaTransfer_t d={initial,block,sizeof initial,0};sceSifWriteBackDCache(initial,sizeof initial);
 unsigned id=sceSifSetDma(&d,1);if(!id){SifFreeIopHeap(block);return -1;}while(sceSifDmaStat(id)>=0){}
 char args[9];snprintf(args,sizeof args,"%08x",a);int modres=-1;
 usb_boot_note("J02 startup journal IOP block=%08x, module=%u bytes",a,usb_journal_irx_bytes);
 int module=SifExecModuleBuffer(usb_journal_irx,usb_journal_irx_bytes,9,args,&modres);
 unsigned state[BJ_WORDS];snapshot(a,state);
 usb_boot_note("J03 worker id=%d start=%d ready=%u error=%u",module,modres,state[BJ_READY],state[BJ_ERROR]);
 if(module<0 || modres!=0 || state[0]!=BJ_MAGIC || state[1]!=BJ_ABI || state[BJ_READY]!=1 || state[BJ_ERROR] || !state[BJ_SERIAL])return -1;
 cfg[1]=a;usb_boot_note("J04 closed header logs/boot-%04u.bin; EE journal enabled",state[BJ_SERIAL]);
 if(usb_boot_flush_log()){cfg[1]=0;usb_boot_note("J04 preflight log flush failed");return -1;}
 /* Exercise the ACTUAL assembled runtime publisher before ExecPS2. A header
  * alone only establishes the IOP writer, not privileged EE access/opcodes. */
#ifdef USB_JOURNAL_TEST
 extern void journal_publish(unsigned,unsigned,unsigned,unsigned);
 journal_publish(publish_at,0xffff0001u,0,a);
#else
 FlushCache(0);FlushCache(2);
 ((void (*)(unsigned,unsigned,unsigned))publish_at)(0xffff0001u,0,a);
#endif
 snapshot(a,state);
 volatile unsigned *diag=(volatile unsigned *)diag_at;
 usb_boot_note("J05 EE cfg=%08x/%08x reason=%u read[%u]=%08x sr=%08x",cfg[0],cfg[1],diag[0],diag[1],diag[2],diag[3]);
 usb_boot_note("J06 IOP magic=%08x ready=%u prod=%u ack=%u err=%u stop=%u",state[0],state[BJ_READY],state[BJ_PRODUCED],state[BJ_ACK],state[BJ_ERROR],state[BJ_STOP]);
 if(state[BJ_ERROR] || state[BJ_READY]!=1 || state[BJ_PRODUCED]!=1 || state[BJ_ACK]!=1 ||
    state[BJ_EVENT]!=0xffff0001u || state[BJ_EVENT+1] || state[BJ_EVENT+2]!=a){
  cfg[1]=0;usb_boot_note("J07 EE publisher self-test failed");return -1;
 }
 usb_boot_note("J07 actual EE publisher self-test closed and acknowledged");return 0;
}
