/* Non-PLAY loading profiler. No allocation, formatting, drawing or file writes
 * in its hot paths. File I/O is measured under the reader's existing lock.
 * Metrics of nested operations are inclusive and MUST NOT be summed as disjoint
 * CPU time. Freeze/export occurs only on completed RESULT exit. */
#include "load_profile.h"
load_profile_config load_profile={1,sizeof(load_profile_data),1,0};
static load_profile_data lp_data;
static volatile unsigned lp_mode=LP_BOOT,lp_frozen;
static unsigned lp_serial,lp_frames,lp_result_scene;
static unsigned char lp_chart_id[16];
static unsigned lp_frame_start,lp_frame_valid,lp_frame_phase,lp_frame_part[5];
static unsigned lp_prep_pair=~0u;
#ifndef LOAD_PROFILE_TEST
static unsigned lp_clock(void){unsigned n;__asm__ volatile("mfc0 %0,$9":"=r"(n));return n;}
#define lp_word(a) (*(volatile unsigned *)(unsigned long)(a))
extern int vd_load_export(const char *,unsigned *);
#define lp_write(p,s) vd_load_export(p,s)
#define lp_native(a) ((unsigned long long (*)(unsigned long long,unsigned long long,unsigned long long,unsigned long long,unsigned long long,unsigned long long,unsigned long long,unsigned long long))(unsigned long)(a))
extern unsigned long long lp_chart_body(void),lp_play_body(void),lp_result_body(void),lp_result_draw_body(void),lp_select_ctor_body(void);
#endif
static void lp_add(unsigned *lo,unsigned *hi,unsigned n){unsigned old=*lo;*lo+=n;*hi+=*lo<old;}
/* Fixed-size per-(phase, operation, LBA) aggregation. Bounded probing and no
 * allocation/path formatting. Aggregate phase counters remain exact on overflow. */
static void lp_file_add(unsigned phase,unsigned metric,unsigned key,unsigned ticks,unsigned bytes,unsigned error)
{
 unsigned at=(key*2654435761u+phase*17u+metric*3u)&(LP_FILES-1);
 for(unsigned probe=1;probe<=LP_FILES;probe++){
  lp_file *f=&lp_data.file[at];lp_metric *m=&f->total;
  if(!m->calls || (f->phase==phase && f->metric==metric && f->key==key)){
   if(!m->calls){f->phase=phase;f->metric=metric;f->key=key;lp_data.h[12]++;}
   m->calls++;lp_add(&m->lo,&m->hi,ticks);lp_add(&m->bytes_lo,&m->bytes_hi,bytes);
   if(ticks>m->max)m->max=ticks;if(error)m->errors++;
   if(probe>lp_data.h[14])lp_data.h[14]=probe;return;
  }
  at=(at+1)&(LP_FILES-1);LOOP_PAD();
 }
 lp_data.h[13]++;
}
unsigned long long lp_begin(void)
{
 if(!load_profile.enabled || lp_frozen || lp_mode==LP_PLAY)return 0;
 return ((unsigned long long)(lp_mode+1)<<32)|lp_clock();
}
void lp_end(unsigned metric,unsigned long long span,unsigned bytes,unsigned key,unsigned error)
{
 if(!span || metric>=LP_METRICS || lp_frozen || lp_mode==LP_PLAY || !load_profile.enabled)return;
 unsigned phase=(unsigned)(span>>32)-1,tick=(unsigned)span,ticks=lp_clock()-tick;
 if(phase>=LP_PHASES || phase==LP_PLAY || lp_frozen)return;
 lp_metric *m=&lp_data.metric[phase][metric];m->calls++;lp_add(&m->lo,&m->hi,ticks);lp_add(&m->bytes_lo,&m->bytes_hi,bytes);
 if(ticks>m->max)m->max=ticks;if(error)m->errors++;
 if(metric==LP_READ || metric==LP_OPEN || metric==LP_SEEK || metric==LP_CLOSE)lp_file_add(phase,metric,key,ticks,bytes,error);
 if(metric==LP_READ || metric==LP_OPEN || metric==LP_SEEK) {
  unsigned at=lp_data.h[0];
  if(at<LP_SLOW)lp_data.h[0]++;
  else {at=0;for(unsigned i=1;i<LP_SLOW;i++){if(lp_data.slow[i].ticks<lp_data.slow[at].ticks)at=i;LOOP_PAD();}if(ticks<=lp_data.slow[at].ticks)return;}
  lp_data.slow[at]=(lp_slow){phase,metric,tick,ticks,key,bytes,error,0};
 }
}
static void lp_record(unsigned phase,unsigned kind,unsigned extra)
{
 unsigned at=lp_data.h[2];
 if(at<LP_EVENTS){lp_data.event[at]=(lp_event){lp_clock(),phase,kind,lp_word(0x2ad534),lp_word(0x78af08),lp_word(0x2ad120),extra,lp_word(0x2ad538)};lp_data.h[2]++;}else lp_data.h[3]++;
}
static void lp_note(unsigned kind,unsigned extra)
{
 if(!lp_frozen && load_profile.enabled && lp_mode!=LP_PLAY)lp_record(lp_mode,kind,extra);
}
void lp_phase(unsigned phase,unsigned kind)
{
 if(phase>=LP_PHASES || lp_frozen || !load_profile.enabled || phase==lp_mode)return;
 lp_record(phase,kind,0);
 lp_mode=phase;if(phase==LP_PLAY)lp_frame_valid=0;if(phase==LP_SELECT || phase==LP_CHART || phase==LP_OTHER)lp_result_scene=0;
}
static void lp_prepare_note(void)
{
 if(lp_mode!=LP_CHART || lp_word(0x2ad534)>=4 || lp_frozen || !load_profile.enabled)return;
 unsigned pair=lp_word(0x2ad534)|(lp_word(0x2ad538)<<8);
 if(pair!=lp_prep_pair){lp_prep_pair=pair;lp_note(7,0);}
}
static unsigned lp_age(void)
{
 unsigned result=lp_word(0x2ad310);
 return lp_mode==LP_RESULT && lp_result_scene && result>=0x994700 && result<0x2000000-0x1580?lp_word(result+0x1384):0xffffffffu;
}
static void lp_frame_end(void)
{
 if(!lp_frame_valid || lp_mode==LP_PLAY || lp_frozen)return;
 lp_frame_valid=0;
 unsigned total=lp_clock()-lp_frame_start,at=lp_data.h[1];
 lp_frame f={lp_frame_phase,++lp_frames,lp_age(),total,lp_frame_part[0],lp_frame_part[1],lp_frame_part[2],lp_frame_part[3],lp_frame_part[4]};
 lp_metric *m=&lp_data.metric[lp_frame_phase][LP_FRAME];m->calls++;lp_add(&m->lo,&m->hi,total);if(total>m->max)m->max=total;
 /* Keep 32 result/result-load frames and 32 other frames. Startup stalls
  * must not crowd out the small result animation hitch we are looking for. */
 unsigned result=f.phase==LP_RESULT || f.phase==LP_RESULT_LOAD,matching=0,least=LP_FRAMES;
 for(unsigned i=0;i<at;i++){
  lp_frame *old=&lp_data.frame[i];
  if((old->phase==LP_RESULT || old->phase==LP_RESULT_LOAD)==result){matching++;if(least==LP_FRAMES || old->total<lp_data.frame[least].total)least=i;}
  LOOP_PAD();
 }
 if(matching<LP_FRAMES/2 && at<LP_FRAMES)lp_data.h[1]++;
 else {at=least;if(at==LP_FRAMES || total<=lp_data.frame[at].total)return;}
 lp_data.frame[at]=f;
}
#define LP_ARGS unsigned long long a,unsigned long long b,unsigned long long c,unsigned long long d,unsigned long long e,unsigned long long f,unsigned long long g,unsigned long long h
#define LP_PASS a,b,c,d,e,f,g,h
#define LP_CALL(name,site,metric,part) \
unsigned long long lp_##name(LP_ARGS){ \
 unsigned long long span=lp_begin(); \
 if(metric==LP_UPDATE && span){lp_frame_valid=1;lp_frame_start=(unsigned)span;lp_frame_phase=(unsigned)(span>>32)-1;for(unsigned i=0;i<5;i++){lp_frame_part[i]=0;LOOP_PAD();}} \
 unsigned long long r=lp_native(site)(LP_PASS); \
 if(span && lp_mode!=LP_PLAY && !lp_frozen){unsigned ticks=lp_clock()-(unsigned)span;lp_end(metric,span,0,0,0);if(part<5)lp_frame_part[part]=ticks;}return r;}
LP_CALL(update,0x14e670,LP_UPDATE,0)
LP_CALL(draw,0x14e280,LP_DRAW,1)
#ifdef PREP_PROBE
unsigned long long lp_submit(LP_ARGS){prep_probe_render();unsigned long long span=lp_begin(),r=lp_native(0x136c30)(LP_PASS);lp_end(LP_SUBMIT,span,0,0,0);return r;}
#else
LP_CALL(submit,0x136c30,LP_SUBMIT,2)
#endif
LP_CALL(flip,0x139be0,LP_FLIP,3)
LP_CALL(vblank,0x10eca0,LP_VBLANK,4)
#undef LP_CALL
unsigned long long lp_tail(LP_ARGS){unsigned long long r=lp_native(0x14e250)(LP_PASS);lp_frame_end();return r;}
unsigned long long lp_chart(LP_ARGS){
#ifdef PREP_PROBE
 prep_probe_start();prep_probe_mark(0x10);
#endif
 lp_prep_pair=~0u;lp_phase(LP_CHART,1);unsigned long long r=lp_native((unsigned long)lp_chart_body)(LP_PASS);
#ifdef PREP_PROBE
 prep_probe_mark(0x11);
#endif
 return r;
}
unsigned long long lp_play(LP_ARGS){
#ifdef PREP_PROBE
 if(lp_word(0x2ad534)>=4)prep_probe_finish();
#endif
 /* Explicit PLAY entry establishes the phase; stale phase9 in menus can never
  * enable/disable profiling by itself. Stop before native phase4/active5. */
 unsigned phase=lp_word(0x2ad534);if(phase>=4)lp_phase(LP_PLAY,2);else lp_prepare_note();
 unsigned long long r=lp_native((unsigned long)lp_play_body)(LP_PASS);
 if(lp_word(0x2ad534)>=4)lp_phase(LP_PLAY,2);else lp_prepare_note();return r;
}
unsigned long long lp_select_ctor(LP_ARGS){
 lp_phase(LP_OTHER,10);lp_note(8,0x18b1e0);
 unsigned long long r=lp_native((unsigned long)lp_select_ctor_body)(LP_PASS);
 lp_note(9,0x18b1e0);return r;
}
unsigned long long lp_result(LP_ARGS){lp_phase(LP_RESULT_LOAD,3);unsigned long long r=lp_native((unsigned long)lp_result_body)(LP_PASS);lp_result_scene=(unsigned)a;return r;}
unsigned long long lp_result_draw(LP_ARGS){
 unsigned state=lp_word((unsigned)a+0x2c);
 if(state==3 || state==4)lp_phase(LP_RESULT,4);
 unsigned long long span=lp_begin(),r=lp_native((unsigned long)lp_result_draw_body)(LP_PASS);
 lp_end(LP_RESULT_DRAW,span,0,state,0);return r;
}
#undef LP_ARGS
#undef LP_PASS
/* The writer calls this only under the existing FILEIO lock after assets and
 * streaming are idle. A failed export keeps the complete frozen snapshot. */
const void *lp_freeze(unsigned *bytes)
{
 if(!lp_frozen)for(unsigned i=0;i<16;i++){lp_chart_id[i]=input_probe.chart_id[i];LOOP_PAD();}
 lp_frozen=1;lp_result_scene=0;lp_frame_valid=0;
#ifndef LOAD_PROFILE_TEST
 for(unsigned i=0;i<4;i++){lp_data.h[8+i]=result_title_stats[i];LOOP_PAD();}
#endif
 lp_data.h[4]=lp_frames;lp_data.h[5]=lp_mode;*bytes=sizeof lp_data;return &lp_data;
}
void lp_export(void)
{
 if(!load_profile.enabled || !load_profile.path || (lp_mode!=LP_RESULT && lp_mode!=LP_RESULT_LOAD) || lp_word(0x78af08) || lp_word(0x78b70c))return;
 int r=lp_write((const char *)(unsigned long)load_profile.path,&lp_serial);
 if(r>=0){unsigned *p=(unsigned*)&lp_data;for(unsigned i=0;i<sizeof lp_data/4;i++){p[i]=0;LOOP_PAD();}lp_frozen=0;lp_frames=0;lp_phase(LP_OTHER,5);}
}

#if !defined(LOAD_PROFILE_TEST) || defined(LOAD_PROFILE_HEADER_TEST)
void lp_make_header(unsigned *h,const void *data,unsigned bytes)
{
 for(unsigned i=0;i<32;i++){h[i]=0;LOOP_PAD();}
 const char *magic="MMIXLOD1";unsigned char *out=(unsigned char*)h;
 for(unsigned i=0;i<8;i++){out[i]=magic[i];LOOP_PAD();}
 h[2]=2;h[3]=128;h[4]=bytes;h[5]=1; /* raw CP0 Count, no assumed frequency */
 h[6]=LP_PHASES;h[7]=LP_METRICS;h[8]=sizeof(lp_metric);h[9]=sizeof(lp_slow);h[10]=sizeof(lp_frame);h[11]=sizeof(lp_event);
 for(unsigned i=0;i<16;i++){out[48+i]=input_probe_build_id[i];out[64+i]=lp_chart_id[i];LOOP_PAD();}
 h[21]=LP_FILES;h[22]=sizeof(lp_file);
 h[20]=cp_crc_full(data,bytes);h[31]=cp_crc_full(h,124);
}
#endif
