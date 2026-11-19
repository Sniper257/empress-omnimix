#ifndef MEGAMIX_LOAD_PROFILE_H
#define MEGAMIX_LOAD_PROFILE_H
/* Private diagnostic ABI. All counters are raw CP0 Count ticks. */
enum {LP_BOOT,LP_SELECT,LP_CHART,LP_PLAY,LP_RESULT_LOAD,LP_RESULT,LP_OTHER,LP_PHASES};
enum {LP_LOCK,LP_LOOKUP,LP_OPEN,LP_CLOSE,LP_SEEK,LP_READ,LP_ZERO,LP_DECODE,LP_STREAM,LP_UPDATE,LP_DRAW,LP_SUBMIT,LP_FLIP,LP_VBLANK,LP_RESULT_DRAW,LP_FRAME,LP_METRICS};
#define LP_SLOW 16
#define LP_FRAMES 64
#define LP_EVENTS 32
#define LP_FILES 256
typedef struct {unsigned schema,bytes,enabled,path;} load_profile_config;
typedef struct {unsigned calls,lo,hi,max,bytes_lo,bytes_hi,errors;} lp_metric;
typedef struct {unsigned phase,metric,tick,ticks,key,bytes,error,reserved;} lp_slow;
typedef struct {unsigned phase,sequence,age,total,update,draw,submit,flip,vblank;} lp_frame;
typedef struct {unsigned tick,phase,kind,a,b,c,d,e;} lp_event;
typedef struct {unsigned phase,metric,key;lp_metric total;} lp_file;
typedef struct {
 unsigned h[32];
 lp_metric metric[LP_PHASES][LP_METRICS];
 lp_slow slow[LP_SLOW];lp_frame frame[LP_FRAMES];lp_event event[LP_EVENTS];
 lp_file file[LP_FILES]; /* hash table, empty iff total.calls==0 */
} load_profile_data;
unsigned long long lp_begin(void);
void lp_end(unsigned,unsigned long long,unsigned,unsigned,unsigned);
void lp_phase(unsigned,unsigned);
void lp_export(void);
#endif
