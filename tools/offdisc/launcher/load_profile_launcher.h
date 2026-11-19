#ifndef MEGAMIX_LAUNCHER_LOAD_PROFILE_H
#define MEGAMIX_LAUNCHER_LOAD_PROFILE_H
#ifdef LOAD_PROFILE
#ifndef HOST_TEST
#include <timer.h>
#include <unistd.h>
#include <fcntl.h>
#else
#include <time.h>
#endif
typedef unsigned long long lpl_u64;
enum {LPL_OPEN,LPL_CLOSE,LPL_SEEK,LPL_READ,LPL_WRITE,LPL_SLURP,LPL_HASH,LPL_CACHE,LPL_BUILD,LPL_IOP,LPL_STAT,LPL_METRICS};
#define LPL_PHASES 10
static const char *lpl_names[]={"bootstrap","lists/menu/user_wait","config/start","build_game","build_songs","build_graphics","build_mods","build_finish","handoff","export_excluded"};
static const char *lpl_metrics[]={"open","close","seek","read","write","slurp_inclusive","hash_cpu","cache_publish_inclusive","build_inclusive","iop_prepare_inclusive","stat"};
static struct {unsigned calls,errors;lpl_u64 ticks,bytes,max;} lpl_stat[LPL_PHASES][LPL_METRICS];
static lpl_u64 lpl_wall[LPL_PHASES],lpl_since,lpl_cal_bus,lpl_cal_count,lpl_cal_prev_bus;
static unsigned lpl_phase,lpl_off,lpl_cal_prev_count,lpl_cal_samples,lpl_enabled,lpl_cal_enabled;
static void lpl_enable(int on){lpl_enabled=!!on;}
static void lpl_cal_enable(int on){lpl_cal_enabled=!!on;}
static lpl_u64 lpl_clock(void){
#ifdef HOST_TEST
 struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (lpl_u64)t.tv_sec*1000000000u+t.tv_nsec;
#else
 return GetTimerSystemTime();
#endif
}
static lpl_u64 lpl_hz(void){
#ifdef HOST_TEST
 return 1000000000u;
#else
 return 147456000u;
#endif
}
static void lpl_add(unsigned phase,unsigned metric,lpl_u64 start,lpl_u64 bytes,int error){
 if(!lpl_enabled || lpl_off)return;
 lpl_u64 t=lpl_clock()-start;unsigned i=phase<LPL_PHASES?phase:0;
 lpl_stat[i][metric].calls++;lpl_stat[i][metric].ticks+=t;lpl_stat[i][metric].bytes+=bytes;lpl_stat[i][metric].errors+=error!=0;if(t>lpl_stat[i][metric].max)lpl_stat[i][metric].max=t;
}
#if defined(MMCE_BENCH)
#define LPL_BOOT_STAGES 23
static const char *lpl_boot_names[]={"done","release_fileio","release_rpc","reboot_buffer","iop_sync","rpc_init","loadfile_bind","heap_bind","prefix_patch","module_iomanX","module_sio2man","module_fileXio","module_mmceman","filexio_bind","ping","gameid_switch","card_queries","base_open","base_read","base_close","startup_log","status_file","final_log"};
static struct {unsigned calls;lpl_u64 ticks,max;} lpl_boot[2][LPL_BOOT_STAGES];
static unsigned lpl_boot_previous,lpl_boot_phase;
static lpl_u64 lpl_boot_since;
void mmce_profile_mark(unsigned handoff,unsigned stage){
 if(!lpl_enabled)return;
 lpl_u64 now=lpl_clock();
 if(lpl_boot_previous){
  lpl_u64 elapsed=now-lpl_boot_since;
  unsigned p=lpl_boot_phase,k=lpl_boot_previous;
  lpl_boot[p][k].calls++;lpl_boot[p][k].ticks+=elapsed;
  if(elapsed>lpl_boot[p][k].max)lpl_boot[p][k].max=elapsed;
 }
 lpl_boot_previous=stage<LPL_BOOT_STAGES?stage:0;lpl_boot_phase=!!handoff;lpl_boot_since=now;
}
#endif
static void lpl_stage(unsigned phase){if(!lpl_enabled){lpl_phase=phase;return;}lpl_u64 now=lpl_clock();if(lpl_since)lpl_wall[lpl_phase]+=now-lpl_since;lpl_since=now;lpl_phase=phase;}
static void lpl_calibrate(void){
 if(!lpl_enabled && !lpl_cal_enabled)return;
#ifndef HOST_TEST
 unsigned c;__asm__ volatile("mfc0 %0,$9":"=r"(c));lpl_u64 now=lpl_clock(),d=now-lpl_cal_prev_bus;
 if(lpl_cal_prev_bus && d>=147456u && d<=147456000u){lpl_cal_bus+=d;lpl_cal_count+=(unsigned)(c-lpl_cal_prev_count);lpl_cal_samples++;}
 lpl_cal_prev_bus=now;lpl_cal_prev_count=c;
#endif
}
static FILE *lpl_fopen(const char *p,const char *m){if(!lpl_enabled)return fopen(p,m);unsigned phase=lpl_phase;lpl_u64 t=lpl_clock();FILE *r=fopen(p,m);lpl_add(phase,LPL_OPEN,t,0,!r);return r;}
static int lpl_fclose(FILE *f){if(!lpl_enabled)return fclose(f);unsigned phase=lpl_phase;lpl_u64 t=lpl_clock();int r=fclose(f);lpl_add(phase,LPL_CLOSE,t,0,r);return r;}
static int lpl_fseek(FILE *f,long off,int whence){if(!lpl_enabled)return fseek(f,off,whence);unsigned phase=lpl_phase;lpl_u64 t=lpl_clock();int r=fseek(f,off,whence);lpl_add(phase,LPL_SEEK,t,0,r);return r;}
static size_t lpl_fread(void *p,size_t n,size_t count,FILE *f){if(!lpl_enabled)return fread(p,n,count,f);unsigned phase=lpl_phase;lpl_u64 t=lpl_clock();size_t r=fread(p,n,count,f);lpl_add(phase,LPL_READ,t,r*n,ferror(f));return r;}
static size_t lpl_fwrite(const void *p,size_t n,size_t count,FILE *f){if(!lpl_enabled)return fwrite(p,n,count,f);unsigned phase=lpl_phase;lpl_u64 t=lpl_clock();size_t r=fwrite(p,n,count,f);lpl_add(phase,LPL_WRITE,t,r*n,r!=count);return r;}
static int lpl_export(void){
 static char text[16384];char path[160];unsigned used=0;int serial,failed=0;
 if(!lpl_enabled)return 0;
 lpl_stage(9);lpl_off=1;
 used+=snprintf(text+used,sizeof text-used,"MMIX_LAUNCH_LOAD 1\nclock_hz %llu\ncount_calibration %llu %llu %u\n",lpl_hz(),lpl_cal_count,lpl_cal_bus,lpl_cal_samples);
 for(unsigned p=0;p<LPL_PHASES;p++){
  used+=snprintf(text+used,sizeof text-used,"phase %u %s wall_ticks %llu\n",p,lpl_names[p],lpl_wall[p]);
  for(unsigned k=0;k<LPL_METRICS;k++)if(lpl_stat[p][k].calls){
   if(used+240>=sizeof text){failed=1;break;}
   used+=snprintf(text+used,sizeof text-used,"metric %u %s calls %u ticks %llu max %llu bytes %llu errors %u\n",p,lpl_metrics[k],lpl_stat[p][k].calls,lpl_stat[p][k].ticks,lpl_stat[p][k].max,lpl_stat[p][k].bytes,lpl_stat[p][k].errors);
  }
  if(failed)break;
 }
#if defined(MMCE_BENCH)
 for(unsigned p=0;p<2;p++)for(unsigned k=1;k<LPL_BOOT_STAGES;k++)if(lpl_boot[p][k].calls){
  if(used+240>=sizeof text){failed=1;break;}
  used+=snprintf(text+used,sizeof text-used,"bootstage %u %s calls %u ticks %llu max %llu bytes 0 errors 0\n",p,lpl_boot_names[k],lpl_boot[p][k].calls,lpl_boot[p][k].ticks,lpl_boot[p][k].max);
 }
#endif
 if(failed)return -1;
 for(serial=1;serial<=9999;serial++){
  snprintf(path,sizeof path,"%slogs/launcher-load-%04d.txt",DEV,serial);
#ifdef HOST_TEST
  FILE *probe=fopen(path,"rb");if(probe){fclose(probe);continue;}
  FILE *f=fopen(path,"wx");if(!f)return -2;unsigned done=0;while(done<used){size_t n=fwrite(text+done,1,used-done,f);if(!n){fclose(f);return -3;}done+=n;}return fclose(f)?-4:serial;
#else
  int fd=open(path,O_RDONLY);if(fd>=0){close(fd);continue;}
  fd=open(path,O_WRONLY|O_CREAT|O_EXCL,0666);if(fd<0)return -2;
  unsigned done=0;while(done<used){int n=write(fd,text+done,used-done);if(n<=0){close(fd);return -3;}done+=n;}return close(fd)?-4:serial;
#endif
 }
 return -5;
}
#ifdef fopen
#undef fopen
#endif
#define fopen lpl_fopen
#define fclose lpl_fclose
#define fseek lpl_fseek
#define fread lpl_fread
#define fwrite lpl_fwrite
#else
#define lpl_enable(n) ((void)0)
#define lpl_cal_enable(n) ((void)0)
#define lpl_stage(n) ((void)0)
#define lpl_calibrate() ((void)0)
#define lpl_export() ((void)0)
#endif
#endif
