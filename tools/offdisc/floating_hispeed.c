/* Fractional note-scroll/cover preferences. Never changes chart/audio time,
 * native input sampling/history, judgement windows or the held-exit path. */
#include "menu_settings.h"
extern struct mm_control menu_control;
#ifndef FHS_TEST
#define fh_byte(a) (*(volatile u8 *)(unsigned long)(a))
#define fh_short(a) (*(volatile short *)(unsigned long)(a))
#define fh_start(s) (((u32 (*)(u32))0x13ad80)(s))
#define fh_select(s) (((u32 (*)(u32))0x13ad30)(s))
#define fh_chart_events() ((const u32 *)(unsigned long)fs_word(0x2ad0c4))
#define fh_native_rect ((void (*)(const short *,unsigned long long,u32,s32))0x137ec0)
#define fh_native_text fs_native_text
#define fh_bpm() (((s32 (*)(void))0x1ea850)())
#define fh_regul(s) (((u32 (*)(u32))0x205b60)(s))
#define fh_sudden(s) (((u32 (*)(u32))0x205910)(s)||fh_byte(0x2ad568+(s)))
#define fh_hidden(s) (((u32 (*)(u32))0x2059d0)(s))
#define fh_native_visible(s) (((u32 (*)(u32))0x205910)(s)||((u32 (*)(u32))0x2059d0)(s))
#define fh_kind() (((u32 (*)(void))0x1f5830)())
#define fh_links(bit) (((u32 (*)(void *,u32))0x16a280)(((void *(*)(void))0x1686e0)(),bit))
#define fh_active_mask() (fs_dp()?3u:gsm_mask())
#define fh_side_visible(s) (fs_dp() || (((u32 (*)(void))0x202eb0)()&(1u<<(s))))
extern unsigned long long fhs_restore_body(FS_ARGS),fhs_commit_body(FS_ARGS),fhs_speed_body(FS_ARGS),fhs_cover_body(FS_ARGS),fhs_index_body(FS_ARGS),fhs_index_set_body(FS_ARGS),fhs_ab_body(FS_ARGS),fhs_ab_set_body(FS_ARGS),fhs_velocity_body(FS_ARGS),fhs_ring_body(FS_ARGS);
extern unsigned long long fhs_hud0_body(FS_ARGS),fhs_hud1_body(FS_ARGS),fhs_hud2_body(FS_ARGS),fhs_hud3_body(FS_ARGS),fhs_hud4_body(FS_ARGS),fhs_hud5_body(FS_ARGS);
extern unsigned long long fhs_chart_body(FS_ARGS),fhs_prime_body(FS_ARGS);
#endif
#define FH_COVER(s) fh_short(0x2ad560+(s)*2)
#define FH_VISIBLE(s) fh_byte(0x2ad564+(s))
#define FH_HOLD(s) fh_byte(0x2ad548+(s))
#define FH_PULSE(s) fh_byte(0x2ad554+(s))
#define FH_TAP(s) fh_short(0x2ad54c+(s)*2)
#define FH_MASTER 1u
#define FH_FLOAT 2u
#define FH_LOCK 4u
#ifdef PLAY_LIFT
#define fh_lift(s) (menu_control.options.lift[fs_dp()?0:(s)]!=0)
#else
#define fh_lift(s) 0u
#endif
static struct {u32 live,mask,dp,scope,dirty,last_side,paired,paired_value,phase,start_bpm,need_start,loading_wait;u32 units[2],previous[2],pending[2],age[2],ready[2],initvis[2],adjusted[2];} fhs;
/* R5900 has 32-bit MULTU and 64-bit shifts, but no DMULT/DDIV. */
static unsigned long long fh_mul(u32 a,u32 b)
{
#ifdef __mips__
 u32 lo,hi;__asm__ volatile("multu %2,%3\n\tmflo %0\n\tmfhi %1":"=r"(lo),"=r"(hi):"r"(a),"r"(b):"hi","lo");return ((unsigned long long)hi<<32)|lo;
#else
 return (unsigned long long)a*b;
#endif
}
static u32 fh_div(unsigned long long n,unsigned long long d)
{
 if(!d)return ~0u;unsigned long long r=0;u32 q=0;
 for(s32 i=63;i>=0;i--){LOOP_PAD();r=(r<<1)|((n>>i)&1);if(r>=d){r-=d;if(i>=32)return ~0u;q|=1u<<i;}}return q;
}
static u32 fh_owner(u32 side){return fhs.dp?0:side;}
static u32 fh_profile(u32 side){return fhs.dp?2+side:side;}
static u32 fh_enabled(u32 side){return side<2 && fhs.live && (fhs.mask&(1u<<side)) && (menu_control.options.fhs[fh_owner(side)]&FH_MASTER);}
static u32 fh_floating(u32 side){return fh_enabled(side) && (menu_control.options.fhs[fh_owner(side)]&FH_FLOAT);}
static void fh_store(u32 *p,u32 v){if(*p!=v){*p=v;fhs.dirty=1;}}
static u32 fh_step(u32 side){u32 n=fhs_index_body(side,0,0,0,0,0,0,0);static const u32 values[11]={400000,600000,800000,900000,1000000,1100000,1200000,1300000,1400000,1500000,1600000};return values[n<=10?n:0];}
/* Called at loaded1e65d0, NOT pre-load1e5e80. Catalog1e3460 is the default/display BPM (OOSANBASHI168), not necessarily
 * the opening chart event (84). Constructor runs only after chart read/unpack.
 * Read the selected native event stream, stop at the first positive tick, and
 * take the last valid time-zero tempo exactly as the native dispatch would. */
static u32 fh_opening_bpm(void){
 const u32 *p=fh_chart_events();u32 address=(u32)(unsigned long)p,bpm=0;
 if((address&3)||address<0x100000||address>0x2000000-4096)return 0;
 for(u32 i=0;i<512;i++){LOOP_PAD();if(p[i*2])break;u32 event=p[i*2+1];if((event&15)==4){u32 value=(event>>8&255)+(event&240)*16;if(value)bpm=value;}}
 return bpm;
}
static u32 fh_bpm_for(u32 side){return fh_regul(side)?150:fhs.start_bpm&&fs_phase()<5?fhs.start_bpm:fh_bpm();}
static u32 fh_distance(u32 side){u32 cover=FH_VISIBLE(side)?(u32)FH_COVER(side):0;if(cover>320)cover=320;if(FH_VISIBLE(side)&&fh_hidden(side))cover=fh_sudden(side)?cover*2:320-cover;if(cover>=320)return 0;u32 wn=fh_div(fh_mul(cover,1000),320);
#ifdef PLAY_LIFT
 wn+=fh_div(fh_mul(lift_pixels(side),1000),320);
#endif
 if(wn>999)wn=999;return 320000-wn*320;}
static u32 fh_green(u32 side,u32 bpm,u32 units){if(!bpm||bpm>100000||!units)return 0;return fh_div(fh_mul(fh_distance(side),65536000),fh_mul(bpm*301,units));}
/* Native1e6bb0 divides the smoothed ring value by100 before spawning.
 * Quantize targets to that real 1/4000-HS resolution, so the GN display
 * never promises precision that the note path discards. */
static u32 fh_speed_clamp(u32 v){v=v<200000?200000:v>4000000?4000000:v;return v-v%100;}
static void fh_speed(u32 side,u32 units){fhs.units[side]=fh_speed_clamp(units);if(fhs.dp && fh_links(0))fhs.units[1-side]=fhs.units[side];}
static void fh_solve_bpm(u32 side,u32 bpm)
{
 u32 gn=menu_control.options.gn[fh_profile(side)];
 if(!fh_floating(side)||!gn||!bpm||bpm>100000||(!FH_VISIBLE(side)&&!fh_lift(side)))return;
 fh_speed(side,fh_div(fh_mul(fh_distance(side),65536000),fh_mul(bpm*301,gn)));
}
static void fh_solve(u32 side){fh_solve_bpm(side,fh_bpm_for(side));}
static void fh_capture(u32 side)
{
 u32 p=fh_profile(side);s32 h=FH_COVER(side);u32 height=h<0?0:h>320?320:(u32)h;
 fh_store(&menu_control.options.cover[p],height);fh_store(&menu_control.options.visible[p],FH_VISIBLE(side)!=0);
}
static void fh_toggle(u32 side)
{
 if(fhs.need_start)return;u32 owner=fh_owner(side),flags=menu_control.options.fhs[owner];
 if(flags&FH_FLOAT){
  for(u32 s=0;s<2;s++){LOOP_PAD();if(!fh_enabled(s)||fh_owner(s)!=owner)continue;
   u32 best=0,diff=~0u;for(u32 i=0;i<11;i++){LOOP_PAD();static const u32 steps[11]={400000,600000,800000,900000,1000000,1100000,1200000,1300000,1400000,1500000,1600000};u32 x=steps[i],v=x>fhs.units[s]?x-fhs.units[s]:fhs.units[s]-x;if(v<diff){diff=v;best=i;}}
   fhs_index_set_body(s,best,0,0,0,0,0,0);
  }
 }else{
  for(u32 s=0;s<2;s++){LOOP_PAD();if(!fh_enabled(s)||fh_owner(s)!=owner)continue;
   fhs.units[s]=fh_step(s);u32 gn=fh_green(s,fh_bpm_for(s),fhs.units[s]);if(gn>9999)gn=9999;if(!gn)gn=1;fh_store(&menu_control.options.gn[fh_profile(s)],gn);fh_capture(s);
  }
 }
 fh_store(&menu_control.options.fhs[owner],flags^FH_FLOAT);
}
/* Native cached held queries only. A long START+SELECT exit cannot also toggle.
 * SELECT invalidates only our/native cover-UI gesture state, never pad history. */
static void fh_gestures(void)
{
 u32 phase=fs_phase();for(u32 s=0;s<2;s++){LOOP_PAD();if(!fh_enabled(s))continue;
  u32 held=(fh_start(s)?1u:0u)|(fh_select(s)?2u:0u),prev=fhs.previous[s];
  if(phase>=4 && !fhs.ready[s]){
   /* Restore's caller may still initialize cover visibility after our early
    * solve. Finish once when startup geometry has settled, BEFORE READY's
    * native speed-ring prime. No overlay/input is required. Preserve explicit
    * pre-READY gear shifts; this is not continuous BPM following. */
   if(phase==4&&!fhs.need_start&&!fhs.adjusted[s]&&!(fhs.dp&&s&&fh_links(0)))fh_solve(s);
   /* Keep the original double-tap detector's counter/pulse intact.
    * Empirical LDJ comparison: check the READY lock at the SECOND tap.
    * READY creation does not
    * erase its countdown. A gesture crossing READY becomes reset if locked,
    * or an ordinary cover toggle otherwise. No shorter native tap window. */
   fhs.ready[s]=1;fhs.pending[s]=0;}
  if(phase>5 || phase<fhs.phase){fhs.pending[s]=0;FH_TAP(s)=0;FH_PULSE(s)=0;}
  if(held&2){FH_TAP(s)=0;FH_PULSE(s)=0;}
  if(phase<=5 && (held&2) && !(prev&2) && (held&1) && (prev&1)) {fhs.pending[s]=1;fhs.age[s]=0;}
  if(fhs.pending[s]){
   if(!(held&1)||phase>5||fh_byte(0x2ad544)||fs_word(0x2ad5b0)>=49)fhs.pending[s]=0;
   else if(!(held&2)) {if(fhs.age[s]<49)fh_toggle(s);fhs.pending[s]=0;}
   else if(fhs.age[s]<50)fhs.age[s]++;
  }
  fhs.previous[s]=held;
 }fhs.phase=phase;
}
unsigned long long fhs_restore_call(FS_ARGS)
{
#ifdef PLAY_LIFT
 lift_prepare();
#endif
#ifdef PLAY_PREVIEW_ACTIVE
 if(PLAY_PREVIEW_ACTIVE()) {fhs.live=0;return 0;}
#endif
 fhs.live=1;fhs.dp=fs_dp()!=0;fhs.mask=fh_active_mask()&3;u32 kind=fh_kind();if(kind==5||kind==8)fhs.mask=0;
 fhs.scope=fhs.paired=0;fhs.phase=0;fhs.start_bpm=0;fhs.need_start=1;fhs.loading_wait=0;
 for(u32 s=0;s<2;s++){LOOP_PAD();fhs.previous[s]=fhs.pending[s]=fhs.age[s]=fhs.ready[s]=fhs.adjusted[s]=0;fhs.initvis[s]=1;fhs.units[s]=fh_step(s);}
 unsigned long long r=fhs_restore_body(FS_PASS);
 for(u32 s=0;s<2;s++){LOOP_PAD();if(!fh_enabled(s))continue;u32 p=fh_profile(s);if(menu_control.options.cover[p]<=320)FH_COVER(s)=menu_control.options.cover[p];else fh_store(&menu_control.options.cover[p],FH_COVER(s));
  /* Visibility is initialized by the native caller AFTER restore. The cover
   * wrapper applies the separate preference on its first actual UI update. */
  if(menu_control.options.visible[p]<=1)FH_VISIBLE(s)=menu_control.options.visible[p];
  else FH_VISIBLE(s)=fh_native_visible(s)!=0;
 }
 if(fhs.dp && fh_links(1) && fh_enabled(0)){FH_COVER(1)=FH_COVER(0);FH_VISIBLE(1)=FH_VISIBLE(0);}
 /* Chart data is not ready here. Do not seed/solve using catalog metadata.
  * Both the constructor and retry prime have a verified loaded event stream. */
 return r;
}
static void fh_start_chart(void)
{
 if(!fhs.need_start || (!fh_enabled(0)&&!fh_enabled(1)))return;
 fhs.start_bpm=fh_opening_bpm();fhs.need_start=0;
 for(u32 s=0;s<2;s++){LOOP_PAD();if(!fh_enabled(s))continue;u32 p=fh_profile(s);
  u32 bpm=fh_regul(s)?150:fhs.start_bpm?fhs.start_bpm:fh_bpm();
  if(!menu_control.options.gn[p]){u32 gn=fh_green(s,bpm,fhs.units[s]);if(!gn)gn=1;if(gn>9999)gn=9999;fh_store(&menu_control.options.gn[p],gn);}
  if(!fhs.adjusted[s])fh_solve_bpm(s,bpm);
 }
 if(fhs.dp&&fh_links(0)&&fh_floating(0))fhs.units[1]=fhs.units[0];
}
static void fh_start_cache(void){for(u32 s=0;s<2;s++){LOOP_PAD();if(fh_floating(s))fs_word(0x2ad4a0+s*4)=fh_div(fhs.units[s],100);}}
unsigned long long fhs_chart_call(FS_ARGS){fh_start_chart();unsigned long long r=fhs_chart_body(FS_PASS);fh_start_cache();
#ifndef FHS_TEST
 bn_start();
#endif
 return r;}
unsigned long long fhs_prime_call(FS_ARGS){fh_start_chart();unsigned long long r=fhs_prime_body(FS_PASS);fh_start_cache();
#ifndef FHS_TEST
 bn_start();
#endif
 return r;}
unsigned long long fhs_commit_call(FS_ARGS){
#ifdef PLAY_PREVIEW_ACTIVE
 if(PLAY_PREVIEW_ACTIVE())return 0;
#endif
#ifdef PLAY_LIFT
if(a<2&&(lift_route_mask&(1u<<(u32)a)))return 0;
#endif
if(!fh_enabled((u32)a))return fhs_commit_body(FS_PASS);fh_capture((u32)a);return 0;}
unsigned long long fhs_ab_call(FS_ARGS){return fh_enabled((u32)c)||fh_lift((u32)c)?0:fhs_ab_body(FS_PASS);}
unsigned long long fhs_ab_set_call(FS_ARGS){return fh_enabled((u32)c)||fh_lift((u32)c)?0:fhs_ab_set_body(FS_PASS);}
unsigned long long fhs_index_call(FS_ARGS){return fhs.scope && fh_floating((u32)a)?5:fhs_index_body(FS_PASS);}
unsigned long long fhs_index_set_call(FS_ARGS)
{
 u32 s=(u32)a;if(!fhs.scope||!fh_floating(s))return fhs_index_set_body(FS_PASS);
 if(fhs.paired==s+1 && fhs.paired_value==(u32)b){fhs.paired=0;return 0;}
 s32 delta=(s32)b-5;if(delta){fhs.adjusted[s]=1;if(fhs.dp&&fh_links(0))fhs.adjusted[1-s]=1;}s32 units=(s32)fhs.units[s]+delta*200000;fh_speed(s,units<200000?200000:(u32)units);
 if(fhs.dp&&fh_links(0)){fhs.paired=2-s;fhs.paired_value=(u32)b;}return 0;
}
unsigned long long fhs_speed_call(FS_ARGS)
{
 for(u32 s=0;s<2;s++){LOOP_PAD();if(fh_enabled(s)&&fhs.initvis[s]){u32 p=fh_profile(s);if(menu_control.options.visible[p]<=1)FH_VISIBLE(s)=menu_control.options.visible[p];fhs.initvis[s]=0;fh_capture(s);}}
 fh_gestures();short before[2]={FH_COVER(0),FH_COVER(1)};fhs.scope=1;fhs.paired=0;
 unsigned long long r=fhs_speed_body(FS_PASS);fhs.scope=fhs.paired=0;
 for(u32 s=0;s<2;s++){LOOP_PAD();if(fh_enabled(s))FH_COVER(s)=before[s];}return r;
}
unsigned long long fhs_cover_call(FS_ARGS)
{
 u8 hold[2]={FH_HOLD(0),FH_HOLD(1)};u32 lock[2]={0,0},changed[2]={0,0};short before[2];u8 visible[2];
#ifdef PLAY_LIFT
 u32 route[2]={0,0},old_lift[2]={lift_pixels(0),lift_pixels(1)};
 u8 automatic[2]={fh_byte(0x2ad568),fh_byte(0x2ad569)};lift_route_mask=0;
#endif
 for(u32 s=0;s<2;s++){LOOP_PAD();if(fh_enabled(s)){
   lock[s]=fh_floating(s) && (menu_control.options.fhs[fh_owner(s)]&FH_LOCK) && fhs.ready[s];
  }before[s]=FH_COVER(s);visible[s]=FH_VISIBLE(s);
 }
 if(fhs.dp && fh_links(1) && (lock[0]||lock[1]))lock[0]=lock[1]=1;
 for(u32 s=0;s<2;s++){LOOP_PAD();if(lock[s]){if(FH_PULSE(s))fh_solve(s);FH_PULSE(s)=0;FH_HOLD(s)=0;}}
#ifdef PLAY_LIFT
 for(u32 s=0;s<2;s++){LOOP_PAD();if(!fh_lift(s)||!(fh_active_mask()&(1u<<s)))continue;
  /* Native cover input applies a16px minimum; offset it to admit LIFT0.
   * Do not route a visibility edge: that must reach the original toggle. */
  if(!lock[s]&&FH_HOLD(s)&&!FH_PULSE(s)&&(!FH_VISIBLE(s)||!fh_sudden(s))){
   route[s]=1;lift_route_mask|=1u<<s;FH_COVER(s)=old_lift[s]+16;FH_VISIBLE(s)=1;fh_byte(0x2ad568+s)=1;
  }
 }
 if(fs_dp()&&lift_route_mask&&fh_links(1))lift_route_mask=3;
#endif
 unsigned long long r=fhs_cover_body(FS_PASS);
#ifdef PLAY_LIFT
 s32 delta[2]={0,0};
 for(u32 s=0;s<2;s++){LOOP_PAD();if(route[s]){
  s32 value=FH_COVER(s)-16;if(value<0)value=0;if(value>265)value=265;
  delta[s]=value-(s32)old_lift[s];FH_COVER(s)=before[s];FH_VISIBLE(s)=visible[s];
 }if(fs_dp()&&lift_route_mask&&fh_links(1)){FH_COVER(s)=before[s];FH_VISIBLE(s)=visible[s];}fh_byte(0x2ad568+s)=automatic[s];}
 lift_route_mask=0;
 if(fs_dp()){
  /* One DP player owns both fields. The inactive/unchanged second field
   * must never overwrite a scratch movement from the first (or vice versa).
   * Linked native covers already duplicate the same delta: apply it once. */
  s32 d=delta[0],other=delta[1];if((other<0?-other:other)>(d<0?-d:d))d=other;
  if(d)fh_store(&menu_control.options.lift_height[0],(u32)((s32)old_lift[0]+d));
 }else for(u32 s=0;s<2;s++){LOOP_PAD();if(delta[s])fh_store(&menu_control.options.lift_height[s],(u32)((s32)old_lift[s]+delta[s]));}
 lift_sync();
 /* Empirical LDJ comparison: store visible span on hide, rebuild
  * SUD around changed LIFT on show, then capture current GN for floating.
  * Scratch on LIFT while a configured SUD is hidden does NOT reseed or
  * float; LIFT-only scratch does float using the existing target. */
 for(u32 s=0;s<2;s++){LOOP_PAD();if(!fh_lift(s)||!(fh_active_mask()&(1u<<s)))continue;
  u32 p=fh_profile(s);
  if(visible[s]&&!FH_VISIBLE(s)){s32 span=320-before[s]-(s32)old_lift[s];fh_store(&menu_control.options.lift_span[p],span<0?0:(u32)span);}
  else if(!visible[s]&&FH_VISIBLE(s)){
   if(menu_control.options.lift_span[p]<=320){s32 h=320-(s32)menu_control.options.lift_span[p]-(s32)lift_pixels(s);
    /* Empirical LDJ comparison: only set a POSITIVE restored SUD height.
    * If the old
     * span cannot fit after raising LIFT, retain the current upper cover;
     * clamping to zero here would silently erase a still-enabled SUD. */
    if(h>0)FH_COVER(s)=h;}
   if(fh_floating(s)){u32 gn=fh_green(s,fh_bpm_for(s),fhs.units[s]);if(!gn)gn=1;if(gn>9999)gn=9999;fh_store(&menu_control.options.gn[p],gn);}
  }
  /* Never let a visible upper cover overlap the raised hit line. */
  if(FH_VISIBLE(s)&&fh_sudden(s)&&(u32)FH_COVER(s)>320-lift_pixels(s))FH_COVER(s)=320-lift_pixels(s);
  if(old_lift[s]!=lift_pixels(s)&&!fh_sudden(s))changed[s]=1;
 }
#endif
 for(u32 s=0;s<2;s++){LOOP_PAD();FH_HOLD(s)=hold[s];if(!fh_enabled(s))continue;
  changed[s]|=FH_COVER(s)!=before[s]||(!visible[s]&&FH_VISIBLE(s))||(fh_lift(s)&&visible[s]&&!FH_VISIBLE(s));fh_capture(s);
 }
 for(u32 s=0;s<2;s++){LOOP_PAD();if(changed[s]){fh_solve(s);if(fhs.dp&&fh_links(0))break;}}
 return r;
}
unsigned long long fhs_velocity_call(FS_ARGS){u32 s=(u32)a;fhs.last_side=s;return fh_floating(s)?fh_div(fh_mul((u32)b*1204,fhs.units[s]),400000):fhs_velocity_body(FS_PASS);}
unsigned long long fhs_ring_call(FS_ARGS){u32 object=(u32)a,s=object==0x79e148;return fhs_ring_body(a,(object==0x79dfa0||object==0x79e148)&&fh_floating(s)?fhs.units[s]:b,c,d,e,f,g,h);}
unsigned long long fhs_gn_text(FS_ARGS,unsigned long long ninth,unsigned long long unused)
{
 char text[16];if(fh_floating(fhs.last_side)||fh_lift(fhs.last_side)){unsigned n=0;mm_number(text,15,&n,fh_green(fhs.last_side,fh_bpm_for(fhs.last_side),fh_floating(fhs.last_side)?fhs.units[fhs.last_side]:fh_step(fhs.last_side)));a=(unsigned long)text;}return fs_native_small_text(FS_PASS,ninth);
}
unsigned long long fhs_label_text(FS_ARGS,unsigned long long ninth,unsigned long long unused)
{
 if(fh_enabled(fhs.last_side))a=(unsigned long)(fh_floating(fhs.last_side)?"F":"N");return fs_native_small_text(FS_PASS,ninth);
}
/* FREE exposes its full field in phase2 while the native asset queue may
 * still be loading. Its120-update intro counter advances even then. FHS needs
 * the completed selected chart for the opening BPM, and font/cover pointers
 * must not be used while that same queue is replacing them.
 * Keep the field black until both are ready, then begin the ORIGINAL intro
 * countdown with the first usable field. Never extend/change the native30-
 * update tap detector, held fade, chart clock or START+SELECT exit path. */
static u32 fh_wait_field(void)
{
 if(!fhs.live || fh_kind()!=4 || fs_phase()!=2)return 0;
 u32 enabled=fh_enabled(0)||fh_enabled(1);
 if(!enabled && !((fhs.mask&1)&&fh_lift(0)) && !((fhs.mask&2)&&fh_lift(1)))return 0;
 if((enabled&&fhs.need_start)||!fs_font_ready()){
  fhs.loading_wait=1;return 1;
 }
 if(fhs.loading_wait){fs_word(0x2ad5b8)=fs_word(0x2ad5bc)=0;fhs.loading_wait=0;}
 return 0;
}
static u32 fh_hud(u32 side,s32 *node)
{
 if(!fh_floating(side))return 0;if(!fh_side_visible(side)||!fs_font_ready())return 1;
 if(node[8]<=0)return 1;
 char text[12];unsigned n=0;u32 hundred=fh_div((unsigned long long)fhs.units[side]+2000,4000);
 mm_append(text,11,&n,"F ",2);mm_number(text,11,&n,hundred/100);mm_append(text,11,&n,".",1);if(hundred%100<10)mm_append(text,11,&n,"0",1);mm_number(text,11,&n,hundred%100);
 /* Source HS value is16x7 at the RIGHT of the cell. Source FONT452 advances
  *10px with12px glyphs. Center the whole F/speed within the76x12 inner cell,
  * covering only its printed HI-SPEED label, preserving source frame borders.
  * Positions follow the native callback/fade; no texture or font is generated. */
 short rect[4]={(short)(node[2]-56),(short)(node[3]-2),76,12};
 /* Native GS per-layer queues prepend packets. Submit text first, then
  * its clearing rectangle: the mask executes first, text last. Use the
  * callback's actual layer (node+34), not the song-select font default2. */
 u32 layer=(u16)node[13];
 fh_native_text(text,0,rect[0]+(76-(s32)n*10)/2,rect[1],node[8],0,layer,0,0);
 fh_native_rect(rect,0,100,layer);return 1;
}
#define FH_HUD(name,body,test,side) unsigned long long name(FS_ARGS){s32 *node=(s32 *)(unsigned long)a;if((test)&&fh_hud((side),node))return 0;return body(FS_PASS);}
FH_HUD(fhs_hud0_call,fhs_hud0_body,node[0]==5,1)
FH_HUD(fhs_hud1_call,fhs_hud1_body,node[0]==5,0)
FH_HUD(fhs_hud2_call,fhs_hud2_body,node[0]==3,0)
FH_HUD(fhs_hud3_call,fhs_hud3_body,node[0]==3,0)
FH_HUD(fhs_hud4_call,fhs_hud4_body,node[0]==3,0)
FH_HUD(fhs_hud5_call,fhs_hud5_body,node[0]==3||node[0]==13,node[0]==3?1:0)

unsigned long long fhs_bar_index(FS_ARGS)
{
 u32 side=(u32)a;if(!fh_floating(side))return fhs_index_body(FS_PASS);
 static const u32 steps[11]={400000,600000,800000,900000,1000000,1100000,1200000,1300000,1400000,1500000,1600000};
 u32 best=0,diff=~0u;for(u32 i=0;i<11;i++){LOOP_PAD();u32 value=steps[i],delta=value>fhs.units[side]?value-fhs.units[side]:fhs.units[side]-value;if(delta<diff){diff=delta;best=i;}}return best;
}
