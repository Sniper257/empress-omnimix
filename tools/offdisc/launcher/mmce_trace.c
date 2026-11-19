/* Link-time observation of the installed SDK reset implementation. No copied
 * replacement reset/driver code: call the real routines with identical args.
 * Wrappers are silent outside MMCE preparation, and never reside in gameplay. */
#include "mmce_boot.h"
#include <string.h>
#ifdef MMCE_DIAG
#include "mmce_diag.h"
int __real__SifLoadModuleBuffer(void *,int,const char *,int *);
int __wrap__SifLoadModuleBuffer(void *ptr,int n,const char *args,int *res)
{
    if(mmce_diag_enabled())return mmce_diag_load(ptr,n,args,res);
    return __real__SifLoadModuleBuffer(ptr,n,args,res);
}
#endif
static int retail_boot;

int __real_SifIopReset(const char *,int);
int __real_SifIopSync(void);
int __real_send_romdrv_rom1_payloads(void *,int,void *,int);
int __real_sbv_patch_romdrv_set_rom1_info(void *,int);
void *__real_SifAllocIopHeap(int);
int __real_SifInitIopHeap(void);
int __real_SifLoadFileInit(void);
int __real__SifLoadModule(const char *,int,const char *,int *,int,int);

int __wrap_SifIopReset(const char *arg,int mode)
{
    int on=mmce_boot_trace_active();
    if(on){retail_boot=0;mmce_boot_note("T01 SDK submits ROM IOP reset");}
    int r=__real_SifIopReset(arg,mode);
    if(on)mmce_boot_note("T01 ROM reset submission returned %d",r);
    return r;
}
int __wrap_SifIopSync(void)
{
    static unsigned polls;static int waiting;
    int on=mmce_boot_trace_active();
    if(on && !waiting){mmce_boot_note("T02 SDK waits for %s BOOTEND",retail_boot?"retail IOP":"ROM IOP");waiting=1;polls=0;}
    int r=__real_SifIopSync();
    if(on && r){mmce_boot_note("T02 %s BOOTEND received",retail_boot?"retail IOP":"ROM IOP");waiting=0;}
    else if(on && (++polls&0x3fffff)==0)mmce_boot_note("T02 waiting for %s BOOTEND (%u)",retail_boot?"retail IOP":"ROM IOP",polls);
    return r;
}
int __wrap_send_romdrv_rom1_payloads(void *a,int n,void *b,int m)
{
    int on=mmce_boot_trace_active();
    if(on)mmce_boot_note("T03 copy IOP image into IOP RAM (%d+%d)",n,m);
    int r=__real_send_romdrv_rom1_payloads(a,n,b,m);
    if(on)mmce_boot_note("T03 image transfer returned %d",r);
    return r;
}
int __wrap_sbv_patch_romdrv_set_rom1_info(void *p,int n)
{
    int on=mmce_boot_trace_active();
    if(on)mmce_boot_note("T04 point ROMDRV at RAM image (%d bytes)",n);
    int r=__real_sbv_patch_romdrv_set_rom1_info(p,n);
    if(on)mmce_boot_note("T04 ROMDRV patch returned %d",r);
    return r;
}
void *__wrap_SifAllocIopHeap(int n)
{
    int on=mmce_boot_trace_active();
    if(on)mmce_boot_note("T05 allocate IOP transfer buffer (%d bytes)",n);
    void *p=__real_SifAllocIopHeap(n);
    if(on)mmce_boot_note("T05 IOP buffer address %p",p);
    return p;
}
int __wrap_SifInitIopHeap(void)
{
    int on=mmce_boot_trace_active();
    if(on)mmce_boot_note("T06 bind IOP heap service");
    int r=__real_SifInitIopHeap();
    if(on)mmce_boot_note("T06 IOP heap binding returned %d",r);
    return r;
}
int __wrap_SifLoadFileInit(void)
{
    int on=mmce_boot_trace_active();
    if(on)mmce_boot_note("T07 bind LOADFILE service");
    int r=__real_SifLoadFileInit();
    if(on)mmce_boot_note("T07 LOADFILE binding returned %d",r);
    return r;
}
int __wrap__SifLoadModule(const char *path,int n,const char *args,int *res,int fno,int async)
{
    int on=mmce_boot_trace_active();
    if(on)mmce_boot_note("T08 SDK loads %s (rpc=%d async=%d)",path,fno,async);
    int r=__real__SifLoadModule(path,n,args,res,fno,async);
    if(on && !strcmp(path,"rom:UDNL"))retail_boot=1;
    if(on)mmce_boot_note("T08 module request returned %d",r);
    return r;
}
