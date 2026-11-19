/* Off-disc launcher for beatmania IIDX 16 EMPRESS.
 *
 * Runs from the offdisc root (PCSX2: boot LAUNCHER.ELF with the host filesystem on; host: = this folder):
 *   EMPRESS/SLPM_552.21, EMPRESS/base.txt, EMPRESS/...   the game minus its songs (unpack.py, split_songs.py)
 *   launcher/vdisc.bin, launcher/vdisc.txt              runtime (virtual disc + song database) and its exe patches
 *   songs/styles.txt                                    style folders in menu order
 *   songs/<style>/style.txt                             "name <title>"; a style with "folder <label.spr>" gets its own
 *                                                       music-select folder (ids 60.., menu in styles.txt order) with
 *                                                       "caption <caption.spr>", "voice <n>", "bar <sprite>";
 *                                                       "custom" = the CUSTOM folder (59); "gamefolder 1" = songs stay
 *                                                       in the game's EMPRESS folder (menu position)
 *   songs/<style>/<song>/song.txt                       one song (tools/offdisc/songfolder.py, split_songs.py)
 *   mods.txt, mods/<x>/mod.txt                          optional patches (W / E lines); mods/480p* = video modes
 *   cache/                                              containers grown at boot (song title sprites)
 * Menu: video mode, mods, styles (on/off). START builds the song list and boots: the game ELF is loaded and patched,
 * the runtime + its tables go into the reserved block after the game's bss, then ExecPS2. Choices -> launcher.cfg.
 *
 * Songs: EMPRESS's own songs keep their ids, records and file ids; their song.txt maps the original sectors to the
 * folder's files and marks the id present (the runtime hides absent ones). Every other song is "added": slot 0..
 * (the game sees alias ids, bound in-game, songdb.h), record from record.bin, new file ids 1062.. at virtual sectors
 * VBASE.., its title card gets data1 index 463..,
 * its text sprites are packed into the wheel/title/genre/artist containers (new sheet, cont.c). */
#ifndef HOST_TEST                                 /* tools/offdisc/test_launcher.c builds build() on the PC */
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <iopcontrol.h>
#include <libpad.h>
#include <gsKit.h>
#include <dmaKit.h>
#include <malloc.h>
#if defined(MMCE_BENCH) || defined(PORTABLE_LAUNCHER)
#include <sys/stat.h>
#define NEWLIB_PORT_AWARE /* Retain fileXio stdio; config absence probes use raw Dread results. */
#include <fileXio_rpc.h>
#endif
#endif
#include <dirent.h>
#include <stdarg.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cont.h"
#include "input_probe.h"
#include "save_probe.h"
#include "song_resources.h"
#include "songdb.h"                               /* copy of tools/offdisc/songdb.h (build_launcher.sh) */

#include "hardware_boot.h"
#include "debug_config.h"
#include "menu_settings.h"
#include "profile_location.h"
static ml_target selected_profile={ML_SCHEMA,0,0,ML_OPEN,{0},{0},{0}};
static char selected_game_id[11]="PIZD-00000";
static int profile_prompt;
#include "customize_settings.h"
static struct cs_preferences shared_customize=CS_INITIALIZER;
static struct mm_options shared_options=MM_OPTIONS_INITIALIZER;
#include "load_profile_launcher.h"
#define RES_LO 0x7c4700u
#define RES_HI 0x944700u
#define TAB_HI (RES_LO + 0xd3000u)                /* 844 KB tables, 692 KB strings; heap/reserve unchanged */
#define MAXI 32
#define MAXSTYLE 24
#define MAXSONG 4096
#define FID0 1062                                 /* first added file id */
#define C_LBA 1219665u                            /* BM2DX16C.DAT: added file offsets are relative to it */
#define DATA1_LBA 134588u
#define DATA1_TAB 0x239c80u                       /* data1 entries {id, offset, sectors, ?} */
#define DATA1_PTRS 0x23b970u                      /* pointer per data1 index, 463 */
#define DATA1_N 463
#define VBASE 0x400000u                           /* virtual sectors for added files */
#define VGAP 512                                  /* zero sectors after each (movie streamer read-ahead) */
#define CUSTOM FOLD0                              /* music-select folder id of the CUSTOM folder */
#define NFTAB 80                                  /* relocated per-folder tables (bar, label, caption, voice) */

#ifndef HOST_TEST
#include "gfx.h"
#endif

typedef struct { u32 lba, sectors, size, path; } ext_t;


typedef struct { char dir[64], name[64]; int on; } item_t;
typedef struct {                                  /* songs/<dir>/style.txt */
    char dir[48], name[48], label[64], caption[64];   /* name, folder bar label .spr, folder caption .spr */
    int on, n;                                    /* enabled, songs */
    int custom, voice, bar, game;                 /* `custom` = the CUSTOM folder, folder voice / bar sprite,
                                                     `gamefolder N` = songs shown by the game's folder N */
    int fold;                                     /* our folder index (id FOLD0 + fold), NOFOLD = none */
} style_t;
typedef struct { char dir[64]; short style, id; } song_t;     /* id -1 = added song */

static item_t mods[MAXI];
static style_t styles[MAXSTYLE];
static song_t songs[MAXSONG];
static int input_mode,input_available;
#ifdef PORTABLE_LAUNCHER
static unsigned dl_location_changed;
static char dl_next_game[DL_ROOT],dl_location_notice[120];
#endif
static int debug_enabled,launcher_debug,save_profile_enabled,prep_watch_enabled;
#include "select_profiler/config.h"
static int select_profile_mode,select_profile_seconds=20,select_profile_osd;
static msp_config *select_runtime;
#ifdef PORTABLE_LAUNCHER
static const char *runtime_bin(void){return prep_watch_enabled?"launcher/vdisc-portable-prep.bin":debug_enabled?"launcher/vdisc-portable-debug.bin":"launcher/vdisc-portable.bin";}
static const char *runtime_txt(void){return prep_watch_enabled?"launcher/vdisc-portable-prep.txt":debug_enabled?"launcher/vdisc-portable-debug.txt":"launcher/vdisc-portable.txt";}
#else
static const char *runtime_bin(void){return prep_watch_enabled?"launcher/vdisc-prep.bin":select_profile_mode?"launcher/vdisc-select.bin":debug_enabled?"launcher/vdisc-debug.bin":"launcher/vdisc.bin";}
static const char *runtime_txt(void){return prep_watch_enabled?"launcher/vdisc-prep.txt":select_profile_mode?"launcher/vdisc-select.txt":debug_enabled?"launcher/vdisc-debug.txt":"launcher/vdisc.txt";}
#endif
#if defined(MMCE_BENCH) && !defined(HOST_TEST)
int msp_stage(msp_config *);
#endif

static u32 save_probe_address;
static const char *INPUT_MODES[]={"off","sample only","sample + export","I/O only + export"};
static int save_batch; /* optional autosave batching; native setting still enables/disables autosave */
static int save_format=1,save_config_error;
static u8 save_create_uuid[16];
static int n_mods, n_styles, n_songs, video;  /* video 0 = original 480i, 1 = 480p, 2 = 480p 480 lines */
static int lane_b = 10, beam_len = 10;          /* Skin settings in tenths: lane brightness 0.0-2.0, keybeam length
                                                   0.1-2.0 (1.0 = the game's own) */
static int note_skin, note_h = 10;              /* note skin: 0 = EMPRESS's own, k = nskins[k - 1]; note height in
                                                   tenths of EMPRESS's 6 rows, 0.5-3.0 */
#define SKIN_NOTE_MIN 5
#define SKIN_NOTE_MAX 30
typedef struct { char dir[16], name[40]; int rows; } nskin_t;   /* skins/notes/<dir>/ (mk_note_skins.py) */
static nskin_t nskins[32];
static int n_nskins;
static int frame_pack, frame_available;       /* bitmask of installed frame packs; only P1 wide SP */
static const char *FRAME_DIR[] = { 0, "djt_p1wide", "eighth_p1wide" };
static const char *FRAME_NAME[] = { "EMPRESS", "DJT P1 wide SP", "8th P1 wide SP" };
#define FRAME_COUNT 3
#define SKIN_LANE_MAX 20
#define SKIN_BEAM_MIN 1
#define SKIN_BEAM_MAX 20
static const char *VIDEO[] = { "480i (original)", "480p (448 lines)", "480p (all 480 lines)" };
static const char *VIDEO_DIR[] = { 0, "480p", "480p480" };
static char pad_buf[2][256] __attribute__((aligned(64)));
static char log_[16384];
#ifdef HARDWARE_BENCH
static int list_trace;
#endif

/* build progress screen (percent, step label); the PC harness has no screen */
#ifdef HOST_TEST
static void progress(int pct, const char *what) { (void)pct; (void)what; }
#else
static void progress(int pct, const char *what);
#endif

static int log_live;                              /* 1 = also append each line to launcher.log (during the build) */
static FILE *log_stream;
static char log_disk_buffer[4096];                 /* launcher-only; no allocation survives handoff */

static void logf_(const char *fmt, ...)
{
    if(!launcher_debug)return;
    va_list ap;
    char msg[512];
    size_t n = strlen(log_);
    va_start(ap, fmt);
    vsnprintf(msg,sizeof msg,fmt,ap);
    va_end(ap);
    size_t len=strlen(msg);
    if(n+len>=sizeof log_){
        char *p=strchr(log_+len,'\n');
        if(p)memmove(log_,p+1,strlen(p+1)+1);else log_[0]=0;
        n=strlen(log_);
    }
    strncat(log_,msg,sizeof log_-n-1);
    if (log_live && log_stream) fputs(msg, log_stream);
}

/* Keep one buffered descriptor. FAT close/sync per verbose line made physical
 * startup pay metadata/flush latency dozens of times. Flush at phase boundaries
 * and close before IOP reset; no descriptor crosses the handoff. */
static void log_start(void)
{
    if(!launcher_debug)return;
    if(log_stream)fclose(log_stream);
    log_stream = fopen(game_path("launcher.log"), "w");
    if(log_stream)setvbuf(log_stream,log_disk_buffer,_IOFBF,sizeof log_disk_buffer);
    log_live = 1;
}

static void log_save(int errs)
{
    log_live = 0;
    if (!log_stream) return;
    fprintf(log_stream, "--- build %s\n", errs ? "FAILED (errors above)" : "ok");
    fclose(log_stream);log_stream=0;
}

#include "file_meta.h"
static long fsize(const char *rel);

#ifdef LOAD_PROFILE
#define slurp slurp_profile_body
#endif
static char *slurp(const char *rel, int *len)
{
    char path[256];
    FILE *f;
    long n;
    char *b;
    lm_entry *meta=lm_find(rel,0);
    if(meta && meta->text){
        b=malloc(meta->size+1);if(!b)return 0;
        memcpy(b,meta->text,meta->size+1);if(len)*len=meta->size;lm_text_hits++;return b;
    }
    n=fsize(rel);if(n<0 || n>0x4000000)return 0;
    snprintf(path, sizeof path, "%s%s",DEV, rel);
#ifdef HARDWARE_BENCH
    if(list_trace)bench_boot_note("L-read %s",rel);
#endif
    if (!(f = fopen(path, "rb"))) return 0;
    b=malloc(n+1);
    if(b){
        if(fread(b,1,n,f)!=(size_t)n || ferror(f)){free(b);b=0;}
        else {b[n]=0;lm_text_keep(rel,b,n);}
    }
    fclose(f);
    if (len) *len = n;
    return b;
}
#ifdef LOAD_PROFILE
#undef slurp
static char * slurp(const char *rel,int *len){if(!lpl_enabled)return slurp_profile_body(rel,len);unsigned phase=lpl_phase;lpl_u64 t=lpl_clock();char * r=slurp_profile_body(rel,len);lpl_add(phase,LPL_SLURP,t,0,0);return r;}
#endif


static long file_size_query(const char *path)
{
    long n=-1;lm_queries++;
#if defined(LAUNCHER_FILE_SIZE) || ((defined(MMCE_BENCH)||defined(PORTABLE_LAUNCHER)) && !defined(HOST_TEST))
#ifdef LOAD_PROFILE
    lpl_u64 start=lpl_enabled?lpl_clock():0;
#endif
#ifdef LAUNCHER_FILE_SIZE
    n=LAUNCHER_FILE_SIZE(path);
#else
    struct stat st;
    if(!stat(path,&st) && S_ISREG(st.st_mode) && st.st_size>=0 && st.st_size<=0x7fffffff)n=st.st_size;
#endif
#ifdef LOAD_PROFILE
    if(lpl_enabled)lpl_add(lpl_phase,LPL_STAT,start,0,n<0);
#endif
    if(n>=0)return n;
#endif
    /* Other backends and an unsupported/failed stat retain the prior path. */
    FILE *f=fopen(path,"rb");if(!f)return -1;
    if(!fseek(f,0,SEEK_END))n=ftell(f);
    if(fclose(f))n=-1;
    return n;
}
static long fsize(const char *rel)
{
    lm_entry *meta=lm_find(rel,0);char path[256];long n;
    if(meta && meta->size>=0){lm_size_hits++;return meta->size;}
    if(snprintf(path,sizeof path,"%s%s",DEV,rel)>=(int)sizeof path)return -1;
    n=file_size_query(path);
    if(n>=0 && (meta=lm_find(rel,1)))meta->size=n;
    return n;
}

/* Generated files are immutable: an active game keeps byte offsets/paths in
 * EE memory even when a second launcher or the host verifier builds a new set.
 * Include two independent 32-bit hashes and the length in each cache name. */
#ifdef LOAD_PROFILE
#define cache_hash cache_hash_profile_body
#endif
static void cache_hash(u32 *a, u32 *b, const u8 *p, int n)
{
    while (n--) { *a = (*a ^ *p) * 16777619u; *b = (*b * 33u) ^ *p++; }
}
#ifdef LOAD_PROFILE
#undef cache_hash
static void cache_hash(u32 *a,u32 *b,const u8 *p,int n){if(!lpl_enabled){cache_hash_profile_body(a,b,p,n);return;}unsigned phase=lpl_phase;lpl_u64 t=lpl_clock();cache_hash_profile_body(a,b,p,n);lpl_add(phase,LPL_HASH,t,n,0);}
#endif


#ifdef LOAD_PROFILE
#define cache_publish cache_publish_profile_body
#endif
static int cache_publish(char *rel, int cap, const char *tag, const u8 *data, int len, const char *input)
{
    u8 buf[32768];
    u32 a=2166136261u, b=5381u, ca=2166136261u, cb=5381u;
    int n, total=0, good=0;
    char path[256];
    FILE *src=0, *f;
    if (input) {
        snprintf(path,sizeof path,"%s%s",DEV,input);
        if (!(src=fopen(path,"rb"))) return -1;
        while ((n=fread(buf,1,sizeof buf,src))>0) { cache_hash(&a,&b,buf,n);total+=n; }
        if (ferror(src) || total!=len) { fclose(src);return -1; }
        rewind(src);
    } else cache_hash(&a,&b,data,len);
    snprintf(rel,cap,"cache/%s_%08x%08x_%x.bin",tag,(unsigned)a,(unsigned)b,len);
    snprintf(path,sizeof path,"%s%s",DEV,rel);
    /* Verify existing contents too; do not truncate a file another game uses. */
    if ((f=fopen(path,"rb"))) {
        total=0;
        while ((n=fread(buf,1,sizeof buf,f))>0) { cache_hash(&ca,&cb,buf,n);total+=n; }
        good=!ferror(f) && total==len && ca==a && cb==b;
        fclose(f);
        if (src) fclose(src);
        if (!good) logf_("cache identity conflict: %s\n",rel);
        return good?0:-1;
    }
    if (!(f=fopen(path,"wb"))) { if(src)fclose(src);return -1; }
    if (src) {
        total=0;
        while ((n=fread(buf,1,sizeof buf,src))>0) {
            if (fwrite(buf,1,n,f)!=(size_t)n) break;
            total+=n;
        }
        good=total==len && !ferror(src);fclose(src);
    } else good=fwrite(data,1,len,f)==(size_t)len;
    if (fclose(f))good=0;
    if (!good)logf_("cannot write %s\n",rel);
    return good?0:-1;
}
#ifdef LOAD_PROFILE
#undef cache_publish
static int cache_publish(char *rel,int cap,const char *tag,const u8 *data,int len,const char *input){if(!lpl_enabled)return cache_publish_profile_body(rel,cap,tag,data,len,input);unsigned phase=lpl_phase;lpl_u64 t=lpl_clock();int r=cache_publish_profile_body(rel,cap,tag,data,len,input);lpl_add(phase,LPL_CACHE,t,len,r!=0);return r;}
#endif


/* next line (in place); returns 0 at the end */
static char *line(char **p)
{
    char *s = *p, *e;
    if (!s || !*s) return 0;
    e = strchr(s, '\n');
    if (e) { *e = 0; *p = e + 1; } else *p = s + strlen(s);
    if ((e = strchr(s, '\r'))) *e = 0;
    return s;
}

static int save_uuid_parse(const char *s)
{
    u8 id[16];unsigned any=0;if(strlen(s)!=32)return -1;
    for(unsigned i=0;i<32;i++){
        unsigned d=s[i]>='0' && s[i]<='9'?s[i]-'0':s[i]>='a' && s[i]<='f'?s[i]-'a'+10:s[i]>='A' && s[i]<='F'?s[i]-'A'+10:99;
        if(d>15)return -1;if(!(i&1))id[i/2]=d<<4;else id[i/2]|=d;
    }
    for(unsigned i=0;i<16;i++)any|=id[i];if(!any)return -1;
    memcpy(save_create_uuid,id,16);return 0;
}
/* Player state is never part of a prepared image. Resolve the selected runtime
 * anew and apply only this startup policy after BOTH cold and cached builds. */
static int save_profile_bind(void)
{
    save_probe_address=0;
    int len=0,seen=0,sp_seen=0,loc_seen=0;u32 at=0,sp_at=0,loc_at=0,loc_bytes=0;char *meta=slurp(runtime_txt(),&len),*p=meta,*l;
    long bytes=fsize(runtime_bin());
    if(!meta || !len || bytes<24 || bytes>RES_HI-RES_LO){free(meta);return -1;}
    while((l=line(&p))){
        if(!strncmp(l,"profile ",8)){
            unsigned abi;char extra;if(seen++ || sscanf(l+8,"%u %x %c",&abi,&at,&extra)!=2 || abi!=2){free(meta);return -1;}
        }else if(!strncmp(l,"profile_location ",17)){
            unsigned abi;char extra;
            if(loc_seen++ || sscanf(l+17,"%u %x %u %c",&abi,&loc_at,&loc_bytes,&extra)!=3 || abi!=ML_SCHEMA || loc_bytes!=sizeof(ml_target)){free(meta);return -1;}
        }else if(!strncmp(l,"saveprobe ",10)){
            unsigned abi,size;char extra;if(sp_seen++ || sscanf(l+10,"%u %x %u %c",&abi,&sp_at,&size,&extra)!=3 || abi!=1 || size!=sizeof(sp_control)){free(meta);return -1;}
        }
    }
    free(meta);
    if(sp_seen){
        if((sp_at&3) || sp_at<RES_LO || sp_at>RES_LO+(u32)bytes-sizeof(sp_control) || *(u32 *)(unsigned long)sp_at!=1)return -1;
        sp_control *probe=(sp_control *)(unsigned long)sp_at;
        save_probe_address=sp_at;
        probe->enabled=debug_enabled && save_profile_enabled;
        snprintf(probe->path,sizeof probe->path,"%slogs/save-",DEV);
#ifdef LOAD_PROFILE
        probe->count_lo=(u32)lpl_cal_count;probe->count_hi=(u32)(lpl_cal_count>>32);
        probe->bus_lo=(u32)lpl_cal_bus;probe->bus_hi=(u32)(lpl_cal_bus>>32);
        probe->samples=lpl_cal_samples;probe->bus_hz=(u32)lpl_hz();
#endif
    }else if(debug_enabled && save_profile_enabled){logf_("Save profiler needs matching diagnostic runtime\n");return -1;}
    if(!seen)return save_format==1?0:-1;
    if((at&3) || at<RES_LO || at>RES_LO+(u32)bytes-24 || *(u32 *)(unsigned long)at!=2)return -1;
    unsigned any=0;for(unsigned i=0;i<16;i++)any|=save_create_uuid[i];
    if(save_format==2 && (save_config_error || !any)){logf_("Compact saves require a nonzero saveuuid in launcher.cfg\n");return -1;}
    u32 *q=(u32 *)(unsigned long)at;q[1]=save_format;memcpy(q+2,save_create_uuid,16);
    if(!ml_valid(&selected_profile) || (selected_profile.managed && (save_format!=2 || !loc_seen)))return -1;
    if(loc_seen){
        if((loc_at&3) || loc_at<RES_LO || bytes<(int)loc_bytes || loc_at>RES_LO+(u32)bytes-loc_bytes || *(u32 *)(unsigned long)loc_at!=ML_SCHEMA)return -1;
        memcpy((void *)(unsigned long)loc_at,&selected_profile,sizeof selected_profile);
    }
    if(selected_profile.managed){
        /* These native strings are in the clean executable, never in a cached
         * selected profile. Keep the original format and maximum19B basename. */
        if(strcmp((char *)0x26a5b0,"BISLPM-55221BM2DX16"))return -1;
        memset((void *)0x26a5b0,0,20);strcpy((char *)0x26a5b0,selected_profile.directory);
    }
    logf_("Save format: %s\n",save_format==2?"compact A/B (legacy read-only import)":"legacy checkpoint v1");return 0;
}
#include "save_profile_launcher.h"


#ifdef USB_HID
#include "usb_runtime_layout.h"
static mui_runtime_layout usb_layout;
static int usb_runtime_resolve(void)
{
    const char *txt=runtime_txt();
    const char *bin=runtime_bin();
    int len=0;char *meta=slurp(txt,&len);long bytes=fsize(bin);
    int result=bytes>0 && bytes<=(long)(RES_HI-RES_LO)?mui_layout_parse(meta,len,RES_LO,(unsigned)bytes,2,&usb_layout):-1;
    free(meta);
    if(result || *(u32 *)(unsigned long)usb_layout.config!=2){
        usb_layout.config=usb_layout.frames=0;
        logf_("USB runtime layout invalid in %s; matching ABI2 runtime required\n",txt);return -1;
    }
    return 0;
}
static int usb_runtime_bind(unsigned player0,unsigned player1,unsigned *config,unsigned *frames)
{
    if(!usb_layout.config || !usb_layout.frames || *(u32 *)(unsigned long)usb_layout.config!=2)return -1;
    u32 *q=(u32 *)(unsigned long)usb_layout.config;q[1]=player0;q[2]=player1;
    *config=usb_layout.config;*frames=usb_layout.frames;return 0;
}
#endif

static void read_mods(void)
{
    char *b = slurp("mods.txt", 0), *p = b, *l;
    n_mods = 0;
    while ((l = line(&p)) && n_mods < MAXI) {
        char path[160], *m, *q, *ml;
        if (!*l || *l == '#') continue;
        strncpy(mods[n_mods].dir, l, 63);
        strncpy(mods[n_mods].name, l, 63);
        snprintf(path, sizeof path, "mods/%s/mod.txt", l);
        if ((m = slurp(path, 0))) {
            q = m;
            while ((ml = line(&q)))
                if (!strncmp(ml, "name ", 5)) { strncpy(mods[n_mods].name, ml + 5, 63); break; }
            free(m);
        }
        mods[n_mods].on = 0;
        n_mods++;
    }
    free(b);
}

/* ---- song folders ---- */
static int cmp_song(const void *a, const void *b)
{
    const song_t *x = a, *y = b;
    return x->style != y->style ? x->style - y->style : strcmp(x->dir, y->dir);
}

/* Generated releases use authoritative installed-set indexes. Developer roots
 * without the marker retain directory discovery and index fallback. */
static int list_style(int st)
{
    char path[160], *b, *p, *l;
    int n = 0, first = n_songs;
    char *marker=slurp("catalog/indexed.txt",0);
    int indexed=marker && !strcmp(marker,"1\n");
    free(marker);
    DIR *d;
    struct dirent *e;
    snprintf(path, sizeof path, "%ssongs/%s",DEV, styles[st].dir);
    if (!indexed && (d = opendir(path))) {
        while ((e = readdir(d)) && n_songs < MAXSONG) {
            if (e->d_name[0] == '.' || e->d_name[0] == '_') continue;
            strncpy(songs[n_songs].dir, e->d_name, 63);
            songs[n_songs].style = st; songs[n_songs].id = -2;
            n_songs++; n++;
        }
        closedir(d);
    }
    if (!n) {
        snprintf(path, sizeof path, "songs/%s/index.txt", styles[st].dir);
        if ((b = slurp(path, 0))) {
            p = b;
            while ((l = line(&p)) && n_songs < MAXSONG) {
                if (!*l || *l == '#') continue;
                strncpy(songs[n_songs].dir, l, 63);
                songs[n_songs].style = st; songs[n_songs].id = -2;
                n_songs++; n++;
            }
            free(b);
        }
    }
    /* keep folders with a song.txt; EMPRESS songs say "id <n>" */
    {
        int i, k = first;
        for (i = first; i < n_songs; i++) {
            char *t, *q;
            snprintf(path, sizeof path, "songs/%s/%s/song.txt", styles[st].dir, songs[i].dir);
            if (!(t = slurp(path, 0))) continue;
            songs[i].id = -1;
            q = t;
            while ((l = line(&q))) if (!strncmp(l, "id ", 3)) { songs[i].id = atoi(l + 3); break; }
            free(t);
            songs[k++] = songs[i];
        }
        n_songs = k;
    }
    qsort(songs + first, n_songs - first, sizeof(song_t), cmp_song);
    return n_songs - first;
}

static int sc_load(void);
static void sc_store(void);
static void read_styles_inner(void)
{
    char *b = slurp("songs/styles.txt", 0), *p = b, *l, path[160];
    n_styles = n_songs = 0;
    while ((l = line(&p)) && n_styles < MAXSTYLE) {
        style_t *s = &styles[n_styles];
        char *t, *q, *tl;
        if (!*l || *l == '#') continue;
        memset(s, 0, sizeof *s);
        strncpy(s->dir, l, 47);
        strncpy(s->name, l, 47);
        snprintf(path, sizeof path, "songs/%s/style.txt", l);
        if ((t = slurp(path, 0))) {
            q = t;
            while ((tl = line(&q))) {
                if (!strncmp(tl, "name ", 5)) strncpy(s->name, tl + 5, 47);
                if (!strncmp(tl, "folder ", 7)) strncpy(s->label, tl + 7, 63);
                if (!strncmp(tl, "caption ", 8)) strncpy(s->caption, tl + 8, 63);
                if (!strcmp(tl, "custom")) s->custom = 1;
                if (!strncmp(tl, "voice ", 6)) s->voice = atoi(tl + 6);
                if (!strncmp(tl, "bar ", 4)) s->bar = strtoul(tl + 4, 0, 0);
                if (!strncmp(tl, "gamefolder ", 11)) s->game = atoi(tl + 11);
            }
            free(t);
        }
        s->on = 1;
        s->n = list_style(n_styles);
        n_styles++;
    }
    free(b);
}

static void read_styles(void)
{
    if(!sc_load()){read_styles_inner();sc_store();}
}

/* ---- note skins: skins/notes/list.txt (dirs in menu order), <dir>/skin.txt ("name ...", "rows N") ---- */
static void read_note_skins(void)
{
    char *b = slurp("skins/notes/list.txt", 0), *p = b, *l, path[96];
    while (b && (l = line(&p)) && n_nskins < (int)(sizeof nskins / sizeof nskins[0])) {
        nskin_t *k = &nskins[n_nskins];
        char *t, *q, *m;
        if (!*l) continue;
        snprintf(k->dir, sizeof k->dir, "%s", l);
        snprintf(k->name, sizeof k->name, "%s", l);
        k->rows = 6;
        snprintf(path, sizeof path, "skins/notes/%s/skin.txt", k->dir);
        if (!(t = slurp(path, 0))) continue;
        q = t;
        while ((m = line(&q))) {
            if (!strncmp(m, "name ", 5)) snprintf(k->name, sizeof k->name, "%s", m + 5);
            if (!strncmp(m, "rows ", 5)) k->rows = atoi(m + 5);
        }
        free(t);
        if (k->rows > 0) n_nskins++;
    }
    free(b);
}

/* ---- launcher.cfg: "video N", "mod <dir>", "styleoff <dir>", "lanebright N", "beamlen N" (tenths),
 *      "noteskin <dir>" (none = EMPRESS's), "noteh N" (tenths) ---- */
static void debug_load(void)
{
    #ifdef PORTABLE_LAUNCHER
    debug_options opt=debug_read(dl_join(dl_boot[0]?dl_boot:SETTINGS_ROOT,"debug.cfg"));
#else
    debug_options opt=debug_read(game_path("debug.cfg"));
#endif
    debug_enabled=opt.enabled;input_mode=debug_enabled?opt.input_mode:0;
    launcher_debug=debug_enabled && opt.launcher_log;
    save_profile_enabled=debug_enabled && opt.save_profile;
    prep_watch_enabled=opt.prep_watch;
    select_profile_mode=opt.select_profile;select_profile_seconds=opt.select_seconds;select_profile_osd=opt.select_osd;
#ifdef MSP_RAM_CONTROL
    if(select_profile_mode==2)select_profile_mode=1;
#endif
    if(select_profile_mode){input_mode=0;save_profile_enabled=0;}
    if(prep_watch_enabled){input_mode=0;save_profile_enabled=0;select_profile_mode=0;launcher_debug=0;}
    lpl_cal_enable(save_profile_enabled || select_profile_mode);
    input_available=debug_enabled;
    lpl_enable(launcher_debug);
#ifdef MMCE_BENCH
    mmce_boot_set_debug(launcher_debug);
#endif
}
static const char *cfg_error_status="Could not save launcher settings";
static int cfg_read_io(void *ctx,const char *path,void *buffer,unsigned cap)
{
    (void)ctx;FILE *f=fopen(path,"rb");
    if(!f){
#if !defined(HOST_TEST) || defined(CONFIG_MMCE_TEST) || defined(CONFIG_HOST_TEST)
        /* Native SDK transports, including the PCSX2 host client, need not
         * preserve a missing-file ENOENT through fopen. MMCE also conflates
         * absent-file, directory EOF and transport errors.
         * No errno/EOF guessing: open-create ONLY the undo file without
         * truncation or writes, close, then require a successful read.
         * Existing valid recovery bytes are never replaced here. */
        unsigned len=strlen(path);
        if(len<8 || strcmp(path+len-8,".pending")){
            cfg_error_status="Could not open launcher.cfg";return -1;
        }
        FILE *init=fopen(path,"ab");
        if(!init){cfg_error_status="Could not open/create settings journal";return -1;}
        if(fclose(init)){cfg_error_status="Could not close settings journal";return -1;}
        f=fopen(path,"rb");
        if(!f){cfg_error_status="Could not reopen settings journal";return -1;}
#else
        return errno==ENOENT?-2:-1;
#endif
    }
    /* Query first, like the native game adapter. MMCE EOF can be -1, and
     * fileXio can mask that failure as zero. Checked exact-length reads also
     * reject premature zero/short/error responses without probing beyond EOF. */
    int error=0;long length=-1;unsigned n=0;
    if(fseek(f,0,SEEK_END) || (length=ftell(f))<0){cfg_error_status="Could not size settings file";error=-1;}
    else if((unsigned long)length>cap){cfg_error_status="Settings file exceeds capacity";error=-3;}
    else if(fseek(f,0,SEEK_SET)){cfg_error_status="Could not rewind settings file";error=-1;}
    else while(n<(unsigned)length){
        unsigned got=fread((char *)buffer+n,1,(unsigned)length-n,f);
        if(!got || ferror(f)){cfg_error_status="Settings file read failed";error=-1;break;}
        n+=got;
    }
    if(fclose(f)){cfg_error_status="Settings file close failed";error=-1;}
    return error?error:(int)n;
}

static int cfg_write_io(void *ctx,const char *path,const void *buffer,unsigned n)
{
    (void)ctx;FILE *f=fopen(path,"wb");if(!f)return -1;
    unsigned done=0;while(done<n){unsigned k=fwrite((const char *)buffer+done,1,n-done,f);if(!k){fclose(f);return -1;}done+=k;}
    return fclose(f)?-1:0;
}
static const struct mm_cfg_io cfg_io={0,cfg_read_io,cfg_write_io};
static char *cfg_read_text(void)
{
    char *text=malloc(MM_CFG_MAX+1);unsigned char *scratch=malloc(MM_CFG_MAX+MM_PENDING_HEADER+1);
    if(!text || !scratch){free(text);free(scratch);return 0;}
    cfg_error_status="Could not read settings or undo journal";
    int n=mm_cfg_load(&cfg_io,dl_settings(),text,scratch);free(scratch);
    if(n<0){free(text);return 0;}return text;
}
static void skin_scalars_apply(skin_t *);
static int shared_settings_bind(void)
{
    char *meta=slurp(runtime_txt(),0),*p=meta,*l;
    unsigned seen=0,at=0,gas=0,fs=0,bytes=0,cs_seen=0,cs_at=0,cs_bytes=0,skin_at=0;int length=fsize(runtime_bin());
    while((l=line(&p))){
        if(!strncmp(l,"skin ",5))skin_at=strtoul(l+5,0,16);
        if(!strncmp(l,"settings ",9)){unsigned abi;char tail;if(seen++ || sscanf(l+9,"%u %x %u %c",&abi,&at,&bytes,&tail)!=3 || abi!=1){free(meta);return -1;}}
        if(!strncmp(l,"customize ",10)){unsigned abi;char tail;if(cs_seen++||sscanf(l+10,"%u %x %u %c",&abi,&cs_at,&cs_bytes,&tail)!=3||abi!=CS_VERSION){free(meta);return -1;}}
        if(!strncmp(l,"gsm ",4))gas=strtoul(l+4,0,16);
        if(!strncmp(l,"fast_slow ",10))fs=strtoul(l+10,0,16);
    }
    free(meta);
    if(!seen || length<(int)sizeof(struct mm_control) || length>(int)(RES_HI-RES_LO) || save_config_error || bytes!=sizeof(struct mm_control) || (at&3) || at<RES_LO || at>RES_LO+(u32)length-bytes || (gas&3) || gas<RES_LO || gas>RES_LO+(u32)length-4 || (fs&3) || fs<RES_LO || fs>RES_LO+(u32)length-4)return -1;
    struct mm_control *control=(struct mm_control *)(unsigned long)at;
    if(control->schema!=1)return -1;control->options=shared_options;
    snprintf(control->path,sizeof control->path,"%s",dl_settings());
    if(cs_seen!=1||cs_bytes!=sizeof(struct cs_preferences)||(cs_at&3)||cs_at<RES_LO||cs_at>RES_LO+(u32)length-cs_bytes)return -1;
    shared_customize.value[0][0]=lane_b;shared_customize.value[0][1]=beam_len;shared_customize.value[0][2]=note_h;cs_players(&shared_customize);
    if(((struct cs_preferences *)(unsigned long)cs_at)->version!=CS_VERSION)return -1;
    *(struct cs_preferences *)(unsigned long)cs_at=shared_customize;
    if((skin_at&3)||skin_at<RES_LO||skin_at>RES_LO+(u32)length-sizeof(skin_t))return -1;
    skin_scalars_apply((skin_t *)(unsigned long)skin_at);
    *(u32 *)(unsigned long)gas=mm_any_gas(&shared_options);*(u32 *)(unsigned long)fs=1;return 0;
}
static void cfg_load(void)
{
    debug_load();
    mm_defaults(&shared_options);cs_defaults(&shared_customize);
    char *b=cfg_read_text(), *p=b, *l;
    if(!b){
        /* Display the original canonical preferences if journal initialization
         * failed, but retain the error latch: never save/launch with guessed
         * defaults or bypass unreadable recovery state. */
        save_config_error=1;
#ifdef PORTABLE_LAUNCHER
        b=0;
#else
        b=slurp("launcher.cfg",0);
#endif
        p=b;
    }
    if(b && mm_parse(&shared_options,b,strlen(b))){save_config_error=1;cfg_error_status="Invalid shared settings in launcher.cfg";}
    if(b&&cs_parse(&shared_customize,b,strlen(b))){save_config_error=1;cfg_error_status="Invalid play-scene settings in launcher.cfg";}
    cs_players(&shared_customize);
    mm_players(&shared_options);
    int i;
    while ((l = line(&p))) {
        if (!strncmp(l, "savebatch ", 10)) save_batch = atoi(l + 10) == 1;
        if (!strncmp(l, "saveformat ", 11)) {int v=atoi(l+11);if(v==1 || v==2)save_format=v;else save_config_error=1;}
        if (!strncmp(l, "saveuuid ", 9) && save_uuid_parse(l+9))save_config_error=1;
        if(!strncmp(l,"profileprompt ",14)){
            int value;char tail;if(sscanf(l+14,"%d %c",&value,&tail)!=1 || value<0 || value>1)save_config_error=1;else profile_prompt=value;
        }
        if(!strncmp(l,"profileslot ",12)){
            int slot;char tail;if(sscanf(l+12,"%d %c",&slot,&tail)!=1 || slot<1 || slot>2)save_config_error=1;
            else {selected_profile.port=slot-1;selected_profile.managed=1;selected_profile.action=ML_OPEN;}
        }
        if(!strncmp(l,"profiledir ",11)){
            if(strlen(l+11)>=ML_DIRECTORY)save_config_error=1;
            else strcpy(selected_profile.directory,l+11);
        }
        if(!strncmp(l,"profilegameid ",14)){
            const char *id=l+14;unsigned len=strlen(id);
            if(!strcmp(id,"-"))selected_game_id[0]=0;
            else if(len!=10 || !ml_game_id(id))save_config_error=1;
            else strcpy(selected_game_id,id);
        }
        if (!strncmp(l, "video ", 6)) {int v=atoi(l+6);video=v>=0 && v<3?v:0;}
        if (!strncmp(l, "lanebright ", 11)) lane_b = atoi(l + 11);
        if (!strncmp(l, "beamlen ", 8)) beam_len = atoi(l + 8);
        if (!strncmp(l, "noteh ", 6)) note_h = atoi(l + 6);
        for (i = 1; i < FRAME_COUNT; i++)
            if (!strncmp(l, "frame ", 6) && !strcmp(l + 6, FRAME_DIR[i]) && (frame_available & (1 << i))) frame_pack = i;
        for (i = 0; i < n_nskins; i++) if (!strncmp(l, "noteskin ", 9) && !strcmp(l + 9, nskins[i].dir)) note_skin = i + 1;
        for (i = 0; i < n_mods; i++) if (!strncmp(l, "mod ", 4) && !strcmp(l + 4, mods[i].dir)) mods[i].on = 1;
        for (i = 0; i < n_styles; i++)
            if (!strncmp(l, "styleoff ", 9) && !strcmp(l + 9, styles[i].dir)) styles[i].on = 0;
    }
    free(b);
    if(selected_profile.managed){memcpy(selected_profile.uuid,save_create_uuid,16);if(!ml_valid(&selected_profile))save_config_error=1;}
    if (lane_b < 0 || lane_b > SKIN_LANE_MAX) lane_b = 10;
    if (beam_len < SKIN_BEAM_MIN || beam_len > SKIN_BEAM_MAX) beam_len = 10;
    if (note_h < SKIN_NOTE_MIN || note_h > SKIN_NOTE_MAX) note_h = 10;
}

static int cfg_emit(char *out,unsigned *n,const char *fmt,...)
{
    va_list ap;va_start(ap,fmt);int count=vsnprintf(out+*n,MM_CFG_MAX-*n,fmt,ap);va_end(ap);
    if(count<0 || (unsigned)count>=MM_CFG_MAX-*n)return -1;*n+=count;return 0;
}
static int cfg_save(void)
{
    if(save_config_error || !ml_game_id(selected_game_id) || !ml_valid(&selected_profile))return -1;
    cfg_error_status="Not enough memory for settings save";
    char *old=cfg_read_text(),*out=malloc(MM_CFG_MAX+1),*fields=malloc(MM_CFG_MAX+1);
    unsigned char *scratch=malloc(MM_CFG_MAX+MM_PENDING_HEADER+1);unsigned n=0;int result=-1;
    if(!old || !out || !fields || !scratch)goto done;
#define EMIT(...) do{if(cfg_emit(fields,&n,__VA_ARGS__))goto done;}while(0)
#ifdef PORTABLE_LAUNCHER
    EMIT("gamepath %s\n",DEV);
#endif
    EMIT("video %d\n",video);
    if(save_batch)EMIT("savebatch 1\n");
    if(save_format!=1)EMIT("saveformat %d\n",save_format);
    if(profile_prompt)EMIT("profileprompt 1\n");
    if(selected_profile.managed)EMIT("profileslot %u\nprofiledir %s\n",selected_profile.port+1,selected_profile.directory);
    EMIT("profilegameid %s\n",selected_game_id[0]?selected_game_id:"-");
    {unsigned any=0;for(unsigned j=0;j<16;j++)any|=save_create_uuid[j];if(any){EMIT("saveuuid ");for(unsigned j=0;j<16;j++)EMIT("%02x",save_create_uuid[j]);EMIT("\n");}}
    shared_customize.value[0][0]=lane_b;shared_customize.value[0][1]=beam_len;shared_customize.value[0][2]=note_h;cs_players(&shared_customize);
    {int size=cs_emit(&shared_customize,fields+n,MM_CFG_MAX-n-1);if(size<0)goto done;n+=size;}
    if(note_skin)EMIT("noteskin %s\n",nskins[note_skin-1].dir);
    if(frame_pack)EMIT("frame %s\n",FRAME_DIR[frame_pack]);
    for(int i=0;i<n_mods;i++)if(mods[i].on)EMIT("mod %s\n",mods[i].dir);
    for(int i=0;i<n_styles;i++)if(!styles[i].on)EMIT("styleoff %s\n",styles[i].dir);
    int count=mm_emit(&shared_options,fields+n,MM_CFG_MAX-n-1);if(count<0)goto done;n+=count;
    {
        const char *owned[14+MM_KEY_COUNT+CS_KEYS+MAXI+MAXSTYLE]={"video","savebatch","saveformat","saveuuid","lanebright","beamlen","noteh","noteskin","frame","settings","gas","fstype1","fstype2","fscounters","timing","nativecmd","native1","native2","fhs1","fhs2","fhsc1","fhsc2","fhsc3","fhsc4","fhsv1","fhsv2","fhsv3","fhsv4","fhsg1","fhsg2","fhsg3","fhsg4","lane1","lane2","lanep1","lanep2","lanep3","lanep4","gas2","fscounters2","timing2","bpmnotice1","bpmnotice2","quickretry1","quickretry2","lift1","lift2","lifth1","lifth2","liftspan1","liftspan2","liftspan3","liftspan4","profileslot","profiledir","profilegameid","profileprompt"};
        char dynamic[MAXI+MAXSTYLE][80];unsigned keys=13+MM_KEY_COUNT,d=0;
#ifdef PORTABLE_LAUNCHER
        owned[keys++]="gamepath";
#endif
        for(unsigned k=0;k<CS_KEYS;k++)owned[keys++]=cs_keys[k];
        for(int i=0;i<n_mods;i++){snprintf(dynamic[d],80,"mod %s",mods[i].dir);owned[keys++]=dynamic[d++];}
        for(int i=0;i<n_styles;i++){snprintf(dynamic[d],80,"styleoff %s",styles[i].dir);owned[keys++]=dynamic[d++];}
        int len=mm_merge(old,strlen(old),fields,n,owned,keys,out,MM_CFG_MAX-1);
        if(len>=0){
            result=mm_cfg_commit(&cfg_io,dl_settings(),out,len,scratch);
            if(result==-4)cfg_error_status="Could not write settings undo journal";
            else if(result==-5)cfg_error_status="Settings undo journal verification failed";
            else if(result==-6)cfg_error_status="Could not write launcher.cfg";
            else if(result==-7)cfg_error_status="launcher.cfg verification failed";
            else if(result==-8 || result==-9)cfg_error_status="Could not clear settings undo journal";
            else if(result)cfg_error_status="Could not read settings for replacement";
        }else cfg_error_status="Launcher settings exceed config capacity";
    }
#undef EMIT
 done:free(old);free(out);free(fields);free(scratch);return result;
}

#ifndef HOST_TEST
#ifdef USB_HID
#include "usb_input.h"
#include "navigation.h"
_Static_assert(MU_NATIVE_VERSION==2 && sizeof(mu_native_config)==20 && sizeof(mu_frame)==128,"USB runtime metadata ABI changed");
#ifndef MUI_BOOT_DIAG
#define MUI_BOOT_DIAG 0
#endif
#endif
/* ---- pad ---- */
static void pad_start(void)
{
    int r;
#ifdef HARDWARE_BENCH
    bench_boot_note("L-pad prepare " BENCH_LABEL " controller");
#endif
#if !defined(MMCE_BENCH) && !defined(PORTABLE_LAUNCHER)
    r=SifLoadModule("rom0:SIO2MAN", 0, 0);
#else
    /* SDK SIO2MAN is resident already; do not load an incompatible ROM copy. */
    r=0;
#endif
#ifdef HARDWARE_BENCH
    bench_boot_note("L-pad SIO2MAN returned %d; load PADMAN",r);
#endif
    r=SifLoadModule("rom0:PADMAN", 0, 0);
#ifdef HARDWARE_BENCH
    bench_boot_note("L-pad PADMAN returned %d; padInit",r);
#endif
    r=padInit(0);
#ifdef HARDWARE_BENCH
    bench_boot_note("L-pad padInit returned %d; open port0",r);
#endif
    r=padPortOpen(0, 0, pad_buf[0]);
    padPortOpen(1, 0, pad_buf[1]);
#ifdef HARDWARE_BENCH
    bench_boot_note("L-pad padPortOpen returned %d",r);
#endif
    (void)r;
}

static u32 pad_physical_port(unsigned port,unsigned navigation)
{
#ifdef USB_HID
    static unsigned kind[2]; /* 0 unknown,1 digital,2 DualShock; reset on unplug */
#endif
    struct padButtonStatus b;
    int st = padGetState(port, 0);
    if (st != PAD_STATE_STABLE && st != PAD_STATE_FINDCTP1){
#ifdef USB_HID
        kind[port]=0;
#endif
        return 0;
    }
    if (!padRead(port, 0, &b)) return 0;
    u32 raw=0xffff ^ b.btns;
#ifdef USB_HID
    if(navigation){
        if((b.mode>>4)==PAD_TYPE_DUALSHOCK)kind[port]=2;
        /* DualShock often boots in DIGITAL mode; its advertised mode table
         * still distinguishes it from a native digital KOC controller. */
        if(!kind[port]){
            int count=padInfoMode(port,0,PAD_MODETABLE,-1);
            for(int i=0;i<count && i<8;i++)
                if(padInfoMode(port,0,PAD_MODETABLE,i)==PAD_TYPE_DUALSHOCK)kind[port]=2;
            if(!kind[port] && st==PAD_STATE_STABLE)kind[port]=1;
        }
        return launcher_physical_navigation(raw,mui_navigation,kind[port]==2);
    }
#else
    (void)navigation;
#endif
    return raw;
}
static u32 pad_physical(void)
{
    return pad_physical_port(0,0)|pad_physical_port(1,0);
}
#endif

#ifndef HOST_TEST
static u32 pad_get(void)
{
    u32 b=pad_physical_port(0,1)|pad_physical_port(1,1);
#ifdef USB_HID
    b|=launcher_usb_navigation(mui_buttons());
#endif
    return b;
}
#endif

/* ---- build the patched game in memory ---- */
static u32 *W(u32 va) { return (u32 *)va; }

static int apply_word(u32 va, u32 orig, u32 nw, const char *who)
{
    if (va < 0x100000 || va >= RES_LO || (va & 3)) { logf_("%s: bad address %x\n", who, va); return -1; }
    if (*W(va) != orig) { logf_("%s: %x is %08x, expected %08x (conflict?)\n", who, va, *W(va), orig); return -1; }
    *W(va) = nw;
    return 0;
}

/* Tables and paths share the reserved block. TAB_HI is the preferred path
 * start for small builds, not a fixed table-capacity limit. Build paths in
 * launcher-only memory, then publish them after the measured final tables. */
static u8 *tab_cur;
static char *str_cur, *str_end, *str_work, *str_start;
static u32 *path_remap;
static unsigned compact_extent_paths;
static u32 *str_offsets; /* Launcher-only exact path interning, never game RAM. */
typedef struct {u32 lui,addiu,path;} module_path_t;
static module_path_t *module_paths;
static u32 module_path_count,module_path_capacity;

static void *tab_alloc(u32 n)
{
    u8 *p = (u8 *)(((u32)tab_cur + 15) & ~15u);
    if ((u32)p > RES_HI || n > RES_HI-(u32)p) return 0;
    tab_cur = p + n;
    memset(p, 0, n);
    return p;
}

/* "host:<rel>" into the string area; returns its offset from RES_LO (0 if full) */
static u32 new_str(const char *rel)
{
    char *s = str_cur;
    u32 n = strlen(DEV) + strlen(rel) + 1;
    u32 slot=~0u;
    if(str_offsets && str_start==str_work && strncmp(rel,"logs/",5)){
        u32 hash=2166136261u;const u8 *p=(const u8 *)rel;
        while(*p)hash=(hash^*p++)*16777619u;
        for(u32 i=0;i<32;i++){
            u32 at=(hash+i)&32767u,off=str_offsets[at];
            if(!off){slot=at;break;}
            if(!strcmp(str_work+off-1+strlen(DEV),rel))return (u32)(str_work+off-1)-RES_LO;
        }
    }
    if (s + n > str_end) return 0;
    strcpy(s, DEV); strcat(s, rel);
    str_cur = s + n;
    if(slot!=~0u)str_offsets[slot]=(u32)(s-str_work)+1;
    return (u32)s - RES_LO;
}

/* Only the paired profiler runtime advertises prefix decoding. Other runtimes
 * keep their exact old strings. All shortening happens AFTER builder consumers
 * finish, with an explicit map for extents, native module paths and log prefix.
 * Module/log paths remain absolute. No game catalog IDs or file extents change. */
static int publish_paths(ext_t *base,u32 base_count,ext_t *mods,u32 mod_count)
{
 u32 original=str_cur-str_work,source=(u32)str_work,bytes=original,i,k;
 for(k=0;k<2;k++){
  ext_t *e=k?mods:base;u32 n=k?mod_count:base_count;
  for(i=0;i<n;i++)if(e[i].path+RES_LO-source>=original)return -1;
 }
 for(i=0;i<module_path_count;i++)if(module_paths[i].path+RES_LO-source>=original)return -1;
 free(path_remap);path_remap=0;
 if(compact_extent_paths){
  char *packed=malloc(original?original:1);path_remap=calloc(original?original:1,sizeof *path_remap);
  if(!packed||!path_remap){free(packed);return -1;}
  unsigned old=0,at=0,prefix=strlen(DEV);
  while(old<original){
   const char *p=str_work+old;unsigned n=strlen(p)+1,keep;
   if(n>original-old||n<prefix+1||strncmp(p,DEV,prefix)){free(packed);return -1;}
   keep=!strncmp(p+prefix,"logs/",5);
   for(i=0;i<module_path_count;i++)if(module_paths[i].path+RES_LO-source==old)keep=1;
   path_remap[old]=at+1;
   if(keep){memcpy(packed+at,p,n);at+=n;}
   else {packed[at++]=1;memcpy(packed+at,p+prefix,n-prefix);at+=n-prefix;}
   old+=n;
  }
  memcpy(str_work,packed,at);bytes=at;free(packed);
  logf_("extent path prefix: %u -> %u bytes\n",original,bytes);
 }
 u32 start=((u32)tab_cur+127u)&~127u;if(start<TAB_HI)start=TAB_HI;
 if(!input_mode||input_mode==3){u32 limit=(RES_HI-0x41c80u)&~127u;
  if(bytes+256u<limit){u32 high=(limit-bytes-256u)&~127u;if(high>=(((u32)tab_cur+127u)&~127u))start=high;}}
 if(start>RES_HI||bytes>RES_HI-start){logf_("reserved block full: tables %u, paths %u\n",(u32)tab_cur-RES_LO,bytes);return -1;}
 memcpy((void *)start,str_work,bytes);
 for(k=0;k<2;k++){
  ext_t *e=k?mods:base;u32 n=k?mod_count:base_count;
  for(i=0;i<n;i++){u32 off=e[i].path+RES_LO-source;
   if(path_remap){if(!path_remap[off])return -1;off=path_remap[off]-1;}e[i].path=start+off-RES_LO;}
 }
 for(i=0;i<module_path_count;i++){
  module_path_t *m=module_paths+i;u32 off=m->path+RES_LO-source;
  if(path_remap){if(!path_remap[off])return -1;off=path_remap[off]-1;}
  u32 address=start+off;*W(m->lui)=(*W(m->lui)&0xffff0000)|((address+0x8000)>>16);*W(m->addiu)=(*W(m->addiu)&0xffff0000)|(address&0xffff);
 }
 str_start=(char *)start;str_cur=str_start+bytes;str_end=(char *)RES_HI;
 logf_("reserved layout: tables end %x, paths %x..%x\n",(u32)tab_cur,start,(u32)str_cur);return 0;
}

/* extents collected on the launcher heap, sorted and copied into the block at the end */
static ext_t *xb, *xm;                            /* base (EMPRESS + songs + added files), mods (first match wins) */
static int nxb, nxm;
#define MAXEXT 16384
#define MAXMEXT 512

static int add_ext(ext_t *arr, int *n, int max, u32 lba, u32 sec, u32 size, const char *rel, const char *who)
{
    u32 s;
    if (*n >= max) { logf_("%s: too many extents\n", who); return -1; }
    if (!(s = new_str(rel))) { logf_("%s: string area full\n", who); return -1; }
    arr[*n].lba = lba; arr[*n].sectors = sec; arr[*n].size = size; arr[*n].path = s;
    (*n)++;
    return 0;
}

static int ext_line(char *l, ext_t *arr, int *n, int max, const char *who)
{
    char *q;
    u32 lba = strtoul(l + 2, &q, 10), sec = strtoul(q, &q, 10), size = strtoul(q, &q, 10);
    while (*q == ' ') q++;
    return add_ext(arr, n, max, lba, sec, size, q, who);
}

static int cmp_ext(const void *a, const void *b)
{
    const ext_t *x = a, *y = b;
    return x->lba < y->lba ? -1 : x->lba > y->lba;
}

/* the file holding a disc sector (base extents incl. songs); 0 if none */
static const ext_t *ext_at(u32 lba)
{
    int i;
    for (i = 0; i < nxb; i++) if (lba >= xb[i].lba && lba < xb[i].lba + xb[i].sectors) return &xb[i];
    return 0;
}

/* the file the game will read at lba (a whole container): mods first, then base */
static const ext_t *ext_mod_at(u32 lba)
{
    int i;
    for (i = 0; i < nxm; i++) if (xm[i].lba == lba) return &xm[i];
    for (i = 0; i < nxb; i++) if (xb[i].lba == lba) return &xb[i];
    return 0;
}

static int load_mod(const char *dir)
{
    char path[160], *b, *p, *l;
    int errs = 0, nw = 0, ne = 0;
    snprintf(path, sizeof path, "mods/%s/mod.txt", dir);
    if (!(b = slurp(path, 0))) { logf_("%s: no mod.txt\n", dir); return -1; }
    p = b;
    while ((l = line(&p))) {
        if (l[0] == 'W' && l[1] == ' ') {
            char *q;
            u32 va = strtoul(l + 2, &q, 16), o = strtoul(q, &q, 16), n = strtoul(q, &q, 16);
            if (apply_word(va, o, n, dir)) errs++; else nw++;
        } else if (l[0] == 'E' && l[1] == ' ') {
            if (ext_line(l, xm, &nxm, MAXMEXT, dir)) { errs++; break; }
            ne++;
        }
    }
    free(b);
    logf_("%s: %d words, %d extents%s\n", dir, nw, ne, errs ? " (ERRORS)" : "");
    return errs ? -1 : 0;
}

/* is mod <dir> enabled ("a+b" = all of them) */
static int mods_on(const char *list)
{
    char one[64];
    while (*list) {
        int n = strcspn(list, "+"), i, on = 0;
        snprintf(one, sizeof one, "%.*s", n, list);
        for (i = 0; i < n_mods; i++) if (mods[i].on && !strcmp(mods[i].dir, one)) on = 1;
        if (!on) return 0;
        list += n + (list[n] == '+');
    }
    return 1;
}

/* `C <mods> lba sectors size path`: extent used instead of the mods' own when all of <mods> are on too (combined
 * files, e.g. simple notes on black-lane note sheets). Added before every mod's E lines: first match wins. */
static int load_combos(const char *dir)
{
    char path[160], *b, *p, *l;
    int errs = 0;
    snprintf(path, sizeof path, "mods/%s/mod.txt", dir);
    if (!(b = slurp(path, 0))) return 0;
    p = b;
    while ((l = line(&p))) {
        char other[128], *q;
        if (l[0] != 'C' || l[1] != ' ') continue;
        q = l + 2;
        snprintf(other, sizeof other, "%.*s", (int)strcspn(q, " "), q);
        q += strlen(other);
        if (!mods_on(other)) continue;
        q[-1] = 'E';                                 /* "...<other>E lba ..." -> ext_line reads from q + 1 */
        if (ext_line(q - 1, xm, &nxm, MAXMEXT, dir)) errs++;
        else logf_("%s: combined with %s\n", dir, other);
    }
    free(b);
    return errs ? -1 : 0;
}

/* ---- Skin settings (menu + preview below; tools/offdisc/findings: patch route, not new assets) ----
 * Lane brightness: the runtime's skin.lane_tint = vertex colour of the lane sprites (draw_hook, songdb.S; 0x80 = 1.0,
 * the GS modulates up to ~2.0). Keybeam length: the keybeam drawer FUN_001fe040 passes zoomY = zoomX = 100
 * (`daddu t1, t0, zero` before both jal FUN_0012fae0) -> `addiu t1, zero, N`; the beams are drawn from their top
 * (y 0x41; the element pivot does not move the sprite), so the runtime's beam_hook moves them down by skin.beam_dy. */
#define BEAM_ZOOM_A 0x1fe26cu                     /* keys */
#define BEAM_ZOOM_B 0x1fe3d4u                     /* turntable */
#define DADDU_T1_T0 0x0100482du

static u32 lane_tint(void)
{
    u32 c = lane_b * 0x80 / 10;
    if (c > 0xff) c = 0xff;
    return c | c << 8 | c << 16;
}

static skin_t *skin;

static void skin_scalars_apply(skin_t *skin)
{
    skin->lane_tint = lane_tint();
    skin->note_zoom = note_h * 10;                                 /* the game's 6-row sprites */
    skin->note_zoom_skin = note_skin ? note_h * 60 / nskins[note_skin - 1].rows : 100;    /* same height on screen */
    skin->beam_dy = 256 * (10 - beam_len) / 10;    /* beams grow from their top: keep the end on the judge line */
}

static int skin_apply(u32 skin_at)
{
    int errs = 0;
    if (!skin_at) { logf_("runtime has no skin settings (old vdisc.bin?)\n"); return -1; }
    skin = (skin_t *)skin_at;
    skin->note_rows=note_skin?nskins[note_skin-1].rows:6;
    skin_scalars_apply(skin);
    skin->frame_pack = 0;                         /* enabled only after the selected pack loads successfully */

    logf_("skin: lane brightness %d.%d, keybeam length %d.%d, notes %s height %d.%d\n", lane_b / 10, lane_b % 10,
          beam_len / 10, beam_len % 10, note_skin ? nskins[note_skin - 1].name : "EMPRESS", note_h / 10, note_h % 10);
    return errs ? -1 : 0;
}

/* note skin: its white / black / turntable / auto-turntable sprites appended to the note containers #109 (SP) and
 * #146 (wide) as the game will load them (after the mods: their extents and data1 entries, e.g. CN's relocation),
 * written to cache/, data1 entry repointed; the note handle tables (0x25f330 / 0x25f360: entries 0 white, 1 black,
 * 2 turntable, 3 auto) point at them, the runtime's note_hook scales them by their own height */
#define NOTE_TAB_SP 0x25f330u
#define NOTE_TAB_WD 0x25f360u
static u32 vfile(const char *rel, u32 *sec, u32 *size, const char *who);

static u8 *spr_file(const char *path, spr_t *s, int *len)
{
    u8 *b = (u8 *)slurp(path, len);
    if (!b || *len < 12 || memcmp(b, "SPR1", 4)) { free(b); return 0; }
    memset(s, 0, sizeof *s);
    s->w = b[4] | b[5] << 8; s->h = b[6] | b[7] << 8; s->clear = b[10]; s->px = b + 12;
    if ((b[8] | b[9] << 8) == 0xffff) {                             /* own palette after the pixels */
        if (*len < 12 + s->w * s->h + 0x400) { free(b); return 0; }
        s->pal = b + 12 + s->w * s->h;
    } else s->sheet = b[8] | b[9] << 8;
    if (*len < 12 + s->w * s->h) { free(b); return 0; }
    return b;
}

static int note_skin_build(void)
{
    static const char *kind[4] = { "white", "black", "turn", "auto" };
    static const int cont[2] = { 109, 146 };
    static const u32 tab[2] = { NOTE_TAB_SP, NOTE_TAB_WD };
    int side, k, len, outlen, errs = 0;
    /* The new per-owner catalog supersedes the old global note-table patch.
     * Its zero selection must mean native artwork, including on old configs. */
    if (skin->custom_art) { skin->note_rows=6;skin->skin_first[0]=skin->skin_first[1]=0xffff;return 0; }
    if (!note_skin) return 0;
    for (side = 0; side < 2; side++) {
        u32 *ent = (u32 *)(DATA1_TAB + cont[side] * 16), lba = DATA1_LBA + ent[1], sec, size, at;
        const ext_t *e = ext_mod_at(lba);
        u8 *data, *out, *buf[4] = { 0 };
        spr_t s[4];
        char path[160], rel[64], err[96];
        FILE *f;
        if (!e) { logf_("note skin: container #%d not found\n", cont[side]); errs++; continue; }
        for (k = 0; k < 4; k++) {
            int l;
            snprintf(path, sizeof path, "skins/notes/%s/%s_%s.spr", nskins[note_skin - 1].dir, side ? "wd" : "sp", kind[k]);
            if (!(buf[k] = spr_file(path, &s[k], &l))) { logf_("note skin: bad %s\n", path); errs++; }
        }
        if (!(data = (u8 *)slurp((char *)(RES_LO + e->path) + strlen(DEV), &len))) {
            logf_("note skin: cannot read container #%d\n", cont[side]); errs++;
        }
        out = data && buf[0] && buf[1] && buf[2] && buf[3] ? cont_add(data, len, cont[side], s, 4, &outlen, err, sizeof err) : 0;
        if (data && !out && buf[3]) { logf_("note skin #%d: %s\n", cont[side], err); errs++; }
        free(data);
        for (k = 0; k < 4; k++) free(buf[k]);
        if (!out) continue;
        snprintf(path, sizeof path, "d%04d_notes", cont[side]);
        if (cache_publish(rel, sizeof rel, path, out, outlen, 0)) { free(out); errs++; continue; }
        free(out);
        if (!(at = vfile(rel, &sec, &size, rel))) { errs++; continue; }
        ent[1] = at - DATA1_LBA; ent[2] = sec;
        for (k = 0; k < 4; k++) *W(tab[side] + k * 4) = s[k].handle;
        skin->skin_first[side] = s[0].handle & 0xffff;
        logf_("note skin %s: container #%d +4 sprites from %d, %d KB\n", nskins[note_skin - 1].name, cont[side],
              s[0].handle & 0xffff, outlen >> 10);
    }
    return errs ? -1 : 0;
}

static void read_frames(void)
{
    int i;
    char path[128];
    frame_available = 1;                            /* original frame is always available */
    for (i = 1; i < FRAME_COUNT; i++) {
        snprintf(path, sizeof path, "skins/frames/%s/frame.if", FRAME_DIR[i]);
        if (fsize(path) <= 0) continue;
        snprintf(path, sizeof path, "skins/frames/%s/frame_black.if", FRAME_DIR[i]);
        if (fsize(path) > 0) frame_available |= 1 << i;
    }
}

static int frame_build(void)
{
    char path[128];
    u32 at, sec, size, *ent = (u32 *)(DATA1_TAB + 69 * 16);
    if (!frame_pack) return 0;
    if (frame_pack < 0 || frame_pack >= FRAME_COUNT || !(frame_available & (1 << frame_pack))) {
        logf_("frame: selected pack missing\n"); return -1;
    }
    snprintf(path, sizeof path, "skins/frames/%s/%s", FRAME_DIR[frame_pack], mods_on("blacklane") ? "frame_black.if" : "frame.if");
    if (!(at = vfile(path, &sec, &size, "frame"))) return -1;
    ent[1] = at - DATA1_LBA;
    ent[2] = sec;
    skin->frame_pack = frame_pack;
    logf_("frame: %s (#69, layout 2), %u KB%s\n", FRAME_NAME[frame_pack], size >> 10,
          mods_on("blacklane") ? ", black lane" : "");
    return 0;
}

/* ---- added songs ---- */
static sdb_t *sdb;
static u32 *asset_mem; /* runtime count, per-row trim pointer, diagnostics */
static u32 vcur = VBASE;
static int n_fid, n_card, n_sq, cap_fid, cap_card, cap_sq;
static u32 *fids;                                 /* {offset from BM2DX16C.DAT, sectors, size} per added file */
static u32 *cards;                                /* {offset from DATA1, sectors} per added data1 container */

typedef struct { int cont; u32 *dst; char path[160]; u8 *buf; spr_t s; } sq_t;  /* queued sprite */
static sq_t *sq;

/* room for one more entry in a growing launcher-heap array */
static int grow(void **arr, int *cap, int n, int size)
{
    void *p;
    if (n < *cap) return 0;
    if (!(p = realloc(*arr, (*cap ? *cap * 2 : 256) * size))) { logf_("out of memory\n"); return -1; }
    *arr = p;
    *cap = *cap ? *cap * 2 : 256;
    return 0;
}

static sq_t *sq_new(void)
{
    if (grow((void **)&sq, &cap_sq, n_sq, sizeof(sq_t))) return 0;
    memset(&sq[n_sq], 0, sizeof(sq_t));
    return &sq[n_sq++];
}

/* Launcher-only path interning for generated WHOLE-file extents. Reusing an
 * exact source path does not merge native resource IDs, charts or assets. Base
 * disc extents and precedence-sensitive mod extents never enter this index. */
static u16 *vfile_index;
/* a file at fresh virtual sectors; returns its first sector (0 on error) */
static u32 vfile(const char *rel, u32 *sec, u32 *size, const char *who)
{
    unsigned hash=2166136261u,slot=~0u;const unsigned char *p=(const unsigned char *)rel;
    while(*p)hash=(hash^*p++)*16777619u;
    if(!vfile_index)vfile_index=calloc(32768,sizeof *vfile_index);
    if(vfile_index)for(unsigned i=0;i<32;i++){
        unsigned k=(hash+i)&32767u,index=vfile_index[k];
        if(!index){slot=k;break;}
        ext_t *e=xb+index-1;
        if(!strcmp((char *)(RES_LO+e->path)+strlen(DEV),rel)){*size=e->size;*sec=e->sectors;return e->lba;}
    }
    long n = fsize(rel);
    u32 at = vcur;
    if (n < 0) { logf_("%s: missing %s\n", who, rel); return 0; }
    *size = n; *sec = (n + 2047) / 2048;
    if (add_ext(xb, &nxb, MAXEXT, at, *sec, *size, rel, who)) return 0;
    if(slot!=~0u)vfile_index[slot]=nxb;
    vcur += *sec + VGAP;
    return at;
}

static void put(u8 *rec, u32 field, int bytes, u32 v)
{
    if (bytes == 2) { rec[field] = v; rec[field + 1] = v >> 8; }
    else { rec[field] = v; rec[field + 1] = v >> 8; rec[field + 2] = v >> 16; rec[field + 3] = v >> 24; }
}

#include "catalog.h"

/* data1 container placeholders in a record (0xfffe = the song's card, 0xff00 + k = its `D k` container):
 * +0x94 card, +0x64 u16[8] animation containers, +0x74 u32[8] clip handles (container in the high half),
 * +0x98 u32[5] overlay pairs (u16 anim, u16 container) */
static void d1_place(u8 *rec, u32 ph, u32 idx)
{
    static const u16 at[] = { 0x94, 0x64, 0x66, 0x68, 0x6a, 0x6c, 0x6e, 0x70, 0x72, 0x76, 0x7a, 0x7e, 0x82, 0x86, 0x8a,
                              0x8e, 0x92, 0x9a, 0x9e, 0xa2, 0xa6, 0xaa };
    int i;
    for (i = 0; i < (int)(sizeof at / sizeof at[0]); i++)
        if ((rec[at[i]] | rec[at[i] + 1] << 8) == ph) put(rec, at[i], 2, idx);
}

/* a data1 container from a file -> new data1 index (0 on error) */
static u32 d1_add(const char *path, const char *who)
{
    u32 sec, size, at;
    if (grow((void **)&cards, &cap_card, n_card, 8)) return 0;
    if (!(at = vfile(path, &sec, &size, who))) return 0;
    cards[n_card * 2] = at - DATA1_LBA; cards[n_card * 2 + 1] = sec;
    return DATA1_N + n_card++;
}

/* Offline compiler validates PNGs and emits bounded native frame banks. The
 * launcher registers extents, not textures: unselected packs consume no PLAY
 * heap and the game never decodes PNGs or reads images on a hit. */
static int custom_art_build(void)
{
    char *b=slurp("custom/catalog.txt",0),*p=b,*l;unsigned n=0,count[2]={0,0};
    if(!b){skin->custom_art=0;return 0;}
    if(!(l=line(&p))||strcmp(l,"MMCA1")){free(b);return -1;}
    
    while((l=line(&p)))if(*l)n++;
    free(b);b=slurp("custom/catalog.txt",0);if(!b)return -1;
    if(n>126){free(b);return -1;}
    struct custom_art_catalog *cat=tab_alloc(sizeof *cat+n*sizeof cat->packs[0]);
    if(!cat){free(b);return -1;}cat->version=CUSTOM_ART_VERSION;
    p=b;line(&p);n=0;unsigned last=0;
    while((l=line(&p))){
        unsigned kind,id,frames;int x,y,at=0;char path[96];
        if(!*l)continue;
        if(sscanf(l,"%u %u %u %d %d %95s %n",&kind,&id,&frames,&x,&y,path,&at)!=6||kind>1||kind<last||!id||id>CUSTOM_ART_MAX||!frames||frames>120||x<0||y<0||x>256||y>256||!at||strlen(l+at)>27||!* (l+at)||strncmp(path,"custom/compiled/",16)||strstr(path,"..")||strchr(path,':')){free(b);return -1;}
        for(unsigned j=0;j<n;j++)if(cat->packs[j].id==id&&((j>=count[0])==kind)){free(b);return -1;}
        struct custom_art_pack *q=cat->packs+n++;memset(q,0,sizeof *q);
        q->id=id;q->frames=frames;q->x=x;q->y=y;strcpy(q->name,l+at);
        if(!(q->fid=d1_add(path,"custom artwork"))){free(b);return -1;}
        count[kind]++;last=kind;
    }
    cat->count[0]=count[0];cat->count[1]=count[1];skin->custom_art=cat;free(b);return 0;
}

/* title sprites (wheel, big title, genre, artist containers 0x12a..): .spr files back to back in cache/sprites.bin,
 * offsets per song slot in sdb->spr_off; the runtime loads them into SPR_ROWS-row slots (songdb.c spr_get) */
#define SPR_CONT 0x12a
static const int SPR_ROWS[NSPR] = { 24, 48, 16, 24 }, SPR_N[NSPR] = { SPR_SLOTS, 6, 6, 6 };
static FILE *bank;
static u32 bank_pos;
#include "wheel_bank.h"
static int spr_sheet[NSPR], spr_clear[NSPR], spr_seen[NSPR];

static int spr_bank_add(int c, int k, const char *path)
{
    int len;
    u8 *b = (u8 *)slurp(path, &len);
    if (!b || len < 12 || memcmp(b, "SPR1", 4)) { logf_("bad sprite %s\n", path); free(b); return -1; }
    { int w=b[4]|b[5]<<8,h=b[6]|b[7]<<8;
      if(w>SPR_W || h>48 || len!=12+w*h) {
        logf_("invalid sprite dimensions/length: %s\n",path);free(b);return -1;
      }
    }
    if (!spr_seen[c]) { spr_seen[c] = 1; spr_sheet[c] = b[8] | b[9] << 8; spr_clear[c] = b[10]; }
    else if ((b[8] | b[9] << 8) != spr_sheet[c] || b[10] != spr_clear[c]) {
        logf_("%s: other palette than the first #%x sprite\n", path, SPR_CONT + c); free(b); return -1;
    }
    if (!bank || fwrite(b, 1, len, bank) != (size_t)len) { logf_("cannot write cache/sprites_build.tmp\n"); free(b); return -1; }
    sdb->spr_off[k * NSPR + c] = bank_pos;
    bank_pos += len;
    if(!c && wheel_file && wheel_add(k,b,(u32)len)){free(b);return -1;}
    free(b);
    return 0;
}

/* the title containers get their blank sprite slots */
static int spr_containers(void)
{
    int c, errs = 0;
    for (c = 0; c < NSPR; c++) {
        u32 *ent = (u32 *)(DATA1_TAB + (SPR_CONT + c) * 16), lba = DATA1_LBA + ent[1], sec, size, at, first;
        const ext_t *e;
        u8 *data, *out;
        int len, outlen;
        char err[96], rel[64], path[200];
        FILE *f;
        if (!spr_seen[c] && !c) continue;
        if (!spr_seen[c]) {spr_sheet[c]=c==1?2:0;spr_clear[c]=0;}
        if (!(e = ext_at(lba)) || e->lba != lba) { logf_("container #%d: no file at %d\n", SPR_CONT + c, lba); errs++; continue; }
        if (!(data = (u8 *)slurp((char *)(RES_LO + e->path) + strlen(DEV), &len))) {
            logf_("container #%d: cannot read\n", SPR_CONT + c); errs++; continue;
        }
        out = cont_strips(data, len, SPR_CONT + c, spr_sheet[c], spr_clear[c], SPR_W, SPR_ROWS[c], SPR_N[c],
                          sdb->n_new+(c!=0), &first, &outlen, err, sizeof err);
        free(data);
        if (!out) { logf_("container #%d: %s\n", SPR_CONT + c, err); errs++; continue; }
        snprintf(path, sizeof path, "d%04d", SPR_CONT + c);
        if (cache_publish(rel, sizeof rel, path, out, outlen, 0)) { free(out); errs++; continue; }
        free(out);
        if (!(at = vfile(rel, &sec, &size, rel))) { errs++; continue; }
        ent[1] = at - DATA1_LBA; ent[2] = sec;
        sdb->spr[c].cont = SPR_CONT + c; sdb->spr[c].first = first; sdb->spr[c].n = SPR_N[c];
        sdb->spr[c].rows = SPR_ROWS[c];
        memset(sdb->spr[c].key, 0xff, sizeof sdb->spr[c].key);
        logf_("container #%d: %d sprite slots of %d rows, %d KB\n", SPR_CONT + c, SPR_N[c], SPR_ROWS[c], outlen >> 10);
    }
    return errs ? -1 : 0;
}

/* song.txt path: "/x" = relative to the offdisc root, else to the song folder */
static void song_path(char *out, int n, const char *base, const char *q)
{
    if (*q == '/') snprintf(out, n, "%s", q + 1);
    else snprintf(out, n, "%s%s", base, q);
}

/* song variants: `variant <style>/<song> <select|ccw|cw> [chance %]` in a song.txt = this song is another version
 * of that one (resolved to slots once every song is loaded) */
typedef struct { int k, how, pct; char main[112]; } varq_t;
static varq_t varq[MAXVAR];
static int n_varq;
static char (*slot_dir)[112];                     /* slot -> "<style>/<song>" */

static void resolve_variants(void)
{
    int i, j;
    for (i = 0; i < n_varq; i++) {
        for (j = 0; j < (int)sdb->n_new; j++) if (!strcmp(slot_dir[j], varq[i].main)) break;
        if (j == (int)sdb->n_new) { logf_("%s: variant of %s, which is not loaded\n", slot_dir[varq[i].k], varq[i].main); continue; }
        sdb->var_main[varq[i].k] = j;
        sdb->vars[sdb->nvar].main = j; sdb->vars[sdb->nvar].var = varq[i].k;
        sdb->vars[sdb->nvar].how = varq[i].how; sdb->vars[sdb->nvar].pct = varq[i].pct;
        sdb->nvar++;
        logf_("%s: variant of %s\n", slot_dir[varq[i].k], varq[i].main);
    }
}

/* Native normal container header: metadata pair, then up to32 texture pairs.
 * Only measure; source data and assets are never changed. */
static void asset_measure(const char *path,u32 *raw,u32 *packed)
{
    lm_entry *meta=lm_find(path,0);
    if(meta && meta->measured){if(meta->raw>*raw)*raw=meta->raw;if(meta->packed>*packed)*packed=meta->packed;lm_measure_hits++;return;}
    u32 local_raw=0,local_packed=0;
    char host[224];FILE *f;u8 head[8];u32 pairs[66],i,n,total;
    snprintf(host,sizeof host,"%s%s",DEV,path);
    if(!(f=fopen(host,"rb")))return;
    if(fread(head,1,8,f)!=8)goto done;
    n=head[2]|head[3]<<8;
    if(n>32 || head[4] || head[5] || fread(pairs,8,n+1,f)!=n+1)goto done;
    total=0;
    for(i=0;i<=n;i++) {
        if(pairs[i*2]>0x2000000u-total)goto done;
        total+=pairs[i*2];
    }
    local_raw=total;
    if(total>*raw)*raw=total;
    /* Large normal imports use the bounded queue reader. Their packed size
     * is no longer a whole-file workspace requirement. Small containers
     * keep the original CD-buffer path and still contribute to its budget. */
    if(total<=0x700000) {
        long bytes=fsize(path);if(bytes<0)goto done;total=bytes;local_packed=total;
        if(total>*packed)*packed=total;
    }
    if((meta=lm_find(path,1))){meta->raw=local_raw;meta->packed=local_packed;meta->measured=1;}
 done:fclose(f);
}

static int native_asset_plans(void)
{
    for(u32 id=0;id<E16_RESOURCE_SONGS;id++)if(sdb->present[id]) {
        u8 *rec=(u8 *)(0x278b70+id*0x134);u32 bank=0,raw=0,packed=0;
        for(u32 at=0xe8;at<0x108;at+=8)for(u32 j=0;j<2;j++){
            u32 fid=*(u16 *)(rec+at+j*2);if(fid==65535)continue;if(fid>=FID0)return -1;
            u32 n=*W(0x23c2f0+fid*12+8);if(n>bank)bank=n;
        }
        for(u32 at=0x64;at<0x74;at+=2){
            u32 fid=*(u16 *)(rec+at);if(fid==65535)continue;if(fid>=DATA1_N)return -1;
            u32 lba=DATA1_LBA+*W(DATA1_TAB+fid*16+4);const ext_t *e=ext_mod_at(lba);
            if(!e)return -1;asset_measure((char *)(RES_LO+e->path)+strlen(DEV),&raw,&packed);
        }
        u32 plan=0;
        if(rec[0xbd]&&bank){if(bank>0x1fff800u)return -1;u32 n=(bank+2047)&~2047u;if(n<ASSET_WORKSPACE_FLOOR)n=ASSET_WORKSPACE_FLOOR;plan=ASSET_WORKSPACE_ABSOLUTE|n;}
        else if(raw>0x700000){u32 n=bank>packed?bank:packed;n=(n+2047)&~2047u;if(n<0x608000)n=0x608000;if(n<0x708000)plan=0x708000-n;}
        ((u32 *)asset_mem[1])[id]=plan;
        if(plan)logf_("native %u: asset plan %x, bank %u, BGA raw %u\n",id,plan,bank,raw);
    }
    return 0;
}

static int add_song(const song_t *sg, int fold)
{
    static char fpath[64][160];                   /* this song's files (same path = same file id) */
    static int fno[64];
    char base[128], path[200], *b, *p, *l;
    int k = sdb->n_new, errs = 0, nf = 0, identity = -1;
    u32 bga_raw=0,bga_packed=0,max_bank=0;
    u8 *rec = sdb->recs + k * 0x134, *t;
    snprintf(base, sizeof base, "songs/%s/%s/", styles[sg->style].dir, sg->dir);
    snprintf(path, sizeof path, "%ssong.txt", base);
    if (!(b = slurp(path, 0))) return -1;
    p = b;
    while ((l = line(&p))) {
        char *q;
        int cat = catalog_from_line(l);
        if (cat>=0) identity=cat;
        if (!strncmp(l, "record ", 7)) {
            int n;
            snprintf(path, sizeof path, "%s%s", base, l + 7);
            if (!(t = (u8 *)slurp(path, &n)) || n != 0x134) { logf_("%s: bad record\n", sg->dir); errs++; free(t); break; }
            memcpy(rec, t, 0x134);
            free(t);
        } else if (l[0] == 'F' && l[1] == ' ') {
            u32 field = strtoul(l + 2, &q, 16), bytes = strtoul(q, &q, 10), sec, size, at;
            int j;
            while (*q == ' ') q++;
            song_path(path, sizeof path, base, q);
            for (j = 0; j < nf; j++)                /* the same file in several fields = one file id */
                if (!strcmp(fpath[j], path)) break;
            if (j == nf) {
                if (nf == 64) { logf_("%s: too many files\n", sg->dir); errs++; continue; }
                if (grow((void **)&fids, &cap_fid, n_fid, 12)) return -1;
                if (!(at = vfile(path, &sec, &size, sg->dir))) { errs++; continue; }
                fids[n_fid * 3] = at - C_LBA;
                fids[n_fid * 3 + 1] = sec;
                fids[n_fid * 3 + 2] = size;
                strncpy(fpath[nf], path, sizeof fpath[0] - 1);
                fno[nf++] = n_fid++;
            }
            put(rec, field, bytes, FID0 + fno[j]);
            if(field>=0xe8 && field<0x108 && ((field-0xe8)&7)<4 && fids[fno[j]*3+2]>max_bank)
                max_bank=fids[fno[j]*3+2];
        } else if (!strncmp(l, "card ", 5)) {
            u32 idx;
            song_path(path, sizeof path, base, l + 5);
            if (!(idx = d1_add(path, sg->dir))) { errs++; continue; }
            put(rec, 0x94, 4, idx);
            d1_place(rec, 0xfffe, idx);
        } else if (l[0] == 'D' && l[1] == ' ') {
            u32 n = strtoul(l + 2, &q, 10), idx;
            while (*q == ' ') q++;
            song_path(path, sizeof path, base, q);
            if (n > 0xfd || !(idx = d1_add(path, sg->dir))) { errs++; continue; }
            d1_place(rec, 0xff00 + n, idx);
            asset_measure(path,&bga_raw,&bga_packed);
        } else if (!strncmp(l, "variant ", 8)) {
            char how[16] = "";
            varq_t *v = &varq[n_varq];
            if (n_varq == MAXVAR) { logf_("%s: more than %d variants\n", sg->dir, MAXVAR); errs++; continue; }
            v->pct = 0;
            if (sscanf(l + 8, "%111s %15s %d", v->main, how, &v->pct) < 2) { logf_("%s: bad variant line\n", sg->dir); errs++; continue; }
            v->how = !strcmp(how, "ccw") ? VAR_CCW : !strcmp(how, "cw") ? VAR_CW : VAR_SELECT;
            v->k = k;
            n_varq++;
            fold = NOFOLD;                          /* a variant is in no folder */
        } else if (l[0] == 'S' && l[1] == ' ') {
            u32 cont = strtoul(l + 2, &q, 16), field = strtoul(q, &q, 16);
            while (*q == ' ') q++;
            snprintf(path, sizeof path, "%s%s", base, q);
            if (cont >= SPR_CONT && cont < SPR_CONT + NSPR) {     /* title sprites: bank, loaded in-game */
                if (spr_bank_add(cont - SPR_CONT, k, path)) errs++;
                put(rec, field, 4, 0);
            } else {
                sq_t *s = sq_new();
                if (!s) return -1;
                s->cont = cont;
                s->dst = (u32 *)(rec + field);
                snprintf(s->path, sizeof s->path, "%s", path);
            }
        }
    }
    free(b);
    if (identity<0) {
        snprintf(path,sizeof path,"custom/%s",sg->dir);
        identity=catalog_find(path,0);
    }
    if (identity<0) {logf_("%s: source missing from identity catalog\n",sg->dir);errs++;}
    catalog_rows[FIRST+k]=identity;
    if(rec[0xbd] && max_bank) {
        /* The native big preset reservesD08000 even for a bank only a few
         * sectors beyond708000. Size from ALL SP/DP/difficulty WVB references;
         * retain the ordinary floor for native scratch/fixed-offset users. */
        if(max_bank>0x1fff800u) {
            logf_("%s: bank exceeds EE workspace bounds\n",sg->dir);errs++;
        } else {
            u32 needed=(max_bank+0x7ff)&~0x7ffu;
            if(needed<ASSET_WORKSPACE_FLOOR)needed=ASSET_WORKSPACE_FLOOR;
            ((u32 *)asset_mem[1])[FIRST+k]=ASSET_WORKSPACE_ABSOLUTE|needed;
            logf_("%s: large-bank workspace %u KB for %u bytes\n",sg->dir,needed>>10,max_bank);
        }
    } else if(bga_raw>0x700000 && !rec[0xbd]) {
        u32 needed=max_bank>bga_packed?max_bank:bga_packed;
        needed=(needed+0x7ff)&~0x7ffu;
        if(needed<0x608000)needed=0x608000;
        if(needed<0x708000) {
            ((u32 *)asset_mem[1])[FIRST+k]=0x708000-needed;
            logf_("%s: large BGA %u KB, native workspace trim %u KB\n",sg->dir,bga_raw>>10,(0x708000-needed)>>10);
        }
    }
    snprintf(slot_dir[k], sizeof slot_dir[0], "%s/%s", styles[sg->style].dir, sg->dir);
    sdb->slot_fold[k] = fold;
    if (fold != NOFOLD) sdb->fold_n[fold]++;
    sdb->n_new++;
    return errs ? -1 : 0;
}

/* sprites queued per container -> one grown container each, at virtual sectors */
static int build_containers(void)
{
    int *done = calloc(n_sq, sizeof(int)), *idx = malloc(n_sq * sizeof(int)), i, j, errs = 0;
    spr_t *s = malloc(n_sq * sizeof(spr_t));
    if (!done || !idx || !s) { logf_("out of memory (sprites)\n"); return -1; }
    for (i = 0; i < n_sq; i++) {
        int len;
        if (!(sq[i].buf = spr_file(sq[i].path, &sq[i].s, &len))) {
            logf_("bad sprite %s\n", sq[i].path); return -1;
        }
    }
    for (i = 0; i < n_sq; i++) {
        int n = 0, len, outlen;
        u32 *ent, lba, sec, size, at;
        const ext_t *e;
        u8 *data, *out;
        char err[96], rel[64], path[200];
        FILE *f;
        if (done[i]) continue;
        for (j = i; j < n_sq; j++)
            if (sq[j].cont == sq[i].cont) { s[n] = sq[j].s; idx[n++] = j; done[j] = 1; }
        ent = (u32 *)(DATA1_TAB + sq[i].cont * 16);
        lba = DATA1_LBA + ent[1];
        if (!(e = ext_at(lba)) || e->lba != lba) { logf_("container #%d: no file at %d\n", sq[i].cont, lba); errs++; continue; }
        if (!(data = (u8 *)slurp((char *)(RES_LO + e->path) + strlen(DEV), &len))) {
            logf_("container #%d: cannot read\n", sq[i].cont); errs++; continue;
        }
        logf_("container #%d: packing %d sprites...\n", sq[i].cont, n);
        lpl_stage(5);
        progress(60 + 35 * i / n_sq, "title graphics");
        if (!(out = cont_add(data, len, sq[i].cont, s, n, &outlen, err, sizeof err))) {
            logf_("container #%d: %s\n", sq[i].cont, err); free(data); errs++; continue;
        }
        free(data);
        snprintf(path, sizeof path, "d%04d", sq[i].cont);
        if (cache_publish(rel, sizeof rel, path, out, outlen, 0)) { free(out); errs++; continue; }
        free(out);
        if (!(at = vfile(rel, &sec, &size, rel))) { errs++; continue; }
        ent[1] = at - DATA1_LBA; ent[2] = sec;
        for (j = 0; j < n; j++) *sq[idx[j]].dst = s[j].handle;
        logf_("container #%d: +%d sprites, %d KB\n", sq[i].cont, n, outlen >> 10);
    }
    for (i = 0; i < n_sq; i++) free(sq[i].buf);
    free(done); free(idx); free(s);
    return errs ? -1 : 0;
}

/* our music-select folders: menu order, per-folder tables (bar sprite, label, caption, voice) as NFTAB-entry copies
 * with the 10 reading sites repointed, labels / captions queued as sprites */
static const u32 TAB_SITES[][5] = {               /* table, lui va, lui word, addiu va, addiu word */
    { 0x25f050, 0x1dd17c, 0x3c040026, 0x1dd180, 0x2484f050 }, { 0x25f050, 0x1dd580, 0x3c030026, 0x1dd588, 0x2463f050 },
    { 0x25f050, 0x1df294, 0x3c030026, 0x1df29c, 0x2463f050 }, { 0x25f050, 0x1df46c, 0x3c030026, 0x1df474, 0x2463f050 },
    { 0x25f140, 0x1dd110, 0x3c040026, 0x1dd120, 0x2484f140 }, { 0x25f140, 0x1dd520, 0x3c040026, 0x1dd530, 0x2484f140 },
    { 0x25f140, 0x1df234, 0x3c030026, 0x1df23c, 0x2463f140 }, { 0x25f140, 0x1df414, 0x3c020026, 0x1df41c, 0x2442f140 },
    { 0x25d6f0, 0x190ca4, 0x3c020026, 0x190ca8, 0x2442d6f0 }, { 0x25d590, 0x18cfc0, 0x3c030026, 0x18cfc4, 0x2463d590 } };

static int folders(void)
{
    static const u32 tabs[4] = { 0x25f050, 0x25f140, 0x25d6f0, 0x25d590 };   /* bar, label, caption, voice */
    u32 *t[4], i, k, errs = 0, seq[96], n = 0, have1 = 0;
    for (k = 0; k < 4; k++) {
        t[k] = tab_alloc(NFTAB * 4);
        if (!t[k]) { logf_("block full (folder tables)\n"); return -1; }
        memcpy(t[k], (void *)tabs[k], 60 * 4);
        for (i = 60; i < NFTAB; i++) t[k][i] = k == 1 ? 0xffffffffu : 0;
    }
    /* menu order: ALL, then the styles' folders in styles.txt order, then the game's LEVEL.. folders;
       OLD VERSION (2) is left out (the runtime also hides it) */
    seq[n++] = 0;
    for (i = 0; i < (u32)n_styles; i++) {
        style_t *s = &styles[i];
        if (s->game == 1 && !have1) { seq[n++] = 1; have1 = 1; }
        if (s->fold == NOFOLD) continue;
        seq[n++] = FOLD0 + s->fold;
        k = FOLD0 + s->fold;
        t[0][k] = s->bar ? (u32)s->bar : 0x1c;               /* VERSION bar */
        t[3][k] = s->voice ? (u32)s->voice : t[3][1];         /* else the EMPRESS folder's voice */
        t[2][k] = t[2][1];                                    /* caption: EMPRESS's until replaced */
        if (s->label[0]) {
            sq_t *q = sq_new();
            if (!q) return -1;
            q->cont = 0; q->dst = &t[1][k];
            snprintf(q->path, sizeof q->path, "songs/%s/%s", s->dir, s->label);
        }
        if (s->caption[0]) {
            sq_t *q = sq_new();
            if (!q) return -1;
            q->cont = 303; q->dst = &t[2][k];
            snprintf(q->path, sizeof q->path, "songs/%s/%s", s->dir, s->caption);
        }
    }
    if (!have1) seq[n++] = 1;
    for (i = 3; i < 0x2c; i++) seq[n++] = i;
    memset(sdb->next, 0xff, sizeof sdb->next);
    for (i = 0; i + 1 < n; i++) sdb->next[seq[i]] = seq[i + 1];
    for (i = 0; i < sizeof TAB_SITES / sizeof TAB_SITES[0]; i++) {
        u32 a = (u32)t[TAB_SITES[i][0] == 0x25f050 ? 0 : TAB_SITES[i][0] == 0x25f140 ? 1 : TAB_SITES[i][0] == 0x25d6f0 ? 2 : 3];
        if (apply_word(TAB_SITES[i][1], TAB_SITES[i][2], (TAB_SITES[i][2] & 0xffff0000) | ((a + 0x8000) >> 16), "folders"))
            errs++;
        if (apply_word(TAB_SITES[i][3], TAB_SITES[i][4], (TAB_SITES[i][4] & 0xffff0000) | (a & 0xffff), "folders"))
            errs++;
    }
    logf_("folders: %d in the menu\n", (int)n);
    return errs ? -1 : 0;
}

/* Generated indexes retain potential fallbacks; evaluate their small coverage
 * masks only after cfg_load and folder selection, before resident allocations. */
static u8 release_covered[MAXSONG];
static int release_row_filter(void)
{
    memset(release_covered,0,sizeof release_covered);
    char *marker=slurp("catalog/indexed.txt",0);
    int enabled=marker && !strcmp(marker,"1\n");free(marker);
    if(!enabled)return 0;
    char *b=slurp("catalog/coverage.txt",0),*p=b,*l;u32 on=0;int version=0,seen=0,error=0;
    if(!b || n_styles>32){free(b);return -1;}
    for(int i=0;i<n_styles;i++)if(styles[i].on)on|=1u<<i;
    while((l=line(&p))){
        unsigned st;char dir[64],extra;int used=0;
        if(!strcmp(l,"V 1") && !version){version=1;continue;}
        if(!version){error=1;break;}
        if(l[0]=='S' && l[1]==' '){
            if(sscanf(l+2,"%u %63s %c",&st,dir,&extra)!=2 || st!=(unsigned)seen || st>=(unsigned)n_styles || strcmp(styles[st].dir,dir)){error=1;break;}
            seen++;continue;
        }
        if(l[0]!='D'||l[1]!=' '||sscanf(l+2,"%u %63s%n",&st,dir,&used)!=2||st>=(unsigned)n_styles){error=1;break;}
        int song=-1;for(int i=0;i<n_songs;i++)if(songs[i].style==(int)st&&!strcmp(songs[i].dir,dir)){song=i;break;}
        if(song<0 || songs[song].id>=0){error=1;break;}
        char *q=l+2+used,*end;int covered=1,masks=0;
        while(*q){while(*q==' ')q++;if(!*q)break;u32 mask=strtoul(q,&end,16);
            if(end==q||(*end&&*end!=' ')||!mask||(n_styles<32&&(mask>>n_styles))){error=1;break;}
            if(!(mask&on))covered=0;masks++;q=end;
        }
        if(error||!masks){error=1;break;}release_covered[song]=covered;
    }
    free(b);if(!version||seen!=n_styles||error){logf_("Invalid release folder coverage index\n");return -1;}return 0;
}

static int build_inner(u32 *entry)
{
    if(release_row_filter())return -1;
    lpl_stage(3);
    progress(0, "reading the game");
    int len, i, vlen, errs = 0, catalog_abi = 0;
    char *elf = slurp("EMPRESS/SLPM_552.21", &len), *vt, *p, *l, path[200];
    u8 *vbin;
    u32 vbase = 0, conf = 0, sdb_at = 0, skin_at = 0, gsm_at = 0, fs_at = 0, asset_at = 0, *vd;
    ext_t *bx, *mx;

    if (!elf) { logf_("cannot read EMPRESS/SLPM_552.21\n"); return -1; }
    {   /* ELF segments */
        u8 *e = (u8 *)elf;
        u32 phoff = *(u32 *)(e + 0x1c), phn = *(u16 *)(e + 0x2c), phsz = *(u16 *)(e + 0x2a);
        *entry = *(u32 *)(e + 0x18);
        for (i = 0; i < (int)phn; i++) {
            u32 *ph = (u32 *)(e + phoff + i * phsz);
            if (ph[0] != 1) continue;
            if (ph[2] < 0x100000 || ph[2] + ph[5] > RES_LO) { logf_("segment %x+%x outside\n", ph[2], ph[5]); return -1; }
            memcpy((void *)ph[2], e + ph[1], ph[4]);
            memset((void *)(ph[2] + ph[4]), 0, ph[5] - ph[4]);
        }
    }
    free(elf);

    if (!(vbin = (u8 *)slurp(runtime_bin(), &vlen)) ||
        !(vt = slurp(runtime_txt(), 0))) {
        logf_("cannot read launcher/vdisc.*\n"); return -1;
    }
    u32 prefix_at=0,prefix_bytes=0,prep_at=0;
    compact_extent_paths=0;
    u32 wheel_labels=0,wheel_at=0,preview_at=0,save_at=0,save_diag_at=0,input_at=0,load_at=0,load_path=0,boot_journal_at=0,boot_publish_at=0,boot_diag_at=0,build_id_at=0;
    int usb_runtime=0,mmce_runtime=0,usb_preloaded=0,portable_runtime=0;
    p = vt;
    while ((l = line(&p))) {
        if (!strncmp(l, "base ", 5)) vbase = strtoul(l + 5, 0, 16);
        else if (!strncmp(l, "conf ", 5)) conf = strtoul(l + 5, 0, 16);
        else if (!strncmp(l,"preview 2 ",10)) preview_at=strtoul(l+10,0,16);
        else if (!strncmp(l,"wheel_cache 1 ",14)) wheel_at=strtoul(l+14,0,16);
        else if (!strncmp(l, "sdb ", 4)) sdb_at = strtoul(l + 4, 0, 16);
        else if (!strncmp(l, "skin ", 5)) skin_at = strtoul(l + 5, 0, 16);
        else if (!strncmp(l, "gsm ", 4)) gsm_at = strtoul(l + 4, 0, 16);
        else if (!strncmp(l, "fast_slow ", 10)) fs_at = strtoul(l + 10, 0, 16);
        else if (!strncmp(l, "asset_memory ", 13)) asset_at = strtoul(l + 13, 0, 16);
        else if (!strncmp(l, "save 1 ", 7)) {char *q; save_at=strtoul(l+7,&q,16);save_diag_at=strtoul(q,0,16);catalog_abi|=4;}
        else if (!strncmp(l,"input 1 ",8)) {char *q;input_at=strtoul(l+8,&q,16);build_id_at=strtoul(q,0,16);}
        else if (!strncmp(l,"load_profile 1 ",15)) load_at=strtoul(l+15,0,16);
        else if (!strncmp(l,"prep_watch 1 ",13)) prep_at=strtoul(l+13,0,16);
        else if (!strncmp(l,"boot_journal 4 ",15)) {char *q;boot_journal_at=strtoul(l+15,&q,16);boot_publish_at=strtoul(q,&q,16);boot_diag_at=strtoul(q,0,16);}
        else if (!strncmp(l,"boot_journal ",13)) {logf_("Unsupported startup journal ABI\n");free(vbin);free(vt);return -1;}
        else if(!strncmp(l,"path_prefix 1 ",14)){char *q;prefix_at=strtoul(l+14,&q,16);prefix_bytes=strtoul(q,0,10);compact_extent_paths=1;}
        else if (!strcmp(l, "catalog 4")) catalog_abi |= 1;
        else if (!strcmp(l, "alphabet 1")) catalog_abi |= 16;
        else if (!strcmp(l, "coalesce 1")) catalog_abi |= 32;
        else if (!strcmp(l, "listing_family 1")) catalog_abi |= 64;
        else if (!strcmp(l, "asset_stream 3")) catalog_abi |= 2;
        else if (!strcmp(l, "asset_workspace 1")) catalog_abi |= 8;
        else if (!strcmp(l,"usb 1")) usb_runtime=1;
        else if (!strcmp(l,"usb_native_preload 1")) usb_preloaded=1;
        else if (!strcmp(l,"mmce 1")) mmce_runtime=1;
        else if (!strcmp(l,"portable 1"))portable_runtime=1;
        else if (l[0]=='S' && l[1]==' ')module_path_capacity++;
    }
    /* The quiet image contains neither capture hooks nor capture-only buffers. */
    if(!debug_enabled && (input_at || load_at)){
        logf_("Diagnostic runtime requires external debug.cfg opt-in\n");free(vbin);free(vt);return -1;
    }
    if(!!prep_at!=!!prep_watch_enabled || (prep_at && (!debug_enabled || input_at || load_at || prep_at<RES_LO || prep_at+8>RES_LO+(u32)vlen || (prep_at&3)))){
        logf_("Pre-READY diagnostic requires matching runtime and external prepprobe 1\n");free(vbin);free(vt);return -1;
    }
#ifdef PORTABLE_LAUNCHER
    if(!portable_runtime||usb_runtime||mmce_runtime||usb_preloaded){logf_("Portable runtime required\n");free(vbin);free(vt);return -1;}
#elif defined(MMCE_BENCH)
    if(!mmce_runtime || usb_runtime) {logf_("MMCE launcher requires MMCE runtime\n");free(vbin);free(vt);return -1;}
#elif defined(USB_BENCH)
    if(!usb_runtime || mmce_runtime) {logf_("USB launcher requires USB runtime\n");free(vbin);free(vt);return -1;}
#else
    if(usb_runtime || mmce_runtime) {logf_("Hardware runtime requires matching hardware launcher\n");free(vbin);free(vt);return -1;}
#endif
#ifdef USB_NATIVE_PRELOAD
    if(!usb_preloaded){logf_("Native-preload launcher requires paired runtime\n");free(vbin);free(vt);return -1;}
#else
    if(usb_preloaded){logf_("Native-preload runtime requires paired launcher\n");free(vbin);free(vt);return -1;}
#endif
#if !defined(USB_BOOT_JOURNAL) && !defined(HOST_TEST)
    if(boot_journal_at){logf_("Startup journal runtime requires matching diagnostic launcher\n");free(vbin);free(vt);return -1;}
#endif
    if (catalog_abi!=127 || !asset_at || !save_at || !save_diag_at) {logf_("runtime catalog/asset/workspace ABI missing; rebuild launcher/runtime together\n");free(vbin);free(vt);return -1;}
    if (vbase != RES_LO || vlen<0 || (u32)vlen>=RES_HI-RES_LO) {
        logf_("runtime base/size %x/%d outside reserved block\n",vbase,vlen);free(vbin);free(vt);return -1;
    }
    memcpy((void *)RES_LO, vbin, vlen);
    free(vbin);
    if(prep_at){
        if(*(u32*)prep_at!=1){free(vt);return -1;}
        ((u32*)prep_at)[1]=1;
    }
    if(compact_extent_paths){
      if(prefix_bytes!=64||prefix_at<RES_LO||prefix_at>RES_LO+(u32)vlen-64||strlen(DEV)>=64){free(vt);return -1;}
      memset((void *)prefix_at,0,prefix_bytes);memcpy((void *)prefix_at,DEV,strlen(DEV)+1);
    }
    if(select_profile_mode&&!compact_extent_paths){free(vt);return -1;}
#if defined(USB_BOOT_JOURNAL) && !defined(HOST_TEST)
    if(boot_journal_at<RES_LO || boot_journal_at+8>RES_LO+(u32)vlen ||
       build_id_at<RES_LO || build_id_at+16>RES_LO+(u32)vlen ||
       boot_publish_at<RES_LO || boot_publish_at+4>RES_LO+(u32)vlen || (boot_publish_at&3) ||
       boot_diag_at<RES_LO || boot_diag_at+16>RES_LO+(u32)vlen || (boot_diag_at&3)) {
        logf_("Startup journal requires paired runtime\n");free(vt);return -1;
    }
    usb_journal_configure(boot_journal_at,build_id_at,boot_publish_at,boot_diag_at);
#endif
    vd = (u32 *)conf;
    sdb = (sdb_t *)sdb_at;
    asset_mem = (u32 *)asset_at;
    if (vd[0] != 0x4b534456 || !sdb || sdb->magic != 0x44474e53) { logf_("runtime magic missing (old vdisc.bin?)\n"); return -1; }
    tab_cur = (u8 *)RES_LO + vlen;
    str_work=malloc(RES_HI-RES_LO-vlen);
    str_offsets=calloc(32768,sizeof *str_offsets);
    module_paths=calloc(module_path_capacity+1,sizeof *module_paths);
    if(!str_work || !str_offsets || !module_paths){logf_("launcher path workspace allocation failed\n");free(vt);return -1;}
    str_start=str_cur=str_work;str_end=str_work+(RES_HI-RES_LO-vlen);
    xb = malloc(MAXEXT * sizeof(ext_t));
    xm = malloc(MAXMEXT * sizeof(ext_t));

    /* runtime patches (line() above already split the buffer into NUL-terminated lines up to p) */
    for (l = vt; l < p; l += strlen(l) + 1) {
        char *q;
        /* Journal shutdown is independent of pad capture: keep only the
         * displaced native reset wrapper when capture is OFF. */
        if (l[0] == 'P' && !input_mode && !(boot_journal_at && strtoul(l+2,0,16)==0x1e5f84))continue;
        if (l[0] == 'W' || l[0] == 'J' || l[0]=='P') {
            u32 va = strtoul(l + 2, &q, 16), o = strtoul(q, &q, 16), n = strtoul(q, &q, 16);
            if (l[0] == 'J') n = 0x0c000000 | ((n >> 2) & 0x3ffffff);
            if (apply_word(va, o, n, "runtime")) errs++;
        } else if (l[0] == 'S') {
            u32 lui = strtoul(l + 2, &q, 16), addiu = strtoul(q, &q, 16), a;
            while (*q == ' ') q++;
            u32 offset=new_str(q);
            if(!offset || module_path_count>=module_path_capacity){errs++;continue;}
            module_paths[module_path_count++]=(module_path_t){lui,addiu,offset};
            a = RES_LO + offset;
            *W(lui) = (*W(lui) & 0xffff0000) | ((a + 0x8000) >> 16);
            *W(addiu) = (*W(addiu) & 0xffff0000) | (a & 0xffff);
        }
    }
    free(vt);
    if (save_at<RES_LO || save_at+4>RES_LO+(u32)vlen || save_diag_at<RES_LO || save_diag_at+48>RES_LO+(u32)vlen) {logf_("runtime save ABI invalid\n");errs++;}
    else {*W(save_at)=save_batch;logf_("Memory-card checkpoints: ON; autosave %s\n",save_batch?"after play at native menu exits":"native exits, unchanged requests skipped");}
    if (skin_apply(skin_at)) errs++;
    if (gsm_at >= RES_LO && gsm_at + 4 <= RES_LO + (u32)vlen) {
        *W(gsm_at) = mm_any_gas(&shared_options);
        logf_("Gauge auto shift: %s (FREE only)\n", *W(gsm_at) ? "ON" : "off");
    } else if (mods_on("gauge_shift")) {
        logf_("runtime has no gauge auto shift flag\n"); errs++;
    }

    if (fs_at >= RES_LO && !(fs_at & 3) && fs_at + 4 <= RES_LO + (u32)vlen) {
        *W(fs_at) = 1;
        logf_("FAST/SLOW: %s\n", *W(fs_at) ? "ON" : "off");
    } else if (mods_on("fast_slow")) {
        logf_("runtime has no FAST/SLOW flag\n"); errs++;
    }

    {   /* base extents */
        char *b = slurp("EMPRESS/base.txt", 0);
        if (!b) { logf_("cannot read EMPRESS/base.txt\n"); return -1; }
        p = b;
        while ((l = line(&p))) if (l[0] == 'E' && ext_line(l, xb, &nxb, MAXEXT, "base")) return -1;
        free(b);
    }

    lpl_stage(4);
    /* songs: per-slot tables sized by the enabled added songs; saves = one unplayed set per alias id */
    {
        int n_add = 0, ok = 1;
        for (i = 0; i < n_songs; i++) if (!release_covered[i] && songs[i].id < 0 && styles[songs[i].style].on) n_add++;
        wheel_labels=(u32)n_add;
        if (catalog_load(n_add))return -1;
        asset_mem[0]=FIRST+n_add;
        asset_mem[1]=(u32)tab_alloc((FIRST+n_add)*4);
        if(!asset_mem[1])return -1;
        memset((void *)asset_mem[1],0,(FIRST+n_add)*4);
        sdb->recs = tab_alloc(n_add * 0x134 + 1);
        sdb->slot_fold = tab_alloc(n_add + 1);
        sdb->alias_of = tab_alloc(n_add + 1);
        for (i = 0; i < NPOOL; i++) ok &= !!(sdb->pools[i].listed = tab_alloc(n_add + 1));
        ok &= !!(sdb->spr_off = tab_alloc(n_add * NSPR * 4 + 4));
        ok &= !!(sdb->var_main = tab_alloc(n_add * 2 + 2));
        if (ok) memset(sdb->var_main, 0xff, n_add * 2);
        free(slot_dir);
        ok &= !!(slot_dir = calloc(n_add + 1, sizeof slot_dir[0]));
        n_varq = 0;
        if (ok) memset(sdb->spr_off, 0xff, n_add * NSPR * 4);
        sdb->saves = tab_alloc(NSAVE * 20 * 0x1c);
        if (!ok || !sdb->recs || !sdb->slot_fold || !sdb->alias_of || !sdb->saves) {
            logf_("block full (%d added songs)\n", n_add); return -1;
        }
        for (i = 0; i < NSAVE * 20; i++)            /* unplayed, unlocked chart records (lamp 9 = no play) */
            memcpy(sdb->saves + i * 0x1c,
                   "\x00\x00\x00\x80\x00\x00\x00\x00\x00\x00\x00\x90\xff\x7f\x00\x00\x00\x40\x00\x00\x99\x3a\x00\x00\x10\x67\xa6\x0e",
                   0x1c);
    }
    memset(sdb->emp_fold, NOFOLD, sizeof sdb->emp_fold);
    memset(sdb->slot_of, 0xff, sizeof sdb->slot_of);
    memset(sdb->fold_n, 0, sizeof sdb->fold_n);
    {                                               /* our folders: CUSTOM = index 0, styles with a label 1.. */
        int nf = 1;
        for (i = 0; i < n_styles; i++) {
            style_t *s = &styles[i];
            s->fold = NOFOLD;
            if (s->custom) s->fold = 0;
            else if (s->label[0] && nf < NFOLD) s->fold = nf++;
            else if (s->label[0]) logf_("%s: no folder slot left (max %d)\n", s->dir, NFOLD - 1);
        }
    }
    if (!(bank = fopen(game_path("cache/sprites_build.tmp"), "wb"))) logf_("cannot write cache/sprites_build.tmp (does offdisc/cache exist?)\n");
    bank_pos = 0;
    if(wheel_at){
        if(wheel_at<RES_LO||wheel_at+8>RES_LO+(u32)vlen||(wheel_at&3)||*(u32*)wheel_at!=WC_VERSION)return -1;
        ((u32*)wheel_at)[1]=0;
        if(wheel_begin(wheel_labels))return -1;
    }
    memset(spr_seen, 0, sizeof spr_seen);
    {
        int n_emp = 0, pass;
        for (pass = 0; pass < 2; pass++)            /* CUSTOM-folder songs first (they win when ids run out) */
        for (i = 0; i < n_songs; i++) {
            song_t *sg = &songs[i];
            progress(5 + 55 * (pass * n_songs + i) / (2 * n_songs + 1), "songs");
            if (release_covered[i] || !styles[sg->style].on || styles[sg->style].custom != (pass == 0)) continue;
            if (sg->id >= 0) {                      /* EMPRESS song: its sectors + present flag */
                char *b, *q;
                if (sg->id >= FIRST) { logf_("%s: id %d is not an EMPRESS song\n", sg->dir, sg->id); errs++; continue; }
                snprintf(path, sizeof path, "songs/%s/%s/song.txt", styles[sg->style].dir, sg->dir);
                if (!(b = slurp(path, 0))) continue;
                q = b;
                while ((l = line(&q))) if (l[0] == 'E' && ext_line(l, xb, &nxb, MAXEXT, sg->dir)) { errs++; break; }
                free(b);
                sdb->present[sg->id] = 1;
                catalog_rows[sg->id]=catalog_find("id",sg->id);
                if (catalog_rows[sg->id]<0) {logf_("EMPRESS%d: missing identity\n",sg->id);errs++;}
                sdb->emp_fold[sg->id] = styles[sg->style].fold;
                if (styles[sg->style].fold != NOFOLD) sdb->fold_n[styles[sg->style].fold]++;
                n_emp++;
            } else {
                if (add_song(sg, styles[sg->style].fold)) errs++;
            }
        }
        if (bank && fclose(bank)) errs++;
        bank = 0;
        if(wheel_at && wheel_finish())errs++;
        resolve_variants();
        if (catalog_finish())errs++;
        sdb->custom_n = sdb->fold_n[0];
        logf_("songs: %d EMPRESS, %d added, %d custom\n", n_emp, sdb->n_new, sdb->custom_n);
    }
    if (folders()) errs++;
    if (n_sq && build_containers()) errs++;
    if (bank_pos) {
        u32 sec, size;
        char rel[80];
        if (cache_publish(rel, sizeof rel, "sprites", 0, bank_pos, "cache/sprites_build.tmp")) errs++;
        else if (!(sdb->spr_lba = vfile(rel, &sec, &size, "sprites"))) errs++;
        else logf_("sprite bank: %d KB\n", (int)(bank_pos >> 10));
    }
    if (spr_containers()) errs++;
    if(preview_at){
        if(preview_at<RES_LO || preview_at+24>RES_LO+(u32)vlen || (preview_at&3)){logf_("Invalid play preview ABI\n");return -1;}
        /* Preview-owned native intro extent: available even if every retail
         * song's folder is disabled. Existing identical extents deduplicate. */
        char *card=slurp("launcher/preview/base.txt",0),*cp=card,*cl;unsigned card_extents=0;
        if(!card){logf_("Missing preview intro extent\n");return -1;}
        while((cl=line(&cp))) {
            if(cl[0]!='E' || cl[1]!=' ' || ext_line(cl,xb,&nxb,MAXEXT,"preview intro")){free(card);return -1;}
            card_extents++;
        }
        free(card);if(!card_extents)return -1;
        int movie_len;char *movie=slurp("launcher/preview/movie.txt",&movie_len);
        if(!movie || movie_len<1 || movie_len>159){free(movie);logf_("Missing preview movie reference\n");return -1;}
        while(movie_len && (movie[movie_len-1]=='\n'||movie[movie_len-1]=='\r'))movie[--movie_len]=0;
        if(!movie_len || movie[0]=='/' || strchr(movie,':') || strstr(movie,"..") || strchr(movie,'\n') || memchr(movie,0,movie_len)){free(movie);return -1;}
        const char *paths[5]={"launcher/preview/chart.bin","launcher/preview/bank.wvb","launcher/preview/silence.pcm",movie,"launcher/preview/chart-dp.bin"};
        u32 *spec=(u32 *)preview_at;spec[0]=0;
        for(unsigned i=0;i<5;i++){
            u32 sec,bytes,at;
            if(n_fid+FID0>=65535 || grow((void **)&fids,&cap_fid,n_fid,12) || !(at=vfile(paths[i],&sec,&bytes,"play preview"))){free(movie);return -1;}
            fids[n_fid*3]=at-C_LBA;fids[n_fid*3+1]=sec;fids[n_fid*3+2]=bytes;spec[i+1]=FID0+n_fid++;
        }
        spec[0]=2;free(movie);
    }
    if(custom_art_build())return -1;
    /* added files and data1 containers into the block */
    if (!(sdb->fid_tab = tab_alloc(n_fid * 12 + 4))) { logf_("block full (%d files)\n", n_fid); return -1; }
    memcpy(sdb->fid_tab, fids, n_fid * 12);
    if (n_card) {                                   /* data1 loader -> the relocated pointer table */
        u32 *ptrs = tab_alloc((DATA1_N + n_card) * 4), *ents = tab_alloc(n_card * 16), a = (u32)ptrs;
        if (!ptrs || !ents) { logf_("block full (%d data1 containers)\n", n_card); return -1; }
        memcpy(ptrs, (void *)DATA1_PTRS, DATA1_N * 4);
        for (i = 0; i < n_card; i++) {
            ents[i * 4] = DATA1_N + i; ents[i * 4 + 1] = cards[i * 2]; ents[i * 4 + 2] = cards[i * 2 + 1];
            ptrs[DATA1_N + i] = (u32)&ents[i * 4];
        }
        if (apply_word(0x14d210, 0x3c030024, 0x3c030000 | ((a + 0x8000) >> 16), "data1")) errs++;
        if (apply_word(0x14d214, 0x2463b970, 0x24630000 | (a & 0xffff), "data1")) errs++;
    }

    /* mods: graphics/gameplay mods, then video */
    lpl_stage(6);
    progress(96, "mods");
    for (i = 0; i < n_mods; i++) if (mods[i].on && load_combos(mods[i].dir)) errs++;
    for (i = 0; i < n_mods; i++) if (mods[i].on && load_mod(mods[i].dir)) errs++;
    if (video && load_mod(VIDEO_DIR[video])) errs++;
    if (frame_build()) errs++;                     /* optional frame gets its own virtual extent after mods */
    if (native_asset_plans()) errs++;
    if (note_skin_build()) errs++;                  /* after the mods: grows the note containers they provide */
    /* Append after every existing virtual asset so their extent/FID identity
     * stays unchanged. The eight-byte optional runtime config is prepared-cache
     * content; older runtimes/launchers simply retain the ordinary art path. */
    if(wheel_at && wheel_labels && wheel_pos){
        char rel[80];u32 sec,size,lba;
        if(wheel_labels!=sdb->n_new || cache_publish(rel,sizeof rel,"wheel1",0,wheel_pos,"cache/wheel_build.tmp") ||
           !(lba=vfile(rel,&sec,&size,"wheel labels")))errs++;
        else {((u32*)wheel_at)[1]=lba;logf_("wheel pages: %u KB, %u labels\n",wheel_pos>>10,sdb->n_new);}
    }

    /* base extents: sort, drop exact duplicates (shared files listed by several songs), check overlaps */
    qsort(xb, nxb, sizeof(ext_t), cmp_ext);
    {
        int k = 0;
        for (i = 0; i < nxb; i++) {
            if (k && xb[i].lba == xb[k - 1].lba && xb[i].sectors == xb[k - 1].sectors) continue;
            if (k && xb[i].lba < xb[k - 1].lba + xb[k - 1].sectors) {
                logf_("overlap: %s / %s\n", (char *)RES_LO + xb[k - 1].path + 5, (char *)RES_LO + xb[i].path + 5);
                errs++;
            }
            xb[k++] = xb[i];
        }
        nxb = k;
    }
    bx = tab_alloc(nxb * sizeof(ext_t));
    mx = tab_alloc(nxm * sizeof(ext_t) + 16);
    if (!bx || !mx) { logf_("block full (extents)\n"); return -1; }
    memcpy(bx, xb, nxb * sizeof(ext_t));
    memcpy(mx, xm, nxm * sizeof(ext_t));
    free(xb); free(xm);
    xb=xm=0;
    if(load_at){
        if(load_at<RES_LO || load_at+16>RES_LO+(u32)vlen || *(u32*)load_at!=1){logf_("Invalid load profiler ABI\n");return -1;}
        u32 off=new_str(select_profile_mode?"logs/select-":"logs/load-");if(!off)return -1;load_path=RES_LO+off-(u32)str_work;
    }
    if(publish_paths(bx,nxb,mx,nxm))return -1;
    if(load_at)((u32*)load_at)[3]=(u32)str_start+(path_remap?path_remap[load_path]-1:load_path);
    vd[1] = nxb; vd[2] = nxm; vd[3] = (u32)bx; vd[4] = (u32)mx; vd[5] = RES_LO;
    /* Publish only the unused TABLE tail, after the final table allocation.
     * Strings/log/MPEG use a separate range at/above TAB_HI. Do not move the
     * game heap or sacrifice the logger's capacity to fit an animation. */
    {
        u32 at=((u32)tab_cur+127u)&~127u;
        asset_mem[10]=asset_mem[11]=asset_mem[12]=0;
        if(at<(u32)str_start) {
            asset_mem[10]=at;asset_mem[11]=((u32)str_start-at)&~127u;
            logf_("BGA texture reserve: %u KB at %x from unused table tail\n",asset_mem[11]>>10,at);
        }
    }
    /* No more table/string allocations after this point. Native240x240 MPEG
     * work needs240*240*9/2+2800 bytes; reserve only genuinely unused tail,
     * never take space away from metadata, paths, or the game's heap. */
    {
        u32 size=0x41c80u,at=(RES_HI-size)&~127u;
        asset_mem[7]=asset_mem[8]=asset_mem[9]=0;
        if(at>=(u32)str_cur && (u32)tab_cur<=at) {
            asset_mem[7]=at;asset_mem[8]=size;
            logf_("native MPEG work: %u KB at %x from unused reserved tail\n",size>>10,at);
        } else logf_("native MPEG work: reserved tail too small, native heap fallback\n");
    }
    if(input_mode){
        if(input_at<RES_LO || input_at+sizeof(input_probe_config)>RES_LO+(u32)vlen){logf_("Input capture requires diagnostic runtime\n");errs++;}
        else {
            input_probe_config *q=(input_probe_config *)input_at;
            u32 path_off=new_str("logs/input-");
            u32 at=((u32)str_cur+127u)&~127u;
            u32 limit=asset_mem[7],capacity=limit>at?(limit-at)/INPUT_PROBE_RECORD_BYTES:0;
            if(q->schema!=1 || !path_off || (input_mode!=3 && capacity<INPUT_PROBE_MIN_RECORDS)){logf_("Input capture: reserved space too small (%u records); use small bench\n",capacity);errs++;}
            else {q->buffer=input_mode==3?0:at;q->capacity=input_mode==3?0:capacity;q->path=RES_LO+path_off;q->mode=input_mode;
                logf_("Input capture: %s, %u records (%u bytes) at %x, no additional game heap\n",INPUT_MODES[input_mode],q->capacity,q->capacity*INPUT_PROBE_RECORD_BYTES,q->buffer);}
        }
    }
    logf_("block: tables %d KB, strings %d KB of %d KB\n", (int)((u32)tab_cur - RES_LO) >> 10,
          (int)(str_cur-str_start) >> 10, (RES_HI - RES_LO) >> 10);
    lpl_stage(7);
    progress(100, "done");
    return errs;
}

#include "build_cache.h"

#ifdef LOAD_PROFILE
#define build build_profile_body
#endif
/* Resolve and rebind AFTER either cold build or prepared restore. Calibration,
 * duration and marker switches are live policy, never baked into a cache. */
#ifdef LOAD_PROFILE
/* floor(count * bus_hz / bus), without overflowing after a long launcher stay.
 * Divide the integer part first; accumulate the fractional product modulo bus.
 * The measured clock must fit the same1..600MHz range as the receiver schema. */
static unsigned select_clock_hz(lpl_u64 count,lpl_u64 bus,unsigned bus_hz)
{
 if(!bus||!bus_hz)return 0;
 lpl_u64 whole=count/bus,a=count%bus,rem=0;
 if(whole>600000000u/bus_hz)return 0;
 unsigned fraction=0;
 for(int bit=31;bit>=0;bit--){
  fraction<<=1;
  if(rem>=bus-rem){rem-=bus-rem;fraction++;}else rem+=rem;
  if((bus_hz>>bit)&1u){if(rem>=bus-a){rem-=bus-a;fraction++;}else rem+=a;}
 }
 lpl_u64 hz=whole*bus_hz+fraction;
 return hz>=1000000u&&hz<=600000000u?(unsigned)hz:0;
}
#endif
static int select_profile_bind(void)
{
 select_runtime=0;if(!select_profile_mode)return 0;
 int length=0,seen=0;unsigned at=0,size=0;char *meta=slurp(runtime_txt(),&length),*p=meta,*l;
 long bytes=fsize(runtime_bin());if(!meta||length<=0||bytes<(long)sizeof(msp_config)){free(meta);return -1;}
 while((l=line(&p)))if(!strncmp(l,"select_profile ",15)){
  unsigned abi;char extra;if(seen++||sscanf(l+15,"%u %x %u %c",&abi,&at,&size,&extra)!=3||abi!=MSP_SELECT_ABI){free(meta);return -1;}
 }
 free(meta);
 if(seen!=1||size!=sizeof(msp_config)||(at&3)||at<RES_LO||at>RES_LO+(unsigned)bytes-size||*(u32 *)(unsigned long)at!=MSP_SELECT_ABI)return -1;
 select_runtime=(msp_config *)(unsigned long)at;
 *select_runtime=(msp_config){.abi=MSP_SELECT_ABI,.enabled=select_profile_mode,.seconds=select_profile_seconds,.osd=select_profile_osd,.peer=0x0a4d4d01u,.local=0x0a4d4d02u,.video=video};
#ifdef LOAD_PROFILE
 if(!lpl_cal_count||!lpl_cal_bus||!lpl_cal_samples)return -1;
 select_runtime->count_lo=(u32)lpl_cal_count;select_runtime->count_hi=(u32)(lpl_cal_count>>32);
 select_runtime->bus_lo=(u32)lpl_cal_bus;select_runtime->bus_hi=(u32)(lpl_cal_bus>>32);
 select_runtime->samples=lpl_cal_samples;select_runtime->bus_hz=147456000u;
 unsigned hz=select_clock_hz(lpl_cal_count,lpl_cal_bus,147456000u);
 if(!hz)return -1;select_runtime->reserved=hz;
#else
 return -1;
#endif
 return 0;
}

static int build(u32 *entry)
{
    int result;u32 key[2];
    progress(0,"checking prepared build");lpl_stage(3);
    int sealed=bc_key(key);lpl_stage(4);
    module_path_count=module_path_capacity=0;
    nxb=nxm=0;xb=xm=0;
    if(sealed && bc_load(key,entry)){result=0;progress(100,"prepared build");lpl_stage(7);}
    else {
        result=build_inner(entry);
        if(!result && sealed)bc_store(key,*entry);
    }
    if(!result && save_profile_bind())result=-1;
    if(!result && select_profile_bind())result=-1;
    if(!result && shared_settings_bind())result=-1;
#ifdef USB_HID
    if(!result && usb_runtime_resolve())result=-1;
#endif
    free(str_work);str_work=0;
    free(str_offsets);str_offsets=0;
    free(path_remap);path_remap=0;
    free(module_paths);module_paths=0;
    free(xb);free(xm);xb=xm=0;
    free(vfile_index);vfile_index=0;
    logf_("Metadata: %u queries, %u size/%u manifest/%u header hits, %u bytes peak\n",lm_queries,lm_size_hits,lm_text_hits,lm_measure_hits,lm_peak);
    lm_clear();
    return result;
}
#ifdef LOAD_PROFILE
#undef build
static int build(u32 *entry){if(!lpl_enabled)return build_profile_body(entry);unsigned phase=lpl_phase;lpl_u64 t=lpl_clock();int r=build_profile_body(entry);lpl_add(phase,LPL_BUILD,t,0,r!=0);return r;}
#endif


#ifndef HOST_TEST
/* ---- screen ---- */
static GSGLOBAL *gs;
static GSTEXTURE t_logo, t_font;
static int gfx_video=-1;
#define RGBA(r, g, b, a) GS_SETREG_RGBAQ(r, g, b, a, 0)
#define WHITE RGBA(0x80, 0x80, 0x80, 0x80)
#define C_TEXT RGBA(0x78, 0x78, 0x78, 0x80)
#define C_DIM RGBA(0x50, 0x48, 0x58, 0x80)
#define C_HEAD RGBA(0x80, 0x50, 0x70, 0x80)
#define C_SEL RGBA(0x80, 0x80, 0x80, 0x80)
#define C_ON RGBA(0x80, 0x30, 0x60, 0x80)

static void gfx_apply_video(void)
{
    if(gfx_video==video)return;
    /* At cold start no GIF DMA has completed yet. The fast COP0 wait needs
     * a latched completion flag and may never return before the first send.
     * Only an existing screen has a queue to drain; poll GIF's busy bit so a
     * live mode switch also works with an idle channel/cleared completion. */
    if(gfx_video>=0){
        dmaKit_wait(DMA_CHANNEL_GIF,0);
        gsKit_queue_reset(gs->Os_Queue);
    }
    gs->Width=640;gs->Height=video==2?480:448;
    gs->Mode=video?GS_MODE_DTV_480P:GS_MODE_NTSC;
    gs->Interlace=video?GS_NONINTERLACED:GS_INTERLACED;
    gs->Field=video?GS_FRAME:GS_FIELD;
    gs->ActiveBuffer=1;gs->PrimContext=0;gs->EvenOrOdd=0;
    gsKit_init_screen(gs);
    gsKit_mode_switch(gs,GS_ONESHOT);
    gsKit_set_primalpha(gs,GS_SETREG_ALPHA(0,1,0,1,0),0);
    gsKit_set_test(gs,GS_ATEST_OFF);
    gsKit_TexManager_init(gs);
    gfx_video=video;
}

static void gfx_init(void)
{
    gs = gsKit_init_global();
    gs->PSM = GS_PSM_CT24;
    gs->ZBuffering = GS_SETTING_OFF;
    gs->PrimAlphaEnable = GS_SETTING_ON;
    dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC, D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);
    dmaKit_chan_init(DMA_CHANNEL_GIF);
    gfx_apply_video();
    t_logo.Width = LOGO_W; t_logo.Height = LOGO_H; t_logo.PSM = GS_PSM_CT32;
    t_logo.Mem = (u32 *)logo_px; t_logo.Filter = GS_FILTER_LINEAR;
    t_font.Width = FONT_W; t_font.Height = FONT_H; t_font.PSM = GS_PSM_CT32;
    t_font.Mem = (u32 *)font_px; t_font.Filter = GS_FILTER_NEAREST;
}

static int text_w(const char *s)
{
    int w = 0;
    for (; *s; s++) if (*s >= 32 && *s < 127) w += glyph[*s - 32][4];
    return w;
}

static void text_fit(int x,int y,const char *s,u64 col,int width)
{
    int total=text_w(s);float scale=total>width && total?((float)width/total):1.0f;
    float at=x;if(width<=0)return;
    gsKit_TexManager_bind(gs,&t_font);
    for(;*s;s++){
        const short *g;if(*s<32 || *s>=127)continue;g=glyph[*s-32];
        if(*s!=' ')gsKit_prim_sprite_texture(gs,&t_font,at+g[3]*scale,y,g[0],g[1],
            at+(g[3]+g[2])*scale,y+FONT_CELL,g[0]+g[2],g[1]+FONT_CELL,1,col);
        at+=g[4]*scale;
    }
}
static void text(int x,int y,const char *s,u64 col){text_fit(x,y,s,col,gs->Width-24-x);}


static void text_c(int y,const char *s,u64 col)
{
    int w=text_w(s);if(w>gs->Width-48)w=gs->Width-48;
    text_fit((gs->Width-w)/2,y,s,col,w);
}

static void frame_begin(void)
{
    gsKit_clear(gs, GS_SETREG_RGBAQ(0x08, 0x02, 0x0c, 0x80, 0));
}

static void frame_end(void)
{
    lpl_calibrate();
    gsKit_queue_exec(gs);
    gsKit_sync_flip(gs);
    gsKit_TexManager_nextFrame(gs);
}

/* logo centred at the top; returns the y below it */
static int draw_logo(int y, int w)
{
    int h = w * LOGO_H / LOGO_W, x = (gs->Width - w) / 2;
    gsKit_TexManager_bind(gs, &t_logo);
    gsKit_set_primalpha(gs, GS_SETREG_ALPHA(0, 2, 2, 1, 0x80), 0);    /* additive: opaque black sheet, like the game */
    gsKit_prim_sprite_texture(gs, &t_logo, x, y, 0, 0, x + w, y + h, LOGO_W, LOGO_H, 1, WHITE);
    gsKit_set_primalpha(gs, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
    return y + h;
}

#define ROW 26

/* a menu row centred on x = xc */
static void row_at(int xc,int y,int sel,const char *s,u64 col)
{
    int w=text_w(s)+24;if(w>gs->Width-48)w=gs->Width-48;
    if(sel)gsKit_prim_sprite(gs,xc-w/2,y+3,xc+w/2,y+ROW+1,1,RGBA(0x70,0x10,0x48,0x50));
    text_fit(xc-(w-24)/2,y,s,sel?C_SEL:col,w-24);
}

static void row(int y, int sel, const char *s, u64 col) { row_at(gs->Width / 2, y, sel, s, col); }

/* styles that have songs (the Songs submenu) */
static int vis_style[MAXSTYLE], n_vis;

/* Profile management is parked. Keep the selected compact profile and its
 * guarded first-start/recovery path; ordinary navigation does not expose it. */
#ifdef USB_HID
#define MAIN_BASE_ROWS 5
#else
#define MAIN_BASE_ROWS 4
#endif
#ifdef PORTABLE_LAUNCHER
#define MAIN_ROWS (MAIN_BASE_ROWS+1)
#define DL_LOCATION_ROW MAIN_BASE_ROWS
#else
#define MAIN_ROWS MAIN_BASE_ROWS
#endif
static void draw_main(int cur)
{
    char b[160];
    int compact=gs->Height<480,step=compact?28:32;
    int i,on=0,y,logo_w=compact?170:220;
    int footer_rows=2;
#ifdef USB_HID
    footer_rows=3;
#endif
    int total=logo_w*LOGO_H/LOGO_W+12+MAIN_ROWS*step+12+(footer_rows-1)*ROW+FONT_CELL;
    frame_begin();
    y = (gs->Height - total) / 2;
    if (y < 24) y = 24;
    y = draw_logo(y, logo_w) + 12;
    snprintf(b, sizeof b, "Video:  < %s >", VIDEO[video]);
    row(y, cur == 0, b, C_TEXT); y += step;
    for (i = on = 0; i < n_vis; i++) on += styles[vis_style[i]].on;
    snprintf(b, sizeof b, "Songs  (%d of %d folders on)  >", on, n_vis);
    row(y, cur == 1, b, C_TEXT); y += step;
    snprintf(b, sizeof b, "Skin settings  (lane %d.%d, keybeam %d.%d)  >", lane_b / 10, lane_b % 10,
             beam_len / 10, beam_len % 10);
    row(y, cur == 2, b, C_TEXT); y += step;
    snprintf(b,sizeof b,"Autosave: < %s >",save_batch?"after play":"native exits");
    row(y,cur==3,b,C_TEXT); y+=step;
#ifdef USB_HID
    row(y,cur==4,"Bind USB controller  >",C_TEXT);y+=step;
#endif
#ifdef PORTABLE_LAUNCHER
    row(y,cur==DL_LOCATION_ROW,"Change game file location  >",C_TEXT);y+=step;
#endif
    y+=12;
#ifdef PORTABLE_LAUNCHER
    if(cur==DL_LOCATION_ROW){
        text_c(y,dl_location_notice[0]?dl_location_notice:"Choose another folder; restart required",C_DIM);
        text_fit(24,y+ROW,dl_location_changed?dl_next_game:DEV,C_DIM,gs->Width-48);
    }else{
#endif
    text_c(y,"UP / DOWN  select     LEFT / RIGHT  change     X  open",C_DIM);
    text_c(y+ROW,cur==3?(save_batch?"After FREE results; requires AUTOSAVE":"Uses in-game AUTOSAVE at native exits"):"START  build and play",C_DIM);
#ifdef PORTABLE_LAUNCHER
    }
#endif
#ifdef USB_HID
    text_c(y+ROW*2,mui_navigation?"DualShock + USB 1/2/6; SELECT: KOC BM":"DualShock + USB 1/2/6; SELECT: KOC pad",C_DIM);
#endif
    frame_end();
}

/* submenu cell: on/off tag in its own colour (stays readable on the highlighted row) + name */
static void cell(int x, int w, int y, int sel, int on, const char *name)
{
    const char *tag = on ? "ON" : "off";
    if (sel) gsKit_prim_sprite(gs, x, y + 3, x + w, y + ROW + 1, 1, RGBA(0x70, 0x10, 0x48, 0x50));
    text(x + 10, y, tag, on ? C_ON : C_DIM);
    text_fit(x + 10 + text_w("off") + 14,y,name,sel?C_SEL:on?C_TEXT:C_DIM,w-text_w("off")-34);
}

/* submenu 1 = mods, 2 = songs (two columns when long) */
static int sub_n(int sub) { return (sub == 1 ? n_mods : n_vis)+1; }
static int sub_cols(int sub) { return sub == 2 && n_vis > 9 ? 2 : 1; }

static void draw_sub(int sub,int cur)
{
    char b[160];int n=sub_n(sub),cols=sub_cols(sub),per=(n+cols-1)/cols;
    int capacity=(gs->Height-48-(ROW+12)-(2*ROW+12))/ROW;
    int visible=per<capacity?per:capacity,top=0;
    if(per && cur%per>=visible)top=cur%per-visible+1;
    int cw=cols==1?420:(gs->Width-48)/2,x0=(gs->Width-cols*cw)/2;
    int total=(ROW+12)+(visible?visible:1)*ROW+12+ROW+FONT_CELL;
    int y=(gs->Height-total)/2;if(y<24)y=24;
    frame_begin();
    snprintf(b,sizeof b,per>visible?"%s  (%d-%d / %d rows)":"%s",sub==1?"MODS":"SONGS",top+1,top+visible,per);
    text_c(y,b,C_HEAD);y+=ROW+12;
    for(int i=0;i<n;i++){
        int c=i/per,r=i%per;if(r<top || r>=top+visible)continue;
        int yy=y+(r-top)*ROW;
        if(i==n-1){row_at(x0+c*cw+(cw-8)/2,yy,cur==i,"Exit",C_TEXT);continue;}
        if(sub==1)cell(x0+c*cw,cw-8,yy,cur==i,mods[i].on,mods[i].name);
        else{style_t *st=&styles[vis_style[i]];snprintf(b,sizeof b,"%s  (%d)",st->name,st->n);cell(x0+c*cw,cw-8,yy,cur==i,st->on,b);}
    }
    if(!n)text_c(y,sub==1?"(mods.txt is empty)":"(no song folders found)",C_DIM);
    y+=(visible?visible:1)*ROW+12;
    text_c(y,cols>1?"UP / DOWN  select     LEFT / RIGHT  column":"UP / DOWN  select     X  on / off",C_DIM);
    text_c(y+ROW,cols>1?"X  on / off     O  back     START  play":"O  back     START  play",C_DIM);
    frame_end();
}

/* ---- Skin settings screen: settings on the left, live playfield preview on the right ----
 * Preview art = the game's own sprites (an enabled mod's file for the container's sectors first, like the game will
 * load them): lane = skin #67 (SP 1P) sprite 3, keybeams = #109 sprites 7 / 9 / 11 (turntable / white / black key,
 * anims 0x19 / 0x1a / 0x18). Keys are pressed by a fixed demo pattern; each beam runs the game's logic (FUN_001fe040:
 * a press restarts the animation at frame 2, holding keeps it at 3..4) and its scale keys (sy 0 -> 100 % over frames
 * 0..6, then sx 100 -> 0 % over 6..20). The preview anchors every animation phase at the judge line and clips
 * notes/beams (with proportional UVs) to the lane. */
static GSTEXTURE t_lane, t_beam[3], t_note[3];   /* notes: turntable / white / black */
static int prev_state;                            /* 0 = not loaded / mods changed, 1 = ok, -1 = failed */
static int notes_for = -1;                        /* note_skin the note textures show (-1 = reload) */

/* data1 container idx as the game will load it: a C line (all its mods on) or E line of an enabled mod that covers
 * its first sector, else EMPRESS/D/<idx>.bin */
static u8 *d1_load(int idx, int *len)
{
    char path[160], *b, *p, *l;
    u8 e[16];
    u32 lba = 0;
    int i, pass;
    FILE *f = fopen(game_path("EMPRESS/SLPM_552.21"), "rb");
    if (f) {
        fseek(f, DATA1_TAB - 0x100000 + 0x80 + idx * 16, SEEK_SET);
        if (fread(e, 1, 16, f) == 16) lba = DATA1_LBA + (e[4] | e[5] << 8 | e[6] << 16 | (u32)e[7] << 24);
        fclose(f);
    }
    for (pass = 0; pass < 2 && lba; pass++)
        for (i = 0; i < n_mods; i++) {
            if (!mods[i].on) continue;
            snprintf(path, sizeof path, "mods/%s/mod.txt", mods[i].dir);
            if (!(b = slurp(path, 0))) continue;
            p = b;
            while ((l = line(&p))) {
                char *q = l + 2, who[128];
                if (l[0] != (pass ? 'E' : 'C') || l[1] != ' ') continue;
                if (!pass) {
                    snprintf(who, sizeof who, "%.*s", (int)strcspn(q, " "), q);
                    if (!mods_on(who)) continue;
                    q += strlen(who);
                }
                if (strtoul(q, &q, 10) != lba) continue;
                strtoul(q, &q, 10); strtoul(q, &q, 10);
                while (*q == ' ') q++;
                snprintf(path, sizeof path, "%s", q);
                free(b);
                return (u8 *)slurp(path, len);
            }
            free(b);
        }
    snprintf(path, sizeof path, "EMPRESS/D/%04d.bin", idx);
    return (u8 *)slurp(path, len);
}

static int sprite_tex(GSTEXTURE *t, const u8 *cont, int len, int sprite, int filter)
{
    int w, h;
    u32 *px = cont ? cont_sprite_rgba(cont, len, sprite, &w, &h) : 0;
    if (!px) return -1;
    if (t->Mem) gsKit_TexManager_invalidate(gs, t);  /* reload (mods changed): drop the GS copy too */
    free(t->Mem);
    t->Mem = memalign(128, w * h * 4);
    if (!t->Mem) { free(px); return -1; }
    memcpy(t->Mem, px, w * h * 4);
    free(px);
    t->Width = w; t->Height = h; t->PSM = GS_PSM_CT32; t->Filter = filter;
    return 0;
}

/* a note skin sprite file (mk_note_skins.py: own palette) as a texture */
static int spr_tex(GSTEXTURE *t, const char *path)
{
    spr_t s;
    int len, i;
    u8 *b = spr_file(path, &s, &len);
    u32 *px;
    if (!b || !s.pal) { free(b); return -1; }
    if (t->Mem) gsKit_TexManager_invalidate(gs, t);
    free(t->Mem);
    if (!(px = t->Mem = memalign(128, s.w * s.h * 4))) { free(b); return -1; }
    for (i = 0; i < s.w * s.h; i++) {
        int v = s.px[i], at = (v ^ ((((v >> 3) ^ (v >> 4)) & 1) * 0x18)) * 4;
        px[i] = s.pal[at] | s.pal[at + 1] << 8 | s.pal[at + 2] << 16 | (u32)s.pal[at + 3] << 24;
    }
    free(b);
    t->Width = s.w; t->Height = s.h; t->PSM = GS_PSM_CT32; t->Filter = GS_FILTER_LINEAR;
    return 0;
}

/* note textures of the current note skin: EMPRESS's own = #109 sprites 601 / 606 / 610 (as the mods provide it) */
static int notes_load(void)
{
    static const char *kind[3] = { "turn", "white", "black" };
    static const int spr[3] = { 601, 606, 610 };
    char path[96];
    int k, len, bad = 0;
    if (note_skin) {
        for (k = 0; k < 3; k++) {
            snprintf(path, sizeof path, "skins/notes/%s/sp_%s.spr", nskins[note_skin - 1].dir, kind[k]);
            bad |= spr_tex(&t_note[k], path);
        }
    } else {
        u8 *d = d1_load(109, &len);
        for (k = 0; k < 3; k++) bad |= sprite_tex(&t_note[k], d, len, spr[k], GS_FILTER_LINEAR);
        free(d);
    }
    notes_for = note_skin;
    return bad;
}

static void preview_load(void)
{
    static const int beam_spr[3] = { 7, 9, 11 };
    int len, k, bad;
    u8 *d = d1_load(67, &len);
    bad = sprite_tex(&t_lane, d, len, 3, GS_FILTER_NEAREST);
    free(d);
    d = d1_load(109, &len);
    for (k = 0; k < 3; k++) bad |= sprite_tex(&t_beam[k], d, len, beam_spr[k], GS_FILTER_LINEAR);
    free(d);
    bad |= notes_load();
    prev_state = bad ? -1 : 1;
}

/* one keybeam per lane: 0 = turntable, 1..7 keys (odd = white, even = black) */
static int beam_f[8];

/* Clip destination and UVs together: tall beams and incoming notes must stay
 * inside the preview without stretching their visible portion. */
static void preview_sprite(GSTEXTURE *tx, float x, float y, float w, float h, int x0, int y0)
{
    float l = x, r = x + w, top = y, b = y + h;
    if (w <= 0.0f || h <= 0.0f) return;
    if (l < x0) l = x0;
    if (r > x0 + 144) r = x0 + 144;
    if (top < y0) top = y0;
    if (b > y0 + 321) b = y0 + 321;
    if (l >= r || top >= b) return;
    gsKit_TexManager_bind(gs, tx);
    gsKit_prim_sprite_texture(gs, tx, l, top, (l - x) * tx->Width / w, (top - y) * tx->Height / h,
                              r, b, (r - x) * tx->Width / w, (b - y) * tx->Height / h, 2, WHITE);
}

static void preview_draw(int x0, int y0, int t)
{
    static const int lane_x[8] = { 0, 31, 49, 63, 81, 95, 113, 127 };
    static const int lane_w[8] = { 30, 17, 13, 17, 13, 17, 13, 17 };
    static int held_old;
    int i, held = 0;
    u32 c = lane_tint() & 0xff;
    gsKit_prim_sprite(gs, x0 - 6, y0 - 6, x0 + 150, y0 + 333, 1, RGBA(0x30, 0x10, 0x28, 0x80));    /* frame */
    gsKit_prim_sprite(gs, x0 - 4, y0 - 4, x0 + 148, y0 + 331, 1, RGBA(0x00, 0x00, 0x00, 0x80));
    gsKit_TexManager_bind(gs, &t_lane);
    gsKit_prim_sprite_texture(gs, &t_lane, x0, y0, 0, 0, x0 + t_lane.Width, y0 + t_lane.Height,
                              t_lane.Width, t_lane.Height, 1, RGBA(c, c, c, 0x80));
    if (notes_for != note_skin && notes_load()) prev_state = -1;
    for (i = 0; i < 8; i++) {                        /* demo pattern: lane i pressed for hold_i of every period_i */
        int period = 41 + i * 13, hold = 4 + (i * 7) % 23, T;
        GSTEXTURE *tn = &t_note[i == 0 ? 0 : (i & 1) ? 1 : 2];
        float nh = 6.0f * note_h / 10.0f;            /* note height on screen: EMPRESS's 6 rows x the setting */
        if ((t + i * 17) % period < hold) held |= 1 << i;
        /* the notes of this lane's coming presses, falling 4 px a frame onto the judge line (behind the beams) */
        for (T = t + (period - (t + i * 17) % period) % period; T <= t + 82; T += period) {
            float yb = y0 + 320 - (T - t) * 4.0f;    /* the note's bottom (like the game: on the measure line) */
            preview_sprite(tn, x0 + lane_x[i], yb - nh, lane_w[i], nh, x0, y0);
        }
    }
    for (i = 0; i < 8; i++) {
        int k = i == 0 ? 0 : (i & 1) ? 1 : 2, f;
        float sx, sy, w, h, x;                       /* sub-pixel like the game (no 1-px jitter while narrowing) */
        GSTEXTURE *tx = &t_beam[k];
        if ((held & ~held_old) >> i & 1) beam_f[i] = 2;                   /* press */
        if ((held >> i & 1) && beam_f[i] > 3) beam_f[i] = 3;               /* held */
        f = ++beam_f[i];
        if (f > 1000) beam_f[i] = f = 1000;
        sy = f >= 6 ? 1.0f : f / 6.0f;
        sx = f <= 6 ? 1.0f : f >= 20 ? 0.0f : 1.0f - (f - 6) / 14.0f;
        w = lane_w[i] * sx;
        h = 256.0f * beam_len / 10.0f * sy;
        x = x0 + lane_x[i] + (lane_w[i] - w) / 2.0f;
        if (w > 0.0f && sy > 0.0f) {
            preview_sprite(tx, x, y0 + 321 - h, w, h, x0, y0);
        }
    }
    held_old = held;
    gsKit_prim_sprite(gs, x0, y0 + 317, x0 + 144, y0 + 321, 3, RGBA(0x80, 0x10, 0x10, 0x80));      /* judge line */
}

/* the settings rows: value, range, SQUARE default */
#define SKIN_ROWS 5
static void skin_row(int k, int **v, int *lo, int *hi, int *def)
{
    static int *const val[SKIN_ROWS] = { &lane_b, &beam_len, &note_skin, &note_h, &frame_pack };
    static const int l[SKIN_ROWS] = { 0, SKIN_BEAM_MIN, 0, SKIN_NOTE_MIN, 0 }, d[SKIN_ROWS] = { 10, 10, 0, 10, 0 };
    *v = val[k]; *lo = l[k]; *def = d[k];
    *hi = k == 0 ? SKIN_LANE_MAX : k == 1 ? SKIN_BEAM_MAX : k == 2 ? n_nskins : k == 3 ? SKIN_NOTE_MAX : FRAME_COUNT - 1;
}

static void draw_skin(int cur, int t)
{
    char b[96];
    int y, x = 40, w = 330, i, px = gs->Width - 200, py = (gs->Height - 321) / 2;
    frame_begin();
    y = py;
    text(x, y, "SKIN SETTINGS", C_HEAD); y += ROW + 16;
    for (i = 0; i < SKIN_ROWS; i++) {
        if (i == 0) snprintf(b, sizeof b, "Lane brightness      < %d.%d >", lane_b / 10, lane_b % 10);
        if (i == 1) snprintf(b, sizeof b, "Keybeam length       < %d.%d >", beam_len / 10, beam_len % 10);
        if (i == 2) snprintf(b, sizeof b, "Notes   < %s >", note_skin ? nskins[note_skin - 1].name : "EMPRESS");
        if (i == 3) snprintf(b, sizeof b, "Note height          < %d.%d >", note_h / 10, note_h % 10);
        if (i == 4) snprintf(b, sizeof b, "Frame  < %s >", FRAME_NAME[frame_pack]);
        if (cur == i) gsKit_prim_sprite(gs, x - 10, y + 3, x + w, y + ROW + 1, 1, RGBA(0x70, 0x10, 0x48, 0x50));
        text(x, y, b, cur == i ? C_SEL : C_TEXT); y += ROW + 8;
    }
    if(cur==SKIN_ROWS)gsKit_prim_sprite(gs,x-10,y+3,x+w,y+ROW+1,1,RGBA(0x70,0x10,0x48,0x50));
    text(x,y,"Exit",cur==SKIN_ROWS?C_SEL:C_TEXT);y+=ROW;
    y += 8;
    text(x, y, "LEFT / RIGHT  change", C_DIM); y += ROW;
    text(x, y, "SQUARE  back to default", C_DIM); y += ROW;
    text(x, y, "O  back     START  play", C_DIM);
    if (frame_pack) text(px - 15, py - ROW, "P1 WIDE SP IN GAME", C_DIM);
    if (prev_state > 0) preview_draw(px, py, t);
    else text(px - 40, py + 140, prev_state < 0 ? "(no preview: art not found)" : "loading...", C_DIM);
    frame_end();
}

/* build progress: logo, title, an EMPRESS-coloured bar (magenta -> pink with a light top edge), percent + step.
 * Redrawn only when the percent moves by 2 or the step changes (every redraw waits for a vsync). */
static void progress(int pct, const char *what)
{
    static int last = -100;
    static const char *last_what;
    char b[96];
    int logo = gs->Height >= 480 ? 360 : 320, bw = 420, bh = 18, x, y, fw;
    if (pct < last + 2 && what == last_what && pct < 100) return;
    if(log_stream && (what!=last_what || pct==100))fflush(log_stream);
    last = pct; last_what = what;
    if (pct > 100) pct = 100;
    frame_begin();
    y = (gs->Height - (logo * LOGO_H / LOGO_W + 16 + ROW + 12 + bh + 12 + ROW)) / 2;
    y = draw_logo(y, logo) + 16;
    text_c(y, "building the song list", C_TEXT); y += ROW + 12;
    x = (gs->Width - bw) / 2;
    gsKit_prim_sprite(gs, x - 2, y - 2, x + bw + 2, y + bh + 2, 1, RGBA(0x50, 0x28, 0x48, 0x80));     /* frame */
    gsKit_prim_sprite(gs, x, y, x + bw, y + bh, 1, RGBA(0x14, 0x04, 0x10, 0x80));                     /* track */
    fw = bw * pct / 100;
    if (fw > 0) {
        gsKit_prim_quad_gouraud(gs, x, y, x + fw, y, x, y + bh, x + fw, y + bh, 1,
                                RGBA(0x60, 0x08, 0x3c, 0x80), RGBA(0x80, 0x38, 0x68, 0x80),
                                RGBA(0x48, 0x04, 0x2c, 0x80), RGBA(0x70, 0x28, 0x58, 0x80));
        gsKit_prim_sprite(gs, x, y, x + fw, y + 2, 1, RGBA(0x80, 0x68, 0x7c, 0x80));                  /* top edge */
    }
    y += bh + 12;
    snprintf(b, sizeof b, "%d%%   %s", pct, what);
    text_c(y, b, C_DIM);
    frame_end();
}

/* full-screen message: logo + centred lines of `msg` */
static void message(const char *msg,const char *footer)
{
    static char buf[sizeof log_];char *p=buf,*l,*lines[12];
    int logo=gs->Height<480?140:180,logo_h=logo*LOGO_H/LOGO_W,n=0;
    int cap=(gs->Height-48-logo_h-12-(footer?FONT_CELL+8:0))/ROW;
    if(cap>12)cap=12;if(cap<1)cap=1;
    snprintf(buf,sizeof buf,"%s",msg);
    while((l=line(&p))){if(n==cap){for(int i=1;i<n;i++)lines[i-1]=lines[i];n--;}lines[n++]=l;}
    int total=logo_h+12+n*ROW+(footer?FONT_CELL+8:0),y=(gs->Height-total)/2;
    if(y<24)y=24;frame_begin();y=draw_logo(y,logo)+12;
    for(int i=0;i<n;i++){text_c(y,lines[i],C_TEXT);y+=ROW;}
    if(footer)text_c(y+8,footer,C_ON);frame_end();
}

#ifdef HARDWARE_BENCH
static void bench_report(const char *text){message(text,BENCH_LABEL " boot trace");}
#endif
#ifdef MMCE_BENCH
void msp_boot_progress(const char *name,int id,int result)
{
 char text[96];
 if(id<0&&result==0)snprintf(text,sizeof text,"profiler: loading %s...",name);
 else snprintf(text,sizeof text,"profiler: %s id=%d start=%d",name,id,result);
 message(text,"R125 handoff; network remains inactive");
 bench_boot_note("N01 %s",text);bench_boot_flush_log();
}
#endif

#ifdef USB_HID
#include "usb_input_ui.h"
#endif
#include "profile_manager.h"
#ifdef PORTABLE_LAUNCHER
static int dl_save_active_settings(void){
    if(cfg_save())return -1;
#ifdef USB_HID
    if(mui_save(SETTINGS_ROOT))return -1;
#endif
    return 0;
}
#include "deployment_ui.h"
#endif
int main(int argc,char **argv)
{
    int cur = 0, rows, k = 0, i, sub = 0, tick = 0;
    u32 old = 0, entry = 0;
    static char *game_argv[] = { "cdrom0:\\SLPM_552.21;1" };

#ifdef PORTABLE_LAUNCHER
    if(argc&&argv&&argv[0]&&!dl_exedir(argv[0]))strcpy(dl_boot,dl_config);
#endif
    lpl_stage(0);
    SifInitRpc(0);
    gfx_init();
#ifdef HARDWARE_BENCH
    bench_boot_set_reporter(bench_report);
    message("starting storage...",0);
    if(bench_boot_prepare(0))for(;;)message(bench_boot_status(),BENCH_LABEL " startup failed");
#endif
#ifdef PORTABLE_LAUNCHER
    pad_start();
    if(dl_setup(argc,argv))for(;;)message("Could not initialize deployment settings",cfg_error_status);
#endif
    debug_load();
#ifdef PORTABLE_LAUNCHER
    if(select_profile_mode)for(;;)message("SELECT profiling requires the existing test launcher","Set selectprofile 0 in the selected debug.cfg");
#endif
#ifdef HARDWARE_BENCH
    list_trace=launcher_debug;
    bench_boot_note("L01 read mods.txt");
#endif
    lpl_stage(1);
    message("loading lists...", 0);
    read_mods();
#ifdef HARDWARE_BENCH
    bench_boot_note("L02 read songs/styles.txt and song indexes");
#endif
    read_styles();
#ifdef HARDWARE_BENCH
    bench_boot_note("L03 read available note skins");
#endif
    read_note_skins();
#ifdef HARDWARE_BENCH
    bench_boot_note("L04 read frame packs");
#endif
    read_frames();
    for (i = 0; i < n_styles; i++) if (styles[i].n) vis_style[n_vis++] = i;
    cfg_load();
#ifdef MMCE_BENCH
    if(mmce_boot_set_game_id(selected_game_id))save_config_error=1;
#endif
#ifdef PORTABLE_LAUNCHER
    if(portable_game_id(selected_game_id))save_config_error=1;
#endif
    gfx_apply_video();
#ifdef HARDWARE_BENCH
    bench_boot_note("L05 lists ready: %d styles / %d songs",n_styles,n_songs);
    bench_boot_note("L06 config: video=%d inputlog=%d frame=%d",video,input_mode,frame_pack);
    list_trace=0;
    bench_boot_flush_log();
    bench_boot_note("L07 initialize launcher controller");
#endif
#ifndef PORTABLE_LAUNCHER
    pad_start();
#endif
#ifdef USB_HID
#ifdef PORTABLE_LAUNCHER
    mui_load(dl_binding_import[0]?dl_binding_import:SETTINGS_ROOT);
#else
    mui_load(SETTINGS_ROOT);
#endif
    if(mui_players[0].enabled||mui_players[1].enabled){
        message("starting USB controllers...",0);mui_boot();
    }
#endif
#ifdef HARDWARE_BENCH
    bench_boot_note("L08 launcher menu ready");bench_boot_flush_log();
#endif
    pm_resume_pending();
    {unsigned any=0;for(unsigned j=0;j<16;j++)any|=save_create_uuid[j];if(profile_prompt || !any){pm_enter();sub=5;cur=0;}}
    for (;;) {                                 /* sub 0 = main screen, 1 = mods, 2 = songs, 3 = skin settings */
        u32 b = pad_get(), nb = b & ~old;
        old = b;
        if(sub==5){
#ifdef USB_HID
            if(nb&PAD_SELECT){mui_navigation^=1;old=0xffffffffu;}
#endif
            if(pm_ui(nb)){sub=0;cur=0;old=0xffffffffu;}continue;
        }
#ifdef USB_HID
        if(sub==4){
            int captured=mui_capture;
            if(!captured&&(nb&PAD_SELECT)){mui_navigation^=1;old=0xffffffffu;}
            if(mui_ui(nb,pad_physical())){if(launcher_debug)mui_diagnostic_save(SETTINGS_ROOT);sub=0;cur=4;}
            /* The binding press and its release never leak into navigation. */
            if(captured||mui_capture)old=0xffffffffu;
            continue;
        }
        if(nb&PAD_SELECT){mui_navigation^=1;old=0xffffffffu;}
        if(!sub && cur==4 && (nb&PAD_CROSS)){message("starting USB controllers...",0);mui_ensure_boot();mui_enter();sub=4;continue;}
#endif
#ifdef PORTABLE_LAUNCHER
        if(!sub&&cur==DL_LOCATION_ROW&&(nb&PAD_CROSS)){
            dl_change_game(pad_get,dl_save_active_settings);old=0xffffffffu;draw_main(cur);continue;
        }
#endif
        if(nb&PAD_START){
#ifdef PORTABLE_LAUNCHER
            if(dl_location_changed){message("Game location saved; restart launcher first",dl_next_game);continue;}
#endif
            if(pm_game_changed){message("Game ID changed; restart launcher first","Settings have been saved");continue;}
            unsigned any=0;for(unsigned j=0;j<16;j++)any|=save_create_uuid[j];
            if((profile_prompt && selected_profile.action==ML_OPEN) || !any){pm_enter();sub=5;cur=0;snprintf(pm_status,sizeof pm_status,"Create or open a profile before starting");continue;}
            break;
        }
        if (!sub) {
            if (nb & PAD_UP) cur = (cur + MAIN_ROWS - 1) % MAIN_ROWS;
            if (nb & PAD_DOWN) cur = (cur + 1) % MAIN_ROWS;
            if(cur==0 && (nb&(PAD_LEFT|PAD_RIGHT|PAD_CROSS))){
                video=(video+((nb&PAD_LEFT)?2:1))%3;gfx_apply_video();
            }
            if(cur==3 && (nb & (PAD_LEFT | PAD_RIGHT | PAD_CROSS)))save_batch^=1;
            if (cur > 0 && cur<3 && (nb & (PAD_CROSS | PAD_RIGHT))) { sub = cur+1; k = 0; }
            draw_main(cur);                          /* one frame per vsync (sync_flip) */
        } else if (sub == 3) {                       /* Skin settings */
            int *v, lo, hi, def;
            if (!prev_state) { draw_skin(k, tick); preview_load(); }
            if (nb & (PAD_CIRCLE | PAD_TRIANGLE)) { sub = 0; draw_main(cur); continue; }
            if (nb & PAD_UP) k = (k + SKIN_ROWS) % (SKIN_ROWS+1);
            if (nb & PAD_DOWN) k = (k + 1) % (SKIN_ROWS+1);
            if(k==SKIN_ROWS){if(nb&PAD_CROSS){sub=0;continue;}draw_skin(k,tick++);continue;}
            skin_row(k, &v, &lo, &hi, &def);
            if ((nb & PAD_LEFT) && *v > lo) {
                (*v)--;
                while (k == 4 && *v > lo && !(frame_available & (1 << *v))) (*v)--;
            }
            if ((nb & (PAD_RIGHT|PAD_CROSS)) && (*v < hi || (nb&PAD_CROSS))) {
                int next = *v + 1;
                while (k == 4 && next <= hi && !(frame_available & (1 << next))) next++;
                if (next <= hi) *v = next;
                else if(nb&PAD_CROSS)*v=lo;
            }
            if (nb & PAD_SQUARE) *v = def;
            draw_skin(k, tick++);
        } else {
            rows = sub_n(sub);
            if (nb & (PAD_CIRCLE | PAD_TRIANGLE)) { sub = 0; draw_main(cur); continue; }
            if (rows) {
                int per = (rows + sub_cols(sub) - 1) / sub_cols(sub);
                if (nb & PAD_UP) k = (k + rows - 1) % rows;
                if (nb & PAD_DOWN) k = (k + 1) % rows;
                if (sub_cols(sub) > 1 && (nb & (PAD_LEFT | PAD_RIGHT))) k = k < per ? (k + per < rows ? k + per : rows - 1) : k - per;
                if (nb & PAD_CROSS) {
                    if(k==rows-1){sub=0;draw_main(cur);continue;}
                    if (sub == 1) { mods[k].on ^= 1; prev_state = 0; notes_for = -1; }   /* preview art may change */
                    else styles[vis_style[k]].on ^= 1;
                }
            }
            draw_sub(sub, k);
        }
    }
    lpl_stage(2);
    if(cfg_save())for(;;)message("Launcher settings were not saved",cfg_error_status);
#ifdef PORTABLE_LAUNCHER
    if(dl_locator(1))for(;;)message("Game location was not saved","Could not update megamix.path beside the executable");
#endif
#ifdef USB_HID
    if(mui_save(SETTINGS_ROOT))for(;;)message("Controller settings could not be saved","Restart launcher to retry");
#endif
#ifdef HARDWARE_BENCH
    bench_boot_note("L09 START: configuration written; build begins");bench_boot_flush_log();
#endif
#ifdef USB_HID
    mui_trace_enable(launcher_debug && (mui_players[0].enabled||mui_players[1].enabled),DEV);
    mui_trace_phase("build: enter",0);
#endif
    log_start();
    i = build(&entry);
#ifdef USB_HID
    mui_trace_phase("build: return",i);
#endif
    log_save(i);
#ifdef HARDWARE_BENCH
    bench_boot_note("L10 game build returned %d; entry=%08x",i,entry);bench_boot_flush_log();
#endif
    if (i) {
#ifdef HARDWARE_BENCH
        /* Never run a partly built physical bench or an unprepared IOP. */
        for(;;)message(launcher_debug?log_:"Build failed. Set debug 1 and launcherlog 1 in debug.cfg for details.","Build failed");
#else
        old = 0xffff;
        for (;;) {                                   /* wait for a fresh START press */
            u32 b = pad_get();
            message(log_, "ERRORS - fix the files above (START = boot anyway)");
            if (b & ~old & PAD_START) break;
            old = b;
        }
#endif
    } else {
        for (k = 0; k < (launcher_debug?45:1); k++) message(log_, "booting...");
    }
#ifdef USB_HID
    unsigned usb_config=0,usb_frames=0;
    if(usb_runtime_bind(mui_players[0].enabled,mui_players[1].enabled,&usb_config,&usb_frames))
        for(;;)message("USB input runtime mismatch","Selected runtime metadata is invalid");
#endif
#ifdef MMCE_BENCH
    /* Handoff-only checkpoints; discovery/build remain quiet and cached. */
    if(select_profile_mode){mmce_boot_set_profile_log(select_profile_mode);mmce_boot_set_debug(1);bench_boot_note("R131b H00 mode=%d boot hit=%u saved=%u menu hit=%u saved=%u; before USB quiesce",select_profile_mode,bc_hit,bc_saved,sc_hit,sc_saved);bench_boot_flush_log();}
#endif
#ifdef USB_HID
    mui_trace_phase("quiesce launcher DMA: enter",0);
    if(mui_quiesce())for(;;)message("USB input transfers did not stop","Game not started");
#endif
#ifdef MMCE_BENCH
    if(select_profile_mode){bench_boot_note("R125 H01 USB quiesced; before physical pad close");bench_boot_flush_log();}
#endif
#ifdef USB_HID
    mui_trace_phase("quiesce complete; close physical pad",0);
#endif
    padPortClose(0, 0);
    padPortClose(1, 0);
    padEnd();
#ifdef HARDWARE_BENCH
#ifdef MMCE_BENCH
    if(select_profile_mode){bench_boot_note("R125 H02 physical pad closed; before retail IOP restore");bench_boot_flush_log();}
#endif
    message("restoring game IOP + " BENCH_LABEL "...",0);
    lpl_stage(8);
#ifdef LOAD_PROFILE
    lpl_u64 handoff_start=lpl_enabled?lpl_clock():0;
#endif
#ifdef USB_HID
    mui_trace_phase("reset/restore MMCE IOP: enter",0);
#endif
    if(bench_boot_prepare(1))for(;;)message(bench_boot_status(),BENCH_LABEL " handoff failed");
#ifdef USB_NATIVE_PRELOAD
    if(usb_boot_preload_native())for(;;)message(usb_boot_status(),"Original module preflight failed");
#endif
#ifdef USB_ENTRY_PROBE
    if(usb_entry_probe_install(&entry))for(;;)message("USB entry probe could not be paired","Game not started");
#endif
#ifdef LOAD_PROFILE
    lpl_add(8,LPL_IOP,handoff_start,0,0);
#endif
#endif
#ifdef USB_HID
    mui_trace_phase("reset/restore MMCE IOP: returned",0);
#ifdef MMCE_BENCH
    if(mui_players[0].enabled||mui_players[1].enabled){
        /* The game uses its original FILEIO client. Release the launcher's
         * surplus 16KiB staging buffer BEFORE USB allocates its pools/stacks.
         * Keep 2KiB for the remaining startup logs; PLAY buffers are unchanged. */
        int compact=fileXioSetRWBufferSize(2048);
        mui_trace_phase("fileXio handoff staging buffer = 2048",compact);
        if(compact<0)for(;;)message("Could not release launcher staging memory","Game not started");
    }
#endif
    if(mui_players[0].enabled||mui_players[1].enabled)message("starting game controllers...",0);
    if((mui_players[0].enabled||mui_players[1].enabled)&&mui_boot_game())for(;;)message(mui_status(),"USB controller handoff failed");
    if(mui_players[0].enabled||mui_players[1].enabled){
        unsigned gate_address,gate_token;
        if(mui_game_target(usb_frames,&gate_address,&gate_token))for(;;)message("USB input handoff failed","Game not started");
        W(usb_config)[3]=gate_address;W(usb_config)[4]=gate_token;
        mui_trace_phase("dormant game DMA target configured",0);
        if(debug_enabled && MUI_BOOT_DIAG)mui_trace_phase("start IOP observer",mui_start_boot_trace());
    }
#endif
#if defined(MMCE_BENCH)
    if(select_runtime && select_profile_mode==2){
        message("starting select profiler Ethernet...",0);
        int staged=msp_stage(select_runtime);
        bench_boot_note("N03 stage result=%d",staged);bench_boot_flush_log();
        if(staged)for(;;)message("Select profiler module staging failed","Set selectprofile 0 in debug.cfg and restart");
        bench_boot_note("N02 modules staged, inactive gate=%08x token=%08x; native START deferred until frozen SELECT",select_runtime->gate,select_runtime->token);bench_boot_flush_log();
    }
#endif
    FlushCache(0);
    FlushCache(2);
#ifndef HARDWARE_BENCH
    while (!SifIopReset("", 0)) ;
    while (!SifIopSync()) ;
#endif
#ifdef HARDWARE_BENCH
    bench_boot_note("L11 ExecPS2 entry=%08x; native game takes over",entry);
    bench_boot_flush_log();
#ifdef MMCE_BENCH
    if(select_profile_mode)message("R125: starting native EMPRESS",select_profile_mode==2?"Ethernet inactive until frozen SELECT":"RAM control: no Ethernet modules");
#endif
#endif
    lpl_export();
#ifdef USB_HID
    mui_trace_phase("ExecPS2 next: launcher complete",0);
#endif
    if(save_profile_launch_receipt()<0)for(;;)message("Save profiler receipt failed","Check logs folder; game not started");
#ifdef USB_NATIVE_PRELOAD
    bench_boot_note("L12 before final SifExitRpc; original modules preloaded");
    if(bench_boot_flush_log())for(;;)message("Final USB checkpoint write failed","Game not started");
    message(USB_TRACE_REV ": releasing launcher RPC...",0);
#endif
    SifExitRpc();
#ifdef USB_NATIVE_PRELOAD
    /* GS-only message: no filesystem/RPC call after releasing SIF. */
    message(USB_TRACE_REV ": RPC released; executing EMPRESS",0);
#endif
    ExecPS2((void *)entry, 0, 1, game_argv);
    return 0;
}
#endif
