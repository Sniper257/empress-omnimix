/* Private PREVIEW_TRACE diagnostic. External debug.cfg selects previewtrace1
 * (RAM/panel) or2 (also sparse closed checkpoints). Never changes readiness,
 * forwards every native sprite call, no allocations/input polls/PLAY writes. */
#ifndef PREVIEW_TRACE_TEST
#define pt_word(a) (*(volatile u32 *)(unsigned long)(a))
#define pt_rect ((void (*)(s32,s32,s32,s32,u32,s32))0x12ff00)
static u32 pt_clock(void){u32 n;__asm__ volatile("mfc0 %0,$9":"=r"(n));return n;}
#define pt_read vd_ui_read
#define pt_append vd_exit_append
#define pt_crc cp_crc_full
extern s32 vd_exit_append(const char *,u32 *,const void *,u32);
extern volatile u32 exit_io[6];
extern volatile s32 preview_journal_open[2];
extern unsigned long long pvt_query_body(FS_ARGS),pvt_sprite_body(FS_ARGS);
extern unsigned long long pvt_decode_body(FS_ARGS),pvt_register_body(FS_ARGS);
#define pt_unpack_native(a,b) asset_unpack((void *)(unsigned long)(a),(u32 *)(unsigned long)(b))
#define pt_thread() (((u32 (*)(void))0x10eb50)())
#define pt_submit_native(...) (((unsigned long long (*)(FS_ARGS))0x136c30)(__VA_ARGS__))
#define pt_release_native(...) (((unsigned long long (*)(FS_ARGS))0x13a530)(__VA_ARGS__))
#define pt_batch_native(...) (((unsigned long long (*)(FS_ARGS))0x14cb20)(__VA_ARGS__))
#define pt_idle_native(...) (((unsigned long long (*)(FS_ARGS))0x14cf70)(__VA_ARGS__))
#endif
/* Main-thread progress only: no snapshots, RPC, allocations or file writes.
 * The worker samples it in its OWN closed record. A submitted panel can lag
 * the CPU; neither this value nor a native call site is an exception PC. */
static volatile u32 pt_main_boundary;
static struct {u32 mode,stage,frames,phase,parent,child,serial,written,error,head,n,sequence;
 u32 missing_cont,missing_idx,missing_site,seen[4],pending[16][32];char path[128];} pt;
/* Only the native asset worker owns this stream/state. Never share the main
 * thread's backlog, snapshot or file cursor across an I/O-induced reschedule.
 * Callback reads have returned and vdisc's lock is released before entry.
 * Each bounded record is closed BEFORE forwarding the operation it describes;
 * failed/busy try-locks never prevent the original operation from running. */
static struct {u32 active,owner,container,input,output,bytes,chunks,serial,written,error,dropped,calls,gates,stage,input_crc,input_bytes;
 u32 record[32];char path[128];} ptl;
volatile u32 preview_trace_snapshot[32];
static u32 pt_playing(void){u32 p=pt_word(0x2ad534);return play_preview_record && p>=5 && p<8;}
static void preview_trace_boundary(u32 stage){
 if(pt.mode && ui_active==2 && play_preview_record && !pt_playing())pt_main_boundary=stage;
}
static u32 pt_scene_phase(u32 p){return p>=0x994700 && p<=0x1ffffb8 && !(p&3)?pt_word(p+0x2c):~0u;}
static void pt_snapshot(void){
 volatile u32 *r=preview_trace_snapshot;
 r[0]=0x58494d4d;r[1]=0x31545650;r[2]=1;r[3]=pt.sequence;r[4]=pt.stage;r[5]=pt.frames;r[6]=pt_clock();
 r[7]=pt.phase;r[8]=pt_scene_phase(pt.parent);r[9]=pt.phase<4?pt_scene_phase(pt.child):~0u;
 r[10]=pt_word(0x2ad534);r[11]=pt_word(0x2ad538);r[12]=pt_word(0x2ad120);r[13]=pt_word(0x2ad134);
 r[14]=pt_word(0x78af08);r[15]=pt_word(0x78af00);r[16]=r[17]=0;
 if(r[14] && r[15]<72){u32 q=0x78af0c+r[15]*28;r[16]=pt_word(q+16);r[17]=pt_word(q+20);}
 r[18]=exit_io[0];r[19]=exit_io[1];r[20]=exit_io[3];r[21]=pt_word(0x78aba4);
 r[22]=pt_word(0x727380);r[23]=pt_word(0x727388);r[24]=pt.missing_cont;r[25]=pt.missing_idx;r[26]=pt.missing_site;
 r[27]=pt.written;r[28]=pt.error;r[29]=pt.n;r[30]=pt.mode;r[31]=0;
}
static void pt_flush(void){
 if(pt.mode!=2 || pt.error || !pt.n || pt_playing() || pt_word(0x78af08) || pt_word(0x2ad120))return;
 s32 n=pt_append(pt.path,&pt.serial,pt.pending[pt.head],128);
 if(n==-8)return;if(n<0){pt.error=(u32)-n;return;}
 pt.head=(pt.head+1)%16;pt.n--;pt.written++;
}
static void preview_trace_context(u32 phase,u8 *parent,u8 *child){if(parent)pt.parent=(u32)(unsigned long)parent;pt.phase=phase;pt.child=(u32)(unsigned long)child;}
static void preview_trace_mark(u32 stage){
 if(!pt.mode || pt_playing())return;pt.stage=stage;pt_snapshot();
 if(stage>=128 || (pt.seen[stage/32]&(1u<<(stage%32))))return;
 pt.seen[stage/32]|=1u<<(stage%32);
 if(pt.mode==2 && pt.n<16){u32 *r=pt.pending[(pt.head+pt.n)%16];
  for(u32 i=0;i<31;i++){r[i]=preview_trace_snapshot[i];LOOP_PAD();}
  r[3]=pt.sequence++;r[31]=pt_crc(r,124);pt.n++;pt_flush();}
}
static void pt_load_mark(u32,u32);
static void preview_fault_start(void);
static void preview_trace_start(u8 *scene){
 /* Read using the same checked-length MMCE client as modal settings. The
  * explicit debug image is chosen by debug1; launcherlog remains independently0. */
 for(u32 i=0;i<sizeof pt/4;i++){((u32 *)&pt)[i]=0;LOOP_PAD();}
 for(u32 i=0;i<sizeof ptl/4;i++){((u32 *)&ptl)[i]=0;LOOP_PAD();}pt_main_boundary=0;pt.parent=(u32)(unsigned long)scene;
 unsigned n=mm_length(menu_control.path),end=0;
 for(u32 i=0;i<n;i++){if(menu_control.path[i]=='/'||menu_control.path[i]==':')end=i+1;LOOP_PAD();}
 if(!end || end>90)return;
 char path[128],text[512];mm_copy(path,menu_control.path,end);mm_copy(path+end,"debug.cfg",10);
 s32 got=pt_read(path,text,sizeof text);if(got<0){pt.mode=1;pt.error=(u32)-got;}
 else for(u32 at=0;at<(u32)got;){u32 end=at;while(end<(u32)got&&text[end]!='\n'){end++;LOOP_PAD();}
  if(mm_key(text+at,end-at,"previewtrace")){int v=0;if(!mm_integer(text+at,end-at,12,&v)&&v>=0&&v<=2)pt.mode=v;}
  at=end+1;
 }
 mm_copy(pt.path,path,end);mm_copy(pt.path+end,"logs/preview-",14);
 mm_copy(ptl.path,path,end);mm_copy(ptl.path+end,"logs/preview-load-",19);preview_trace_mark(1);
 /* Prove this stream can be created before handing it to the worker. The
  * preflight record is explicitly a MAIN-thread record, not callback proof. */
 ptl.active=1;pt_load_mark(0x7f,0);ptl.active=0;preview_fault_start();
}
static void pt_load_mark(u32 stage,u32 site){
 ptl.stage=stage;
 if(!ptl.active || pt.mode!=2 || ptl.error || ptl.written>=256 || pt_playing())return;
 u32 *r=ptl.record;for(u32 i=0;i<32;i++){r[i]=0;LOOP_PAD();}
 r[0]=0x58494d4d;r[1]=0x31545650;r[2]=4;r[3]=ptl.written;r[4]=stage;r[5]=pt.frames;r[6]=pt_clock();
 r[7]=ptl.container;r[8]=ptl.input;r[9]=ptl.output;r[10]=ptl.bytes;r[11]=ptl.chunks;
 r[12]=pt_main_boundary;r[13]=site;r[14]=pt_word(0x78af08);r[15]=pt_word(0x78af00);
 if(r[14] && r[15]<72){u32 q=0x78af0c+r[15]*28;r[16]=pt_word(q+16);r[17]=pt_word(q+20);}
 r[18]=exit_io[0];r[19]=exit_io[1];r[20]=exit_io[3];r[21]=pt_word(0x78aba4);
 r[22]=pt_word(0x2ad534);r[23]=pt_word(0x2ad538);
 r[24]=ptl.input_crc;r[25]=ptl.input_bytes;
 r[26]=exit_io[4];r[27]=ptl.written;r[28]=ptl.error;r[29]=ptl.dropped;r[30]=pt.mode;r[31]=pt_crc(r,124);
 s32 n=pt_append(ptl.path,&ptl.serial,r,128);
 if(n==-8){ptl.dropped++;return;}if(n<0){ptl.error=(u32)-n;return;}ptl.written++;
}
unsigned long long pvt_unpack_call(FS_ARGS){
 if(pt.mode && ui_active==2){
  ptl.calls++;ptl.gates=(pt.mode==2?1:0)|(play_preview_record?2:0)|(!pt_playing()?4:0)|(ptl.active?8:0);
 }
 u32 observe=!ptl.active && pt.mode && ui_active==2 && play_preview_record && !pt_playing();
 if(observe){
  ptl.owner=pt_thread();ptl.active=1;ptl.container=pt_word((u32)b+0x210);ptl.input=pt_word((u32)a);
  /* Fingerprint the actual queued RAM buffer before any journal RPC. At most
   * 256KiB per asset, never beyond the completed request or EE RAM. This is
   * the whole22528B title card in the reported stall, not its source file. */
  ptl.input_crc=ptl.input_bytes=0;
  u32 head=pt_word(0x78af00);
  if(pt.mode==2 && pt_word(0x78af08) && head<72){
   u32 q=0x78af0c+head*28,sectors=pt_word(q+20);
   if(pt_word(q+8)==ptl.input && sectors && sectors<=0x100000/2048 && ptl.input>=0x100000 && ptl.input<0x2000000){
    u32 bytes=sectors*2048;if(bytes>0x40000)bytes=0x40000;
    if(bytes<=0x2000000-ptl.input){ptl.input_bytes=bytes;ptl.input_crc=pt_crc((const void *)(unsigned long)ptl.input,bytes);}
   }
  }
  ptl.output=ptl.bytes=ptl.chunks=0;pt_load_mark(0x80,0x1359d0);
 }
 pt_unpack_native(a,b);
 if(observe){pt_load_mark(0x81,0x1359d0);ptl.active=0;}return 0;
}
unsigned long long pvt_decode_call(FS_ARGS){
 u32 observe=ptl.active && pt_thread()==ptl.owner;
 if(observe){ptl.output=(u32)a;ptl.bytes=(u32)c;pt_load_mark(0x82,0x1361e0);}
 unsigned long long r=pvt_decode_body(FS_PASS);
 if(observe){ptl.chunks++;pt_load_mark(0x83,0x1361e0);}return r;
}
unsigned long long pvt_register_call(FS_ARGS){
 u32 observe=ptl.active && pt_thread()==ptl.owner;
 if(observe)pt_load_mark(0x84,0x135860);
 unsigned long long r=pvt_register_body(FS_PASS);
 if(observe)pt_load_mark(0x85,0x135860);return r;
}
/* Existing JAL sites, not a rewritten dequeue or a mid-block C call. Do not
 * keep ptl.active set across the batch: that would suppress unpack tracing.
 * Each mark owns the worker stream only until its append has CLOSED. */
static u32 pt_worker_scope(void){return pt.mode && ui_active==2 && play_preview_record && !pt_playing();}
static void pt_worker_mark(u32 stage,u32 site){ptl.active=1;pt_load_mark(stage,site);ptl.active=0;}
unsigned long long pvt_release_call(FS_ARGS){
 u32 observe=pt_worker_scope() && pt_thread()==ptl.owner && !ptl.active;
 if(observe)pt_worker_mark(0x86,0x14cce8);
 unsigned long long r=pt_release_native(FS_PASS);
 if(observe)pt_worker_mark(0x87,0x14cce8);return r;
}
unsigned long long pvt_batch_call(FS_ARGS){
 u32 observe=pt_worker_scope() && !ptl.active;
 if(observe){ptl.owner=pt_thread();pt_worker_mark(0x88,0x14cd88);}
 unsigned long long r=pt_batch_native(FS_PASS);
 if(observe)pt_worker_mark(0x89,0x14cd88);return r;
}
unsigned long long pvt_idle_call(FS_ARGS){
 preview_trace_boundary(0xaa);unsigned long long r=pt_idle_native(FS_PASS);
 preview_trace_boundary(0xab);return r;
}
static void pt_missing(u32 cont,u32 idx,u32 site){
 if(!pt.mode || ui_active!=2 || pt.missing_site || pt_playing())return;
 u32 n=pt_word(0x2ad030);if(n>64)return;
 for(u32 i=0;i<n;i++){if(pt_word(0x34aa80+i*0x228+0x210)==cont)return;LOOP_PAD();}
 pt.missing_cont=cont;pt.missing_idx=idx;pt.missing_site=site;preview_trace_mark(0x60);
 /* Forward original call below: this observes a defect, never hides it. */
}
unsigned long long pvt_query_call(FS_ARGS){pt_missing((u32)a,(u32)b,0x135770);return pvt_query_body(FS_PASS);}
unsigned long long pvt_sprite_call(FS_ARGS){pt_missing((u32)a,(u32)b,0x135670);return pvt_sprite_body(FS_PASS);}
static void pt_digit(s32 x,s32 y,u32 digit){
 static const u8 bits[16]={63,6,91,79,102,109,125,7,127,111,119,124,57,94,121,113};
 static const u8 rect[7][4]={{1,0,5,1},{5,1,6,6},{5,7,6,12},{1,12,5,13},{0,7,1,12},{0,1,1,6},{1,6,5,7}};
 for(u32 i=0;i<7;i++)if(bits[digit&15]&(1u<<i))pt_rect(x+rect[i][0],y+rect[i][1],x+rect[i][2],y+rect[i][3],0xffffff,0);
}
unsigned long long pvt_submit_call(FS_ARGS){
 if(pt.mode && ui_active==2 && !pt_playing()){
  pt.frames++;pt_flush();pt_snapshot();u32 at=pt_word(0x2ad040);
  if(at>=0x34d100 && at<=0x38d100 && 0x38d100-at>=0x24000){
   pt_rect(38,38,365,204,0,1);pt_digit(48,42,10);pt_digit(56,42,3);
   /* File-free evidence remains visible even if the worker writer fails or
    * its observation predicate never opens. The regular stream is unchanged. */
   u32 panel[24]={pt.stage,pt.frames,exit_io[5],pt.phase,pt_scene_phase(pt.parent),pt.phase<4?pt_scene_phase(pt.child):~0u,
    pt_word(0x2ad120),pt_word(0x78af08),exit_io[0],ptl.calls,ptl.gates,ptl.stage,
    ptl.written,ptl.error,ptl.dropped,ptl.bytes,ptl.chunks,ptl.output,
    (u32)preview_journal_open[0],(u32)preview_journal_open[1],ptl.input_crc,
    pt_main_boundary,pt_word(0x78af00),pt_word(0x2ad078)};
   for(u32 row=0;row<8;row++)for(u32 col=0;col<3;col++)for(u32 i=0;i<8;i++)
    pt_digit(48+col*100+i*8,60+row*17,panel[row*3+col]>>(28-i*4));
  }
 }
 preview_trace_boundary(0xa4);unsigned long long r=pt_submit_native(FS_PASS);
 preview_trace_boundary(0xa5);return r;
}
