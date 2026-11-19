/* SELECT_PROFILE only. Main-thread event memory is allocated once, only in
 * ready SELECT, then released before CHART. Worker I/O records and all RPC/DMA
 * storage are permanent: cancelling can never free memory a worker still owns.
 * IO callbacks share vdisc's existing lock. Other metrics have separate native
 * producers (decode/stream vs update/draw/submit/flip/vblank); nested spans are
 * inclusive. Freeze copies only after asset idle + a nonblocking reader lock.
 * No allocations, file/RPC/export or formatting in measurement callbacks. */
#include "capture.h"
static unsigned sp_div(unsigned long long n,unsigned d){
 unsigned q=0;if(!d)return ~0u;
 for(int bit=31;bit>=0;bit--){unsigned long long v=(unsigned long long)d<<bit;if(n>=v){n-=v;q|=1u<<bit;}LOOP_PAD();}
 return q;
}
#define MSP_MARKER_DIV(n,d) sp_div(n,d)
#include "video_marker.h"
#undef MSP_MARKER_DIV
load_profile_config load_profile={1,0,0,0};
msp_config select_profile={MSP_SELECT_ABI,0,20,0,0x0a4d4d01,0x0a4d4d02};
enum {SP_ARMED,SP_CAPTURE,SP_FREEZING,SP_FROZEN,SP_EXPORT,SP_STOPPING,SP_DONE,SP_FAILED,SP_CANCELLED};
static struct {
 unsigned state,count,drops,io_count,io_drops,frames,reason,serial,lastkey,lastfolder,seen,age,packet,round,whole,bytes,pending,operation,wait,gate_dma;
 unsigned last,high,start,frame_start,frame_valid,frame_tick_lo,frame_tick_hi,marker_cost,previous_draw,parts[5],interval,peak_asset,peak_batch;
 unsigned art[16],cut[16],stop_error,network_ready,start_submitted,shutdown_done;
 unsigned failure,detail,failed_asset,failed_batch;
 unsigned stats_valid,stats_at;
 unsigned long long deadline;
 volatile unsigned long long published __attribute__((aligned(8)));
 unsigned char *memory;
 lp_metric metric[LP_METRICS];
 msp_event io[MSP_IO_EVENTS];
 msp_video_marker marker;
 char label[MSP_MARKER_TEXT];
 unsigned client[16] __attribute__((aligned(64)));
 unsigned char request[MSP_ROUND(sizeof(msp_request))] __attribute__((aligned(64)));
 unsigned char reply[MSP_ROUND(sizeof(msp_reply))] __attribute__((aligned(64)));
 unsigned zero[32] __attribute__((aligned(128)));
} sp;
#ifndef SELECT_CAPTURE_TEST
static unsigned lp_clock(void){unsigned n;__asm__ volatile("mfc0 %0,$9":"=r"(n));return n;}
#define lp_word(a) (*(volatile unsigned *)(unsigned long)(a))
#define sp_native(a) ((unsigned long long (*)(unsigned long long,unsigned long long,unsigned long long,unsigned long long,unsigned long long,unsigned long long,unsigned long long,unsigned long long))(unsigned long)(a))
#define sp_bind(c) (((int (*)(void *,unsigned,int))0x112b50)(c,MSP_RPC,1))
#define sp_busy(c) (((int (*)(void *))0x112f30)(c))
#define sp_call(c,op,q,n,r) (((int (*)(void *,unsigned,int,void *,unsigned,void *,unsigned,void *,void *))0x112d30)(c,op,1,q,n,r,MSP_ROUND(sizeof(msp_reply)),0,0))
#define sp_dma(d) (((int (*)(void *,int))0x10f020)(d,1))
#define sp_dma_stat(id) (((int (*)(int))0x10f000)(id))
#define sp_wait_frame() (((void (*)(void))0x10eca0)())
#define sp_zero_words ((volatile unsigned *)((unsigned long)sp.zero|0x20000000u))
extern int vd_select_trylock(void);extern void vd_select_unlock(void);
extern unsigned vd_select_export_info[4];
extern int vd_probe_export(const char *,unsigned *,const void *,unsigned,const void *,unsigned);
extern unsigned long long lp_chart_body(void),lp_play_body(void),lp_result_body(void),lp_result_draw_body(void),lp_select_ctor_body(void);
#endif
static unsigned long long sp_now(void){
 unsigned n=lp_clock();if(n<sp.last)sp.high++;sp.last=n;
 unsigned long long now=((unsigned long long)sp.high<<32)|n;sp.published=now;return now;
}
static unsigned sp_span(void){return sp.state==SP_CAPTURE?lp_clock():0;}
static msp_event *sp_events(void){return (msp_event *)(sp.memory+MSP_HEADER+MSP_METRICS);}
static void sp_note(unsigned kind,unsigned a,unsigned b,unsigned c,unsigned d,unsigned e,unsigned f,unsigned g,unsigned h,unsigned i){
 if(sp.state!=SP_CAPTURE)return;
 /* An unchanged folder panel calls stats every update. Retain its maximum,
  * sample count and saturated total instead of consuming one event/frame. */
 if(kind==10&&a==2){
  if(sp.stats_valid&&sp.stats_at<sp.count){
   msp_event *old=sp_events()+sp.stats_at;
   if(old->c==c&&old->d==d&&old->e==e&&old->f==f&&old->g==g){
    if(b>old->b)old->b=b;old->h++;
    old->i=~0u-old->i<b?~0u:old->i+b;return;
   }
  }
  h=1;i=b;
 }
 if(sp.count==MSP_EVENTS){sp.drops++;sp.reason=2;return;}
 if(kind==10&&a==2){sp.stats_valid=1;sp.stats_at=sp.count;}
 unsigned long long now=sp_now();sp_events()[sp.count++]=(msp_event){kind,(unsigned)now,(unsigned)(now>>32),a,b,c,d,e,f,g,h,i};
}
static void sp_cursor(void){
 if(sp.state!=SP_CAPTURE||!MS_STATE)return;
 unsigned alias=I32(MS_STATE+0x24),folder=I32(MS_STATE+0x30),key=alias==124?0xfffffffeu:(unsigned)alias_key(alias);
 if(key==sp.lastkey&&folder==sp.lastfolder)return;
 sp.lastkey=key;sp.lastfolder=folder;sp.seen=0;u8 *ctx=song_ctx();
 sp_note(1,key,alias,sdb.clock,folder,I32(MS_STATE+8),window.diff,ctx?I32(ctx+0x20728):0,ctx?I32(ctx+0x20724):0,0);
}
static void sp_art(unsigned kind,unsigned c,unsigned k,unsigned ticks){
 if(sp.state!=SP_CAPTURE||c>=4)return;
 if(kind>=2&&kind<=5)sp.art[(kind-2)*4+c]++;
 if(!c)return; /* wheel strip traffic is aggregated; current title is detailed */
 unsigned key=k==sdb.n_new?0xfffffffeu:SONG_KEY+k;
 if(key!=sp.lastkey)return;
 if(kind==2||kind==8){unsigned bit=1u<<(c+(kind==8?8:0));if(sp.seen&bit)return;sp.seen|=bit;}
 sp_note(kind,c,key,ticks,sdb.clock,0,0,0,0,0);
}
unsigned long long lp_begin(void){return sp.state==SP_CAPTURE?(1ull<<32)|lp_clock():0;}
static void sp_add(unsigned *lo,unsigned *hi,unsigned n){unsigned old=*lo;*lo+=n;*hi+=*lo<old;}
void lp_end(unsigned metric,unsigned long long span,unsigned bytes,unsigned key,unsigned error){
 if(!span||metric>=LP_METRICS)return;
 if(sp.state!=SP_CAPTURE){sp.cut[metric]++;return;}
 unsigned now=lp_clock(),ticks=now-(unsigned)span;lp_metric *m=sp.metric+metric;
 m->calls++;sp_add(&m->lo,&m->hi,ticks);sp_add(&m->bytes_lo,&m->bytes_hi,bytes);if(ticks>m->max)m->max=ticks;if(error)m->errors++;
 /* Every metric <=ZERO is emitted under the EXISTING vdisc reader lock,
  * including main-thread title reads. The IO ring never shares its producer
  * cursor or writable heap with the main-thread frame/event ring. */
 if(metric>=LP_OPEN&&metric<=LP_READ){
  unsigned at=sp.io_count;
  if(at==MSP_IO_EVENTS){
   sp.io_drops++;at=0;
   for(unsigned i=1;i<MSP_IO_EVENTS;i++){if(sp.io[i].b<sp.io[at].b)at=i;LOOP_PAD();}
   if(ticks<=sp.io[at].b)return;
  }else sp.io_count++;
  unsigned long long ref=sp.published,stamp=(unsigned long long)((long long)ref+(int)(now-(unsigned)ref));
  sp.io[at]=(msp_event){12,(unsigned)stamp,(unsigned)(stamp>>32),metric,ticks,bytes,key,error,sp.marker.frame,0,0,0};
 }
}
void lp_phase(unsigned phase,unsigned kind){(void)kind;if(phase!=LP_SELECT&&sp.state>=SP_CAPTURE&&sp.state<=SP_STOPPING)sp_leave();}
void lp_export(void){} /* No old RESULT-triggered load exports in this variant. */
static int sp_gate_off(void){
 if(select_profile.enabled!=2||!select_profile.gate)return 1;
 if(!sp.gate_dma){
  for(unsigned i=0;i<32;i++){sp_zero_words[i]=0;LOOP_PAD();}
#ifndef SELECT_CAPTURE_TEST
  __asm__ volatile("sync":::"memory");
#endif
  struct {void *source,*destination;unsigned bytes,attributes;} d={(void *)sp.zero,(void *)(unsigned long)select_profile.gate,16,0};
  int id=sp_dma(&d);if(id>0)sp.gate_dma=(unsigned)id;
 }
 return sp.gate_dma&&sp_dma_stat(sp.gate_dma)<0;
}
static void sp_release(void){sp.frame_valid=0;load_profile.enabled=0;if(sp.memory){free_(sp.memory);sp.memory=0;}}
static void sp_finish(unsigned state){sp_gate_off();sp.state=state;sp_release();}
static void sp_fail(unsigned code,unsigned detail){
 sp.failure=code;sp.detail=detail;sp.failed_asset=lp_word(0x78af08);sp.failed_batch=lp_word(0x78b70c);sp_finish(SP_FAILED);
}
static int sp_rpc_call(unsigned operation,unsigned bytes){
 if(sp.pending||sp_busy(sp.client))return -1;
 msp_request *q=(msp_request *)sp.request;q->abi=MSP_ABI;q->token=select_profile.token;q->bytes=bytes;q->peer=select_profile.peer;q->local=select_profile.local;
 if(sp_call(sp.client,operation,q,MSP_ROUND(20+bytes),sp.reply)<0)return -1;
 sp.pending=1;sp.operation=operation;sp.wait=0;
 if(operation==MSP_START)sp.start_submitted=1;
 return 0;
}
/* A late START may complete after cancellation's zero gate reached the IOP.
 * Re-zero AFTER its reply, then stop with permanent buffers. This never frees
 * or reuses a pending request, and never adds another capture in PLAY. */
static void sp_shutdown(void){
 if(select_profile.enabled!=2||sp.state<SP_DONE||sp.shutdown_done||!sp.start_submitted)return;
 if(sp.pending){
  if(sp_busy(sp.client))return;
  sp.pending=0;
  if(sp.operation==MSP_STOP){sp.shutdown_done=1;return;}
  if(sp.operation==MSP_START)sp.gate_dma=0;
 }
 if(!sp_gate_off()||!sp.client[9])return;
 if(sp_rpc_call(MSP_STOP,0))sp.shutdown_done=1;
}
static void sp_snapshot(void){
 unsigned *h=(unsigned *)sp.memory;memcpy_(h,"MMIXSEL1",8);
 unsigned n=sp.io_count;if(n>MSP_IO_EVENTS)n=MSP_IO_EVENTS;
 memcpy_(sp_events()+sp.count,sp.io,n*sizeof(msp_event));
 h[2]=2;h[3]=MSP_HEADER;h[4]=sp.bytes=MSP_HEADER+MSP_METRICS+(sp.count+n)*sizeof(msp_event);h[5]=sizeof(msp_event);
 h[6]=sp.count+n;h[7]=sp.drops+sp.io_drops;h[8]=sp.serial;h[9]=sp.frames;h[10]=sp.reason;h[11]=select_profile.video;
 h[12]=sp.peak_asset;h[13]=sp.peak_batch;h[14]=MSP_CAPTURE_BYTES;h[15]=sp.start;
 h[16]=select_profile.count_lo;h[17]=select_profile.count_hi;h[18]=select_profile.bus_lo;h[19]=select_profile.bus_hi;h[20]=select_profile.samples;h[21]=select_profile.bus_hz;
 h[22]=sp.last;h[23]=sp.high;memcpy_(h+24,input_probe_build_id,16);
 h[28]=sp.count;h[29]=n;h[30]=sp.io_drops;h[31]=select_profile.osd;h[32]=select_profile.reserved;h[50]=select_profile.enabled;
 for(unsigned i=0;i<16;i++){h[33+i]=sp.art[i];h[49]+=sp.cut[i];LOOP_PAD();}
 memcpy_(sp.memory+MSP_HEADER,sp.metric,MSP_METRICS);
 h[62]=cp_crc_full(sp.memory+MSP_HEADER,sp.bytes-MSP_HEADER);h[63]=cp_crc_full(h,252);sp.whole=cp_crc_full(h,sp.bytes);
}
static void sp_freeze(unsigned reason){
 if(sp.state!=SP_CAPTURE)return;
 sp_now();sp.reason=reason;sp.state=SP_FREEZING;sp.frame_valid=0;load_profile.enabled=0;sp.age=0;
}
static void sp_packet(void){
 unsigned off=sp.packet*MSP_CHUNK,n=sp.bytes-off;if(n>MSP_CHUNK)n=MSP_CHUNK;
 msp_request *q=(msp_request *)sp.request;unsigned *h=(unsigned *)q->data;
 memcpy_(h,MSP_MAGIC,8);h[2]=1;h[3]=sp.serial;h[4]=sp.packet;h[5]=(sp.bytes+MSP_CHUNK-1)/MSP_CHUNK;h[6]=sp.bytes;h[7]=off;h[8]=n;
 memcpy_(q->data+48,sp.memory+off,n);h[9]=cp_crc_full(q->data+48,n);h[10]=sp.whole;h[11]=cp_crc_full(h,44);
 if(sp_rpc_call(MSP_DATA,48+n))sp_fail(8,MSP_DATA);
}
static void sp_transport(void){
 if(sp.pending){
  if(sp_busy(sp.client)){if(++sp.wait>180)sp_fail(9,sp.operation);return;}
  sp.pending=0;
  if(sp.operation==99){if(!sp.client[9]){sp_fail(7,99);return;}}
  else{
   msp_reply *r=(msp_reply *)sp.reply;
   if(r->abi!=MSP_ABI){sp_fail(10,r->abi);return;}
   if(sp.operation==MSP_STOP){sp.shutdown_done=1;if(r->result||sp.stop_error)sp_fail(11,r->result);else sp_finish(SP_DONE);return;}
   if(sp.operation==MSP_START){
    if(r->result){sp_fail(12,r->result);return;}
    sp.network_ready=1;
   }
   if(r->result==0&&sp.operation==MSP_DATA){sp.packet++;sp.age=0;}
   else if(r->result<0&&++sp.age>180){sp.state=SP_STOPPING;sp.stop_error=1;sp_gate_off();}
   if(sp.state==SP_EXPORT&&sp.packet*MSP_CHUNK>=sp.bytes){if(++sp.round<2)sp.packet=0;else{sp.state=SP_STOPPING;sp.age=0;}}
  }
 }
 if(sp.state==SP_EXPORT){
  if(!sp.network_ready){if(sp_rpc_call(MSP_START,0))sp_fail(8,MSP_START);}
  else sp_packet();
  return;
 }
 if(sp.state==SP_STOPPING){
  msp_reply *r=(msp_reply *)sp.reply;
  if(!sp.stop_error&&r->ack_session==sp.serial&&r->ack_crc==sp.whole&&!r->queued){sp_gate_off();if(sp_rpc_call(MSP_STOP,0))sp_fail(8,MSP_STOP);return;}
  if(++sp.age>180){sp.stop_error=1;sp_gate_off();}
  if(sp.stop_error){if(sp_rpc_call(MSP_STOP,0))sp_fail(8,MSP_STOP);return;}
  if(sp_rpc_call(MSP_STATUS,0))sp_fail(8,MSP_STATUS);
 }
}
/* Native 18a230 stays in SELECT while opening a folder (6 -> 7 -> 3)
 * and returning to the categories (8 -> 1). These are measured transitions,
 * not scene departures. Arm only on a settled view; keep an active capture
 * across the transitions. Native options/exit and our modal owners still
 * retire the capture before their workspace can be reused. */
static int sp_select_scope(void){
 if(!MS_STATE||ui_active||menu.active)return 0;
 unsigned s=I32(MS_STATE+0x3c);
 return s==1||s==3||s==6||s==7||s==8;
}
static void sp_tick(void){
 if(!select_profile.enabled)return;
 if(sp.state>=SP_DONE)return;
 if(sp.state==SP_ARMED){
  if(!MS_STATE||(I32(MS_STATE+0x3c)!=1&&I32(MS_STATE+0x3c)!=3)||ui_active||menu.active||!fs_font_ready())return;
  if(!select_profile.reserved||select_profile.seconds<5||select_profile.seconds>60){sp_fail(1,select_profile.reserved);return;}
  sp.memory=malloc_(MSP_CAPTURE_BYTES);if(!sp.memory){sp_fail(2,MSP_CAPTURE_BYTES);return;}
  for(unsigned i=0;i<MSP_CAPTURE_BYTES/4;i++){((unsigned *)sp.memory)[i]=0;LOOP_PAD();}
  sp.last=sp.start=lp_clock();sp.high=0;sp.published=sp.start;sp.serial=sp.start?sp.start:1;
  sp.deadline=0;for(unsigned i=0;i<select_profile.seconds;i++){sp.deadline+=select_profile.reserved;LOOP_PAD();}
  msp_marker_begin(&sp.marker,sp.serial,sp.start,select_profile.reserved);sp.lastkey=sp.lastfolder=~0u;
  sp.state=SP_CAPTURE;load_profile.enabled=1;sp.frame_start=sp.start;
 }
 if(sp.state==SP_CAPTURE){
  if(!sp_select_scope()){sp.detail=1;sp_leave();return;}
  unsigned long long now=sp_now();sp_cursor();
  unsigned q=lp_word(0x78af08),b=lp_word(0x78b70c);if(q>sp.peak_asset)sp.peak_asset=q;if(b>sp.peak_batch)sp.peak_batch=b;
  if(sp.reason==2)sp_freeze(2);else if(now-sp.start>=sp.deadline)sp_freeze(1);else return;
 }
 if(sp.state==SP_FREEZING){
  if(lp_word(0x78af08)||lp_word(0x78b70c)){if(++sp.age>180)sp_fail(3,sp.age);return;}
  if(vd_select_trylock()<0){if(++sp.age>180)sp_fail(4,sp.age);return;}
  sp_snapshot();vd_select_unlock();sp.state=SP_FROZEN;
 }
 if(sp.state==SP_FROZEN){
  if(select_profile.enabled==1){unsigned serial=0;int r=vd_probe_export((const char *)(unsigned long)load_profile.path,&serial,sp.memory,MSP_HEADER,sp.memory+MSP_HEADER,sp.bytes-MSP_HEADER);if(r<0)sp_fail(5,(unsigned)r);else sp_finish(SP_DONE);return;}
  if(!select_profile.gate||!select_profile.token){sp_fail(6,select_profile.gate);return;}
  if(sp_bind(sp.client)<0){sp_fail(7,0);return;}
  sp.pending=1;sp.operation=99;sp.wait=0;sp.state=SP_EXPORT;return;
 }
 sp_transport(); /* Async calls only after freeze. Navigation stays live. */
}
static void sp_after_update(void){
 if(sp.state==SP_CAPTURE){if(!sp_select_scope()){sp.detail=2;sp_leave();}else sp_cursor();}
}
static void sp_leave(void){
 /* Native CHART/PLAY constructors can run before the first SELECT capture.
  * They have nothing to retire then; don't consume the one-shot session. */
 if(!select_profile.enabled||sp.state==SP_ARMED||sp.state>=SP_DONE)return;
 sp.failed_asset=MS_STATE?I32(MS_STATE+0x3c):~0u;
 sp.state=SP_CANCELLED;sp_release();
 /* A pending RPC owns only PERMANENT request/reply storage, never the freed
  * frame heap. Do not clear/reuse those bytes on timeout or scene departure. */
 if(select_profile.enabled!=2)return;
 for(unsigned i=0;i<4&&!sp_gate_off();i++)sp_wait_frame();
 for(unsigned i=0;sp.pending&&i<4&&sp_busy(sp.client);i++)sp_wait_frame();
 if(!sp.pending||!sp_busy(sp.client)){
  if(sp.pending&&sp.operation==MSP_START){sp.gate_dma=0;sp_gate_off();}
  sp.pending=0;
  if(sp.client[9]&&!sp_rpc_call(MSP_STOP,0)){
   for(unsigned i=0;i<4&&sp_busy(sp.client);i++)sp_wait_frame();
   if(!sp_busy(sp.client)){sp.pending=0;sp.shutdown_done=1;}
  }
 }
}
static void sp_hex(char *p,unsigned n){
 for(unsigned i=0;i<8;i++){p[7-i]="0123456789ABCDEF"[n&15];n>>=4;LOOP_PAD();}
}
static void sp_draw(void){
 if(!select_profile.enabled||!select_profile.osd||sp.state==SP_ARMED||!fs_font_ready())return;
 if(sp.state==SP_CAPTURE){
  unsigned before=lp_clock();msp_marker_text(&sp.marker,sp.label);fs_text(sp.label,0,20,22,100);sp.marker_cost=lp_clock()-before;
 }else{
  const char *text=sp.state<SP_DONE?"PROFILE SENDING":sp.state==SP_DONE?"PROFILE COMPLETE":sp.state==SP_CANCELLED?"PROFILE CANCELLED":"PROFILE FAILED";
  fs_text(text,sp.state>=SP_FAILED?4:0,20,22,100);
  if(sp.state==SP_FAILED){
   char row[]="E00000000 R00000000";sp_hex(row+1,sp.failure);sp_hex(row+11,sp.detail);fs_text(row,4,20,36,100);
   row[0]='Q';row[10]='B';sp_hex(row+1,sp.failed_asset);sp_hex(row+11,sp.failed_batch);fs_text(row,4,20,50,100);
   if(sp.failure==5){row[0]='O';row[10]='N';sp_hex(row+1,vd_select_export_info[0]);sp_hex(row+11,vd_select_export_info[1]);fs_text(row,4,20,64,100);
    row[0]='A';row[10]='C';sp_hex(row+1,vd_select_export_info[2]);sp_hex(row+11,vd_select_export_info[3]);fs_text(row,4,20,78,100);}
  }else if(sp.state==SP_CANCELLED){char row[]="C00000000 S00000000";sp_hex(row+1,sp.detail);sp_hex(row+11,sp.failed_asset);fs_text(row,4,20,36,100);}
  else if(sp.label[0])fs_text(sp.label,0,20,36,100);
 }
}
#define SP_ARGS unsigned long long a,unsigned long long b,unsigned long long c,unsigned long long d,unsigned long long e,unsigned long long f,unsigned long long g,unsigned long long h
#define SP_PASS a,b,c,d,e,f,g,h
unsigned long long lp_update(SP_ARGS){
 sp_shutdown();
 if(sp.state==SP_CAPTURE){sp.frame_start=lp_clock();for(unsigned i=0;i<5;i++){sp.parts[i]=0;LOOP_PAD();}}
 unsigned long long r=sp_native(0x14e670)(SP_PASS);
 if(sp.state==SP_CAPTURE){unsigned ticks=lp_clock()-sp.frame_start;sp.parts[0]=ticks;lp_end(LP_UPDATE,(1ull<<32)|sp.frame_start,0,0,0);}return r;
}
unsigned long long lp_draw(SP_ARGS){
 if(sp.state==SP_CAPTURE){unsigned long long now=sp_now();sp.frame_tick_lo=(unsigned)now;sp.frame_tick_hi=(unsigned)(now>>32);sp.marker.last=(unsigned)now;sp.marker.high=(unsigned)(now>>32);sp.marker.frame++;
  sp.interval=sp.frames?sp.frame_tick_lo-sp.previous_draw:0;sp.previous_draw=sp.frame_tick_lo;sp.marker_cost=0;sp.frame_valid=1;}
 unsigned long long span=lp_begin(),r=sp_native(0x14e280)(SP_PASS);
 if(span&&sp.state==SP_CAPTURE){sp.parts[1]=lp_clock()-(unsigned)span;lp_end(LP_DRAW,span,0,0,0);}return r;
}
#define SP_CALL(name,site,metric,part) unsigned long long lp_##name(SP_ARGS){unsigned long long span=lp_begin(),r=sp_native(site)(SP_PASS);if(span&&sp.state==SP_CAPTURE){sp.parts[part]=lp_clock()-(unsigned)span;lp_end(metric,span,0,0,0);}return r;}
SP_CALL(submit,0x136c30,LP_SUBMIT,2)
SP_CALL(flip,0x139be0,LP_FLIP,3)
SP_CALL(vblank,0x10eca0,LP_VBLANK,4)
#undef SP_CALL
unsigned long long lp_tail(SP_ARGS){
 unsigned long long r=sp_native(0x14e250)(SP_PASS);
 if(sp.state==SP_CAPTURE&&sp.frame_valid){
  sp.frame_valid=0;sp.frames++;unsigned total=lp_clock()-sp.frame_start;
  if(sp.count==MSP_EVENTS){sp.drops++;sp.reason=2;return r;}
  sp_events()[sp.count++]=(msp_event){9,sp.frame_tick_lo,sp.frame_tick_hi,sp.marker.frame,sp.interval,total,sp.parts[0],sp.parts[1],sp.parts[2],sp.parts[3],sp.parts[4],sp.marker_cost};
 }
 return r;
}
unsigned long long lp_chart(SP_ARGS){if(sp.state>SP_ARMED&&sp.state<SP_DONE)sp.detail=3;sp_leave();return sp_native((unsigned long)lp_chart_body)(SP_PASS);}
unsigned long long lp_play(SP_ARGS){if(sp.state>SP_ARMED&&sp.state<SP_DONE)sp.detail=4;sp_leave();return sp_native((unsigned long)lp_play_body)(SP_PASS);}
unsigned long long lp_select_ctor(SP_ARGS){return sp_native((unsigned long)lp_select_ctor_body)(SP_PASS);}
unsigned long long lp_result(SP_ARGS){return sp_native((unsigned long)lp_result_body)(SP_PASS);}
unsigned long long lp_result_draw(SP_ARGS){return sp_native((unsigned long)lp_result_draw_body)(SP_PASS);}
#undef SP_ARGS
#undef SP_PASS
