/* Main owns demand/texture publication. One native CD-worker request owns an
 * immutable bundle description and private SELECT heap buffer until dequeue.
 * No custom thread, IOP module, read from draw, or PLAY prefetch. */
#define SA_LIMIT (4u*(12u+SPR_W*48u))
#define SA_DEMAND SPR_SLOTS
#include "wheel_cache.h"
wheel_cache_config wheel_cache={WC_VERSION,0};
#ifndef SELECT_ART_TEST
#define sa_word(a) (*(volatile u32 *)(unsigned long)(a))
#define sa_enqueue(p,b) (((void (*)(u32,u32,u32,u32))0x14cf90)(0,(u32)(unsigned long)(b),(u32)(unsigned long)sa_worker,(u32)(unsigned long)(p)))
#define sa_yield() (((void (*)(void))0x13bed0)())
#endif
extern u32 vd_extent_size(u32);
static struct {
 u8 *data,*focus_data;u32 capacity,bank,bank_bytes,entered,live,turn,show;
 volatile u32 state;
 u32 job,off,bytes,wanted[SA_DEMAND],seen[SA_DEMAND],rank[SA_DEMAND],clock;
 u32 focus_key,focus_off,focus_bytes,drawing,published;
 u32 reads,ready,failed,discarded,peak,badkey;
 u32 kind,read_bank,need[SA_DEMAND],wheel_state,wheel_bank,wheel_bytes,page_first,uploads;
 u32 *index;u8 *pages;u32 page[WC_PAGES],page_age[WC_PAGES],page_bytes[WC_PAGES];
} sa;
static int sa_cached(u32,u32);
static u8 *spr_cont(spr_cache_t *);
static void sa_publish(void);
static void spr_ensure(s32,u8 *,s32);
static u32 sa_owned_queue(void);
static u32 sa_focus(void){
 s32 row=MS_STATE?song_catalog_row(I32(MS_STATE+0x24)):-1;
 return row>=FIRST?(u32)row-FIRST:NOSLOT;
}
static void sa_barrier(void){
#ifdef __mips__
 __asm__ volatile("sync":::"memory");
#else
 __asm__ volatile("":::"memory");
#endif
}
static int sa_wheel_index(void){
 u32 count=sdb.n_new,bytes=16+count*4,first=bytes;
 if(first>sa.capacity)first=16;
 if(vd_pread(sa.wheel_bank,0,first,sa.data)!=(s32)first)return 0;
 u32 *h=(u32*)sa.data;
 if(h[0]!=WC_MAGIC||h[1]!=WC_VERSION||h[2]!=count||h[3]!=WC_PAGE)return 0;
 if(first>16)memcpy_(sa.index,sa.data+16,count*4);
 else for(u32 at=0;at<count*4;){
  u32 n=count*4-at;if(n>sa.capacity)n=sa.capacity;
  if(vd_pread(sa.wheel_bank,16+at,n,sa.data)!=(s32)n)return 0;
  memcpy_((u8*)sa.index+at,sa.data,n);at+=n;LOOP_PAD();
 }
 for(u32 k=0;k<count;k++){
  u32 off=sa.index[k];
  if(off!=~0u&&(off<sa.page_first||off>sa.wheel_bytes-12||(off&(WC_PAGE-1))>WC_PAGE-12))return 0;
  LOOP_PAD();
 }
 return 1;
}
/* Only main mutates the page cache, after the native request was dequeued.
 * The worker's staging is separate from every published/retained page. */
static void sa_wheel_publish(void){
 if(sa.published||sa.state!=2||!sa.kind||sa_word(0x78af08)||sa_word(0x78b70c)||ui_active)return;
 if(sa.kind==1)sa.wheel_state=1;
 else {
  u32 oldest=0;
  for(u32 i=0;i<WC_PAGES;i++){
   if(sa.page[i]==~0u){oldest=i;break;}
   if(sa.page_age[i]<sa.page_age[oldest])oldest=i;LOOP_PAD();
  }
  memcpy_(sa.pages+oldest*WC_PAGE,sa.data,sa.bytes);
  sa.page[oldest]=sa.off;sa.page_bytes[oldest]=sa.bytes;sa.page_age[oldest]=sa.clock;
 }
 sa.published=1;sa.ready++;
}
static const u8 *sa_wheel_source(u32 k,u32 touch){
 if(sa.wheel_state!=1||k>=sdb.n_new||ui_active||sa_word(0x78b70c)||
    (sa_word(0x78af08)&&!sa_owned_queue()))return 0;
 u32 off=sa.index[k];if(off==~0u)return 0;
 for(u32 i=0;i<WC_PAGES;i++){if(off>=sa.page[i]&&off-sa.page[i]<sa.page_bytes[i]){
  u32 pos=off-sa.page[i];if(sa.page_bytes[i]-pos<12)continue;
  const u8 *p=sa.pages+i*WC_PAGE+pos;
  u32 w=spr_u16(p+4),h=spr_u16(p+6);
  if(spr_u32(p)!=0x31525053u||!w||!h||w>SPR_W||h>48||12+w*h>sa.page_bytes[i]-pos){sa.wheel_state=2;return 0;}
  if(touch)sa.page_age[i]=sa.clock;return p;
 }LOOP_PAD();}
 return 0;
}
static int sa_range(u32 k,u32 *off,u32 *bytes){
 if(k>=sdb.n_new)return 0;u32 lo=~0u,hi=0,end=sa.bank_bytes;
 for(u32 c=0;c<NSPR;c++){u32 v=sdb.spr_off[k*NSPR+c];if(v!=~0u){if(v<lo)lo=v;if(v>hi)hi=v;}LOOP_PAD();}
 if(lo==~0u)return 0;
 for(u32 j=k+1;j<sdb.n_new;j++){
  u32 next=~0u;for(u32 c=0;c<NSPR;c++){u32 v=sdb.spr_off[j*NSPR+c];if(v<next)next=v;LOOP_PAD();}
  if(next!=~0u){end=next;break;}LOOP_PAD();
 }
 if(end>sa.bank_bytes||lo>=end||hi>=end||end-lo>SA_LIMIT)return 0;
 *off=lo;*bytes=end-lo;return 1;
}
/* Kept resident even when a menu overlay occupies spr_buf. */
static __attribute__((noinline)) int sa_worker(u32 *q){
 if(q[0]!=(u32)(unsigned long)sa_worker || q[1]!=(u32)(unsigned long)&sa || q[2]!=(u32)(unsigned long)sa.data)return 0;
 if(sa.state!=1)return 1;
 int ok;
 if(sa.kind==1)ok=sa_wheel_index();
 else ok=sa.bank==sdb.spr_lba&&vd_pread(sa.read_bank,sa.off,sa.bytes,sa.data)==(s32)sa.bytes;
 sa.reads++;
 for(u32 c=0;!sa.kind&&c<NSPR&&ok;c++){
  u32 off=sdb.spr_off[sa.job*NSPR+c];if(off==~0u)continue;
  if(off<sa.off||off-sa.off>sa.bytes||sa.bytes-(off-sa.off)<12){ok=0;break;}
  u8 *p=sa.data+off-sa.off;u32 w=spr_u16(p+4),h=spr_u16(p+6);
  if(spr_u32(p)!=0x31525053u||!w||!h||w>SPR_W||h>48||12+w*h>sa.bytes-(off-sa.off))ok=0;
  LOOP_PAD();
 }
 sa_barrier();sa.state=ok?2:3;return 1;
}
static u32 sa_owned_queue(void){
 u32 n=sa_word(0x78af08),head=sa_word(0x78af00);
 if(!sa.data||!sa.state||n!=1||head>=72)return 0;
 u32 q=0x78af0c+head*28;
 return sa_word(q)==(u32)(unsigned long)sa_worker&&sa_word(q+4)==(u32)(unsigned long)&sa&&sa_word(q+8)==(u32)(unsigned long)sa.data;
}
static void sa_clear_demand(void){for(u32 i=0;i<SA_DEMAND;i++){sa.wanted[i]=NOSLOT;sa.seen[i]=sa.need[i]=0;LOOP_PAD();}}
static void sa_release(void){
 if(sa.data)free_(sa.data);sa.data=sa.focus_data=0;sa.focus_key=NOSLOT;
 sa.state=sa.live=sa.entered=sa.show=sa.published=sa.drawing=0;sa_clear_demand();
 sa.index=0;sa.pages=0;sa.wheel_bank=sa.wheel_state=sa.kind=0;
}
static void sa_retire(void){
 sa.live=0;sa_clear_demand();
 /* Only a scene/modal boundary may wait; draw never calls this. Ready is not
  * dequeue. All loader requests ahead of our one job must finish before free. */
 while(sa.data&&sa.state&&(sa.state==1||sa_word(0x78af08)))sa_yield();
 sa_release();
}
static void sa_enter(void){
 if(sa.entered)return;sa.entered=1;sa.bank=sdb.spr_lba;sa.bank_bytes=vd_extent_size(sa.bank);
 u32 cap=0,off,n;sa_clear_demand();
 for(u32 k=0;k<sdb.n_new;k++){if(sa_range(k,&off,&n)&&n>cap)cap=n;LOOP_PAD();}
 if(!cap)return;sa.badkey=sa.focus_key=NOSLOT;sa.capacity=(cap+127)&~127u;
 /* One worker half plus one main-owned current-song half. The latter survives
  * later wheel reads, so native delayed title/genre/artist callbacks stay hits.
  * Both halves are SELECT-only and retire before preview/chart workspace. */
 u32 basic=sa.capacity,extra=0;
 sa.wheel_bank=wheel_cache.version==WC_VERSION?wheel_cache.bank:0;
 if(sa.wheel_bank&&sdb.n_new<=(0x7fffffffu-16-WC_PAGE)/4){
  sa.wheel_bytes=vd_extent_size(sa.wheel_bank);sa.page_first=(16+sdb.n_new*4+WC_PAGE-1)&~(WC_PAGE-1);
  if(sa.wheel_bytes>=sa.page_first+WC_PAGE&&!(sa.wheel_bytes&(WC_PAGE-1))){
   if(sa.capacity<WC_PAGE)sa.capacity=WC_PAGE;
   extra=((sdb.n_new*4+127)&~127u)+WC_PAGES*WC_PAGE;
  }else sa.wheel_bank=0;
 }else sa.wheel_bank=0;
 sa.data=malloc_(2*sa.capacity+extra);
 if(!sa.data&&extra){sa.capacity=basic;sa.wheel_bank=0;extra=0;sa.data=malloc_(2*sa.capacity);}
 if(sa.data&&extra){
  sa.index=(u32*)(sa.data+2*sa.capacity);sa.pages=(u8*)sa.index+((sdb.n_new*4+127)&~127u);
  for(u32 i=0;i<WC_PAGES;i++){sa.page[i]=~0u;sa.page_age[i]=sa.page_bytes[i]=0;LOOP_PAD();}
 }
 sa.wheel_state=0;
 if(sa.data){sa.focus_data=sa.data+sa.capacity;sa.live=1;if(2*sa.capacity+extra>sa.peak)sa.peak=2*sa.capacity+extra;}
}
static void sa_need(u32 k,u32 rank,u32 mask){
 if(!sa.live||k>=sdb.n_new||sa.badkey==k||(sa.state&&!sa.kind&&sa.job==k))return;
 u32 oldest=0;
 for(u32 i=0;i<SA_DEMAND;i++){
  if(sa.wanted[i]==k){
   sa.seen[i]=sa.clock;
   if(sa.need[i]==1&&mask==1)sa.rank[i]=rank;
   else if(rank>sa.rank[i])sa.rank[i]=rank;
   sa.need[i]|=mask;return;
  }
  if(sa.wanted[i]==NOSLOT){oldest=i;break;}
  if(sa.rank[i]<sa.rank[oldest]||(sa.rank[i]==sa.rank[oldest]&&sa.seen[i]<sa.seen[oldest]))oldest=i;LOOP_PAD();
 }
 if(sa.wanted[oldest]!=NOSLOT&&sa.rank[oldest]>rank)return;
 sa.wanted[oldest]=k;sa.seen[oldest]=sa.clock;sa.rank[oldest]=rank;sa.need[oldest]=mask;
}
static void sa_want(u32 k,u32 rank){sa_need(k,rank,15);}
static void sa_hold_focus(void){
 if(sa.kind)return;
 LOOP_PAD();
 if(sa.state!=2||sa_word(0x78af08)||sa.bank!=sdb.spr_lba||sa.job!=sa_focus()||sa.focus_key==sa.job)return;
 memcpy_(sa.focus_data,sa.data,sa.bytes);sa.focus_off=sa.off;sa.focus_bytes=sa.bytes;sa.focus_key=sa.job;
}
static const u8 *sa_source(u32 c,u32 k){
 if(sa.focus_data&&sa.focus_key==k&&sa.bank==sdb.spr_lba){
  u32 off=sdb.spr_off[k*NSPR+c];
  if(!sa_word(0x78b70c)&&(!sa_word(0x78af08)||sa_owned_queue())&&off>=sa.focus_off&&off-sa.focus_off<sa.focus_bytes)return sa.focus_data+off-sa.focus_off;
  return 0; /* Retained bytes need no duplicate read while a foreign owner waits. */
 }
 if(!c){const u8 *p=sa_wheel_source(k,sa.drawing);if(p)return p;}
 if(!sa.kind&&sa.state==2&&!sa_word(0x78af08)&&!sa_word(0x78b70c)&&sa.job==k&&sa.bank==sdb.spr_lba){
  u32 off=sdb.spr_off[k*NSPR+c];
  if(off>=sa.off&&off-sa.off<sa.bytes)return sa.data+off-sa.off;
 }
 if(sa.badkey==k||(sa.state&&!sa.kind&&sa.job==k))return 0; /* Invalid input: retry only after scene re-entry. */
 /* Native handle queries may enumerate the whole97-row alias window. They
  * are not evidence that those labels are visible. Nearby priming and real
  * draws own wheel demand; panel getters retain their existing behaviour. */
 if(c||sa.drawing||k==sa_focus()||sa.wheel_state!=1)sa_need(k,sa.drawing?2:1,1u<<c);
 return 0;
}
static s32 sa_near_focus(u8 *scene){
 u8 *wheel=MS_WHEEL;
 if(!window.active||!window.rows||!wheel||window.ctx!=(u8*)(unsigned long)I32(scene+0xd4)||window.n<=0)return -1;
 s32 physical=I32(wheel+0x5f0);if((u32)physical>=(u32)window.view)return -1;
 return view_global(physical);
}
static u32 sa_near_key(s32 focus,s32 step){
 s32 at=wrap_row(focus+step,window.n+window.header);if(at<window.header)return NOSLOT;
 u32 key=window.rows[at-window.header].key;
 return key>=SONG_KEY&&key-SONG_KEY<sdb.n_new?key-SONG_KEY:NOSLOT;
}
/* Prepare only nearby rows, nearest first, using already-published bytes on
 * main. Never wait/read here or upload an entire page at once. A four-strip
 * update budget is shared with getters/draw, including the selected label. */
static void sa_prime(u8 *scene){
 if(sa.wheel_state!=1||sa_word(0x78af08)||sa_word(0x78b70c))return;
 s32 focus=sa_near_focus(scene);if(focus<0)return;
 u8 *e=spr_cont(&sdb.spr[0]);if(!e||!sdb.spr[0].n)return;
 for(s32 hop=0;hop<=10&&sa.uploads<4;hop++)for(s32 dir=0;dir<(hop?2:1)&&sa.uploads<4;dir++){
  u32 k=sa_near_key(focus,dir?-hop:hop);
  if(k<sdb.n_new&&sdb.spr_off[k*NSPR]!=~0u&&!sa_cached(0,k)){
   sa.drawing=1;spr_ensure(0,e,k);sa.drawing=0;
  }
  LOOP_PAD();
 }
}
static void sa_neighbors(u8 *scene){
 if(sa.wheel_state!=1)return;
 s32 focus=sa_near_focus(scene);if(focus<0)return;
 for(s32 hop=0;hop<=10;hop++)for(s32 direction=0;direction<(hop?2:1);direction++){
  u32 k=sa_near_key(focus,direction?-hop:hop);if(k>=sdb.n_new)continue;
  if(sdb.spr_off[k*NSPR]!=~0u&&!sa_cached(0,k)&&!sa_wheel_source(k,0))sa_need(k,hop<=7?32-hop:1,1);
  LOOP_PAD();
 }
}
/* A shuffled/level-sorted wheel can place every nearby title in a different
 * source page. Do not pay for32KiB when only one small SPR1 is useful there.
 * The mixed-bank offsets give the exact record length without a header read. */
static u32 sa_wheel_record_bytes(u32 k){
 u32 off=sdb.spr_off[k*NSPR],end=sa.bank_bytes;
 for(u32 next=k;next<sdb.n_new;next++){
  for(u32 c=next==k?1:0;c<NSPR;c++){
   u32 at=sdb.spr_off[next*NSPR+c];if(at!=~0u){end=at;goto found;}
   LOOP_PAD();
  }
  LOOP_PAD();
 }
found:
 return off!=~0u&&end>off&&end-off<=WC_PAGE-(sa.index[k]&(WC_PAGE-1))?end-off:0;
}
static u32 sa_page_useful(u8 *scene,u32 k){
 s32 focus=sa_near_focus(scene);if(focus<0)return 2; /* no canonical view: existing page fallback */
 u32 page=sa.index[k]&~(WC_PAGE-1);
 for(s32 hop=0;hop<=10;hop++)for(s32 dir=0;dir<(hop?2:1);dir++){
  u32 other=sa_near_key(focus,dir?-hop:hop);
  if(other<sdb.n_new&&other!=k&&sa.index[other]!=~0u&&
     (sa.index[other]&~(WC_PAGE-1))==page&&!sa_cached(0,other)&&!sa_wheel_source(other,0))return 2;
  LOOP_PAD();
 }
 return 1;
}
static void sa_select(u8 *scene,u32 modal){
 /* Native SELECT opens folders through6/7 and returns through8. Retiring
  * staging there makes spr_ensure fall back to blocking FILEIO during the
  * same animation's draws. Keep the existing worker owner across the whole
  * folder transition; actual scene/modal/PLAY boundaries still retire it. */
 u32 state=MS_STATE?(u32)I32(MS_STATE+0x3c):~0u;
 u32 active=scene&&MS_STATE&&I32(scene+0x2c)==3&&!sa_word(0x2ad5e8)&&!ui_active&&(state==1||state==3||state==6||state==7||state==8);
 if(!active){
  sa.live=0;sa_clear_demand();
  if(!sa.state||(!sa_word(0x78af08)&&sa.state!=1))sa_release();return;
 }
 sa_enter();if(!sa.data)return;sa.live=1;sa.clock++;sa.uploads=0;
 if(sa.state==1||sa_word(0x78af08)||sa_word(0x78b70c))return;
 sa_barrier();
 if(sa.state){
  if(sa.state==2){if(sa.kind)sa_wheel_publish();else sa_hold_focus();if(!sa.published)return;}
  else {sa.failed++;if(sa.kind)sa.wheel_state=2;else sa.badkey=sa.job;}
  sa.state=sa.show=sa.published=0;
 }
 if(modal)return;
 if(sa.wheel_bank&&!sa.wheel_state){
  sa.kind=1;sa.job=NOSLOT;sa.off=0;sa.bytes=0;sa.read_bank=sa.wheel_bank;
  sa_barrier();sa.state=1;sa_enqueue(&sa,sa.data);return;
 }
 sa_prime(scene);sa_neighbors(scene);
 u32 focus=sa_focus(),best=SA_DEMAND;
 if(focus<sdb.n_new&&sa.focus_key!=focus){
  for(u32 c=0;c<NSPR;c++)if(sdb.spr_off[focus*NSPR+c]!=~0u&&!sa_cached(c,focus)){sa_want(focus,3);break;}
 }
 for(u32 i=0;i<SA_DEMAND;i++){
  if(sa.wanted[i]==NOSLOT)continue;
  if(sa.need[i]==1&&(sa_cached(0,sa.wanted[i])||sa_wheel_source(sa.wanted[i],0))){sa.wanted[i]=NOSLOT;continue;}
  if(sa.clock-sa.seen[i]>3){sa.wanted[i]=NOSLOT;sa.discarded++;continue;}
  if(best==SA_DEMAND||sa.wanted[i]==focus||sa.rank[i]>sa.rank[best]||(sa.rank[i]==sa.rank[best]&&sa.seen[i]<sa.seen[best]))best=i;
  if(sa.wanted[i]==focus)break;LOOP_PAD();
 }
 if(best==SA_DEMAND)return;
 u32 k=sa.wanted[best];sa.wanted[best]=NOSLOT;
 sa.kind=0;sa.read_bank=sa.bank;
 if(sa.wheel_state==1&&sa.need[best]==1&&sa.index[k]!=~0u){
  sa.kind=2;sa.off=sa.index[k]&~(WC_PAGE-1);sa.bytes=WC_PAGE;sa.read_bank=sa.wheel_bank;
  if(sa_page_useful(scene,k)==1){u32 n=sa_wheel_record_bytes(k);if(n){sa.off=sa.index[k];sa.bytes=n;}}
 }else if(!sa_range(k,&sa.off,&sa.bytes)||sa.bytes>sa.capacity)return;
 sa.job=k;sa_barrier();sa.state=1;sa_enqueue(&sa,sa.data);
}
