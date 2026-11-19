/* First loose-file USB hardware bench. The launcher's embedded copies survive
 * each reset; no storage reads are attempted until drivers have been restored.
 * Native EMPRESS sound/SIO2/PAD/MC modules are still loaded by the game itself.
 * Sources/licences/module hashes are recorded by make_usb_bench.py. */
#ifndef USB_BOOT_TEST
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <iopcontrol.h>
#include <iopcontrol_special.h>
#include <iopheap.h>
#define NEWLIB_PORT_AWARE /* fioExit only: no open descriptors cross reset */
#include <fileio.h>
#include <fileXio_rpc.h>
#include <delaythread.h>
#include <sbv_patches.h>
#endif
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "usb_boot.h"
#ifdef USB_MEMORY_REDUCED
#include "usb_memory.h"
#endif

typedef struct { const char *name; void *data; unsigned bytes; } usb_module;
extern usb_module usb_modules[];
extern unsigned usb_module_count;
extern unsigned char usb_ioprp[];
extern unsigned usb_ioprp_bytes;
static char status[2048];
static char boot_log[8192],recent[8][112],view[1100];
static unsigned recent_count,boot_sequence,boot_bytes,flushed_bytes;
static int disk_log_started;
static int trace_active,disk_ready,boot_phase;
static void (*report)(const char *);
const char *usb_boot_status(void) { return status; }
void usb_boot_set_reporter(void (*fn)(const char *)){report=fn;}
int usb_boot_trace_active(void){return trace_active;}
void usb_boot_note(const char *fmt,...)
{
    char text[104],line[120];va_list ap;
    va_start(ap,fmt);vsnprintf(text,sizeof text,fmt,ap);va_end(ap);
    snprintf(line,sizeof line,"%03u %s\n",++boot_sequence,text);
    unsigned used=strlen(boot_log),bytes=strlen(line);
    boot_bytes+=bytes;
    if(used+bytes>=sizeof boot_log){
        char *p=strchr(boot_log+bytes,'\n');
        if(p)memmove(boot_log,p+1,strlen(p+1)+1);else boot_log[0]=0;
    }
    strncat(boot_log,line,sizeof boot_log-strlen(boot_log)-1);
    if(recent_count==8){memmove(recent,recent+1,sizeof recent-sizeof recent[0]);recent_count--;}
    snprintf(recent[recent_count++],sizeof recent[0],"%.110s",line);
    snprintf(view,sizeof view,"MEGAMIX USB boot trace " USB_TRACE_REV " (%s)\n",boot_phase?"handoff":"launcher");
    for(unsigned i=0;i<recent_count;i++)strncat(view,recent[i],sizeof view-strlen(view)-1);
    if(report)report(view);
}
int usb_boot_flush_log(void)
{
    if(!disk_ready)return -1;
    unsigned pending=boot_bytes-flushed_bytes,have=strlen(boot_log);
    if(!pending)return 0;
    FILE *f=fopen(USB_BENCH_ROOT "usb-startup.log",disk_log_started?"a":"w");
    if(!f)return -1;
    int failed=0;
    if(pending>have){failed=fputs("[early RAM trace overflow: older lines unavailable]\n",f)<0;pending=have;}
    if(fputs(boot_log+have-pending,f)<0)failed=1;
    if(fclose(f))failed=1;
    if(!failed){flushed_bytes=boot_bytes;disk_log_started=1;}
    return failed?-1:0;
}

#ifdef USB_IOP_BUDGET
#include "usb_iop_budget.h"
#endif

#ifdef USB_NATIVE_PRELOAD
/* Diagnostic only. These are the original seven 12f790 loads, in original
 * order, with no arguments. The paired prepared image skips only the duplicate
 * LOAD calls; every native EE service/client initializer remains intact.
 * Validate the live image even on a prepared-cache hit, BEFORE loading any
 * module. Never proceed from a failed load or a failed durable checkpoint. */
#ifndef USB_BOOT_TEST
#define usb_game_word(a) (*(volatile unsigned *)(a))
#endif
int usb_boot_preload_native(void)
{
    static const struct { unsigned site; const char *name; } native[]={
        {0x12f834,"LIBSD.IRX"}, {0x12f854,"SDDRVIOP.IRX"},
        {0x12f874,"SIO2MAN.IRX"}, {0x12f894,"MCMAN.IRX"},
        {0x12f8b4,"MCSERV.IRX"}, {0x12f8d4,"PADMAN.IRX"},
        {0x12f8f4,"CDVDSTM.IRX"}
    };
    for(unsigned i=0;i<sizeof native/sizeof native[0];i++)
        if(usb_game_word(native[i].site)!=0x24020001u){
            usb_boot_note("N00 unpaired native module site %08x",native[i].site);
            snprintf(status,sizeof status,"R134 native-module image mismatch\nGame not started");
            usb_boot_flush_log();return -1;
        }
#ifdef USB_UNUSED_RING
    if(usb_game_word(0x14d300)!=0x0000102d){
        usb_boot_note("N00 unpaired optical-ring allocation site");
        snprintf(status,sizeof status,"R143 optical-ring image mismatch\nGame not started");usb_boot_flush_log();return -1;
    }
#endif
#ifdef USB_IOP_BUDGET
    if(ib_sdk_configure()){
        snprintf(status,sizeof status,"R143 IOP budget metadata mismatch\nGame not started");return -1;
    }
#endif
    for(unsigned i=0;i<sizeof native/sizeof native[0];i++){
        char path[112];int result=-1;
        snprintf(path,sizeof path,USB_BENCH_ROOT "EMPRESS/MODULES/%s",native[i].name);
        usb_boot_note("N01 original module %u/7 %s: LOADFILE enter",i+1,native[i].name);
        if(usb_boot_flush_log())goto log_failed;
        int id=SifLoadStartModule(path,0,0,&result);
        usb_boot_note("N02 %s returned id=%d start=%d",native[i].name,id,result);
#ifdef USB_IOP_BUDGET
        ib_sdk_sample(0x20+i);
#endif
        if(usb_boot_flush_log())goto log_failed;
        if(id<0 || (result!=0 && result!=2)){
            snprintf(status,sizeof status,"R134 native module failed\n%s: id=%d start=%d\nGame not started",native[i].name,id,result);
            return -1;
        }
    }
    usb_boot_note("N03 all seven original modules resident; native EE init retained");
    if(usb_boot_flush_log())goto log_failed;
    return 0;
log_failed:
    snprintf(status,sizeof status,"R134 native-module checkpoint write failed\nGame not started");
    return -1;
}
#endif

int usb_boot_prepare(int game_handoff)
{
    unsigned i;int module_result, id, k, n=0;
    FILE *f;
    boot_phase=game_handoff;
    status[0]=0;
    if(!boot_sequence)usb_boot_note("MEGAMIX USB launcher trace " USB_TRACE_REV);
    usb_boot_note("U01 %s preparation",game_handoff?"game handoff":"launcher");
    if(disk_ready)usb_boot_flush_log();
    disk_ready=0;trace_active=1;
    usb_boot_note("U02 release old fileXio / FILEIO clients");
    fileXioExit();
    fioExit();
    usb_boot_note("U03 release old LOADFILE / IOP heap clients");
    SifLoadFileExit();
    SifExitIopHeap();
    /* Reboot from an EE buffer, never from a mass: path whose own driver is
     * being destroyed. The native IOPRP310 image is embedded unchanged. */
    usb_boot_note("U04 reboot retail IOP image (%u bytes)",usb_ioprp_bytes);
    if(SifIopRebootBuffer(usb_ioprp,usb_ioprp_bytes)!=1) {
        snprintf(status,sizeof status,"USB: IOPRP buffer reboot failed");trace_active=0;return -1;
    }
    usb_boot_note("U05 wait for retail IOP boot completion");
    unsigned polls=0;
    while(!SifIopSync()) {if((++polls&0x3fffff)==0)usb_boot_note("U05 retail IOP still waiting (poll %u)",polls);}
    usb_boot_note("U06 initialize SIF RPC");
    SifInitRpc(0);
    usb_boot_note("U07 bind LOADFILE RPC");
    id=SifLoadFileInit();usb_boot_note("U07 LOADFILE result %d",id);
    if(id<0) {snprintf(status,sizeof status,"USB: LOADFILE binding failed (%d)",id);trace_active=0;return -1;}
    usb_boot_note("U08 bind IOP heap RPC");
    id=SifInitIopHeap();usb_boot_note("U08 IOP heap result %d",id);
    if(id<0) {
        snprintf(status,sizeof status,"USB: IOP services failed");trace_active=0;return -1;
    }
#ifdef USB_IOP_BUDGET
    ib_sdk_bind();ib_sdk_sample(0x01);
#endif
    /* Retail 3.1 LOADFILE already implements buffer RPC #6 (dispatcher at
     * +578, eleven entries). The ROM-only LMB patch rejects this newer image. */
    usb_boot_note("U09 enable mass: module paths");
    if(sbv_patch_disable_prefix_check()) {
        snprintf(status,sizeof status,"USB: LOADFILE compatibility failed");trace_active=0;return -1;
    }
    n=snprintf(status,sizeof status,"USB phase %s; retail IOPRP310\n",
               game_handoff?"game handoff":"launcher");
    for(i=0;i<usb_module_count;i++) {
        module_result=-1;
        usb_boot_note("U10 module %u/%u: %s (%u bytes)",i+1,usb_module_count,usb_modules[i].name,usb_modules[i].bytes);
#ifdef USB_MEMORY_REDUCED
        static const char host_args[]=USB_MEMORY_HOST_ARGS;
        int host=!strcmp(usb_modules[i].name,"usbd_mini");
        if(host)usb_boot_note("U10 bounded USB host: dev8 ed24 gtd48 itd2 req32");
        id=SifExecModuleBuffer(usb_modules[i].data,usb_modules[i].bytes,host?sizeof host_args:0,host?host_args:0,&module_result);
#else
        id=SifExecModuleBuffer(usb_modules[i].data,usb_modules[i].bytes,0,0,&module_result);
#endif
        usb_boot_note("U10 %s returned id=%d start=%d",usb_modules[i].name,id,module_result);
#ifdef USB_IOP_BUDGET
        ib_sdk_sample(0x02+i);
#endif
        n+=snprintf(status+n,sizeof status-n,"%s: id %d, start %d\n",usb_modules[i].name,id,module_result);
        /* 0=resident, 2=removable resident; 1 means the driver did not stay. */
        if(id<0 || (module_result!=0 && module_result!=2)){trace_active=0;return -1;}
    }
    /* SDK fio speaks the old ROM wire protocol, not EMPRESS FILEIO's worker/
     * SIFCMD completion protocol. Keep matching fileXio stdio hooks throughout
     * the launcher, as on MMCE R4+. Gameplay retains its compatible native
     * client. Release fileXio before the next reset, never cross stale handles. */
    usb_boot_note("U11 bind matching fileXio launcher client");
    id=fileXioInit();
    if(id<0){snprintf(status+n,sizeof status-n,"fileXio bind failed %d\n",id);fileXioExit();trace_active=0;return -1;}
#ifdef USB_MEMORY_REDUCED
    if(game_handoff){
        id=fileXioSetRWBufferSize(USB_MEMORY_STAGING);
        usb_boot_note("U11 launcher-only staging %u bytes returned %d",USB_MEMORY_STAGING,id);
        if(id<0){snprintf(status+n,sizeof status-n,"USB: launcher staging resize failed (%d)\n",id);trace_active=0;return -1;}
    }
#endif
    for(k=0;k<150;k++) {
        if(k%10==0)usb_boot_note("U11 open mass:/MEGAMIX/EMPRESS/base.txt (%d/150)",k+1);
        f=fopen(USB_BENCH_ROOT "EMPRESS/base.txt","rb");
        if(f) {
            usb_boot_note("U12 fileXio open OK; read base.txt");
            char check[2]={0};size_t got=fread(check,1,2,f);
            int ok=got==2 && check[0]=='E' && check[1]==' ';
            usb_boot_note("U12 read returned %u bytes (%02x %02x); close",(unsigned)got,
                          (unsigned char)check[0],(unsigned char)check[1]);
            int closed=fclose(f);if(closed)ok=0;
            usb_boot_note("U12 close returned %d",closed);
            if(!ok) { snprintf(status+n,sizeof status-n,"base.txt read failed\n");trace_active=0;return -1; }
            break;
        }
        DelayThread(100000);
    }
    if(k==150) {
        snprintf(status+n,sizeof status-n,"Missing " USB_BENCH_ROOT "EMPRESS/base.txt\n");trace_active=0;return -1;
    }
    snprintf(status+n,sizeof status-n,"Launcher fileXio mass: read OK; game retains native FILEIO\n");
#ifdef USB_IOP_BUDGET
    ib_sdk_sample(0x09);
#endif
    disk_ready=1;
    usb_boot_note("U13 USB read verified; write startup log");
    if(usb_boot_flush_log())usb_boot_note("U13 startup log write failed");
    /* Existing diagnostic files are deliberately distinct for both phases. */
    f=fopen(game_handoff?USB_BENCH_ROOT "usb-handoff.txt":USB_BENCH_ROOT "usb-launcher.txt","w");
    if(!f){snprintf(status+n,sizeof status-n,"USB diagnostic write-open failed\n");trace_active=0;return -1;}
    k=fputs(status,f)<0;
    if(fclose(f))k=1;
    if(k)snprintf(status+n,sizeof status-n,"USB diagnostic write/close failed\n");
    usb_boot_note("U14 %s preparation %s",game_handoff?"handoff":"launcher",k?"FAILED":"complete");
    if(!k)usb_boot_flush_log();
#ifdef USB_BOOT_JOURNAL
    if(!k && game_handoff && usb_journal_start()) {
        /* Keep the error screen short enough for its logo/text layout. The
         * complete trace is on disk; the final four lines include J05–J07. */
        snprintf(status,sizeof status,"Startup journal preparation failed\n");
        for(unsigned j=recent_count>4?recent_count-4:0;j<recent_count;j++)
            strncat(status,recent[j],sizeof status-strlen(status)-1);
        k=1;
    }
    /* Failure diagnostics are the most important lines to persist. A failed
     * preflight still leaves matching fileXio available in the launcher. */
    if(usb_boot_flush_log()) {
        usb_boot_note("J09 final startup log flush failed");
        snprintf(status,sizeof status,"Startup journal log flush failed\n%s",view);k=1;
    }
#endif
    trace_active=0;
    return k?-1:0;
}
