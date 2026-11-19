/* EE publisher: one IOP pending slot, short protected accesses, exact Status
 * restoration. Waiting occurs with the caller's interrupts/mode restored.
 * No EE RPC/file I/O/renderer. Only the launcher preflight waits for close;
 * every game-side marker is RAM-only and never waits for the IOP worker.
 * Deadline or journal failure DISABLES logging, never bypasses native work. */
#include "usb_journal/shared.h"
u32 boot_journal_config[2]={BJ_ABI,0};
/* EE-only failure reason, last IOP word index/value and entry Status. Kept
 * separate from the shared block so failed mapped reads remain diagnosable. */
volatile u32 boot_journal_diag[4];
static volatile u32 bj_busy,bj_dead;
#ifndef BOOT_JOURNAL_TEST
static u32 bj_enter(void)
{
 /* R5900 DI is COP0.CO/0x39. MIPS32 MFMC0 DI (0x41606000) is invalid
  * on the EE; host mocks cannot verify this privileged instruction. */
 u32 old;__asm__ volatile("mfc0 %0,$12\n.word 0x42000039\n.word 0x0000040f":"=r"(old)::"memory");
 u32 k=old&~0x10018u;__asm__ volatile("mtc0 %0,$12\n.word 0x0000040f"::"r"(k):"memory");boot_journal_diag[3]=old;return old;
}
static void bj_leave(u32 old){__asm__ volatile("mtc0 %0,$12\n.word 0x0000040f"::"r"(old):"memory");}
static volatile u32 *bj_ptr(void){return (volatile u32 *)(0xbc000000u+boot_journal_config[1]);}
static u32 bj_clock(void){u32 r;__asm__ volatile("mfc0 %0,$9":"=r"(r));return r;}
#else
extern u32 bj_enter(void);extern void bj_leave(u32);extern volatile u32 *bj_ptr(void);extern u32 bj_clock(void);
#endif
static u32 bj_read(u32 i){u32 s=bj_enter(),r=bj_ptr()[i];bj_leave(s);boot_journal_diag[1]=i;boot_journal_diag[2]=r;return r;}
static void bj_set(u32 i,u32 v){u32 s=bj_enter();bj_ptr()[i]=v;bj_leave(s);}
static int bj_is_preflight(u32 site)
{
 return site==0xffff0001u;
}
static void bj_early_mark(u32 site,u32 after,u32 value)
{
 /* Retain only the last startup boundary in shared IOP RAM. R19 and R21
  * both stopped recording at the first post-Exec closed event. Never make
  * game startup depend on filesystem progress or an IOP acknowledgment. */
 u32 status=bj_enter();volatile u32 *p=bj_ptr();u32 seq=p[BJ_EARLY_SEQ]&~1u;
 p[BJ_EARLY_SEQ]=seq+1;
 p[BJ_EARLY]=site;p[BJ_EARLY+1]=after;p[BJ_EARLY+2]=value;
 p[BJ_EARLY+3]=bj_clock();p[BJ_EARLY+4]=boot_trace_state[3];
 p[BJ_EARLY+5]=status;p[BJ_EARLY+6]=0;p[BJ_EARLY+7]=0;
#ifndef BOOT_JOURNAL_TEST
 __asm__ volatile("sync":::"memory");
#else
 __asm__ volatile("":::"memory");
#endif
 p[BJ_EARLY_SEQ]=seq+2;bj_leave(status);boot_journal_diag[0]=15;
}
static void boot_trace_mark(u32 site,u32 after,u32 value)
{
 boot_trace_state[0]=site;boot_trace_state[1]=after;boot_trace_state[2]=value;
 if(bj_dead)return; /* Retain the first failure across later native markers. */
 boot_journal_diag[0]=1;
 u32 a=boot_journal_config[1];if(boot_journal_config[0]!=BJ_ABI || a<0x1000 || a>0x200000-128 || (a&15)){boot_journal_diag[0]=3;return;}
 if(!bj_is_preflight(site)){bj_early_mark(site,after,value);return;}
 u32 s=bj_enter();if(bj_busy){bj_leave(s);boot_trace_state[3]++;boot_journal_diag[0]=4;return;}bj_busy=1;bj_leave(s);
 if(bj_read(0)!=BJ_MAGIC){boot_journal_diag[0]=5;bj_dead=1;goto done;}
 if(bj_read(BJ_READY)!=1){boot_journal_diag[0]=6;bj_dead=1;goto done;}
 if(bj_read(BJ_ERROR)){boot_journal_diag[0]=7;bj_dead=1;goto done;}
 if(bj_read(BJ_STOP)){boot_journal_diag[0]=8;bj_dead=1;goto done;}
 u32 n=bj_read(BJ_PRODUCED);if(n>=BJ_CAP){boot_journal_diag[0]=9;bj_dead=1;goto done;}
 if(bj_read(BJ_ACK)!=n){boot_journal_diag[0]=10;bj_dead=1;goto done;}
 u32 e[8]={site,after,value,bj_clock(),boot_trace_state[3],0,0,0};
 s=bj_enter();volatile u32 *p=bj_ptr();for(u32 i=0;i<8;i++){p[BJ_EVENT+i]=e[i];LOOP_PAD();}
#ifndef BOOT_JOURNAL_TEST
 __asm__ volatile("sync":::"memory");
#else
 __asm__ volatile("":::"memory");
#endif
 p[BJ_PRODUCED]=++n;bj_leave(s);
 u32 start=bj_clock();
 while(bj_read(BJ_ACK)!=n){
  if(bj_read(BJ_ERROR)){boot_journal_diag[0]=11;bj_dead=1;break;}
  if(bj_read(BJ_READY)!=1){boot_journal_diag[0]=12;bj_dead=1;break;}
  if((u32)(bj_clock()-start)>0x10000000u){boot_journal_diag[0]=13;bj_dead=1;break;}
  for(u32 i=0;i<1024;i++){LOOP_PAD();LOOP_PAD();}
 }
 if(!bj_dead)boot_journal_diag[0]=14;
 done:bj_busy=0;
}
void boot_trace_io(u32 site,u32 after,u32 value){boot_trace_mark(site,after,value);}
void boot_trace_stop(void)
{
 if(!bj_dead && boot_journal_config[1]>=0x1000 && boot_journal_config[1]<=0x200000-128 && !(boot_journal_config[1]&15) && bj_read(0)==BJ_MAGIC)bj_set(BJ_STOP,1);
 bj_dead=1;
}
