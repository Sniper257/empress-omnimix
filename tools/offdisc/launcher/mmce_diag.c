/* Startup-only diagnostic. Standard LOADFILE buffer RPC, asynchronously waited
 * so the EE can display an IOP RAM breadcrumb even if the IOP stops responding.
 * Direct IOP reads follow PS2SDK ee/rpc/ahx iop_readwrite (real PS2 only).
 * This is not a gameplay timing/benchmark build. */
#ifndef MMCE_DIAG_TEST
#include <kernel.h>
#include <sifrpc.h>
#include <iopheap.h>
#include <loadfile.h>
#include <delaythread.h>
#include <tamtypes.h>
extern SifRpcClientData_t _lf_cd;
#endif
#include <stdio.h>
#include <string.h>
#include "mmce_boot.h"
#include "mmce_diag.h"
static unsigned trace_address;
static volatile int completed;
static void rpc_done(void *unused){(void)unused;completed=1;}
static unsigned initial[16] __attribute__((aligned(64)))={0x4d545243};

static const char *stage_name(unsigned n)
{
    switch(n){
    case 1:return "IRX entry";
    case 2:return "before startup printf";case 3:return "after startup printf";
    case 10:return "before SIO2 init";case 11:return "after SIO2 init";
    case 12:return "before slot1 probe";case 13:return "after slot1 probe";
    case 14:return "before slot2 probe";case 15:return "after slot2 probe";
    case 16:return "before FS register";case 17:return "after FS register";
    case 18:return "before module flags";case 19:return "IRX startup finished";
    case 20:return "before SIO2 hooks";case 21:return "after SIO2 hooks";
    case 22:return "before event flag";case 23:return "after event flag";
    case 24:return "before DMA/IRQ setup";case 25:return "after DMA/IRQ setup";
    case 30:return "before INTRMAN internals";case 31:return "after INTRMAN internals";
    case 32:return "before library lookup";case 33:return "after library lookup";
    case 34:return "library traversal";case 35:return "matched SIO2 library";
    case 40:return "before PING bus lock";case 41:return "after PING bus lock";
    case 42:return "before PING transfer";case 43:return "after PING transfer";
    case 44:return "after PING bus unlock";
    case 50:return "before PIO start";case 51:return "before timeout alarm";
    case 52:return "before PIO event wait";case 53:return "after PIO event wait";
    case 60:return "probe attempt";case 61:return "probe reply";
    case 70:return "before device delete";case 71:return "after device delete";
    case 72:return "before device add";case 73:return "after device add";
    case 74:return "FS init begins";case 75:return "FS init completed";
    default:return "IOP breadcrumb";
    }
}
static int read_trace(unsigned result[5])
{
    unsigned a,b,magic;
#ifdef MMCE_DIAG_TEST
    extern void diag_read_words(unsigned,unsigned *);
    unsigned words[5];diag_read_words(trace_address,words);
    magic=words[0];a=words[1];result[0]=words[2];result[1]=words[3];result[2]=words[4];b=a;
#else
    DI();ee_kmode_enter();
    volatile unsigned *p=(volatile unsigned *)(0xbc000000u+trace_address);
    magic=p[0];a=p[1];result[0]=p[2];result[1]=p[3];result[2]=p[4];b=p[1];
    ee_kmode_exit();EI();
#endif
    result[3]=b;
    return magic==0x4d545243 && a==b && !(b&1);
}
int mmce_diag_open(char *args,unsigned cap)
{
    void *p=SifAllocIopHeap(sizeof initial);
    unsigned address=(unsigned)(unsigned long)p;
    if(address<0x1000 || address>0x200000-sizeof initial || (address&15))return -1;
    memset(initial,0,sizeof initial);initial[0]=0x4d545243;
    SifDmaTransfer_t dma={initial,p,sizeof initial,0};
    sceSifWriteBackDCache(initial,sizeof initial);
    unsigned id=sceSifSetDma(&dma,1);
    if(!id){SifFreeIopHeap(p);return -1;}
    while(sceSifDmaStat(id)>=0){}
    trace_address=address;
    int n=snprintf(args,cap,"trace=%08x",address);
    if(n<0 || (unsigned)n>=cap){mmce_diag_close();return -1;}
    mmce_boot_note("T09 IOP breadcrumb buffer=%08x",address);
    return n+1;
}
void mmce_diag_close(void)
{
    if(trace_address){unsigned a=trace_address;trace_address=0;SifFreeIopHeap((void *)(unsigned long)a);}
}
int mmce_diag_enabled(void){return trace_address!=0;}
int mmce_diag_load(void *ptr,int len,const char *args,int *modres)
{
    struct _lf_module_buffer_load_arg arg __attribute__((aligned(64)));
    if(SifLoadFileInit()<0)return -SCE_EBINDMISS;
    memset(&arg,0,sizeof arg);arg.p.ptr=ptr;
    if(args && len>0){arg.q.arg_len=len>LF_ARG_MAX?LF_ARG_MAX:len;memcpy(arg.args,args,arg.q.arg_len);}
    completed=0;
    mmce_boot_note("T10 submit LOADFILE buffer RPC6; keep EE display alive");
    if(sceSifCallRpc(&_lf_cd,LF_F_MOD_BUF_LOAD,SIF_RPC_M_NOWAIT,&arg,sizeof arg,&arg,8,rpc_done,0)<0)return -SCE_ECALLMISS;
    unsigned last=~0u,polls=0,record[5]={0};
    while(!completed){
        if(read_trace(record) && record[3]!=last){
            last=record[3];
            mmce_boot_note("D%03u %s a=%08x b=%08x",record[0],stage_name(record[0]),record[1],record[2]);
        }
        if(++polls%50==0)mmce_boot_note("T11 pending %us D%03u a=%08x b=%08x",polls/10,record[0],record[1],record[2]);
        DelayThread(100000);
    }
    /* RPC response is DMA-written. Read its uncached alias after completion. */
#ifdef MMCE_DIAG_TEST
    struct _lf_module_buffer_load_arg *answer=&arg;
#else
    volatile struct _lf_module_buffer_load_arg *answer=(void *)((unsigned)&arg|0x20000000u);
#endif
    if(modres)*modres=answer->q.modres;
    mmce_boot_note("T12 buffer RPC completed id=%d start=%d",answer->p.result,answer->q.modres);
    return answer->p.result;
}
