/* Song-select-only modal. No controller polls/history edits or PLAY callbacks.
 * Native select update is held at its action boundary; draw remains native. */
#include "menu_settings.h"
struct mm_control menu_control={1,MM_OPTIONS_INITIALIZER,{0}};
#if !defined(MOD_MENU_TEST) || defined(CUSTOMIZE_MENU_TEST)
static struct cs_preferences menu_customize;
#endif
#include "native_modifiers.c"
#ifndef MOD_MENU_TEST
extern unsigned long long menu_update_body(FS_ARGS),menu_draw_body(FS_ARGS);
extern s32 vd_config_load(const char *,char *,u8 *),vd_config_commit(const char *,const char *,u32,u8 *);
#define menu_raw(s) (*(volatile u16 *)(unsigned long)(0x724c8a+(s)*0x1c0))
#define menu_scheme(s) fs_word(0x724c9c+(s)*0x1c0)
#define menu_system() (((u8 *(*)(void))0x1686e0)())
#define menu_get_timing(p) (((s32 (*)(u8 *))0x16a3c0)(p))
#define menu_set_timing(p,v) (((void (*)(u8 *,s32))0x16a3d0)(p,v))
#define menu_option_timing(v) (((void (*)(s32))0x1cb450)(v))
#define menu_native_rect ((void (*)(const short *,unsigned long long,u32,s32))0x137ec0)
#endif
#if (defined(__mips__) && (defined(USB_HID) || defined(PLAY_PREVIEW))) || defined(MOD_MENU_TEST)
#define MENU_PREVIEW_ROW 1
#else
#define MENU_PREVIEW_ROW 0
#endif
enum {MENU_ROOT,MENU_GAME,MENU_FHS,MENU_PLAYER,MENU_LANE,MENU_PATTERN,MENU_BIND};
static struct {u32 active,owner,row,page,release,armed,repeat,held,edit,seeded,native_seeded,dirty,fhs_attempt,field,capture,cap_count,cap_keys,timing_dirty;u8 seq[7];u8 *scene;struct mm_options draft;struct nm_state native;s32 error;u32 nav_seen[2],hot_held;} menu;
#include "lane_options.c"
static int menu_eligible(u8 *scene)
{
    if(!scene || !MS_STATE || fs_word(0x2ad5e8) || I32(scene+0x2c)!=3)return 0;
    s32 phase=I32(MS_STATE+0x3c);
    return (phase==1 || phase==3) && (!fs_word(0x78af08)
#ifdef SPR_ASYNC
      || sa_owned_queue()
#endif
    );
}
static void menu_apply(void)
{
    gauge_shift_enabled=mm_any_gas(&menu_control.options);fast_slow_enabled=1;
    fs_type[0]=menu_control.options.type[0];fs_type[1]=menu_control.options.type[1];
    fs_counters_enabled=mm_any_counters(&menu_control.options);
}
/* One-time config precedence AFTER native startup LOAD. Later native Options
 * changes are read on opening and can be committed here; no second offset. */
static void menu_seed(void)
{
    if(menu.seeded)return;u8 *system=menu_system();if(!system)return;
    if(menu_control.options.timing==MM_TIMING_UNSET){menu_control.options.timing=menu_get_timing(system);menu.dirty=1;}
    else {menu_set_timing(system,menu_control.options.timing);menu_option_timing(menu_control.options.timing);}
    mm_players(&menu_control.options);menu_apply();menu.seeded=1;
}
/* Only retail1e7720's offset lookup is replaced. Native clocks, +10 bias,
 * 0.1 conversion, windows and F/S signed hit deltas stay native. */
unsigned long long player_note_timing(FS_ARGS){
 u32 node=(u32)b,side;if(!menu.seeded||node<0x100000||node>0x2000000-32)return (unsigned long long)(s32)(signed char)menu_get_timing((u8 *)(unsigned long)a);
 side=*(u8 *)(unsigned long)(node+10);if(side>1)return (unsigned long long)(s32)(signed char)menu_get_timing((u8 *)(unsigned long)a);
 return (unsigned long long)(long long)mm_timing(&menu_control.options,fs_dp()?0:side);
}
static void menu_sync_timing(void){
 if(menu.active)return;u8 *system=menu_system();if(!system)return;s32 v=(signed char)menu_get_timing(system);
 if(v>=-50&&v<=50&&v!=menu_control.options.timing){menu_control.options.timing=v;menu.dirty=menu.timing_dirty=1;menu.fhs_attempt=0;}
}
#define MENU_DOWN 1u
#define MENU_UP 2u
#define MENU_OK 4u
#define MENU_BACK 8u
#define MENU_LEFT 16u
#define MENU_RIGHT 32u
static u32 menu_navigation(u32 side)
{
    u32 raw_mask=menu_raw(side),key_mask=fs_keys(side),b=0;
    if(menu_scheme(side)==1 || menu_scheme(side)==4){
        if(raw_mask&0x4000)b|=MENU_DOWN;if(raw_mask&0x1000)b|=MENU_UP;
        if(raw_mask&0x20)b|=MENU_OK;if(raw_mask&0x80)b|=MENU_BACK;
        if(raw_mask&0x8000)b|=MENU_LEFT;if(raw_mask&0x2000)b|=MENU_RIGHT;
    }else if(menu_scheme(side)==3){
        if(raw_mask&0x80)b|=MENU_DOWN;if(raw_mask&4)b|=MENU_UP;if(raw_mask&0x20)b|=MENU_OK;
    }else{
        if(key_mask&1)b|=MENU_DOWN;if(key_mask&2)b|=MENU_UP;if(key_mask&32)b|=MENU_OK;
    }return b;
}
static u32 menu_release_keys(void)
{
    u32 s=menu.owner,held=fs_keys(s)&127;
    if(fs_dp())held|=fs_keys(1)&127;
    held|=(menu_raw(0)|menu_raw(1))&0x900; /* START/SELECT, not KOC's permanent ID bits */
    held|=menu_navigation(s);return held;
}
/* One press, from either physical/logical player, in every modal page. */
static __attribute__((noinline)) u32 menu_shortcut(void){
    u32 held=(menu_raw(0)|menu_raw(1))&0x900,press=held&~menu.hot_held;
    menu.hot_held=held;return press&0x100?2:press&0x800?1:0;
}
static int menu_save(void)
{
    if(!menu_control.path[0])return -10;
    char *old=malloc_(MM_CFG_MAX+1),*out=malloc_(MM_CFG_MAX+1);
    u8 *scratch=malloc_(MM_CFG_MAX+MM_PENDING_HEADER+1);s32 result=-11;
    if(old && out && scratch){
        int n=vd_config_load(menu_control.path,old,scratch);
        if(n>=0){
            struct mm_options check;mm_defaults(&check);
            if(mm_parse(&check,old,n))result=-12;
            else {
                char fields[1536];int size=mm_emit(&menu.draft,fields,sizeof fields-1);
                #if !defined(MOD_MENU_TEST) || defined(CUSTOMIZE_MENU_TEST)
                const char *keys[MM_KEY_COUNT+CS_KEYS];for(u32 k=0;k<MM_KEY_COUNT;k++){LOOP_PAD();keys[k]=mm_keys[k];}for(u32 k=0;k<CS_KEYS;k++){LOOP_PAD();keys[MM_KEY_COUNT+k]=cs_keys[k];}
                struct cs_preferences check_skin;cs_defaults(&check_skin);
                int add=size<0?-1:cs_emit(&menu_customize,fields+size,sizeof fields-size-1);
                int merged=(size<0||add<0||cs_parse(&check_skin,old,n))?-1:mm_merge(old,n,fields,size+add,keys,MM_KEY_COUNT+CS_KEYS,out,MM_CFG_MAX-1);
#else
                int merged=size<0?-1:mm_merge(old,n,fields,size,mm_keys,MM_KEY_COUNT,out,MM_CFG_MAX-1);
#endif
                result=merged<0?-13:vd_config_commit(menu_control.path,out,merged,scratch);
            }
        }else result=n;
    }
    if(old)free_(old);if(out)free_(out);if(scratch)free_(scratch);
    return result;
}
static u32 menu_pending_changes(void){
 u32 changed=menu.dirty;
#if !defined(MOD_MENU_TEST) || defined(CUSTOMIZE_MENU_TEST)
 changed|=!mm_equal(&menu_customize,&customize,sizeof customize);
#endif
#if !defined(MOD_MENU_TEST) || defined(MOD_MENU_FHS_TEST)
 changed|=fhs.dirty;
#endif
 return changed;
}

static __attribute__((noinline)) void menu_close(u32 save)
{
    if(save){
        if(!mm_equal(&menu.draft,&menu_control.options,sizeof menu.draft) || menu_pending_changes()){
            if((menu.error=menu_save()))return;
        }
        menu_control.options=menu.draft;menu_apply();
#if !defined(MOD_MENU_TEST) || defined(CUSTOMIZE_MENU_TEST)
        customize=menu_customize;cs_apply();
#endif
        if(nm_available())nm_store(&menu.native);
        u8 *system=menu_system();if(system){menu_set_timing(system,menu.draft.timing);menu_option_timing(menu.draft.timing);}
        menu.dirty=0;
#if !defined(MOD_MENU_TEST) || defined(MOD_MENU_FHS_TEST)
        fhs.dirty=0;
#endif
        menu.timing_dirty=0;
    }
    menu.active=2;menu.release=1;menu.edit=0;menu.held=0;
}
static u32 menu_rows(void){return menu.page==MENU_FHS?3:menu.page==MENU_GAME?5:menu.page==MENU_PLAYER?(MENU_PREVIEW_ROW?4:7):menu.page==MENU_PATTERN?11:menu.page==MENU_ROOT?7+MENU_PREVIEW_ROW:6;}
static void menu_back(void){
 if(menu.page==MENU_FHS){menu.page=MENU_PLAYER;menu.row=MENU_PREVIEW_ROW?1:3;}
 else if(menu.page==MENU_PATTERN){menu.page=MENU_LANE;menu.row=3;menu.capture=0;}
 else {menu.row=menu.page==MENU_GAME?3:menu.page==MENU_PLAYER?1:0;menu.page=MENU_ROOT;}
 menu.edit=0;
}
static u32 menu_pattern_profile(void){return lq_profile(menu.owner,menu.field);}
static void menu_pattern_open(void){u32 m=menu.draft.lane[menu.owner];if(m!=LQ_EXACT&&m!=LQ_COLOUR)return;lq_pattern_get(menu.draft.lanep[menu_pattern_profile()],m==LQ_COLOUR,menu.seq);menu.page=MENU_PATTERN;menu.row=0;menu.error=0;}
static void menu_change(int delta){
 menu.error=0;
 if(menu.page==MENU_FHS){u32 *flags=&menu.draft.fhs[menu.owner];if(menu.row==0)*flags^=FH_MASTER;else if(menu.row==1&&(*flags&FH_MASTER))*flags^=FH_LOCK;return;}
 if(menu.page==MENU_PATTERN){if(menu.row<7){u32 n=menu.draft.lane[menu.owner]==LQ_COLOUR?2:7;menu.seq[menu.row]=(menu.seq[menu.row]+n+(delta<0?-1:1))%n;}return;}
 if(menu.page==MENU_LANE){
  if(menu.row==0){u32 m=menu.draft.lane[menu.owner];m=(m+9+(delta<0?-1:1))%9;
   u8 *opt=nm_options(nm_preferences(),nm_style())+menu.owner*0x38;
   if(m>=6&&*(u32 *)(opt+0x20)){menu.error=-20;return;}
   menu.draft.lane[menu.owner]=m;lq_set_base(&menu.native,menu.owner,m);nm_encode(&menu.draft,&menu.native);
  }else if(menu.row==1||menu.row==2){if(menu.draft.lane[menu.owner]>=6){menu.error=-23;return;}nm_change(&menu.native,menu.row==1?4:5,menu.owner,delta);nm_encode(&menu.draft,&menu.native);}
  else if(menu.row==4&&fs_dp())menu.field^=1;
  return;
 }
 if(menu.page==MENU_GAME){if(menu.row==3)menu.draft.quick_retry[menu.owner]^=1;else if(nm_available()&&menu.row<3){nm_change(&menu.native,menu.row+1,menu.owner,delta);nm_encode(&menu.draft,&menu.native);}return;}
 if(menu.page==MENU_PLAYER){if(menu.row==(MENU_PREVIEW_ROW?2:5)){menu.draft.lift[menu.owner]^=1;return;}if(menu.row==0){if(menu.owner)menu.draft.gas2^=1;else menu.draft.gas^=1;}
  else if(!MENU_PREVIEW_ROW){u32 s=menu.owner;if(menu.row==1)menu.draft.type[s]=(menu.draft.type[s]+(delta<0?3:1))&3;else if(menu.row==2){if(s)menu.draft.counters2^=1;else menu.draft.counters^=1;}else if(menu.row==4)menu.draft.bpm_notice[s]=(menu.draft.bpm_notice[s]+(delta<0?4:1))%5;}
  return;}
 if(menu.row==2){int v=mm_timing(&menu.draft,menu.owner)+delta;if(v< -50)v= -50;if(v>50)v=50;if(menu.owner)menu.draft.timing2=v;else menu.draft.timing=v;}
}
static void menu_pattern_accept(void){
 u32 colour=menu.draft.lane[menu.owner]==LQ_COLOUR;
 if(menu.row==7){menu.capture=1;menu.cap_count=menu.cap_keys=0;menu.release=1;menu.error=0;return;}
 if(menu.row==8){if(!lq_valid(menu.seq,colour)){menu.error=colour?-22:-21;return;}u32 p=menu_pattern_profile();menu.draft.lanep[p]=lq_pattern_put(menu.draft.lanep[p],colour,menu.seq);menu_back();menu.error=0;return;}
 if(menu.row==9){lq_pattern_get(MM_LANE_PATTERN_DEFAULT,colour,menu.seq);menu.error=0;return;}
 if(menu.row==10){menu_back();menu.error=0;return;}menu.edit=1;
}
static void menu_capture_step(void){
 u32 side=fs_dp()?menu.field:menu.owner,held=fs_keys(side)&127,old=menu.cap_keys;menu.cap_keys=held;
 if((menu_raw(menu.owner)|(fs_dp()?menu_raw(1):0))&0x800){menu.capture=0;menu.release=1;menu.error=0;return;}
 if(old||!held||(held&(held-1)))return;u32 key=0;while(!(held&(1u<<key))){LOOP_PAD();key++;}
 u32 colour=menu.draft.lane[menu.owner]==LQ_COLOUR,v=colour?(key&1):key,n=0;
 for(u32 i=0;i<menu.cap_count;i++){LOOP_PAD();if(!colour&&menu.seq[i]==v){menu.error=-21;return;}n+=menu.seq[i]==v;}
 if(colour&&n>=(v?3u:4u)){menu.error=-22;return;}
 menu.seq[menu.cap_count++]=v;menu.error=0;if(menu.cap_count==7){menu.capture=0;menu.row=8;menu.release=1;}
}
#if defined(__mips__) && (defined(USB_HID) || defined(PLAY_PREVIEW))
#include "ui_overlay.c"
#endif
#if defined(__mips__) && defined(USB_HID)
#define lb_open uib_open
#define lb_update uib_update
#define lb_draw uib_draw
#pragma clang section text=".ui_bind.text" rodata=".ui_bind.rodata" data=".ui_bind.data" bss=".ui_bind.bss"
#include "live_controller.c"
#pragma clang section text="" rodata="" data="" bss=""
#undef lb_open
#undef lb_update
#undef lb_draw
static void lb_open(u32 player){if(ui_load(1))uib_open(player);else menu.error=-30;}
static void lb_update(void){if(ui_active==1){uib_update();if(!lb)ui_release();}}
static void lb_draw(void){if(ui_active==1)uib_draw();}
#else
#include "live_controller.c"
#endif
#if defined(__mips__) && (defined(USB_HID) || defined(PLAY_PREVIEW))
static void pve_open(u8 *),pve_select(u8 *);static u32 pve_draw_hold(void);
#define PVE_PRESENT 1
#endif
static u32 menu_update(u8 *scene)
{
#ifdef PVE_PRESENT
    if(ui_active==2){pve_select(scene);return 1;}
#endif
    if(!menu_eligible(scene)){
        menu.active=menu.armed=menu.held=menu.fhs_attempt=0;menu.scene=scene;return 0;
    }
    #if !defined(MOD_MENU_TEST) || defined(MOD_MENU_FHS_TEST)
    fhs.live=0;
#endif
    lq.live=0;
#if !defined(MOD_MENU_TEST) || defined(QUICK_RETRY_TEST)
    qr_select_clear();
#endif
    menu_seed();menu_sync_timing();if(!menu.seeded || menu_control.schema!=1)return 0;
    /* Seed optional native preferences only at the first real FREE select.
     * Later leaving-mode clears and original command shortcuts stay native. */
    if(!menu.native_seeded && nm_available()){
        lq_restore(&menu_control.options);nm_restore_preferences(&menu_control.options);menu.native_seeded=1;
    }
    #if !defined(MOD_MENU_TEST) || defined(MOD_MENU_FHS_TEST)
    if(!menu.active && (fhs.dirty||menu.timing_dirty) && !menu.fhs_attempt){
        menu.fhs_attempt=1;menu.draft=menu_control.options;
#if !defined(MOD_MENU_TEST) || defined(CUSTOMIZE_MENU_TEST)
        /* Automatic preference commits can happen before MOD MENU was ever
         * opened. Its dormant draft is zero/stale; serialize live artwork. */
        menu_customize=customize;
#endif
        if(!menu_save()){fhs.dirty=menu.dirty=menu.timing_dirty=0;}
    }
#endif
    if(menu.active){
        if(menu.release){menu_shortcut();if(!menu_release_keys()){menu.release=0;if(menu.active==2)menu.active=0;}return 1;}
        if(menu.page==MENU_BIND){lb_update();return 1;}
        u32 shortcut=menu_shortcut();if(shortcut){
            if(shortcut==1&&menu.page==MENU_PATTERN){u32 row=menu.row;menu.row=8;menu_pattern_accept();if(menu.page==MENU_PATTERN){menu.row=row;return 1;}}
            menu.capture=0;menu_close(shortcut==1);return 1;
        }
        if(menu.capture){menu_capture_step();return 1;}
        u32 nav[2]={menu_navigation(0),menu_navigation(1)};
        if(!fs_dp()&&nm_style()>=4&&!menu.edit&&menu.page!=MENU_PATTERN){u32 other=1-menu.owner;if(!nav[menu.owner]&&(nav[other]&~menu.nav_seen[other])){menu.owner=other;menu.held=menu.repeat=0;}}
        menu.nav_seen[0]=nav[0];menu.nav_seen[1]=nav[1];
        u32 held=nav[menu.owner],press=held&~menu.held;
        if(held && held==menu.held){if(++menu.repeat>=24 && ((menu.repeat-24)%5)==0)press=held&(MENU_UP|MENU_DOWN|MENU_LEFT|MENU_RIGHT);}
        else menu.repeat=0;
        menu.held=held;
        if(press&MENU_BACK){if(menu.edit)menu.edit=0;else if(menu.page)menu_back();else menu_close(0);return 1;}
        if(menu.edit){
            if(press&(MENU_UP|MENU_RIGHT))menu_change(1);
            if(press&(MENU_DOWN|MENU_LEFT))menu_change(-1);
            if(press&MENU_OK)menu.edit=0;
        }else{
            if(press&MENU_DOWN)menu.row=menu.row+1==menu_rows()?0:menu.row+1;
            if(press&MENU_UP)menu.row=menu.row?menu.row-1:menu_rows()-1;
            if(press&MENU_LEFT)menu_change(-1);
            if(press&MENU_RIGHT)menu_change(1);
            if(press&MENU_OK){
                if(menu.page==MENU_PATTERN)menu_pattern_accept();
                else if(menu.page){
                 if(menu.row+1==menu_rows())menu_back();
                 else if(menu.page==MENU_PLAYER&&menu.row==(MENU_PREVIEW_ROW?1:3)){menu.page=MENU_FHS;menu.row=0;}
                 else if(menu.page==MENU_LANE&&menu.row==3)menu_pattern_open();
                 else menu.edit=1;
                }else if(menu.row==0){if(nm_available()){menu.page=MENU_LANE;menu.row=0;}}
                else if(menu.row==1){menu.page=MENU_PLAYER;menu.row=0;}
                else if(menu.row==2)menu.edit=1;
                else if(menu.row==3){if(nm_available()){menu.page=MENU_GAME;menu.row=0;}}
                else if(menu.row==4)lb_open(menu.owner);
                #ifdef PVE_PRESENT
                else if(menu.row==5&&MENU_PREVIEW_ROW)pve_open(scene);
#else
                else if(menu.row==5&&MENU_PREVIEW_ROW)menu.error=-31;
#endif
                else if(menu.row==5+MENU_PREVIEW_ROW)menu_close(1);else menu_close(0);
            }
        }
        return 1;
    }
    u32 logical[2]={fs_keys(0)&127,fs_keys(1)&127};
    if(!logical[0]&&!logical[1])menu.armed=1;
    if(!menu.armed || !fs_font_ready())return 0;
    u32 side=2;
    if(fs_dp()){if((menu_scheme(0)==3?(menu_raw(0)&0xc0)==0xc0:(logical[0]&5)==5) && (menu_scheme(1)==3?(menu_raw(1)&0xc0)==0xc0:(logical[1]&5)==5))side=0;}
    else for(u32 s=0;s<2;s++)if(menu_scheme(s)==3?(menu_raw(s)&0xe0)==0xe0:(logical[s]&21)==21){side=s;break;}
    if(side>1)return 0;
    menu.owner=side;menu.row=menu.page=0;menu.active=1;menu.release=1;menu.armed=0;menu.error=0;menu.edit=0;menu.held=0;menu.field=menu.capture=0;
    menu.nav_seen[0]=menu.nav_seen[1]=0;menu.scene=scene;menu.draft=menu_control.options;
#if !defined(MOD_MENU_TEST) || defined(CUSTOMIZE_MENU_TEST)
    menu_customize=customize;
#endif
    if(nm_available()){nm_capture(&menu.native);lq_import(&menu.draft,&menu.native);nm_encode(&menu.draft,&menu.native);}
    return 1;
}
unsigned long long menu_update_call(FS_ARGS)
{
#ifdef SPR_ASYNC
    sa_select((u8 *)(unsigned long)a,menu.active);
#endif
#ifdef SELECT_PROFILE
    sp_tick();
#endif
    if(menu_update((u8 *)(unsigned long)a))return 0;
    song_window_update((u8 *)(unsigned long)a);
    unsigned long long result=menu_update_body(FS_PASS);
#ifdef SELECT_PROFILE
    sp_after_update();
#endif
    return result;
}
static void menu_line(char *out,const char *label,const char *value)
{
    unsigned n=0;mm_append(out,79,&n,label,mm_length(label));mm_append(out,79,&n,value,mm_length(value));
}
static const char *menu_save_error(s32 error)
{
    if(error==-31)return "COULD NOT LOAD PLAY PREVIEW - RETRY";
    if(error==-30)return "COULD NOT LOAD CONTROLLER EDITOR - RETRY";
    if(error==-23)return "PLUS VARIANTS REQUIRE NATIVE ARRANGEMENT";
    if(error==-20)return "R-RAN / TRAINER REQUIRE 7KEYS";
    if(error==-21)return "USE EACH KEY 1-7 EXACTLY ONCE";
    if(error==-22)return "NEED FOUR WHITE AND THREE BLUE LANES";
    if(error==-101)return "COULD NOT OPEN SETTINGS - RETRY SAVE";
    if(error==-102)return "COULD NOT READ SETTINGS - RETRY SAVE";
    if(error==-104)return "COULD NOT SIZE SETTINGS - RETRY SAVE";
    if(error==-103)return "COULD NOT CLOSE SETTINGS - RETRY SAVE";
    if(error==-4)return "COULD NOT WRITE UNDO JOURNAL - RETRY SAVE";
    if(error==-5)return "COULD NOT VERIFY UNDO JOURNAL - RETRY SAVE";
    if(error==-6)return "COULD NOT WRITE SETTINGS - RETRY SAVE";
    if(error==-7)return "COULD NOT VERIFY SETTINGS - RETRY SAVE";
    if(error==-8 || error==-9)return "COULD NOT FINISH SETTINGS SAVE - RETRY";
    if(error==-10)return "SETTINGS PATH UNAVAILABLE - CANCEL";
    if(error==-11)return "NOT ENOUGH MEMORY - CANCEL AND RETRY";
    if(error==-12)return "INVALID SETTINGS FILE - CANCEL";
    if(error==-13 || error==-3)return "SETTINGS FILE TOO LARGE - CANCEL";
    return "SETTINGS NOT SAVED - RETRY SAVE AND EXIT";
}
static void menu_row_text(u32 i,u32 s,char *line,u32 *pink_out,u32 *pattern_out){
 const char *types[4]={"TYPE A","TYPE B","TYPE C","OFF"};const char *bpmtypes[5]={"OFF","TYPE A","TYPE B","TYPE C","TYPE D"};
 u32 pink=0,pattern=0;

        if(menu.page==MENU_FHS){u32 flags=menu.draft.fhs[s];if(i==0)menu_line(line,"FLOATING HI-SPEED: ",flags&FH_MASTER?"ON":"OFF");else if(i==1)menu_line(line,"LOCK COVER / START RESET: ",!(flags&FH_MASTER)?"FHS REQUIRED":flags&FH_LOCK?"ON":"OFF");else menu_line(line,"BACK","");}
        else if(menu.page==MENU_GAME){if(i==0){menu_line(line,"REGUL SPEED: ",menu.native.regul?"ON":"OFF");pink=1;}else if(i==1){const char *v[3]={"OFF","HYPER","ANOTHER"};menu_line(line,"GAMBOL TIMING: ",v[menu.native.gambol]);}else if(i==2)menu_line(line,"HAZARD: ",menu.native.hard[s]==2?"ON":menu.native.hard[s]==1?"OFF":"SELECT HARD FIRST");else if(i==3)menu_line(line,"QUICK RETRY: ",menu.draft.quick_retry[s]?"ON":"OFF");else menu_line(line,"BACK","");}
        else if(menu.page==MENU_PLAYER){if(i==0)menu_line(line,"GAUGE AUTO SHIFT: ",mm_gas(&menu.draft,s)?"ON":"OFF");else if(i==(MENU_PREVIEW_ROW?2:5))menu_line(line,"LIFT: ",menu.draft.lift[s]?"ON":"OFF");else if(MENU_PREVIEW_ROW)menu_line(line,i==1?"FLOATING HI-SPEED  >":"BACK","");else if(i==1)menu_line(line,"FAST/SLOW: ",types[menu.draft.type[s]]);else if(i==2)menu_line(line,"F/S COUNTERS: ",mm_counters(&menu.draft,s)?"ON":"OFF");else if(i==4)menu_line(line,"BPM CHANGE NOTICE: ",bpmtypes[menu.draft.bpm_notice[s]]);else menu_line(line,i==3?"FLOATING HI-SPEED  >":"BACK","");}
        else if(menu.page==MENU_LANE){u32 m=menu.draft.lane[s];if(i==0){menu_line(line,"ARRANGEMENT: ",m<9?lq_names[m]:"NATIVE");pink=m==4||m==5;}else if(i==1){u32 r=menu.native.random[s];menu_line(line,r>=3?"S-RAN+: ":"RANDOM+: ",m>=6?"NATIVE ARRANGEMENT ONLY":!nm_plus_allowed(s)?"ASSIST INCOMPATIBLE":!r?"SELECT RANDOM / S-RAN":(r==2||r==4)?"ON":"OFF");pink=1;}else if(i==2){menu_line(line,"MIRROR+: ",!nm_plus_allowed(s)?"ASSIST INCOMPATIBLE":!menu.native.mirror[s]?"SELECT MIRROR FIRST":menu.native.mirror[s]==2?"ON":"OFF");pink=1;}else if(i==3)menu_line(line,"TRAINER PATTERN",m>=7?"  >":"  (SELECT TRAINER)");else if(i==4)menu_line(line,"FIELD: ",fs_dp()?(menu.field?"RIGHT":"LEFT"):s?"PLAYER 2":"PLAYER 1");else menu_line(line,"BACK","");}
        else if(menu.page==MENU_PATTERN){if(i<7){char label[16]="LANE 0: ";label[5]='1'+i;u32 colour=menu.draft.lane[s]==LQ_COLOUR;char glyph[2]={colour?(menu.seq[i]?'B':'W'):'1'+menu.seq[i],0};menu_line(line,label,glyph);pattern=1;}else {const char *v[]={"RECORD KEY SEQUENCE","APPLY PATTERN","RESET PATTERN","BACK"};menu_line(line,v[i-7],"");}}
        else if(i==0)menu_line(line,"LANE OPTIONS",nm_available()?"  >":"  (FREE ONLY)");
        else if(i==1)menu_line(line,"PLAYER OPTIONS  >","");
        else if(i==2){unsigned n=0;mm_append(line,79,&n,"ADJUSTMENT TIMING: ",19);int v=mm_timing(&menu.draft,s);mm_append(line,79,&n,v<0?"-":"+",1);if(v<0)v=-v;mm_number(line,79,&n,v/10);mm_append(line,79,&n,".",1);mm_number(line,79,&n,v%10);}
        else if(i==3)menu_line(line,"GAME OPTIONS",nm_available()?"  >":"  (FREE ONLY)");
        else menu_line(line,i==4?"REBIND CONTROLLER  >":i==5&&MENU_PREVIEW_ROW?"CUSTOMIZE PLAY SCENE  >":i==5+MENU_PREVIEW_ROW?"SAVE AND EXIT":"CANCEL","");
 *pink_out=pink;*pattern_out=pattern;
}
static void menu_center(const char *text,u32 colour,s32 x,s32 y,s32 width){
 u32 n=mm_length(text);if(!n)return;u32 step=width/n;if(step>10)step=10;
 fs_text_sized(text,colour,x+(width-(s32)(n*step))/2,y,100,step,0,2,0,0);
}
static u32 menu_paired(u32 row){
 if(fs_dp()||nm_style()<4)return 0;
 return menu.page==MENU_ROOT?row==2:menu.page==MENU_PLAYER?(row==0||row==(MENU_PREVIEW_ROW?2:5)):menu.page==MENU_FHS?row<2:menu.page==MENU_GAME?(row==2||row==3):menu.page==MENU_LANE?row<3:0;
}
static void menu_draw(u8 *scene)
{
 if(I32(scene+0x2c)!=3){menu.active=menu.armed=0;return;}
 if(menu.active!=1||menu.scene!=scene||!menu_eligible(scene)||!fs_font_ready())return;
 short shade[4]={0,0,640,480};menu_native_rect(shade,0,70,3);
 if(menu.page==MENU_BIND){lb_draw();return;}
 const char *titles[]={"MOD MENU","GAME OPTIONS","FLOATING HI-SPEED","PLAYER OPTIONS","LANE OPTIONS","RANDOM TRAINER"};
 menu_center(titles[menu.page],3,80,76,480);
 if(!fs_dp()&&nm_style()>=4){fs_text("P1",4,190,99,100);fs_text("P2",1,430,99,100);menu_center("2 PLAYER",0,240,99,160);}
 else menu_center(fs_dp()?"DOUBLE PLAY":menu.owner?"PLAYER 2":"PLAYER 1",3,80,99,480);
 u32 first=menu.row/6*6,last=first+6;if(last>menu_rows())last=menu_rows();
 for(u32 i=first;i<last;i++){LOOP_PAD();char line[80],label[80];u32 pink,pattern;menu_row_text(i,menu.owner,line,&pink,&pattern);
  s32 y=128+(s32)(i-first)*39;u32 hover=i==menu.row,paired=menu_paired(i);char *value=0;
  u32 len=mm_length(line),cut=len;for(u32 j=0;j<len;j++){LOOP_PAD();if(line[j]==':'){cut=j;value=line+j+1;while(*value==' '){LOOP_PAD();value++;}break;}}
  mm_copy(label,line,cut);label[cut]=0;
  if(value){
   menu_center(label,pink?3:0,90,y,460);
   for(u32 side=0;side<(paired?2:1);side++){LOOP_PAD();char other[80];u32 unused,pat;
    u32 owner=paired?side:menu.owner;menu_row_text(i,owner,other,&unused,&pat);char *v=other;
    for(u32 j=0;other[j];j++){LOOP_PAD();if(other[j]==':'){v=other+j+1;while(*v==' '){LOOP_PAD();v++;}break;}}
    s32 xx=paired?90+(s32)side*235:90,ww=paired?225:460;
    menu_center(v,unused?3:pat?((menu.draft.lane[owner]==LQ_COLOUR?menu.seq[i]:(menu.seq[i]&1))?1:0):hover&&menu.edit&&owner==menu.owner?0:paired?(side?1:4):0,xx,y+17,ww-8);
    if(hover&&menu.edit&&(!paired||owner==menu.owner)){short cell[4]={xx,(short)(y+14),ww-8,19};menu_native_rect(cell,paired?(side?0x602015:0x151560):0x503050,85,3);}
   }
   if(paired){short split[4]={319,(short)(y+17),1,12};menu_native_rect(split,0x604060,100,3);}
  }else{
   if(pattern){line[8]=0;menu_center(line,0,180,y+9,120);char glyph[2]={menu.draft.lane[menu.owner]==LQ_COLOUR?(menu.seq[i]?'B':'W'):'1'+menu.seq[i],0};fs_text(glyph,(menu.draft.lane[menu.owner]==LQ_COLOUR?menu.seq[i]:(menu.seq[i]&1))?1:0,340,y+9,100);}
   else menu_center(line,pink?3:0,90,y+9,460);
  }
  if(hover){short band[4]={82,(short)(y-3),476,35};menu_native_rect(band,0x302828,90,3);short edge[4]={82,(short)(y-3),3,35};menu_native_rect(edge,0x603060,100,3);}
  short rule[4]={82,(short)(y+33),476,1};menu_native_rect(rule,0x503040,70,3);
 }
 u32 scheme=menu_scheme(menu.owner),pad=scheme==1||scheme==4;
 menu_center(pad?(menu.edit?"D-PAD VALUE   O DONE":"D-PAD MOVE   O EDIT   SQUARE BACK"):menu.edit?(scheme==3?"1/2 VALUE   5 DONE":"1/2 VALUE   6 DONE"):(scheme==3?"1/2 MOVE   5 SELECT":"1/2 MOVE   6 SELECT"),0,80,380,480);
 if(menu.capture){char text[48];unsigned n=0;mm_append(text,47,&n,"PRESS KEY ",10);mm_number(text,47,&n,menu.cap_count+1);mm_append(text,47,&n," OF 7 - START CANCELS",20);menu_center(text,3,70,406,500);}
 else if(menu.error)menu_center(menu_save_error(menu.error),4,45,406,550);
 else menu_center("START SAVE AND EXIT   SELECT DISCARD",0,75,406,490);
 char page[20];unsigned n=0;mm_number(page,19,&n,first/6+1);mm_append(page,19,&n," / ",3);mm_number(page,19,&n,(menu_rows()+5)/6);fs_text(page,0,500,434,100);
 short border[4]={78,120,484,2};menu_native_rect(border,0x603060,100,3);
 short panel[4]={72,64,496,386};menu_native_rect(panel,0x100810,90,3);
}

unsigned long long menu_draw_call(FS_ARGS)
{
#ifdef PVE_PRESENT
    if(pve_draw_hold())return 0;
#endif
    /* The native CMD block reads only these five global preference bytes.
     * Hide that legacy single-owner block while drawing, then restore before
     * any update; our per-player, multi-line equivalent uses the real flags. */
    u8 saved[5];u8 *p=nm_available()?nm_preferences():0;u32 off[5]={0x1c4,0x1c5,0x1c6,0x1c8,0x1c9};
    if(p)for(u32 i=0;i<5;i++){LOOP_PAD();saved[i]=p[off[i]];p[off[i]]=0;}
    unsigned long long r=menu_draw_body(FS_PASS);
    if(p)for(u32 i=0;i<5;i++){LOOP_PAD();p[off[i]]=saved[i];}
#ifdef PVE_PRESENT
    if(ui_active==2)return r;
#endif
    if(p&&menu_eligible((u8 *)(unsigned long)a))lq_commands();menu_draw((u8 *)(unsigned long)a);
#ifdef SELECT_PROFILE
    sp_draw();
#endif
    return r;
}
