/* Private native test scene25. The module shares title-read scratch only while
 * SELECT is suspended; fixed-address image/CRC are paired with this runtime.
 * No real song row, score identity, save, PLAY worker reset or IOP reset. */
volatile struct {u32 version,chart,bank,backing,movie,chart_dp;} preview_assets;
#ifndef PLAY_PREVIEW_TEST
#define PV_CALL(at,type) ((type)(at))
#define pv_native_ctor() PV_CALL(0x154930,u8 *(*)(void))()
#define pv_native_select(p) PV_CALL(0x1919a0,void (*)(u8 *))(p)
#endif
static void pvu_open(u8 *),pvu_select(u8 *),pvu_ctor(u8 *);
static u32 pvu_hold(void);

#if defined(__mips__)
#pragma clang section text=".ui_preview.text" rodata=".ui_preview.rodata" data=".ui_preview.data" bss=".ui_preview.bss"
#endif
#define PV0(at) PV_CALL(at,u32 (*)(void))()
#define PV1(at,v) PV_CALL(at,void (*)(u32))((u32)(unsigned long)(v))
#define PV2(at,a,b) PV_CALL(at,void (*)(u32,u32))((u32)(unsigned long)(a),(u32)(unsigned long)(b))
enum {PV_FADE=1,PV_PUSH,PV_PLAY,PV_RETURN,PV_DONE};
#include "preview_artwork.h"
static struct pv_state {
 u32 phase,exit,armed,held,cycles;
 u8 *parent,*child;
 u32 vtable[45];u8 record[0x134];
 u8 ms[0x50],work[0x68],covers[0x30],prefs[0x70];
 s32 song,diff,kind,override;u32 mask,autoplay[2];
 __typeof__(fhs) fhs;__typeof__(qr) qr;__typeof__(lq) lq;
 __typeof__(fs_side) counts;struct mm_options options;
 u32 stopping,style,reload;
 struct cs_preferences cs_draft;struct mm_options display_draft;
 u32 edit_mask,row,nav_held[2],nav_repeat[2],accept;
 struct pv_art_state art;
 u32 native_layout[2];
 u32 quit_menu;
} *pv;
#if defined(PLAY_PREVIEW_TEST) && !defined(PLAY_PREVIEW_ART_TEST)
/* Native artwork/worker execution has its own production regression. */
static void pv_art_init(void){for(u32 i=0;i<sizeof pv->art/4;i++){((u32 *)&pv->art)[i]=0;LOOP_PAD();}}
static void pv_art_clear(void){pv_art_init();}
static void pv_art_tick(void){}
static int pv_art_idle(void){return 1;}
static void pv_art_change(u32 side,u32 kind,u32 value){(void)side;(void)kind;(void)value;}
#else
#include "preview_artwork.c"
#endif
#ifdef PREVIEW_TRACE
static void preview_trace_start(u8 *),preview_trace_context(u32,u8 *,u8 *),preview_trace_mark(u32);
static void preview_trace_boundary(u32);
#define PVT(n) do{preview_trace_context(pv?pv->phase:0,pv?pv->parent:0,pv?pv->child:0);preview_trace_mark(n);}while(0)
#define PVB(n) preview_trace_boundary(n)
#else
#define PVT(n) ((void)0)
#define PVB(n) ((void)0)
#endif
static void pv_editor_begin(void),pv_editor_update(void),pv_editor_draw(void),pv_editor_adopt(void);
static void pv_layout_restore(void){
#if !defined(PLAY_PREVIEW_TEST) || defined(PLAY_PREVIEW_EDITOR_TEST)
 mm_copy(cs_system()+0x84,pv->native_layout,8);
#endif
}
static void pv_restore(void){
 pv_layout_restore();
 mm_copy((void *)0x7a3290,pv->work,sizeof pv->work);mm_copy((void *)0x2ad560,pv->covers,sizeof pv->covers);
 mm_copy(nm_options(nm_preferences(),nm_style()),pv->prefs,sizeof pv->prefs);
 PV1(0x205e10,pv->song);PV1(0x205df0,pv->diff);PV1(0x1f5840,pv->kind);PV1(0x2104d0,pv->mask);
 for(u32 s=0;s<2;s++){LOOP_PAD();PV2(0x1eae10,s,pv->autoplay[s]);}
 SONG_OVERRIDE=pv->override;fhs=pv->fhs;qr=pv->qr;lq=pv->lq;
 mm_copy(fs_side,pv->counts,sizeof fs_side);menu_control.options=pv->options;
 cs_live=0;cs_apply();menu_apply();
}
static void pv_noop(u8 *scene){(void)scene;}
static void pv_auto(u8 *scene){
 (void)scene;u32 mask=pv->style==0?1:pv->style==1?2:3;
 /* DP has one owner and two fields; battle has two independent owners. Both
  * need AUTO on two native fields, while SP must leave its inactive side off. */
 PV2(0x1eae10,0,mask&1);PV2(0x1eae10,1,(mask>>1)&1);
}
static void pv_diff(u8 *scene){(void)scene;PV1(0x205df0,0);}
static void pv_retire(void){
 /* Scene25's retain-assets flag bypasses155b40. We reload both destinations,
  * so perform its FULL graphics retirement after the native drain/fade.
  * 1357e0 alone leaves the20-entry model table and its allocation alive;
  * another SELECT pack appends11 records into the adjacent asset queue.
  * 14c2b0 frees/reset models AND renderer work, as native155b40 does. */
 pv_art_clear();PVT(0x41);PV0(0x13db90);PVT(0x42);PV0(0x1357e0);PVT(0x43);PV0(0x14c2b0);PVT(0x44);
}
static void pv_select_reload(u8 *scene){
 /*18b1e0 reloads SELECT's base resources, while our1919a0 bridge retains
  * the list allocations. Its skipped1dc530 wheel constructor also owned two
  * graphics requests: container0 (bars/lamps) and12a (song titles). Restore
  * those requests without resetting the retained wheel/canonical aliases.
  * Font1c4 from229380 is already in18b1e0's base list: do not enqueue twice.
  * Queue before parent phase0; native asset-idle barrier precedes any draw. */
 PV1(0x18b1e0,scene);PV1(0x136190,0);PV1(0x136190,0x12a);
}
static void pv_finish(u8 *scene){
 /* Common phase5 runs ONLY after1f2170 has drained PLAY. Child remains alive
  * until native action queue deletes it, so the cloned vtable is retained. */
 pv_retire();
 if(!pv->exit){pv->cycles++;pv->stopping=pv->reload=0;pv->armed=0;I32(scene+0x2c)=I32(scene+0x30)=0;I32(scene+0x34)=I32(scene+0x38)=0;PV1(0x155e00,scene);return;}
 play_preview_record=0;pv_restore();pv->phase=PV_RETURN;
 /* Native resource constructor reloads textures/sounds. Its scoped1919a0
  * bridge retains the exact existing SELECT lists, wheel and window bindings. */
 pv_select_reload(pv->parent);I32(pv->parent+0x2c)=I32(pv->parent+0x30)=0;
 I32(pv->parent+0x34)=I32(pv->parent+0x38)=0;PV2(0x151200,scene,0);
}
static void pv_update(u8 *scene){
 PVT(0x50);PVB(0xa0);pv_art_tick();pv_editor_update();PVB(0xa1);
 if((pv->exit||pv->reload) && fs_phase()==5 && !pv->stopping && pv_art_idle()){
  /* Match native1f5280 held exit: stop chart dispatch and PLAY input BEFORE
   * setting the abort flag. A bare203d50 keeps AUTO/key producers running
   * while1f3200 stops the bank/stream and draws its20-field abort fade.
   * An exit queued during LOAD/READY waits for the normal PLAY boundary. */
  pv->stopping=1;PV1(0x1ea200,1);PV1(0x13ac70,0);PV2(0x203d50,1,0);
 }
 PVB(0xa2);u32 finished=pv->art.finished?1:PV0(0x1f2170);PVB(0xa3);PVT(0x51);if(finished) {pv->art.finished=1;if(PV0(0x203d40)&&!pv->exit&&!pv->reload){pv->exit=pv->accept=1;}if(pv_art_idle())PV1(0x155920,scene);}
 else if(!pv->exit&&!pv->reload)pv_editor_draw();

}
#ifdef PREVIEW_TRACE
static void pv_setup(u8 *scene){PVT(0x46);PV1(0x1a1730,scene);PVT(0x47);}
#endif
static __attribute__((noinline)) void pvu_ctor(u8 *child){
 if(!pv || pv->phase!=PV_PUSH)return;
 PVT(0x40);pv_retire();
 pv->child=child;pv->phase=PV_PLAY;
 mm_copy(pv->vtable,(void *)0x2ac6f0,sizeof pv->vtable);
 #ifdef PREVIEW_TRACE
 pv->vtable[0x6c/4]=(u32)(unsigned long)pv_setup;
#endif
 pv->vtable[0x70/4]=(u32)(unsigned long)pv_update;
 pv->vtable[0x74/4]=(u32)(unsigned long)pv_finish;
 pv->vtable[0x90/4]=(u32)(unsigned long)pv_auto;
 pv->vtable[0x98/4]=(u32)(unsigned long)pv_diff;
 pv->vtable[0x8c/4]=(u32)(unsigned long)pv_noop;
 *(u32 **)child=pv->vtable;play_preview_record=pv->record;PVT(0x45);
}
static __attribute__((noinline)) void pvu_open(u8 *parent){
 PVT(0x10);
 if(pv || preview_assets.version!=2 || preview_assets.chart<1062 || preview_assets.chart>65534 || preview_assets.chart_dp<1062 || preview_assets.chart_dp>65534 || preview_assets.bank<1062 || preview_assets.bank>65534 || preview_assets.backing<1062 || preview_assets.backing>65534 || preview_assets.movie<1062){menu.error=-31;return;}
 pv=malloc_(sizeof *pv);if(!pv){menu.error=-31;return;}
#ifdef PREVIEW_TRACE
 pv->phase=0;pv->parent=parent;pv->child=0;
#endif
 mm_copy(pv->record,rec_of(song_ctx(),29),sizeof pv->record);
 mm_copy(pv->ms,MS_STATE,sizeof pv->ms);mm_copy(pv->work,(void *)0x7a3290,sizeof pv->work);
 mm_copy(pv->covers,(void *)0x2ad560,sizeof pv->covers);
 mm_copy(pv->prefs,nm_options(nm_preferences(),nm_style()),sizeof pv->prefs);
 pv->fhs=fhs;pv->qr=qr;pv->lq=lq;pv->options=menu_control.options;
 mm_copy(pv->counts,fs_side,sizeof fs_side);
 pv->song=PV0(0x205e00);pv->diff=PV0(0x205de0);pv->kind=PV0(0x1f5830);pv->override=SONG_OVERRIDE;pv->mask=*(u16 *)0x2ad660;
 for(u32 s=0;s<2;s++){LOOP_PAD();pv->autoplay[s]=PV_CALL(0x1eade0,u32 (*)(u32))(s);}
 pv->style=nm_style();
#if !defined(PLAY_PREVIEW_TEST) || defined(PLAY_PREVIEW_EDITOR_TEST)
 mm_copy(pv->native_layout,cs_system()+0x84,8);
#endif
 /* SP and battle consume one SP source stream. Native battle makes its own
  * second side; DP consumes explicit P1/P2 source events. Feeding a DP chart
  * to SP creates unwanted P2 nodes, even when only one field is visible. */
 u32 chart=(pv->style==2 || pv->style==3)?preview_assets.chart_dp:preview_assets.chart;
 /* Private descriptor, preserving validated donor timing/mixer flags. */
 for(u32 i=0;i<64;i++){LOOP_PAD();pv->record[i]=0;}
 mm_copy(pv->record,"PLAY SCENE PREVIEW",18);*(u16 *)(pv->record+0x42)=120;
 *(u16 *)(pv->record+0x44)=*(u16 *)(pv->record+0x46)=0xffff;
 for(u32 i=0;i<10;i++){LOOP_PAD();pv->record[0x4a+i]=1;*(u32 *)(pv->record+0xc0+i*4)=chart;}
 *(u32 *)(pv->record+0x58)=preview_assets.movie;*(u32 *)(pv->record+0x5c)=0;pv->record[0x62]=0;
 for(u32 i=0x64;i<0x94;i++){LOOP_PAD();pv->record[i]=255;}
 /* Keep the donor's PLAY title handles+98..A8 paired with its container+94.
  * Native201b10 ALWAYS constructs this sprite, even in test kind8. A zero
  * handle is container0/sprite0, not "no title"; retired SELECT container0
  * is absent here and native135770 dereferences214. Optional zero variants
  * already fall back to the donor's nonzero base handle via1e3240. */
 for(u32 i=0xe8;i<0x108;i+=8){LOOP_PAD();*(u16 *)(pv->record+i)=*(u16 *)(pv->record+i+2)=preview_assets.bank;*(u16 *)(pv->record+i+4)=*(u16 *)(pv->record+i+6)=preview_assets.backing;}
 for(u32 i=0x108;i<0x126;i+=2){LOOP_PAD();*(u16 *)(pv->record+i)=704;}
 PVT(0x11);pv->child=pv_native_ctor();PVT(0x12);if(!pv->child){free_(pv);pv=0;menu.error=-31;return;}
 if(!*(u32 *)(pv->child+0x28)){PV_CALL(0x226890,void (*)(u8 *,u32))(pv->child,1);free_(pv);pv=0;menu.error=-31;return;}
 pv->parent=parent;pv->quit_menu=pv->exit=pv->armed=pv->held=pv->cycles=pv->stopping=pv->reload=0;pv_art_init();pv->phase=PV_FADE;pv_editor_begin();
 /* SELECT owns both a resident bank and a streaming BGM channel. 13e240
  * fades only the bank: 13d450 then waits forever on the still-playing BGM.
  * Native13e170 schedules BOTH fades through the ordinary audio worker. */
 PVT(0x13);PV1(0x13e170,15);PVT(0x14);PV1(0x155920,parent);I32(parent+0x30)=3;PVT(0x20);
}
static __attribute__((noinline)) u32 pvu_hold(void){return pv && ((pv->phase>=PV_PUSH && pv->phase<PV_RETURN) || (pv->phase==PV_RETURN && I32(pv->parent+0x2c)<2));}
static __attribute__((noinline)) void pvu_select(u8 *scene){
 if(!pv)return;
 if(pv->phase==PV_FADE){
  PVT(0x21);if(PV0(0x1560c0))return;
  /* 13f420 prevents overlap with SELECT's native sound/stream task. */
  PVT(0x22);if(PV0(0x13f420))return;
  PVT(0x23);if(!PV0(0x14cf70))return;
  PVT(0x24);if(PV0(0x13d450))return;
  pv->phase=PV_PUSH;SONG_OVERRIDE=-1;PVT(0x30);
  if(!PV_CALL(0x151350,u32 (*)(u8 *,u32,u32,u32))(scene,0x25,0,0)){PV_CALL(0x226890,void (*)(u8 *,u32))(pv->child,1);pv_retire();pv_restore();pv->phase=PV_RETURN;
   pv_select_reload(scene);I32(scene+0x2c)=I32(scene+0x30)=0;}
 }else if(pv->phase==PV_RETURN){
  mm_copy(MS_STATE,pv->ms,sizeof pv->ms);pv_restore();pv->phase=PV_DONE;
 }
 if(pv->phase==PV_DONE){PVT(0x70);menu.active=1;menu.page=MENU_ROOT;menu.row=5;menu.release=1;menu.error=0;pv_editor_adopt();free_(pv);pv=0;}
}
#include "play_preview_editor.c"
#if defined(__mips__)
#pragma clang section text="" rodata="" data="" bss=""
#endif
/* The CD worker reaches overlay code only for its owned, still-live request.
 * pv_update drains it before fade/retirement, and ui_release follows return. */
static int pve_asset_queue(u32 *q){
#if !defined(PLAY_PREVIEW_TEST) || defined(PLAY_PREVIEW_ART_TEST)
 if(play_preview_record)return pv_art_queue(q);
#endif
 return 0;
}
/* Resident gates never call unloaded code. Release occurs after overlay return
 * and after native action processing has destroyed the child and its vtable. */
static void pve_open(u8 *scene){
#ifdef PREVIEW_TRACE
 preview_trace_start(scene);
#endif
 if(ui_load(2)){pvu_open(scene);if(!pv)ui_release();}else menu.error=-31;}
static void pve_select(u8 *scene){pvu_select(scene);if(!pv)ui_release();}
static u32 pve_draw_hold(void){return ui_active==2 && pvu_hold();}
unsigned long long pve_ctor_call(FS_ARGS){
 u8 *child;
 if(ui_active==2 && pv && pv->phase==PV_PUSH){child=pv->child;pvu_ctor(child);}
 else child=pv_native_ctor();
 return (unsigned long long)(unsigned long)child;
}
unsigned long long pve_select_init_call(FS_ARGS){if(ui_active==2 && pv && pv->phase==PV_RETURN)return 0;pv_native_select((u8 *)(unsigned long)a);return 0;}
/* All original callers/arguments are forwarded outside the explicit lifetime. */
#define PV_FORWARD(name,at) unsigned long long name(FS_ARGS){if(play_preview_record)return 0;return PV_CALL(at,unsigned long long (*)(FS_ARGS))(FS_PASS);}
PV_FORWARD(pve_graph_call,0x1e4d70)
PV_FORWARD(pve_rival_call,0x1e4b40)
PV_FORWARD(pve_graph_save_call,0x1e4eb0)
PV_FORWARD(pve_cover_save_call,0x203150)
PV_FORWARD(pve_options_save_call,0x1f3930)
unsigned long long pve_options_call(FS_ARGS){
 if(!play_preview_record)return PV_CALL(0x203490,unsigned long long (*)(FS_ARGS))(FS_PASS);
 /* The native test forces SP. Use the same actual style preferences as PLAY,
  * then restore kind8 before any asynchronous load/update can observe it. */
 PV1(0x1f5840,4);PV0(0x203490);PV1(0x1f5840,8);
 /* The preview is always the requested seven-key staircase, independent of
  * saved RANDOM/MIRROR/assist/5KEY. Only private working options are changed. */
 for(u32 s=0;s<2;s++){LOOP_PAD();u32 *o=(u32 *)(0x7a32a0+s*0x2c);o[1]=o[2]=o[5]=o[7]=o[8]=0;}
 return 0;
}

#undef PVT
