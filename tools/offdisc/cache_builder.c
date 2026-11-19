/* Portable offline launcher builder. Compiled to32-bit WASI; game instructions
 * are data only. Filesystem inputs are read-only; only /output is writable. */
#define HOST_TEST
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <assert.h>
#include "launcher/cont.h"
typedef unsigned long long u64;
char dl_game[64],dl_config[64],dl_boot[64],dl_cfg[80];
unsigned dl_persistent,dl_usb_active,dl_mmce_active;
static const char *game_mount="/game/",*config_mount="/config/";
void portable_boot_note(const char *fmt,...){(void)fmt;}
static const char *map_path(const char *p,char out[600]){
 const char *rel=0;const char *base=0;
 if(!strncmp(p,dl_config,strlen(dl_config))&&(!strncmp(p+strlen(dl_config),"launcher.cfg",12)||!strcmp(p+strlen(dl_config),"debug.cfg"))){base=config_mount;rel=p+strlen(dl_config);}
 else if(!strncmp(p,dl_game,strlen(dl_game))){rel=p+strlen(dl_game);if(!strncmp(rel,"cache/",6)){base="/output/cache/";rel+=6;}else base=game_mount;}
 if(!base){errno=EACCES;return 0;}
 if(snprintf(out,600,"%s%s",base,rel)>=600){errno=ENAMETOOLONG;return 0;}
 return out;
}
static FILE *cache_open(const char *p,const char *mode){char b[600];const char *q=map_path(p,b);if(!q)return 0;if(strpbrk(mode,"wa+")&&strncmp(q,"/output/cache/",14)){errno=EACCES;return 0;}return fopen(q,mode);}
static DIR *cache_opendir(const char *p){char b[600];const char *q=map_path(p,b);return q?opendir(q):0;}
static int cache_rename(const char *a,const char *b){char x[600],y[600];if(!map_path(a,x)||!map_path(b,y)||strncmp(x,"/output/cache/",14)||strncmp(y,"/output/cache/",14)){errno=EACCES;return -1;}return rename(x,y);}
static long cache_size(const char *p){char b[600];struct stat st;const char *q=map_path(p,b);return !q||stat(q,&st)||!S_ISREG(st.st_mode)?-1:st.st_size;}
#define fopen cache_open
#define opendir cache_opendir
#define rename cache_rename
#define LAUNCHER_FILE_SIZE cache_size
#include "launcher/main.c"
#undef fopen
int main(int argc,char **argv){
 if(argc!=5){fputs("Expected logical game/settings paths and mounts\n",stderr);return 2;}
#ifdef PORTABLE_LAUNCHER
 if(dl_root(dl_game,argv[1])||dl_set_config(argv[2]))
#else
 if(strcmp(argv[1],"host:")||strcmp(argv[2],"host:"))
#endif
 {fputs("Invalid logical game/settings path\n",stderr);return 2;}
#ifndef PORTABLE_LAUNCHER
 strcpy(dl_game,"host:");strcpy(dl_config,"host:");
#endif
 game_mount=argv[3];config_mount=argv[4];
 debug_load();
 if(debug_enabled||prep_watch_enabled){fputs("Set debug0 and prepprobe0 before building an offline cache\n",stderr);return 2;}
 read_mods();read_styles();read_note_skins();read_frames();cfg_load();
 if(save_config_error){fprintf(stderr,"Cannot read settings: %s\n",cfg_error_status);return 2;}
 u32 entry=0;int r=build(&entry);
 if(r||!bc_saved&&!bc_hit||!sc_saved&&!sc_hit){fprintf(stderr,"CACHE BUILD FAILED: r=%d boot=%u/%u menu=%u/%u\n%s",r,bc_saved,bc_hit,sc_saved,sc_hit,log_);return 1;}
 FILE *f=fopen("/output/receipt.txt","w");if(!f)return 1;
 fprintf(f,"cache_builder 1\nboot_hit %u\nboot_saved %u\nmenu_hit %u\nmenu_saved %u\nentry %x\n",bc_hit,bc_saved,sc_hit,sc_saved,entry);if(fclose(f))return 1;
 return 0;
}
