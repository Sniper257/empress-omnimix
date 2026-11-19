/* Loose-file MMCE hardware compatibility bench. The launcher's embedded copies survive
 * each reset; no storage reads are attempted until drivers have been restored.
 * SDK SIO2MAN replaces retail SIO2MAN; native sound/PAD/MC stay unchanged.
 * Sources/licences/module hashes are recorded by make_mmce_bench.py. */
#ifndef MMCE_BOOT_TEST
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
#include <stdlib.h>
#include <errno.h>
#include "debug_config.h"
#include "mmce_boot.h"
#include "profile_location.h"
#ifdef MMCE_DIAG
#include "mmce_diag.h"
#endif

#ifdef LOAD_PROFILE
extern void mmce_profile_mark(unsigned,unsigned);
#define MMCE_PROFILE(stage) mmce_profile_mark(boot_phase,stage)
#else
#define MMCE_PROFILE(stage) ((void)0)
#endif

typedef struct { const char *name; void *data; unsigned bytes; } mmce_module;
extern mmce_module mmce_modules[];
extern unsigned mmce_module_count;
extern unsigned char mmce_ioprp[];
extern unsigned mmce_ioprp_bytes;
static char status[2048];
static char boot_log[8192],recent[8][112],view[1100];
static unsigned recent_count,boot_sequence,boot_bytes,flushed_bytes;
static int disk_log_started;
static const char *disk_log_path=MMCE_BENCH_ROOT "mmce-startup.log";
static int trace_active,disk_ready,boot_phase,debug_enabled;
void mmce_boot_set_debug(int enabled){debug_enabled=!!enabled;}
void mmce_boot_set_profile_log(int mode){
    disk_log_path=mode==2?MMCE_BENCH_ROOT "mmce-startup-net.log":MMCE_BENCH_ROOT "mmce-startup-ram.log";
    disk_log_started=0;flushed_bytes=0;
}
static void (*report)(const char *);
const char *mmce_boot_status(void) { return status; }
void mmce_boot_set_reporter(void (*fn)(const char *)){report=fn;}
int mmce_boot_trace_active(void){return trace_active && debug_enabled;}
void mmce_boot_note(const char *fmt,...)
{
    if(!debug_enabled)return;
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
    snprintf(view,sizeof view,"MEGAMIX MMCE boot trace " MMCE_TRACE_REV " (%s)\n",boot_phase?"handoff":"launcher");
    for(unsigned i=0;i<recent_count;i++)strncat(view,recent[i],sizeof view-strlen(view)-1);
    if(report)report(view);
}
int mmce_boot_flush_log(void)
{
    if(!debug_enabled)return 0;
    if(!disk_ready)return -1;
    unsigned pending=boot_bytes-flushed_bytes,have=strlen(boot_log);
    if(!pending)return 0;
    FILE *f=fopen(disk_log_path,disk_log_started?"a":"w");
    if(!f)return -1;
    int failed=0;
    if(pending>have){failed=fputs("[early RAM trace overflow: older lines unavailable]\n",f)<0;pending=have;}
    if(fputs(boot_log+have-pending,f)<0)failed=1;
    if(fclose(f))failed=1;
    if(!failed){flushed_bytes=boot_bytes;disk_log_started=1;}
    return failed?-1:0;
}

static char selected_game_id[11]=MMCE_GAME_ID;
int mmce_boot_set_game_id(const char *id)
{
    if(!ml_game_id(id))return -1;
    strcpy(selected_game_id,id);return 0;
}
static int mmce_select_game_card(char *error,unsigned capacity)
{
    /* Protocol commands: SET_GAMEID8, GET_STATUS2, GET_GAMEID7. Status has
     * errno in its high byte and busy in bit0. A command ACK alone does not
     * establish completion. Defer the first poll so the firmware's queued
     * switch can run; require two idle, matching readbacks 100ms apart.
     * No file handles are open and native MCMAN is not loaded at this point. */
    char wanted[11],seen[256]={0};
    if(!selected_game_id[0])return 0;
    strcpy(wanted,selected_game_id);
    mmce_boot_note("G01 select dedicated GameID %s",wanted);
    int result=fileXioDevctl("mmce0:",8,wanted,sizeof wanted,0,0);
    if(result<0){snprintf(error,capacity,"MMCE: SET_GAMEID failed (%d); game not started\n",result);return -1;}
    unsigned stable=0;int state=-1,readback=-1;
    for(unsigned poll=0;poll<150;poll++){
        DelayThread(100000);
        state=fileXioDevctl("mmce0:",2,0,0,0,0);
        if(state>=0 && (state&0xff00)){
            snprintf(error,capacity,"MMCE: card switch error %04x; game not started\n",state);return -1;
        }
        if(state<0 || (state&1)){stable=0;}
        else {
            memset(seen,0,sizeof seen);
            readback=fileXioDevctl("mmce0:",7,0,0,seen,sizeof seen);
            seen[sizeof seen-1]=0;
            if(readback>=0 && !strcmp(seen,wanted)){
                if(++stable==2){
                    mmce_boot_note("G03 GameID %s verified; card switch idle",seen);
                    return 0;
                }
            }else stable=0;
        }
        if(poll%10==0)mmce_boot_note("G02 wait %u/150 status=%04x readback=%d id=%.32s",poll+1,state,readback,seen);
    }
    snprintf(error,capacity,"MMCE: GameID switch unverified (status=%04x, id=%.32s); game not started\n",state,seen);
    return -1;
}
int mmce_boot_select_game_card(void){return mmce_select_game_card(status,sizeof status);}

/* Failure-only, read-only probe. A newlib fopen failure does not prove that a
 * file is absent. Report the direct driver result without continuing through
 * a damaged stdio client or letting a diagnostic read bypass normal startup. */
static void mmce_open_failure(char *out,unsigned capacity,int first,int last)
{
    unsigned char bytes[128] __attribute__((aligned(64)))={0};
    int fd=fileXioOpen(MMCE_BENCH_ROOT "EMPRESS/base.txt",1,0),got=-1,closed=-1;
    if(fd>=0){got=fileXioRead(fd,bytes,2);closed=fileXioClose(fd);}
    int ping=fileXioDevctl("mmce0:",1,0,0,0,0);
    int state=fileXioDevctl("mmce0:",2,0,0,0,0);
    snprintf(out,capacity,
        "R131b: cannot open " MMCE_BENCH_ROOT "EMPRESS/base.txt\n"
        "stdio errno first=%d last=%d\n"
        "raw open=%d read=%d close=%d bytes=%02x %02x\n"
        "MMCE ping=%08x status=%08x\nGame not started; photograph this screen\n",
        first,last,fd,got,closed,bytes[0],bytes[1],ping,state);
}

static int mmce_boot_prepare_body(int game_handoff)
{
    unsigned i;int module_result, id, k, n=0;
    FILE *f;
    boot_phase=game_handoff;
    status[0]=0;
    if(!boot_sequence)mmce_boot_note("MEGAMIX MMCE launcher trace " MMCE_TRACE_REV);
    mmce_boot_note("M01 %s preparation",game_handoff?"game handoff":"launcher");
    if(disk_ready)mmce_boot_flush_log();
    disk_ready=0;trace_active=1;
    MMCE_PROFILE(1);
    mmce_boot_note("M02 release old FILEIO client");
    fileXioExit(); /* Restore the default legacy FILEIO stdio hooks. */
    fioExit();
    MMCE_PROFILE(2);
    mmce_boot_note("M03 release old LOADFILE / IOP heap clients");
    SifLoadFileExit();
    SifExitIopHeap();
    /* Reboot from an EE buffer, never from a mmce0: path whose own driver is
     * being destroyed. The native IOPRP310 image is embedded unchanged. */
    MMCE_PROFILE(3);
    mmce_boot_note("M04 reboot retail IOP image (%u bytes)",mmce_ioprp_bytes);
    if(SifIopRebootBuffer(mmce_ioprp,mmce_ioprp_bytes)!=1) {
        snprintf(status,sizeof status,"MMCE: IOPRP buffer reboot failed");trace_active=0;return -1;
    }
    MMCE_PROFILE(4);
    mmce_boot_note("M05 wait for retail IOP boot completion");
    unsigned polls=0;
    while(!SifIopSync()) {if((++polls&0x3fffff)==0)mmce_boot_note("M05 retail IOP still waiting (poll %u)",polls);}
    MMCE_PROFILE(5);
    mmce_boot_note("M06 initialize SIF RPC");
    SifInitRpc(0);
    MMCE_PROFILE(6);
    mmce_boot_note("M07 bind LOADFILE RPC");
    id=SifLoadFileInit();mmce_boot_note("M07 LOADFILE result %d",id);
    if(id<0) {snprintf(status,sizeof status,"MMCE: LOADFILE binding failed (%d)",id);trace_active=0;return -1;}
    MMCE_PROFILE(7);
    mmce_boot_note("M08 bind IOP heap RPC");
    id=SifInitIopHeap();mmce_boot_note("M08 IOP heap result %d",id);
    if(id<0) {
        snprintf(status,sizeof status,"MMCE: IOP services failed");trace_active=0;return -1;
    }
    /* Retail 3.1 LOADFILE already implements buffer RPC #6 (dispatcher at
     * +578, eleven entries). The ROM-only LMB patch rejects this newer image. */
    MMCE_PROFILE(8);
    mmce_boot_note("M09 enable mmce0: module paths");
    if(sbv_patch_disable_prefix_check()) {
        snprintf(status,sizeof status,"MMCE: LOADFILE compatibility failed");trace_active=0;return -1;
    }
    n=snprintf(status,sizeof status,"MMCE phase %s; retail IOPRP310 + SDK SIO2MAN\n",
               game_handoff?"game handoff":"launcher");
    for(i=0;i<mmce_module_count;i++) {
        module_result=-1;
        MMCE_PROFILE(9+i);
        mmce_boot_note("M10 module %u/%u: %s (%u bytes)",i+1,mmce_module_count,mmce_modules[i].name,mmce_modules[i].bytes);
#ifdef MMCE_DIAG
        char args[32];int arglen=0,diagnostic=!strcmp(mmce_modules[i].name,"mmceman");
        if(diagnostic){
            arglen=mmce_diag_open(args,sizeof args);
            if(arglen<0){snprintf(status+n,sizeof status-n,"Cannot allocate IOP diagnostic buffer\n");trace_active=0;return -1;}
        }
        id=SifExecModuleBuffer(mmce_modules[i].data,mmce_modules[i].bytes,arglen,diagnostic?args:0,&module_result);
        if(diagnostic)mmce_diag_close();
#else
        id=SifExecModuleBuffer(mmce_modules[i].data,mmce_modules[i].bytes,0,0,&module_result);
#endif
        mmce_boot_note("M10 %s returned id=%d start=%d",mmce_modules[i].name,id,module_result);
        n+=snprintf(status+n,sizeof status-n,"%s: id %d, start %d\n",mmce_modules[i].name,id,module_result);
        /* 0=resident, 2=removable resident; 1 means the driver did not stay. */
        if(id<0 || (module_result!=0 && module_result!=2)){trace_active=0;return -1;}
    }
    MMCE_PROFILE(13);
    mmce_boot_note("M11 bind fileXio for read-only MMCE identity queries");
    id=fileXioInit();
    if(id<0){snprintf(status+n,sizeof status-n,"fileXio bind failed %d\n",id);fileXioExit();trace_active=0;return -1;}
    MMCE_PROFILE(14);
    int ping=fileXioDevctl("mmce0:",1,0,0,0,0);
    mmce_boot_note("M11 slot1 PING=%08x (protocol/product/revision)",ping);
    if(ping<0){snprintf(status+n,sizeof status-n,"No MMCE response in memory-card slot 1\n");fileXioExit();trace_active=0;return -1;}
    MMCE_PROFILE(15);
    if(game_handoff && mmce_select_game_card(status+n,sizeof status-n)){
        trace_active=0;return -1;
    }
    MMCE_PROFILE(16);
    int card=fileXioDevctl("mmce0:",3,0,0,0,0);
    int channel=fileXioDevctl("mmce0:",5,0,0,0,0);
    if(game_handoff && (card<0 || channel<0)){
        snprintf(status+n,sizeof status-n,"MMCE: selected card/channel query failed; game not started\n");trace_active=0;return -1;
    }
    n+=snprintf(status+n,sizeof status-n,"slot1 protocol=%u product=%u revision=%u card=%d channel=%d\n",
               (ping>>16)&255,(ping>>8)&255,ping&255,card,channel);
    if(game_handoff){
        n+=snprintf(status+n,sizeof status-n,"GameID %s; switch idle\n",selected_game_id[0]?selected_game_id:"unchanged");
        mmce_boot_note("M11 GameID %s card=%d channel=%d",selected_game_id[0]?selected_game_id:"unchanged",card,channel);
    }else mmce_boot_note("M11 card=%d channel=%d; GameID deferred until START",card,channel);
    /* Keep fileXio's newlib hooks for ALL launcher storage access. The SDK's
     * fio client speaks the older ROM protocol, NOT EMPRESS FILEIO's worker/
     * SIFCMD completion protocol (even though both bind RPC 0x80000001).
     * Retail's immediate RPC acknowledgement is not an open/read result.
     * Gameplay uses its own compatible native client in vdisc.c; this hook
     * belongs to the launcher and does not replace the game's file functions. */
    int first_open_error=0,last_open_error=0;
    for(k=0;k<150;k++) {
        if(k%10==0)mmce_boot_note("M11 open mmce0:/MEGAMIX/EMPRESS/base.txt (%d/150)",k+1);
        MMCE_PROFILE(17);
    errno=0;f=fopen(MMCE_BENCH_ROOT "EMPRESS/base.txt","rb");
        if(f) {
            mmce_boot_note("M12 fileXio open OK; read base.txt");
            MMCE_PROFILE(18);
    char check[2]={0};size_t got=fread(check,1,2,f);
            int ok=got==2 && check[0]=='E' && check[1]==' ';
            mmce_boot_note("M12 read returned %u bytes (%02x %02x); close",(unsigned)got,
                           (unsigned char)check[0],(unsigned char)check[1]);
            MMCE_PROFILE(19);
    int read_error=ferror(f),saved_errno=errno;
    int closed=fclose(f);if(closed)ok=0;
            mmce_boot_note("M12 close returned %d",closed);
            if(!ok) { snprintf(status+n,sizeof status-n,"R131b base.txt read failed: got=%u bytes=%02x %02x error=%d errno=%d close=%d\n",(unsigned)got,(unsigned char)check[0],(unsigned char)check[1],read_error,saved_errno,closed);trace_active=0;return -1; }
            break;
        }
        last_open_error=errno;if(!k)first_open_error=errno;
        DelayThread(100000);
    }
    if(k==150) {
        mmce_open_failure(status+n,sizeof status-n,first_open_error,last_open_error);trace_active=0;return -1;
    }
    snprintf(status+n,sizeof status-n,"Launcher fileXio mmce0: read OK; game retains native FILEIO\n");
    disk_ready=1;
    /* The first bootstrap must restore storage before external settings can be
     * read. Keep it quiet until then; never require log writes to boot a game. */
    if(!game_handoff)mmce_boot_set_debug(debug_read(MMCE_BENCH_ROOT "debug.cfg").launcher_log);
    if(!debug_enabled){trace_active=0;return 0;}
    mmce_boot_note("M13 MMCE read verified; write startup log");
    MMCE_PROFILE(20);
    if(mmce_boot_flush_log())mmce_boot_note("M13 startup log write failed");
    /* Existing diagnostic files are deliberately distinct for both phases. */
    MMCE_PROFILE(21);
    f=fopen(game_handoff?MMCE_BENCH_ROOT "mmce-handoff.txt":MMCE_BENCH_ROOT "mmce-launcher.txt","w");
    if(!f){snprintf(status+n,sizeof status-n,"MMCE diagnostic write-open failed\n");trace_active=0;return -1;}
    k=fputs(status,f)<0;
    if(fclose(f))k=1;
    if(k)snprintf(status+n,sizeof status-n,"MMCE diagnostic write/close failed\n");
    mmce_boot_note("M14 %s preparation %s",game_handoff?"handoff":"launcher",k?"FAILED":"complete");
    MMCE_PROFILE(22);
    if(!k)mmce_boot_flush_log();
    trace_active=0;
    return k?-1:0;
}

int mmce_boot_prepare(int game_handoff)
{
 int result=mmce_boot_prepare_body(game_handoff);
 MMCE_PROFILE(0);
 return result;
}
