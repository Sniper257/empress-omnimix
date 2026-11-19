#ifndef MEGAMIX_IOP_BUDGET_H
#define MEGAMIX_IOP_BUDGET_H
/* Private USB boot experiment. Existing retail FILEIO heap RPC (80000003):
 * op6 -> sysmem7 largest free; op7 -> sysmem8 total free. No IOP module.
 * ONE main-thread writer: SDK before Exec, native initializer after Exec.
 * Header: magic,mode(0inactive/1observe/2reserve),ptr,ownership(0/1pending/
 * 2native/3failed/4released/5bad handoff),count,query errors,early stop,release rc.
 * Records: tag,total free,largest free,result; all quantities are BYTES. */
#define IB_MAGIC 0x32424849u
#define IB_WORDS 136u
#define IB_RECORDS 32u
#define IB_AUDIO_BYTES 0xbe000u
static int ib_pointer(unsigned p){return !(p&255)&&p>=0x1000u&&p<=0x200000u-IB_AUDIO_BYTES;}
static int ib_enabled(const volatile unsigned *s){return s[0]==IB_MAGIC&&(s[1]==1||s[1]==2);}
static void ib_record(volatile unsigned *s,unsigned tag,unsigned total,unsigned largest,unsigned result){
 if(!ib_enabled(s))return;
 if(total>0x200000u||largest>total){s[5]++;total=largest=~0u;}
 unsigned n=s[4];if(n>=IB_RECORDS)return;
 volatile unsigned *r=s+8+n*4;r[0]=tag;r[1]=total;r[2]=largest;r[3]=result;
 __asm__ volatile("":::"memory");s[4]=n+1;
}
#endif
