/* Preview overlay only. The resident queue gate admits just this preview's
 * staged record. Worker completion is consumed before scene/overlay teardown. */
#ifndef PV_ART_TEST
#define pa_entries ((u32 *)0x34aa80)
#define pa_count (*(u32 *)0x2ad030)
#define pa_idle_native() PV0(0x14cf70)
#endif
static u32 *pa_entry(u32 n){return pa_entries+n*(0x228/4);}
static void pa_zero(u32 *p,u32 words){for(u32 i=0;i<words;i++){p[i]=0;LOOP_PAD();}}
static void pv_art_init(void){pa_zero((u32 *)&pv->art,sizeof pv->art/4);}
static void pa_barrier(void){
#ifdef __mips__
 __asm__ volatile("sync":::"memory");
#else
 __asm__ volatile("":::"memory");
#endif
}
static void pa_drop(void){
 if(pv->art.staged[0])asset_release((void *)(unsigned long)pv->art.staged[0]);
 pv->art.staged[0]=0;
}
static void pv_art_clear(void){
 /* Native retirement is entered only after pv_art_idle, including natural
  * chart end, queued Cancel and frame reload. Current native slots are owned
  * by the following ALL-container unload, not by this staging cleanup. */
 pa_drop();pv_art_init();
}
#include "preview_frame.c"
static __attribute__((noinline)) s32 *pa_preference(u32 s){
 return s==2?&pv->cs_draft.frame:s<2?&pv->cs_draft.art[s][2]:s<5?&pv->cs_draft.notes[s-3]:&pv->cs_draft.art[s-5][1];
}
static int pv_art_idle(void){
 struct pv_art_state *a=&pv->art;if(a->state)return 0;
 if(a->attached==1&&pv->exit&&pv->accept&&!pv->reload&&!a->finished){
  for(u32 s=0;s<7;s++){LOOP_PAD();if(a->applied[s]!=(u32)*pa_preference(s))return 0;}
 }
 return 1;
}
static u32 pa_id(u32 cover){u8 *hud=*(u8 **)0x2ad5e8;return *(u32 *)(unsigned long)(0x281e00+cover*8+(hud[5]?0:4));}
static u32 pa_mask(void){return pv->style==0?1:pv->style==1?2:cs_dp()?1:3;}
static __attribute__((noinline)) u32 pa_job_id(u32 s){
 if(s<2)return pa_id(pv->art.wanted[s]);if(s==2)return pf_id(pv->art.wanted[s]);
 const struct custom_art_pack *p=cs_pack(s>=5,pv->art.wanted[s]-(s>=5?CUSTOM_BOMB_BASE:0));return p?p->fid:0;
}
static __attribute__((noinline)) u32 pa_pool(u32 s,u32 i){return s==2?pv->art.frame_slot:s<2?pv->art.native_slot[i]:pv->art.custom_slot[s>=5][i];}
static u32 pa_pool_size(u32 s){return s==2?1:s<2?pv->art.slots:2;}
static u32 pa_find(u32 s,u32 id){
 for(u32 i=0;i<pa_pool_size(s);i++){LOOP_PAD();u32 k=pa_pool(s,i);if(k<16&&pa_entry(k)[0]&&pa_entry(k)[0x84]==id)return k;}return 16;
}
static __attribute__((noinline)) u32 pa_target(u32 s){
 u32 keep=0;if(s<2&&pa_mask()==3)keep=pa_id(ca.cover[s^1]);
 /* The peer's requested ID can differ from its still-visible art. Protect
  * the actual current handle, not the queued destination. */
 if(s>=3&&pa_mask()==3){u32 side=(s-3)&1;keep=s<5?skin.field[side^1].pad[0]>>16:ca.custom_bomb[side^1]?ca.custom_bomb[side^1]->fid:0;}
 for(u32 i=0;i<pa_pool_size(s);i++){LOOP_PAD();u32 k=pa_pool(s,i);if(k<16&&(!pa_entry(k)[0]||pa_entry(k)[0x84]!=keep))return k;}
 if(s>=3&&pa_count<16){
  u32 *pool=pv->art.custom_slot[s>=5];for(u32 i=0;i<2;i++){LOOP_PAD();if(pool[i]==16){u32 k=pa_count++;pool[i]=k;pa_zero(pa_entry(k),0x228/4);pa_entry(k)[0x84]=0xffff;return k;}}
 }return 16;
}
static int pa_attach(void){
 struct pv_art_state *a=&pv->art;u32 first=(*(u8 **)0x2ad5e8)[5]?110:147;
 if(pa_count>16)return 0;
 for(u32 s=0;s<7;s++){LOOP_PAD();a->applied[s]=a->wanted[s]=*pa_preference(s);}
 for(u32 s=0;s<2;s++){LOOP_PAD();a->wanted[s]=ca.cover[s];a->custom_slot[0][s]=a->custom_slot[1][s]=16;}
 a->wanted[2]=(*(u8 **)0x2ad5e8)[9];a->frame_slot=16;
 for(u32 i=0;i<pa_count;i++){LOOP_PAD();u32 *p=pa_entry(i);if(!p[0])continue;
  if(p[0x84]==pf_id(a->wanted[2]))a->frame_slot=i;
  if(p[0x84]>=first&&p[0x84]<first+36){if(a->slots==2)return 0;a->native_slot[a->slots++]=i;}
  for(u32 kind=0;kind<2;kind++){LOOP_PAD();if(p[0x84]!=pa_job_id(3+kind*2)&&p[0x84]!=pa_job_id(4+kind*2))continue;
   u32 *pool=a->custom_slot[kind];pool[pool[0]==16?0:1]=i;
  }
 }
 if(!a->slots)return 0;
 if(pa_mask()==3&&a->slots==1){if(pa_count==16)return 0;u32 i=pa_count;u32 *p=pa_entry(i);pa_zero(p,0x228/4);p[0x84]=0xffff;a->native_slot[a->slots++]=i;pa_count=i+1;}
 return 1;
}
static void pa_show(u32 side){
 struct pv_art_state *a=&pv->art;ca.cover[side]=a->wanted[side];a->applied[side]=pv->cs_draft.art[side][2];
 if(cs_dp()){ca.cover[1]=ca.cover[0];a->applied[1]=a->applied[0];}
 u8 *hud=*(u8 **)0x2ad5e8;u32 primary=pv->style==1?1:0;
 hud[12]=ca.cover[primary];hud[13]=hud[12]==10||hud[12]==12;*(u32 *)(hud+0x228)=pa_id(ca.cover[primary])<<16;
}
static __attribute__((noinline)) void pa_custom_show(u32 s){
 struct pv_art_state *a=&pv->art;u32 side=(s-3)&1,kind=s>=5;
 if(a->applied[s]==a->wanted[s])return;ca_preview_pack(side,kind,a->wanted[s]);a->applied[s]=a->wanted[s];
 /* Returning to native art frees unreferenced custom sheets. Empty reserved
  * records stay reusable; browsing never appends an unbounded history. */
 for(u32 i=0;i<2;i++){LOOP_PAD();u32 k=a->custom_slot[kind][i];if(k>=16||!pa_entry(k)[0])continue;u32 id=pa_entry(k)[0x84],used=0;
  for(u32 p=0;p<2;p++){LOOP_PAD();u32 visible=kind?(ca.custom_bomb[p]?ca.custom_bomb[p]->fid:0):skin.field[p].pad[0]>>16;if(visible==id)used=1;}
  if(!used){asset_release((void *)(unsigned long)pa_entry(k)[0]);pa_zero(pa_entry(k),0x228/4);pa_entry(k)[0x84]=0xffff;}
 }
}
static void pa_failed(u32 side,u32 sequence){
 struct pv_art_state *a=&pv->art;
 if(a->serial[side]==sequence){a->error=120;*pa_preference(side)=a->applied[side];a->wanted[side]=side==2?(*(u8 **)0x2ad5e8)[9]:side<2?ca.cover[side]:a->applied[side];if(pv->exit&&pv->accept){pv->exit=pv->accept=0;}}
}
static void pv_art_change(u32 side,u32 kind,u32 value){
 if(side>1||kind>4)return;
 struct pv_art_state *a=&pv->art;
 if(!kind){ca_preview_effect(side,kind,value);return;}
 if(kind==1){
  u32 job=5+side;if(value<=10&&!ca.custom_bomb[side]){ca_preview_effect(side,1,value);a->applied[job]=a->wanted[job]=value;a->serial[job]++;return;}side=job;
 }else if(kind==4)side+=3;
 else if(kind==3){
  side=2;u32 current=(*(u8 **)0x2ad5e8)[9];
  if(value==7){value=ca_random(7);if(value==6)value=0;}
  if(current==6||value==6||(skin.frame_pack&&**(u32 **)0x2ad5e8==2)){pv->reload=1;return;}
  if(a->attached!=1||a->frame_slot==16||!pf_reserve()){pa_failed(side,a->serial[side]);return;}
 }
 if(a->attached!=1){pa_failed(side,a->serial[side]);return;}
 a->wanted[side]=side<2?ca_cover_resolve(value):value;a->delay[side]=8;a->serial[side]++;a->error=0;
}
/* Worker stays in the live overlay; bounded decoding is a resident helper. */
static __attribute__((noinline)) int pv_art_queue(u32 *q){
 if(!pv||q[0]!=0x1359d0||q[1]!=(u32)(unsigned long)pv->art.staged)return 0;
 struct pv_art_state *a=&pv->art;if(a->state!=1)return 0;
 u32 ok=asset_preview_decode(q,a->staged);pa_barrier();a->state=ok?2:3;return 1;
}
static void pv_art_tick(void){
 struct pv_art_state *a=&pv->art;u32 saving=pv->exit&&pv->accept&&!pv->reload&&!a->finished;
 u32 closing=(pv->exit&&!pv->accept)||pv->reload||a->finished;
 if(a->error)a->error--;
 if(!a->attached){
  if(closing||fs_phase()!=5||!pa_idle_native())return;
  a->attached=pa_attach()?1:2;if(a->attached==2){a->error=120;return;}
 }
 if(a->attached!=1)return;
 if(a->state){
  /* Ready alone is not native dequeue, nor permission to retire worker code. */
  if(a->state==1||!pa_idle_native())return;
  pa_barrier();u32 s=a->owner;
  if(a->state==2&&!closing&&a->serial[s]==a->sequence){
   u32 k=pa_target(s);
   if(k<16){
    /* Prior DMA has completed and51980 reset texture identities. Publish
     * metadata/handles together before this frame's native PLAY producers. */
    u32 *p=pa_entry(k),old=p[0],callback=p[0x88],context=p[0x89];mm_copy(p,a->staged,0x228);a->staged[0]=0;
    if(s==2){p[0x88]=callback;p[0x89]=context;pf_show();}else if(s<2)pa_show(s);else pa_custom_show(s);
    if(old)asset_release((void *)(unsigned long)old);
   }else pa_failed(s,a->sequence);
  }else if(a->state==3&&!closing)pa_failed(s,a->sequence);
  pa_drop();a->state=0;
 }
 if(closing||fs_phase()!=5)return;
 u32 mask=pa_mask();
 for(u32 s=0;s<7;s++){LOOP_PAD();if(saving)a->delay[s]=0;else if(a->delay[s])a->delay[s]--;}
 for(u32 i=0;i<7;i++){LOOP_PAD();u32 s=a->turn+i;if(s>=7)s-=7;u32 side=s<2?s:(s-3)&1;
  if((s!=2&&!(mask&(1u<<side)))||a->delay[s])continue;
  u32 id=pa_job_id(s);if(s==2&&a->frame_slot==16)continue;
  if(!id||pa_find(s,id)<16){if(s==2)a->applied[s]=pv->cs_draft.frame;else if(s<2)pa_show(s);else pa_custom_show(s);continue;}
  if(!pa_idle_native())return;
  if(pa_target(s)==16){pa_failed(s,a->serial[s]);continue;}
  pa_zero(a->staged,0x228/4);a->staged[0x84]=id;a->owner=s;a->cover=a->wanted[s];a->sequence=a->serial[s];a->turn=s==6?0:s+1;
  pa_barrier();a->state=1;
  PV_CALL(0x14d1c0,void (*)(u32,u32,u32,u32))(id,0,0x1359d0,(u32)(unsigned long)a->staged);return;
 }
}
