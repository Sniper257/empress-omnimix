/* Virtual disc for EMPRESS (SLPM_552.21) off-disc play: answers the game's libcdvd calls from loose files.
 *
 * The game keeps its own LBA world (DAT bases + file tables untouched); every read is mapped through an
 * extent list {lba, sectors, size, path} to a file opened with the game's own fileio client (sceOpen/
 * sceLseek/sceRead/sceClose -> host:, mass:, ... whatever the IOP can open). Mod extents are searched first
 * (they may cover part of a base file), then the sorted base extents. Sectors no extent covers read as zeros.
 *
 * The libcdvd calls come from the game's CD thread (FUN_0014cd10 and its helpers) and boot (FUN_0012f790), so a
 * blocking read is fine: the fileio RPC waits on a semaphore and the other threads keep running. vd_pread (song
 * title sprites, songdb.c) runs on the main thread: a kernel semaphore keeps the file client to one thread at a time.
 * Built like the CN module (clang n32, EE register ABI), linked at the reserved block the launcher carves
 * off the top of the game's memory arena; the launcher fills `vd` and patches the jal sites. */

typedef unsigned char u8;
typedef unsigned short u16;
typedef int s32;
typedef unsigned int u32;

#ifdef LOAD_PROFILE
#include "load_profile.h"
static u32 lp_io_key;
#endif

typedef struct {
    u32 lba, sectors, size, path;      /* path = offset into the string pool */
} ext_t;

typedef struct {
    u32 magic;                         /* 'VDSK' */
    u32 n_base, n_mod;
    ext_t *base, *mod;                 /* base sorted by lba; mod searched linearly, first match wins */
    const char *strings;
    u32 reads, errors;                 /* statistics (visible in a savestate) */
    u32 last_lba, last_sectors;
} vd_conf_t;

vd_conf_t vd = { 0x4b534456 };
#ifdef USB_BOOT_TEXT
/* Reader owns this record only while holding the existing vdisc lock.
 * Publication before unlock proves payload completion, NOT unlock/caller return.
 * Main takes a bounded best-effort sample; never takes that lock or waits. */
static u32 vd_boot_read_active;
volatile u32 vd_boot_io[8]; /* stage, serial, completed serial, LBA, sectors, bytes, error, raw result */
#define VB_IO(i,v) (vd_boot_io[i]=(u32)(v))
#define VB_READ(i,v) do{if(vd_boot_read_active)VB_IO(i,v);}while(0)
void vd_boot_snapshot(u32 out[12]){
 volatile vd_conf_t *v=&vd;out[0]=v->reads;out[1]=v->errors;
 out[2]=v->last_lba;out[3]=v->last_sectors;
 for(u32 i=0;i<8;i++){out[4+i]=vd_boot_io[i];__asm__ volatile("nop\n\tnop\n\tnop");}
}
#else
#define VB_IO(i,v) ((void)0)
#define VB_READ(i,v) ((void)0)
#endif


/* game functions (EMPRESS); tools/offdisc/test_vdisc.c builds this file on the PC with its own */
#ifndef HOST_TEST
#define sceOpen  ((s32 (*)(const char *, s32, s32))0x114170)
#define sceClose ((s32 (*)(s32))0x114400)
#define sceLseek ((s32 (*)(s32, s32, s32))0x114578)
#define sceRead  ((s32 (*)(s32, void *, s32))0x1147b8)
#define vd_fio_ack() (*(volatile u32 *)0x202b0ec0)
#elif !defined(vd_fio_ack)
#define vd_fio_ack() 0u
#endif

#define LOOP_PAD() __asm__ volatile("nop\n\tnop\n\tnop")
#if defined(SELECT_PROFILE) || defined(SPR_ASYNC)
char vd_path_prefix[64]; /* Paired launcher publishes the common device/root. */
static const char *vd_extent_path(const ext_t *e,char out[256])
{
 const char *p=vd.strings+e->path;if((u8)*p!=1)return p;
 unsigned n=0,i=1;
 while(n<64&&vd_path_prefix[n]){out[n]=vd_path_prefix[n];n++;LOOP_PAD();}
 if(n==64)return 0;
 while(p[i]&&n<255){out[n++]=p[i++];LOOP_PAD();}
 if(p[i])return 0;out[n]=0;return out;
}
#endif
#ifdef USB_BOOT_JOURNAL
extern void boot_trace_io(u32,u32,u32);
#define BJ_IO(a,b,c) boot_trace_io(a,b,c)
#else
#define BJ_IO(a,b,c) ((void)0)
#endif
#define NFD 4

#ifdef AUDIO_PROBE
/* Fixed descriptor words only, never dereference a recorded descriptor/pointer.
 * First read and idle export snapshots; no per-frame/PLAY history or file I/O. */
/* v2 sidecar: original2112B prefix, counters, first32 opens, first32 failures,
 * first failed path. Stop all mutation before the first diagnostic export. */
static struct {u32 table[4][128],cache[16],opens[16],first[32][16],failed[32][16];u8 failed_path[160];} vd_fd_data;
static u32 vd_fd_mask,vd_fd_phase[4];
static s32 vd_fd_first;
#ifndef HOST_TEST
#define vd_fd_word(i) (((volatile u32 *)0x2b1780)[i])
#define vd_fd_scene() (*(volatile u32 *)0x2ad534)
#define vd_open_word(a) (*(volatile u32 *)(unsigned long)(a))
#endif
static u32 vd_open_context;
static void vd_open_masks(u32 *free_mask,u32 *reserved_mask)
{
    u32 f=0,r=0;
    for(u32 i=0;i<32;i++){
        u32 flags=vd_fd_word(i*4+1);
        if(!flags)f|=1u<<i;
        if(flags==0x10000000u)r|=1u<<i;
        LOOP_PAD();
    }
    *free_mask=f;*reserved_mask=r;
}
static s32 vd_file_open(const char *path,s32 flags,s32 permissions)
{
    u32 *s=vd_fd_data.opens;
    if(s[13])return sceOpen(path,flags,permissions);
    u32 event[16],detail=s[4]<32 || s[7]<32;
    for(u32 i=0;i<16;i++){event[i]=0;LOOP_PAD();}
    event[0]=s[3]+1;event[1]=vd_fd_scene();event[3]=(u32)flags;
    event[14]=vd_open_context;
    if(detail){
        vd_open_masks(event+6,event+7);
        u32 hash=2166136261u;
        for(u32 i=0;path[i] && i<1024;i++){hash=(hash^(u8)path[i])*16777619u;LOOP_PAD();}
        event[2]=hash;
    }
    s32 result=sceOpen(path,flags,permissions);
    event[4]=(u32)result;event[5]=vd_open_word(0x202b0ec0);
    s[3]++;if(result<0){s[6]++;if(result==-11){s[10]++;if(!event[5])s[14]++;}else if(result==-19)s[11]++;else s[12]++;}else s[9]++;
    if(detail){
        vd_open_masks(event+8,event+9);
        event[10]=vd_open_word(0x2b0694);
        if(event[10]<32){event[11]=vd_fd_word(event[10]*4+1);event[12]=vd_fd_word(event[10]*4);}
        event[13]=vd_open_word(0x23619c);event[15]=vd_open_word(0x2b028c);
        if(event[6]&event[9])s[15]++;
    }
    if(s[4]<32){for(u32 i=0;i<16;i++){vd_fd_data.first[s[4]][i]=event[i];LOOP_PAD();}s[4]++;}else s[5]++;
    if(result<0){
        if(s[7]<32){for(u32 i=0;i<16;i++){vd_fd_data.failed[s[7]][i]=event[i];LOOP_PAD();}s[7]++;}else s[8]++;
        if(s[6]==1){
            u32 i;for(i=0;i<159 && path[i];i++){vd_fd_data.failed_path[i]=(u8)path[i];LOOP_PAD();}
            vd_fd_data.failed_path[i]=0;
        }
    }
    return result;
}
static void vd_fd_capture(u32 slot)
{
    if(vd_fd_mask&(1u<<slot))return;
    for(u32 i=0;i<128;i++){vd_fd_data.table[slot][i]=vd_fd_word(i);LOOP_PAD();}
    vd_fd_phase[slot]=vd_fd_scene();vd_fd_mask|=1u<<slot;
}
const void *vd_probe_fd_snapshot(u32 *bytes,u32 *mask,u32 *phases,s32 *first)
{
    vd_fd_data.opens[0]=1;vd_fd_data.opens[1]=64;vd_fd_data.opens[2]=32;
    *bytes=sizeof vd_fd_data;*mask=vd_fd_mask;*first=vd_fd_first;
    for(u32 i=0;i<4;i++){phases[i]=vd_fd_phase[i];LOOP_PAD();}
    return &vd_fd_data;
}
#else
#define vd_file_open sceOpen
#endif

static struct { const ext_t *e; s32 fd; u32 age; } fds[NFD];
static u32 clock;
static u32 st_lba;                     /* stream cursor */
static u32 read_error;                 /* completed request status, sceCdGetError */
#if defined(EXIT_TRACE) || defined(PREP_PROBE) || defined(PREVIEW_TRACE) || defined(PREP_WATCH)
/* RAM breadcrumbs only; no added requests, waits or persistence. Diagnostic
 * build only. Stage, LBA, bytes, return value, error, reserved. */
volatile u32 exit_io[6];
#define EXIT_IO(s) (exit_io[0]=(s))
#else
#define EXIT_IO(s) ((void)0)
#endif
#ifdef PREVIEW_TRACE
#define PREVIEW_IO(s) do{exit_io[0]=(s);exit_io[5]=(u32)(unsigned long)__builtin_return_address(0);}while(0)
#else
#define PREVIEW_IO(s) ((void)0)
#endif
#define VD_READ_ERROR 0x30u             /* SCECdErREAD */
#ifdef FILE_IO_LOG
#include "io_probe.h"
static void io_count(unsigned);
#define IO_COUNT(n) io_count(n)
#else
#define IO_COUNT(n) ((void)0)
#endif

#ifdef LOAD_PROFILE
#define find find_profile_body
#endif
static const ext_t *find(u32 lba)
{
    u32 i, lo, hi;
    for (i = 0; i < vd.n_mod; i++) {
        const ext_t *e = &vd.mod[i];
        if (lba >= e->lba && lba < e->lba + e->sectors) return e;
        LOOP_PAD();
    }
    lo = 0; hi = vd.n_base;
    while (lo < hi) {                  /* last base extent with e->lba <= lba */
        u32 mid = (lo + hi) >> 1;
        if (vd.base[mid].lba <= lba) lo = mid + 1; else hi = mid;
    }
    if (lo && lba < vd.base[lo - 1].lba + vd.base[lo - 1].sectors) return &vd.base[lo - 1];
    return 0;
}

#ifdef LOAD_PROFILE
#undef find
static const ext_t *find(u32 lba){unsigned long long span=lp_begin();const ext_t *e=find_profile_body(lba);lp_end(LP_LOOKUP,span,0,lba,0);return e;}
#endif

/* next extent start above lba (to know how far a zero gap goes) */
#ifdef SPR_ASYNC
/* Immutable extent metadata only; no lock, client call or capture side effect. */
u32 vd_extent_size(u32 lba){
#ifdef LOAD_PROFILE
 const ext_t *e=find_profile_body(lba);
#else
 const ext_t *e=find(lba);
#endif
 return e&&e->lba==lba?e->size:0;
}
#endif

static u32 next_start(u32 lba)
{
    u32 i, best = 0xffffffff;
    for (i = 0; i < vd.n_mod; i++) {
        if (vd.mod[i].lba > lba && vd.mod[i].lba < best) best = vd.mod[i].lba;
        LOOP_PAD();
    }
    for (i = 0; i < vd.n_base; i++) {
        if (vd.base[i].lba > lba) { if (vd.base[i].lba < best) best = vd.base[i].lba; break; }
        LOOP_PAD();
    }
    return best;
}

static s32 open_ext(const ext_t *e)
{
    int i, old = 0;
#ifdef LOAD_PROFILE
    lp_io_key=e->lba;
#endif
    for (i = 0; i < NFD; i++) {
        if (fds[i].e == e && fds[i].fd >= 0) { IO_COUNT(IO_HITS); fds[i].age = ++clock; return fds[i].fd; }
        LOOP_PAD();
    }
    for (i = 1; i < NFD; i++) { if (fds[i].age < fds[old].age) old = i; LOOP_PAD(); }
    IO_COUNT(IO_MISSES);
#ifdef AUDIO_PROBE
    vd_fd_capture(0);
#endif
    if (fds[old].e && fds[old].fd >= 0) {
        IO_COUNT(IO_EVICTIONS);EXIT_IO(10);
#ifdef LOAD_PROFILE
        unsigned long long close_span=lp_begin();
#endif
        s32 closed=sceClose(fds[old].fd);
#ifdef LOAD_PROFILE
        lp_end(LP_CLOSE,close_span,0,fds[old].e->lba,closed<0);
#endif
        if(closed<0)IO_COUNT(IO_CLOSE_FAIL);
        EXIT_IO(11);
    }
    fds[old].e = e;
    EXIT_IO(3);
    BJ_IO(0x114170,0,e->lba);
#ifdef LOAD_PROFILE
    unsigned long long open_span=lp_begin();
#endif
#ifdef AUDIO_PROBE
    vd_open_context=e->lba;
#endif
#if defined(SELECT_PROFILE) || defined(SPR_ASYNC)
    char path_buffer[256];const char *open_path=vd_extent_path(e,path_buffer);
    if(!open_path){fds[old].fd=-1;return -1;}
#else
    const char *open_path=vd.strings+e->path;
#endif
    fds[old].fd = vd_file_open(open_path, 1, 0);   /* O_RDONLY */
    if(fds[old].fd==-11 && vd_fio_ack()==0){
        /* Native FILEIO explicitly rejected this open before completion. On
         * MMCE+USB hardware, retaining an idle reader prevents a second open;
         * the same file succeeds when that reader is closed. Evict one owned
         * idle LRU under the existing vdisc lock, then retry ONCE. No fixed
         * one-file limit, global descriptor reset, polling or busy wait. */
        int victim=-1;
        for(i=0;i<NFD;i++){
            if(i!=old && fds[i].e && fds[i].fd>=0 &&
               (victim<0 || fds[i].age<fds[victim].age))victim=i;
            LOOP_PAD();
        }
        if(victim>=0){
            s32 cached=fds[victim].fd;
            fds[victim].e=0;fds[victim].fd=-1;fds[victim].age=0;
            IO_COUNT(IO_EVICTIONS);
            s32 closed=sceClose(cached);
            if(closed<0)IO_COUNT(IO_CLOSE_FAIL);
            else fds[old].fd=vd_file_open(open_path,1,0);
        }
    }
#ifdef AUDIO_PROBE
    if(!(vd_fd_mask&2u)){vd_fd_first=fds[old].fd;vd_fd_capture(1);}
#endif
#ifdef LOAD_PROFILE
    lp_end(LP_OPEN,open_span,0,e->lba,fds[old].fd<0);
#endif
    BJ_IO(0x114170,1,fds[old].fd);
    EXIT_IO(4);
    if(fds[old].fd<0)IO_COUNT(IO_OPEN_FAIL);
    fds[old].age = ++clock;
    return fds[old].fd;
}

#ifdef HOST_TEST
static void lock(void) {}
static void unlock(void) {}
#else
static s32 ksys(s32 n, void *arg)
{
    register s32 v1 __asm__("$3") = n;
    register void *a0 __asm__("$4") = arg;
    register s32 v0 __asm__("$2");
    __asm__ volatile("syscall" : "=r"(v0), "+r"(v1), "+r"(a0) : : "memory", "$1", "$5", "$6", "$7", "$8", "$9",
                     "$10", "$11", "$12", "$13", "$14", "$15", "$24", "$25");
    return v0;
}
static s32 sema = -1;
static void lock(void)
{
    if (sema < 0) {                                     /* CreateSema({count 1, max 1, init 1}) */
        static s32 desc[6] = { 1, 1, 1, 0, 0, 0 };
        sema = ksys(64, desc);
    }
    if (sema >= 0) ksys(68, (void *)(long)sema);       /* WaitSema */
}
static void unlock(void)
{
    if (sema >= 0) ksys(66, (void *)(long)sema);       /* SignalSema */
}
#endif

#ifdef SELECT_PROFILE
/* Snapshot boundary only. Never add a blocking wait to native navigation. */
u32 vd_select_export_info[4]; /* operation, native result, immediate ACK, own evictions */
#ifndef HOST_TEST
#define vd_select_fd_count() (((s32 (*)(void))0x1134d0)())
#endif
static s32 vd_select_open(const char *path,s32 flags,s32 permissions){
    s32 fd=vd_file_open(path,flags,permissions);
    /* One proven zero-ACK retry, under the caller's existing reader lock. */
    if(fd==-11 && vd_fio_ack()==0){
        s32 victim=-1;
        for(u32 j=0;j<NFD;j++){
            if(fds[j].e && fds[j].fd>=0 &&
               (victim<0 || fds[j].age<fds[victim].age))victim=(s32)j;
            LOOP_PAD();
        }
        if(victim>=0){
            s32 cached=fds[victim].fd;fds[victim].e=0;fds[victim].fd=-1;fds[victim].age=0;
            s32 closed=sceClose(cached);
            if(closed<0){vd_select_export_info[0]=5;vd_select_export_info[1]=(u32)closed;return -1008;}
            vd_select_export_info[3]++;fd=vd_file_open(path,flags,permissions);
        }
    }
    return fd;
}
int vd_select_trylock(void){
#ifdef HOST_TEST
 return 0;
#else
 return sema<0?-1:ksys(69,(void *)(long)sema);
#endif
}
void vd_select_unlock(void){unlock();}
#endif

/* Entered ONLY from native114364, after explicit zero-ACK rejection and after
 * DeleteSema. s3 is that operation's allocator-owned descriptor, not a guessed
 * free-mask difference. A completed/live/foreign entry is never altered. */
void vd_fio_reject_cleanup(u32 descriptor)
{
#if !defined(HOST_TEST) || defined(FIO_NATIVE_TEST)
#ifndef HOST_TEST
#define vd_fio_sema() (*(volatile s32 *)0x2361a8)
#define vd_fio_flags(p) (*(volatile u32 *)(unsigned long)((p)+4))
#define vd_fio_wait(s) ksys(68,(void *)(long)(s))
#define vd_fio_signal(s) ksys(66,(void *)(long)(s))
#endif
    if(descriptor<0x2b1780 || descriptor>=0x2b1980 || (descriptor&15))return;
    s32 id=vd_fio_sema();
    if(id<0 || vd_fio_wait(id)<0)return;
    if(vd_fio_flags(descriptor)==0x10000000u)vd_fio_flags(descriptor)=0;
    vd_fio_signal(id);
#else
    (void)descriptor;
#endif
}

#ifdef FILE_IO_LOG
#include "io_probe.c"
#endif

#ifdef LOAD_PROFILE
#define zero zero_profile_body
#endif
static void zero(u8 *p, u32 n)
{
    u32 *w = (u32 *)p;
    if (((u32)(unsigned long)p & 3) == 0) { for (; n >= 4; n -= 4) { *w++ = 0; LOOP_PAD(); } p = (u8 *)w; }
    for (; n; n--) { *p++ = 0; LOOP_PAD(); }
}

#ifdef LOAD_PROFILE
#undef zero
static void zero(u8 *p,u32 n){unsigned long long span=lp_begin();zero_profile_body(p,n);lp_end(LP_ZERO,span,n,0,0);}
#endif

/* Exact file payload, allowing positive short transfers. EOF before the
 * advertised size is an error; only declared sector tails/gaps are padding. */
static s32 payload(s32 fd, u32 off, u8 *buf, u32 n, u32 *done)
{
    *done=0;
    if (fd<0)return -1;
    EXIT_IO(5);
    BJ_IO(0x114578,0,off);
#ifdef LOAD_PROFILE
    unsigned long long seek_span=lp_begin();
#endif
    s32 position=sceLseek(fd,off,0);
#ifdef LOAD_PROFILE
    lp_end(LP_SEEK,seek_span,0,lp_io_key,position!=(s32)off);
#endif
    BJ_IO(0x114578,1,position);EXIT_IO(6);
    if(position!=(s32)off){IO_COUNT(IO_SEEK_FAIL);return -1;}
    while (*done<n) {
        IO_COUNT(IO_READ_CALLS);
        EXIT_IO(7);
        BJ_IO(0x1147b8,0,n-*done);
#ifdef LOAD_PROFILE
        unsigned long long read_span=lp_begin();
#endif
        VB_READ(0,5);
        s32 got=sceRead(fd,buf+*done,n-*done);
        VB_READ(7,got);VB_READ(0,6);
#ifdef LOAD_PROFILE
        lp_end(LP_READ,read_span,got>0?(u32)got:0,lp_io_key,got<=0 || (u32)got>n-*done);
#endif
        BJ_IO(0x1147b8,1,got);
        EXIT_IO(8);
#if defined(EXIT_TRACE) || defined(PREP_PROBE) || defined(PREVIEW_TRACE) || defined(PREP_WATCH)
        exit_io[3]=got;
#endif
        if(got<=0 || (u32)got>n-*done){
            if(!got)IO_COUNT(IO_EOF);else IO_COUNT(IO_READ_FAIL);return -1;
        }
        if((u32)got<n-*done)IO_COUNT(IO_SHORT_READ);
        *done+=(u32)got;
        LOOP_PAD();
    }
    return 0;
}

/* Blocking, accepted CD request. `completed` never counts a failed sector. */
static s32 vread(u32 lba, u32 sectors, u8 *buf, u32 *completed)
{
    s32 err = 0;
    u32 done=0;
    EXIT_IO(1);
#ifdef LOAD_PROFILE
    unsigned long long lock_span=lp_begin();
#endif
    lock();
#ifdef LOAD_PROFILE
    lp_end(LP_LOCK,lock_span,0,lba,0);
#endif
    EXIT_IO(2);
#if defined(EXIT_TRACE) || defined(PREP_PROBE) || defined(PREVIEW_TRACE) || defined(PREP_WATCH)
    exit_io[1]=lba;exit_io[2]=sectors*2048;
#endif
#ifdef FILE_IO_LOG
    io_probe_event event;io_begin(&event,lba,sectors*2048,1);
#endif
    vd.reads++; vd.last_lba = lba; vd.last_sectors = sectors;
    #ifdef USB_BOOT_TEXT
    vd_boot_read_active=1;
#endif
    VB_IO(0,2);VB_IO(1,vd.reads);VB_IO(3,lba);VB_IO(4,sectors);VB_IO(5,0);VB_IO(6,0);
    if(sectors>0x1fffffu || lba+sectors<lba) { err=1; sectors=0; }
    while (sectors) {
        const ext_t *e = find(lba);
        u32 n;
        if (!e) {                                       /* gap: zeros up to the next extent */
            u32 nx = next_start(lba);
            n = nx - lba < sectors ? nx - lba : sectors;
            zero(buf, n * 2048);
        } else {
            u32 off = (lba - e->lba) * 2048, avail = 0, want;
            u32 got = 0;
            n = e->lba + e->sectors - lba;
            if (n > sectors) n = sectors;
            want = n * 2048;
            if (off < e->size) avail = e->size - off;
            if (avail > want) avail = want;
            #ifdef USB_BOOT_TEXT
            VB_IO(0,3);
            s32 fd=avail?open_ext(e):-1;VB_IO(7,fd);VB_IO(0,4);
            if (avail && payload(fd,off,buf,avail,&got)) {
#else
            if (avail && payload(open_ext(e),off,buf,avail,&got)) {
#endif
                /* Preserve complete earlier sectors, zero the unread tail,
                 * leave streaming cursor at the first incomplete sector. */
                zero(buf+got,want-got);
                done+=got/2048;
                sectors-=n;buf+=want;
                zero(buf,sectors*2048);
                err=1;break;
            }
            if (got < want) zero(buf + got, want - got);
        }
        done+=n;
        lba += n; buf += n * 2048; sectors -= n;
    }
    if (err) vd.errors++;
    read_error=err?VD_READ_ERROR:0;
#ifdef FILE_IO_LOG
    io_end(&event,done*2048,read_error);
#endif
    VB_IO(5,done*2048);VB_IO(6,read_error);VB_IO(2,vd.reads);VB_IO(0,9);
    if(completed)*completed=done;
    EXIT_IO(9);
#if defined(EXIT_TRACE) || defined(PREP_PROBE) || defined(PREVIEW_TRACE) || defined(PREP_WATCH)
    exit_io[4]=read_error;
#endif
#ifdef USB_BOOT_TEXT
    vd_boot_read_active=0;
#endif
    unlock();
    PREVIEW_IO(10); /* SignalSema returned; stage9 alone does not prove this. */
    return err;
}

/* n bytes at byte `off` of the extent starting at lba (any thread); returns the bytes read, the rest zeroed */
s32 vd_pread(u32 lba, u32 off, u32 n, void *buf)
{
    const ext_t *e;
    s32 got = 0, fd;
#ifdef LOAD_PROFILE
    unsigned long long lock_span=lp_begin();
#endif
    lock();
#ifdef LOAD_PROFILE
    lp_end(LP_LOCK,lock_span,0,lba,0);
#endif
#ifdef FILE_IO_LOG
    io_probe_event event;io_begin(&event,lba,n,2);
#endif
    e = find(lba);
    if (e) {
        off += (lba - e->lba) * 2048;
        if (off < e->size) {
            u32 count=0,want=e->size-off<n?e->size-off:n;
            fd=open_ext(e);
            if(payload(fd,off,buf,want,&count)){
                vd.errors++;
#ifdef FILE_IO_LOG
                event.error=VD_READ_ERROR;
#endif
            }
            got=count;
        }
    }
    if ((u32)got < n) zero((u8 *)buf + got, n - got);
#ifdef FILE_IO_LOG
    io_end(&event,got,event.sequence?event.error:0);
#endif
    unlock();
    return got;
}

/* ---- libcdvd replacements (signatures as the game calls them) ---- */

s32 vd_read(u32 lba, u32 sectors, void *buf, void *mode) { (void)mode; vread(lba, sectors, buf,0); PREVIEW_IO(11);return 1; }
s32 vd_sync(s32 mode) { (void)mode;PREVIEW_IO(13);return 0; }   /* 0 = done */
s32 vd_ready(s32 mode) { (void)mode;PREVIEW_IO(14);return 2; }  /* SCECdComplete */
s32 vd_error(void) { PREVIEW_IO(12);return read_error; }
s32 vd_seek(u32 lba) { (void)lba; return 1; }
s32 vd_st_init(u32 bufmax, u32 bankmax, void *buf) { (void)bufmax; (void)bankmax; (void)buf; return 1; }
s32 vd_st_start(u32 lba, void *mode) { (void)mode; st_lba = lba; read_error=0; return 1; }
s32 vd_st_seek(u32 lba) { st_lba = lba; return 1; }
s32 vd_st_stop(void) { return 1; }
s32 vd_st_read(u32 sectors, void *buf, u32 mode, u32 *err)
{
    (void)mode;
    u32 completed;
    vread(st_lba, sectors, buf,&completed);
    st_lba += completed;
    if (err) *err = read_error;
    return completed;
}

#if !defined(HOST_TEST) || defined(VDISC_CONFIG_TEST)
#include "menu_settings.h"
#ifndef HOST_TEST
#define mm_native_write ((s32 (*)(s32,const void *,s32))0x114a28)
#endif
/* Same native zero-ACK resource rejection as ordinary asset opens. Settings
 * are opened only under the vdisc lock in the select modal. Release at most
 * ONE idle reader owned by this cache, then retry ONCE; never close a foreign
 * descriptor or retry a completion error/uncertain close. */
static s32 vd_cfg_open(const char *path,s32 flags,s32 permissions)
{
    s32 fd=sceOpen(path,flags,permissions);
    if(fd==-11 && vd_fio_ack()==0){
        s32 victim=-1;
        for(u32 i=0;i<NFD;i++){
            if(fds[i].e && fds[i].fd>=0 &&
               (victim<0 || fds[i].age<fds[victim].age))victim=(s32)i;
            LOOP_PAD();
        }
        if(victim>=0){
            s32 cached=fds[victim].fd;
            fds[victim].e=0;fds[victim].fd=-1;fds[victim].age=0;
            if(sceClose(cached)==0)fd=sceOpen(path,flags,permissions);
        }
    }
    return fd;
}
static int vd_cfg_read(void *ctx,const char *path,void *out,unsigned cap)
{
    (void)ctx;s32 fd=vd_cfg_open(path,1,0);if(fd<0)return -101;
    /* MMCE firmware reports an error when the first read chunk has no data
     * (including an empty journal/EOF), unlike POSIX. Retail FILEIO preserves
     * that error; fileXio often masks it as zero bytes. Never probe EOF or ask
     * for dummy data: bound every request by this open file's checked length. */
    s32 length=sceLseek(fd,0,2),error=0;
    unsigned n=0;
    if(length<0)error=-104;
    else if((unsigned)length>cap)error=-3;
    else if(sceLseek(fd,0,0)!=0)error=-104;
    else while(n<(unsigned)length){
        unsigned remaining=(unsigned)length-n;
        s32 r=sceRead(fd,(u8 *)out+n,remaining);
        if(r<=0 || (unsigned)r>remaining){error=-102;break;}
        n+=(unsigned)r;
    }
    s32 closed=sceClose(fd);
    if(error)return error;
    if(closed!=0)return -103;
    return (int)n;
}
static int vd_cfg_write(void *ctx,const char *path,const void *data,unsigned n)
{
    (void)ctx;s32 fd=vd_cfg_open(path,0x602,0666);if(fd<0)return fd;
    unsigned used=0;int error=0;
    while(used<n){s32 r=mm_native_write(fd,(const u8 *)data+used,n-used);if(r<=0 || (unsigned)r>n-used){error=-1;break;}used+=(unsigned)r;}
    if(sceClose(fd)!=0)error=-1;return error;
}
/* Explicit modal Exit only; callers ensure SELECT, no pending asset queue.
 * Ordinary vdisc readers retain this same lock. No extra PLAY work. */
s32 vd_ui_read(const char *path,void *out,u32 cap)
{lock();s32 r=vd_cfg_read(0,path,out,cap);unlock();return r;}
s32 vd_config_load(const char *path,char *out,u8 *scratch)
{
    struct mm_cfg_io io={0,vd_cfg_read,vd_cfg_write};lock();
    s32 r=mm_cfg_load(&io,path,out,scratch);unlock();return r;
}
s32 vd_config_commit(const char *path,const char *data,u32 n,u8 *scratch)
{
    struct mm_cfg_io io={0,vd_cfg_read,vd_cfg_write};lock();
    s32 r=mm_cfg_commit(&io,path,data,n,scratch);unlock();return r;
}
#include "usb_hid/profile_store.h"
static int vd_controller_read(void *ctx,const char *path,void *out,unsigned cap)
{
    /* Never infer a missing file from MMCEMAN's ambiguous open error. Create
     * without truncation/writes, close, then read its known extent. */
    s32 fd=vd_cfg_open(path,0x202,0666);if(fd<0)return -101;
    if(sceClose(fd))return -103;
    return vd_cfg_read(ctx,path,out,cap);
}
s32 vd_controller_load(const char *root,mu_saved *out)
{
    struct mu_store_io io={0,vd_controller_read,vd_cfg_write};lock();
    s32 r=mu_store_load(&io,root,out);unlock();return r;
}
s32 vd_controller_save(const char *root,const mu_saved *old,const mu_saved_player *player,u32 side)
{
    struct mu_store_io io={0,vd_controller_read,vd_cfg_write};lock();
    s32 r=mu_store_save(&io,root,old,player,side);unlock();return r;
}
#endif

#ifdef INPUT_LOG
/* Diagnostic export only after RESULT exit, sharing the retail fileio lock.
 * Exclusive create means previous captures survive restarts and failed writes.
 * Return error leaves the RAM capture frozen for a later retry. */
#ifndef HOST_TEST
#define sceWrite ((s32 (*)(s32,const void *,s32))0x114a28)
#endif
u32 vd_probe_reads(void){return vd.reads;}
#ifdef AUDIO_PROBE
/* Last native FILEIO return, for the RESULT-only diagnostic panel. */
s32 vd_probe_native_error;
u32 vd_probe_handles,vd_probe_path_hash;
s32 vd_probe_read_error;
#ifndef HOST_TEST
#define vd_probe_fd_count() (((s32 (*)(void))0x1134d0)())
#endif
#endif
#if defined(SAVE_PROFILE) && !defined(HOST_TEST)
#define vd_save_lock() (sema<0?-1:ksys(68,(void *)(long)sema))
#define vd_save_fd_count() (((s32 (*)(void))0x1134d0)())
#endif
s32 vd_probe_export(const char *prefix,u32 *serial,const void *header,u32 hn,const void *data,u32 dn)
{
    char path[160];u32 i,k,len=0;s32 fd=-1,r=-1;
#ifdef AUDIO_PROBE
    vd_probe_native_error=0;
    vd_probe_handles=vd_probe_path_hash=0;vd_probe_read_error=0;
#endif
    if(!prefix || !serial)return -1;
    while(prefix[len] && len<140){path[len]=prefix[len];len++;LOOP_PAD();}
    if(prefix[len] || len+9>=sizeof path)return -2;
#if defined(SAVE_PROFILE)
    int save_report=hn==128 && ((const u8 *)header)[4]=='S' && ((const u8 *)header)[5]=='V';
    if(save_report){
        /* Diagnostic callers are completed-MC/idle-menu boundaries. Queue like
         * ordinary vdisc readers: repeated PollSema can starve behind the CD
         * thread even with periodic retries. No PLAY export is introduced. */
        if(vd_save_lock()<0)return -8;
        /* Release only virtual-disc-owned idle readers if the retail client
         * cannot spare two slots. This is after SAVE/LOAD, outside timing. */
        for(u32 f=0;f<NFD && vd_save_fd_count()>=30;f++)if(fds[f].e && fds[f].fd>=0){
            s32 cached=fds[f].fd;fds[f].e=0;fds[f].fd=-1;fds[f].age=0;
            if(sceClose(cached)<0){r=-8;goto done;}
        }
    }else lock();
#else
    lock();
#endif
#ifdef SELECT_PROFILE
    /* Frozen SELECT only. The retail client has32 local slots; leave room
     * for the probe/create transaction, retiring ONLY this cache's readers
     * under its existing lock. A failed close aborts, never retries. */
    for(u32 j=0;j<4;j++){vd_select_export_info[j]=0;LOOP_PAD();}
    for(u32 j=0;j<NFD && vd_select_fd_count()>=30;j++)if(fds[j].e && fds[j].fd>=0){
        s32 cached=fds[j].fd;fds[j].e=0;fds[j].fd=-1;fds[j].age=0;
        s32 closed=sceClose(cached);vd_select_export_info[0]=5;vd_select_export_info[1]=(u32)closed;
        if(closed<0){r=-8;goto done;}vd_select_export_info[3]++;
    }
#endif
#ifdef AUDIO_PROBE
    /* Export is an idle RESULT boundary. Release ONLY virtual-disc-owned read
     * handles while holding the same lock as every vdisc reader. Native EE
     * sceOpen has32 local slots and returns-19 if none is free (113550/114170).
     * A diagnostic needs a spare slot even if cached reads already hold theirs.
     * Subsequent native reads reopen normally; PLAY policy/LRU stays unchanged. */
    if(!(vd_fd_mask&4u))for(u32 f=0;f<NFD;f++){
        vd_fd_data.cache[f*4]=(u32)(unsigned long)fds[f].e;
        vd_fd_data.cache[f*4+1]=(u32)fds[f].fd;vd_fd_data.cache[f*4+2]=fds[f].age;
        LOOP_PAD();
    }
    vd_fd_capture(2);
    vd_probe_handles=((u32)vd_probe_fd_count()&255u)<<16;
    u32 released=0;
    for(u32 f=0;f<NFD;f++)if(fds[f].e && fds[f].fd>=0){
        s32 cached=fds[f].fd;fds[f].e=0;fds[f].fd=-1;fds[f].age=0;
        s32 closed=sceClose(cached);
        if(closed<0){vd_probe_native_error=closed;r=-8;goto done;}
        released++;LOOP_PAD();
    }
    vd_probe_handles|=(((u32)vd_probe_fd_count()&255u)<<8)|released;
    vd_fd_capture(3);
    vd_fd_data.opens[13]=1; /* Freeze before export itself opens any files. */
#endif
    for(i=*serial+1;i<=9999;i++){
        k=i;for(int j=3;j>=0;j--){path[len+j]=(char)('0'+k%10);k/=10;LOOP_PAD();}
        path[len+4]='.';path[len+5]='b';path[len+6]='i';path[len+7]='n';path[len+8]=0;
#ifdef AUDIO_PROBE
        vd_probe_path_hash=2166136261u;
        for(u32 h=0;path[h];h++){vd_probe_path_hash=(vd_probe_path_hash^(u8)path[h])*16777619u;LOOP_PAD();}
#endif
        /* Probe existing paths first: PCSX2 HostFs implementations may not
         * implement every O_EXCL detail. Only create after a failed read-open. */
        #ifdef SELECT_PROFILE
        fd=vd_select_open(path,1,0);if(fd==-1008){r=-8;goto done;}
#else
        fd=vd_file_open(path,1,0);
#endif
#ifdef SELECT_PROFILE
        vd_select_export_info[0]=1;vd_select_export_info[1]=(u32)fd;vd_select_export_info[2]=vd_fio_ack();
#endif
#ifdef AUDIO_PROBE
        vd_probe_read_error=fd;
#endif
        if(fd>=0){if(sceClose(fd)<0){r=-3;goto done;}continue;}
        #ifdef SELECT_PROFILE
        fd=vd_select_open(path,0xa02,0666);if(fd==-1008){r=-8;goto done;}
#else
        fd=vd_file_open(path,0xa02,0666);
#endif /* FIO_WRONLY|CREAT|EXCL, never TRUNC */
#ifdef SELECT_PROFILE
        vd_select_export_info[0]=2;vd_select_export_info[1]=(u32)fd;vd_select_export_info[2]=vd_fio_ack();
#endif
        if(fd<0){
#ifdef AUDIO_PROBE
            vd_probe_native_error=fd;
#endif
            r=-4;goto done;}
        *serial=i;r=0;break;
    }
    if(fd<0 || i>9999){r=-5;goto done;}
    s32 written=sceWrite(fd,header,hn);
#ifdef SELECT_PROFILE
    vd_select_export_info[0]=3;vd_select_export_info[1]=(u32)written;
#endif
    if(written!=(s32)hn)r=-6;
    const u8 *p=data;
    while(!r && dn){u32 n=dn>16384?16384:dn;
        written=sceWrite(fd,p,n);
#ifdef SELECT_PROFILE
        vd_select_export_info[1]=(u32)written;
#endif
        if(written!=(s32)n){r=-6;break;}
        p+=n;dn-=n;LOOP_PAD();
    }
#ifdef FILE_IO_LOG
    /* Version 2 keeps the pad records unchanged and appends a checksummed,
     * frozen I/O snapshot in the SAME exclusive file/capture transaction. */
    if(!r && hn==128 && ((const u8 *)header)[7]=='2') {
        const u8 *extra=(const u8 *)&io_capture;u32 left=sizeof io_capture;
        while(!r && left){u32 n=left>16384?16384:left;
            if(sceWrite(fd,extra,n)!=(s32)n){r=-6;break;}
            extra+=n;left-=n;
        }
    }
#endif
    s32 closed=sceClose(fd);
#ifdef SELECT_PROFILE
    if(!r || closed<0){vd_select_export_info[0]=4;vd_select_export_info[1]=(u32)closed;}
#endif
    if(closed<0)r=-7;
done:
    unlock();return r;
}
#endif

#if defined(EXIT_TRACE) || defined(PREP_PROBE) || defined(PREVIEW_TRACE)
/* Separate exit-only journal. Each128B record is written and closed before
 * returning. No truncation, live pad capture or changes to the CD read path.
 * Poll the shared fileio lock: diagnostic contention disables the journal
 * rather than introducing another potentially unbounded semaphore wait. */
#ifndef HOST_TEST
static s32 exit_trylock(void)
{
    if(sema<0)return -1;
    return ksys(69,(void *)(long)sema); /* PollSema, never WaitSema */
}
#else
#ifndef exit_trylock
#define exit_trylock() 0
#endif
#endif
#ifdef PREVIEW_TRACE
/* Exact final native open result and immediate ACK, diagnostic only. Keep
 * these independent of the worker's record buffer and its generic -4 code. */
volatile s32 preview_journal_open[2];
static s32 preview_journal_file_open(const char *path,s32 flags,s32 permissions)
{
    /* Same one-owned-idle-reader retry as modal settings. The caller already
     * holds the vdisc lock; only zero-ACK -11 permits eviction, never a
     * completion failure, uncertain close or foreign descriptor. */
    s32 fd=vd_cfg_open(path,flags,permissions);
    preview_journal_open[0]=fd;preview_journal_open[1]=(s32)vd_fio_ack();
    return fd;
}
#define EXIT_OPEN preview_journal_file_open
#else
#define EXIT_OPEN vd_file_open
#endif
s32 vd_exit_append(const char *prefix,u32 *serial,const void *record,u32 bytes)
{
    char path[160];u32 len=0,i,k;s32 fd=-1,r=0;
    if(!prefix || !serial || bytes!=128)return -1;
    while(len<140 && prefix[len]){path[len]=prefix[len];len++;LOOP_PAD();}
    if(prefix[len] || len+9>=sizeof path || *serial>9999)return -2;
    if(exit_trylock()<0)return -8;
    for(i=*serial?*serial:1;i<=9999;i++) {
        k=i;for(s32 j=3;j>=0;j--){path[len+j]='0'+k%10;k/=10;LOOP_PAD();}
        path[len+4]='.';path[len+5]='b';path[len+6]='i';path[len+7]='n';path[len+8]=0;
        if(*serial) {
            fd=EXIT_OPEN(path,2,0); /* existing file only; never CREAT/TRUNC */
            if(fd<0){r=-4;goto done;}
            s32 end=sceLseek(fd,0,2);
            if(end<128 || end%128 || end>=128*256){r=-5;break;}
            break;
        }
        fd=EXIT_OPEN(path,1,0);
        if(fd>=0){if(sceClose(fd)<0){r=-3;goto done;}fd=-1;continue;}
        fd=EXIT_OPEN(path,0xa02,0666); /* exclusive new journal */
        if(fd<0){r=-4;goto done;}
        *serial=i;break;
    }
    if(fd<0){r=-4;goto done;}
    if(!r && sceWrite(fd,record,bytes)!=(s32)bytes)r=-6;
    if(sceClose(fd)<0)r=-7;
done:
    unlock();return r;
}
#endif

#if defined(LOAD_PROFILE) && !defined(SELECT_PROFILE)
/* Dedicated export, never called from PLAY. Profiling freezes under the same
 * reader lock, after native asset/stream queues are idle. No nested lock or
 * INPUT2 trailer. Failure preserves RAM for retry; filenames never truncate. */
extern const void *lp_freeze(unsigned *);
extern void lp_make_header(unsigned *,const void *,unsigned);
s32 vd_load_export(const char *prefix,u32 *serial)
{
 char path[160];u32 len=0,i,k,bytes;s32 fd=-1,r=-1;
 if(!prefix || !serial)return -1;
 while(len<140 && prefix[len]){path[len]=prefix[len];len++;LOOP_PAD();}
 if(prefix[len] || len+9>=sizeof path)return -2;
 lock();
 const void *snapshot=lp_freeze(&bytes);u32 header[32];lp_make_header(header,snapshot,bytes);
 for(i=*serial+1;i<=9999;i++){
  k=i;for(s32 j=3;j>=0;j--){path[len+j]='0'+k%10;k/=10;LOOP_PAD();}
  path[len+4]='.';path[len+5]='b';path[len+6]='i';path[len+7]='n';path[len+8]=0;
  fd=vd_file_open(path,1,0);
  if(fd>=0){if(sceClose(fd)<0){r=-3;goto done;}continue;}
  fd=vd_file_open(path,0xa02,0666);if(fd<0){r=-4;goto done;}*serial=i;r=0;break;
 }
 if(fd<0 || i>9999){r=-5;goto done;}
 if(sceWrite(fd,header,128)!=128)r=-6;
 const u8 *p=snapshot;
 while(!r && bytes){u32 n=bytes>16384?16384:bytes;if(sceWrite(fd,p,n)!=(s32)n){r=-6;break;}p+=n;bytes-=n;LOOP_PAD();}
 if(sceClose(fd)<0)r=-7;
done:unlock();return r;
}
#endif
