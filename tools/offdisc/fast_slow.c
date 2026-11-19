/* Independent implementation of the user-supplied FAST/SLOW HUD behaviour.
 * Read native judgement events, never judge again or change the event ring.
 * Reuse EMPRESS font452: colour1 blue,4 red. No near-top-of-RAM binary blob,
 * new textures, allocations, file reads or additional input polling. */
#define FS_ARGS unsigned long long a,unsigned long long b,unsigned long long c,unsigned long long d,unsigned long long e,unsigned long long f,unsigned long long g,unsigned long long h
#define FS_PASS a,b,c,d,e,f,g,h
#ifndef FAST_SLOW_TEST
#include "menu_settings.h"
extern struct mm_control menu_control;
#define fs_judge_y(h,s) cs_judge_y(h,s)
#define fs_player_counters(s) mm_counters(&menu_control.options,fs_dp()?0:(s))
#define fs_word(a) (*(volatile u32 *)(unsigned long)(a))
#define fs_phase() fs_word(0x2ad534)
#define fs_frame() fs_word(0x2ad470)
#define fs_auto() (*(volatile u8 *)0x2ad440 & 3u)
#define fs_dp() (((s32 (*)(void))0x205d50)())
#define fs_layout() (((s32 (*)(void))0x1f9d00)())
#define fs_settings() (((u32 (*)(void))0x168630)())
#define fs_ghost_enabled(p) (((u32 (*)(u32))0x177dd0)(p))
#define fs_ghost_kind(p) (((u32 (*)(u32))0x177db0)(p))
#define fs_player_ghost(s) cs_ghost(s)
/* R5900/n32: eight argument registers AND one stack argument. Native text
 * forwards rotation, layer, optional rectangle pointer and flags to 12fdd0.
 * A five-argument declaration leaves a7 as an arbitrary pointer. */
#define fs_native_text ((s32 (*)(const char *,s32,s32,s32,s32,s32,s32,const void *,s32))0x1e1850)
#define fs_native_small_text ((unsigned long long (*)(FS_ARGS,unsigned long long))0x1e1730)
#define fs_keys(s) (*(volatile u32 *)(unsigned long)(0x724cf0+(s)*0x1c0)&0x7fu)
extern unsigned long long fs_ring_body(FS_ARGS),fs_play_body(FS_ARGS),fs_result_body(FS_ARGS),fs_input_body(FS_ARGS);
#endif
#ifndef fs_player_ghost
#define fs_player_ghost(s) fs_ghost_setting()
#endif
#ifndef fs_player_counters
#define fs_player_counters(s) 1u
#endif
#ifndef fs_judge_y
#define fs_judge_y(h,s) (*(short *)(unsigned long)((h)+0x10c))
#endif
/* Retail ghost getters require the current settings pointer in a0. Use the
 * same client/argument as native1fb950; a no-argument getter is invalid. */
#if !defined(FAST_SLOW_TEST) || defined(FS_SETTINGS_TEST)
static u32 fs_ghost_setting(void){u32 p=fs_settings();if(!fs_ghost_enabled(p))return 0;u32 t=fs_ghost_kind(p);return t<3?t+1:0;}
#elif !defined(fs_ghost_setting)
#define fs_ghost_setting() 0u
#endif
u32 fast_slow_enabled;
static struct {u32 fast,slow,stamp,shown,late;} fs_side[2];
/* TYPE survives the per-song counter reset, matching the supplied mod. */
static u32 fs_type[2];
static u32 fs_pair_drawn;
static void fs_pair_fallback(void);
#if !defined(FAST_SLOW_TEST) || defined(FS_GHOST_DRIVER_TEST)
static u32 fs_ghost_side=2;
extern unsigned long long fs_ghost_body(FS_ARGS);
unsigned long long fs_ghost_call(FS_ARGS){
    if(PLAY_PREVIEW_ACTIVE())return 0; /* editor has isolated visual samples */
    u8 *p=(u8 *)(unsigned long)fs_settings();u8 kind=p[5],enabled=p[6];
    fs_pair_drawn=0;u32 types[2]={fs_player_ghost(0),fs_player_ghost(1)};unsigned long long result=0;
    /* Retail1FB950 retains all target-score/availability/AUTO/lane-cover gates.
     * It only reads these bytes. Bound the override to this synchronous draw;
     * unequal battle types run separately and filter the other text call. */
    u32 count=types[0]==types[1]?1:2;
    for(u32 s=0;s<count;s++){LOOP_PAD();u32 type=types[s];fs_ghost_side=count==1?2:s;
        p[5]=type?type-1:0;p[6]=type!=0;result=fs_ghost_body(FS_PASS);
    }
    p[5]=kind;p[6]=enabled;fs_ghost_side=2;fs_pair_fallback();return result;
}
#endif
u32 fs_counters_enabled=1;
static u32 __attribute__((noinline)) fs_counts_on(u32 side){return side<2&&fs_player_counters(side);}
static s32 __attribute__((noinline)) fs_text(const char *s,s32 colour,s32 x,s32 y,s32 alpha)
{
    return fs_native_text(s,colour,x,y,alpha,0,2,0,0);
}
/* Native FONT452 glyphs are10x12. In the narrow Original-DP margins only,
 * compress X while retaining the normal12px height and all draw arguments. */
#ifndef fs_native_glyph
#define fs_native_glyph ((s32 (*)(u32,s32,s32,s32,s32,s32,s32,s32,s32,s32,const void *,s32))0x12fdd0)
#define fs_native_colour ((void (*)(s32,s32,s32))0x1d7b40)
#define fs_glyph(c) fs_word(0x2789e0+(u32)(unsigned char)(c)*4)
#endif
static s32 fs_text_sized(const char *s,s32 colour,s32 x,s32 y,s32 alpha,u32 step,s32 rotation,s32 layer,const void *rect,s32 flags){
 if(step==10)return fs_native_text(s,colour,x,y,alpha,rotation,layer,rect,flags);
 fs_native_colour(colour,0,6);u32 n=0;
 while(s[n]&&n<23){LOOP_PAD();fs_native_glyph(fs_glyph(s[n])+colour,x+(s32)n*(s32)step,y,step*10,100,0,0,alpha,rotation,layer,rect,flags);n++;}return n;
}
/* These are actual native2815A0/frame animation lane rectangles, not HD pixel
 * projections. C/D notices must ALIGN away from their edge, not be centred on
 * an offset point. Split a shared pane for battle; DP remains one owner. */
static u32 fs_outside(u32 hud,u32 side,u32 length,u32 dp_center,s32 *x,u32 *step){
 u32 layout=fs_word(hud);if(layout>5||side>1||!length||length>23)return 0;
 static const short regions[6][4]={{177,316,324,463},{8,145,495,632},{282,632,282,632},{8,358,8,358},{367,495,503,632},{8,52,588,632}};
 s32 lo=regions[layout][side*2],hi=regions[layout][side*2+1];
 u32 right=side!=0;
 if(layout==1||layout==5)right=side==0; /* outer, rather than cramped battle/DP gap */
 if(layout==2)right=0;if(layout==3)right=1;
 if(layout==4)right=0; /* no usable left margin: use the native BGA pane */
 if(dp_center){if(layout==4){lo=367;hi=632;}else if(layout==5){lo=301;hi=339;}else return 0;}
 u32 fit=(hi-lo)/length;if(fit>10)fit=10;if(fit<2)return 0;*step=fit;
 *x=dp_center?(lo+hi-(s32)(length*fit))/2:right?hi-(s32)(length*fit):lo;return 1;
}
static void fs_reset(void)
{
    for(u32 s=0;s<2;s++){
        fs_side[s].fast=fs_side[s].slow=fs_side[s].stamp=0;
        fs_side[s].shown=fs_side[s].late=0;LOOP_PAD();
    }
}
/* One owner for retry/options/start/abort/result-end, including the original
 * 205200 prologue. GAS restores its temporary gauge before native reset. */
void play_mods_reset(void){gsm_reset();fs_reset();
#ifndef FAST_SLOW_TEST
 bn_reset();
#endif
}
/* Empirical comparison with LDJ: bands0..3 FAST,4 PGREAT,5..8 SLOW;
 * only2/3 and5/6 increment totals. EMPRESS combines both GREAT directions
 * and both empty-POOR directions, so only those need its signed hit delta.
 * Other judgements already encode their direction: do not reinterpret their
 * native band using the sign (custom/GAMBOL windows need not be symmetric).
 * Non-timed GREAT retains the legacy late-band fallback; native manual key
 * and scratch calls both supply the real delta. Miss expiry14/15 is SLOW. */
static u32 fs_band(u32 event,s32 delta,u32 timed)
{
    switch(event){
    case 4:return 1;
    case 5:return 2;
    case 6:return timed && delta<=0?3:5;
    case 7:return 6;
    case 8:return 7;
    case 9:case 14:case 15:return 8;
    case 10:return 4;
    case 11:return timed && delta>0?8:0;
    default:return 12;
    }
}
static void fs_record(u32 event,u32 lane,u32 side,s32 delta,u32 timed)
{
    u32 p=fs_phase();
    if(!fast_slow_enabled || p<5 || p>=8 || side>1 || (fs_auto()&(1u<<side)))return;
    /* DP uses one native score/result; battle keeps each player independent. */
    u32 owner=fs_dp()?0:side;
    (void)lane;
    u32 band=fs_band(event,delta,timed);
    if(band==4){fs_side[owner].shown=0;return;}
    if(band>8)return;
    u32 late=band>=5;
    fs_side[owner].stamp=fs_frame();fs_side[owner].shown=1;fs_side[owner].late=late;
    /* Counter bands were empirically compared with LDJ independently of
     * timing-word visibility.
     * BAD and all empty/missed POOR bands display only, never count. */
    if(band==2 || band==3 || band==5 || band==6){
        if(late)fs_side[owner].slow++;else fs_side[owner].fast++;
    }
}
unsigned long long fs_timed(FS_ARGS)
{
    fs_record((u32)a&65535,(u32)b&65535,(u32)c,(s32)d,1);
    return fs_ring_body(a,b,0,0,0,0,0,0);
}
unsigned long long fs_ring(FS_ARGS)
{
    fs_record((u32)a&65535,(u32)b&65535,((u32)b>>15)&1,0,0);
    return fs_ring_body(FS_PASS);
}
static u32 fs_font_ready(void)
{
    /* Native unload/enqueue leaves stale pointers with the NEW id until the
     * unpack callback completes. Never interpret freed HUD sprites as glyphs. */
    if(fs_word(0x78af08)
#ifdef SPR_ASYNC
       && !sa_owned_queue()
#endif
    )return 0;
    u32 n=fs_word(0x2ad030);if(n>64)return 0;
    for(u32 i=0;i<n;i++){
        u32 *entry=(u32 *)(unsigned long)(0x34aa80+i*0x228);
        if(entry[0x84]==452 && entry[0] && entry[3]==414)return 1;
        LOOP_PAD();
    }
    return 0;
}
static u32 fs_result_font_ready(void)
{
    /* RESULT numbers may already be visible while later sound/title reads
     * remain queued. Only a pending FONT unpack makes this entry unsafe.
     * Include the in-flight head: it is retired AFTER its callback returns. */
    u32 count=fs_word(0x78af08),head=fs_word(0x78af00);
    if(count>72 || head>=72)return 0;
    u32 n=fs_word(0x2ad030);if(n>64)return 0;
    for(u32 i=0;i<n;i++){
        u32 address=0x34aa80+i*0x228;
        u32 *entry=(u32 *)(unsigned long)address;
        if(entry[0x84]!=452){LOOP_PAD();continue;}
        for(u32 q=0;q<count;q++){
            u32 request=0x78af0c+((head+q)%72)*28;
            if(fs_word(request)==0x1359d0 && fs_word(request+4)==address)return 0;
            LOOP_PAD();
        }
        return entry[0] && entry[3]==414;
    }
    return 0;
}
unsigned long long fs_input(FS_ARGS)
{
    unsigned long long r=fs_input_body(FS_PASS);
    /* Shortcut ownership moved to the song-select modal. Native polling and
     * all decoded history remain untouched in every scene. */
    return r;
}
static void fs_counter(u32 side,s32 x,s32 y,s32 dx,s32 dy)
{
    char line[8];u32 values[2]={fs_side[side].fast,fs_side[side].slow};
#if !defined(FAST_SLOW_TEST) || defined(FS_PREVIEW_TEST)
    if(PLAY_PREVIEW_ACTIVE()){values[0]=12;values[1]=8;}
#endif
    for(u32 k=0;k<2;k++){
        u32 v=values[k],place=10000,n=1,started=0;
        if(v>99999)v=99999;line[0]=k?'S':'F';
        for(u32 j=0;j<5;j++){
            u32 digit=v/place;v%=place;
            if(digit || started || j==4){line[n++]=(char)('0'+digit);started=1;}
            place/=10;LOOP_PAD();
        }
        line[n]=0;fs_text(line,k?4:1,x+(s32)k*dx,y+(s32)k*dy,100);LOOP_PAD();
    }
}
static void fs_word_position(u32 hud,u32 side,u32 type,s32 *x,s32 *y)
{
    s32 judge_y=fs_judge_y(hud,side);
    /*2815A0 origin -25 (+44 for wide): +97 reaches the native lane/
     * judgement center, exact in144px fields and rounded in233px fields. */
    *x=(s32)fs_word(hud+0x104+side*4)+77;
    *y=judge_y-13;
    if(type==0){
        return;
    }
    /* B/C lane-edge placement was empirically compared with LDJ. Use full
     * native field/glyph
     * rectangles instead of copying the retail Ghost anchors or the old
     * supplied patch's unconditional C:+10 horizontal offset.
     * Empirical LDJ comparison: judgement-height changes affect ALL types.
     * Retail B/C ghost numbers are fixed-height; only our timing words
     * follow the native judgement displacement from its default235. */
    static const short by[6]={250,230,250,250,230,230};
    static const short cy[6]={326,311,326,326,311,311};
    u32 layout=fs_word(hud);
    if(layout>=6)return; /* Safe judgement-relative fallback for unknown HUDs. */
    u32 step;fs_outside(hud,side,4,0,x,&step);
    *y=(type==1?by[layout]:cy[layout])-14+judge_y-235;
}
/* Only native1fb950's ghost text call is interposed. Native eligibility,
 * values, formatting, colour, alpha and nine draw arguments remain native. */
unsigned long long fs_ghost_text(FS_ARGS,unsigned long long ninth,unsigned long long side)
{
    u32 hud=fs_word(0x2ad5e8),mode=side<2?fs_player_ghost((u32)side):0;
#if !defined(FAST_SLOW_TEST) || defined(FS_GHOST_DRIVER_TEST)
    if(fs_ghost_side<2&&side!=fs_ghost_side)return 0;
#endif
    if(hud&&mode&&side<2){
        if(mode>1){s32 fx,fy;fs_word_position(hud,(u32)side,mode-1,&fx,&fy);d=(unsigned long long)(s32)(fy+27);}
        else d=(unsigned long long)((s32)d+fs_judge_y(hud,(u32)side)-*(short *)(unsigned long)(hud+0x10c));
        if(mode>1){
            const char *text=(const char *)(unsigned long)a;u32 n=0,step;while(n<16&&text[n]){n++;LOOP_PAD();}
            s32 x;if(n<16&&fs_outside(hud,(u32)side,n,0,&x,&step)){
                if(step<10)return fs_text_sized(text,(s32)b,x,(s32)d-6,(s32)e,step,(s32)f,(s32)g,(const void *)(unsigned long)h,(s32)ninth);
                c=(unsigned long long)(s32)(x+5);
            }
        }
        if(mode==1&&fast_slow_enabled&&fs_type[fs_dp()?0:(u32)side]==0){
            const char *text=(const char *)(unsigned long)a;u32 n=0;
            while(n<16&&text[n]){n++;LOOP_PAD();}
            if(n&&n<16){
                /* Empirical LDJ comparison: center the complete text pair.
                 * Native small text subtracts5 fromX. */
                s32 center=(s32)fs_word(hud+0x104+(u32)side*4)+97;
                s32 left=center-((s32)n*10+4+40)/2;
                c=(unsigned long long)(s32)(left+5);
                u32 owner=fs_dp()?0:(u32)side;
                if(!(fs_auto()&(1u<<side))&&(gsm_mask()&(1u<<side))&&fs_side[owner].shown&&(u32)(fs_frame()-fs_side[owner].stamp)<22&&fs_phase()>=5&&fs_phase()<8&&fs_font_ready())
                    fs_text(fs_side[owner].late?"SLOW":"FAST",fs_side[owner].late?4:1,left+(s32)n*10+4,fs_judge_y(hud,(u32)side)-13,100);
                fs_pair_drawn|=1u<<side;
            }
        }
    }
    return fs_native_small_text(a,b,c,d,e,f,g,h,ninth);
}
unsigned long long fs_play(FS_ARGS)
{
#ifndef FAST_SLOW_TEST
    if(fh_wait_field())return 0;
#endif
    unsigned long long r=fs_play_body(FS_PASS);
#ifndef FAST_SLOW_TEST
    bn_draw();
#ifdef PLAY_LIFT
    lift_draw();
#endif
#endif
    if(!fast_slow_enabled || !fs_font_ready())return r;
    u32 hud=fs_word(0x2ad5e8),p=fs_phase();
    if(!hud || p<5 || p>=8)return r;
    u32 is_dp=fs_dp();
    for(u32 s=0;s<2;s++){
        if(fs_auto()&(1u<<s)){LOOP_PAD();continue;}
        if(!(gsm_mask()&(1u<<s)) || (is_dp && s)){LOOP_PAD();continue;}
        u32 type=fs_type[s];
        if(type<3 && !(type==0&&fs_player_ghost(s)==1) && fs_side[s].shown && (u32)(fs_frame()-fs_side[s].stamp)<22){
            s32 gap=(s32)fs_word(hud+0x108)-(s32)fs_word(hud+0x104);
            u32 short_word=gap>=100 && gap<400 && (type==2 || (type==1 && fs_word(hud)==4));
            const char *word=fs_side[s].late?(short_word?"SL":"SLOW"):(short_word?"FA":"FAST");
            s32 x,y;fs_word_position(hud,s,type,&x,&y);
            fs_text(word,fs_side[s].late?4:1,x,y,100);
        }
        LOOP_PAD();
    }
    return r;
}
/* Ghost may be suppressed by native target/AUTO/cover gates. The deferred
 * A word then falls back to its centered solo position in the same draw. */
static void fs_pair_fallback(void){
 if(!fast_slow_enabled||!fs_font_ready()||fs_phase()<5||fs_phase()>=8)return;
 u32 hud=fs_word(0x2ad5e8);if(!hud)return;
 for(u32 s=0;s<2;s++){LOOP_PAD();if((fs_pair_drawn&(1u<<s))||fs_type[s]||fs_player_ghost(s)!=1||(fs_auto()&(1u<<s))||!(gsm_mask()&(1u<<s))||(fs_dp()&&s)||!fs_side[s].shown||(u32)(fs_frame()-fs_side[s].stamp)>=22)continue;
  s32 x,y;fs_word_position(hud,s,0,&x,&y);fs_text(fs_side[s].late?"SLOW":"FAST",fs_side[s].late?4:1,x,y,100);
 }
}
static u32 fs_counts_ready(u32 side)
{
    u32 p=fs_phase();
    return fast_slow_enabled && fs_counters_enabled && side<2 && fs_counts_on(side) && p>=5 && p<8 &&
        (!(fs_auto()&(1u<<side))
#if !defined(FAST_SLOW_TEST) || defined(FS_PREVIEW_TEST)
        || PLAY_PREVIEW_ACTIVE()
#endif
        ) && fs_font_ready();
}
/* These callbacks are reached only when the NATIVE judge breakdown is drawn.
 * Keep its display-option/mode/side gating, and use its actual x/y rather
 * than the independently movable judgement-word HUD origin. */
unsigned long long fs_counts_single(FS_ARGS,unsigned long long ninth,unsigned long long side)
{
    u32 draw=fs_counts_ready((u32)side);
    unsigned long long r=fs_native_small_text(a,b,c,d+(draw?4:0),e,f,g,h,ninth);
    if(draw){
        s32 native_layout=fs_layout(),width=(native_layout==1 || native_layout==4 || native_layout==5)?50:90;
        /* Reference mod: F/S at layout y+412, CBRK y+426 ->430.
         * Layout5's native CBRK has an additional nine-pixel offset. */
        fs_counter((u32)side,(s32)c+15-(90-width)/2,(s32)d-14-(native_layout==5?9:0),(width+50)/2,0);
    }
    return r;
}
unsigned long long fs_counts_battle(FS_ARGS,unsigned long long ninth,unsigned long long side)
{
    unsigned long long r=fs_native_small_text(a,b,c,d,e,f,g,h,ninth);
    (void)side;
    for(u32 s=0;s<2;s++){
        /* Native battle has two number columns and no CBRK row. Stack F/S
         * beneath their corresponding count column instead of the playfield. */
        if((gsm_mask()&(1u<<s)) && fs_counts_ready(s))
            fs_counter(s,(s32)c-5+(s32)s*120,(s32)d+6,0,12);
        LOOP_PAD();
    }
    return r;
}
unsigned long long fs_result(FS_ARGS)
{
    unsigned long long r=fs_result_body(FS_PASS);
    /* 17c570 supplies the real scene and native side. Its >0 reveal state is
     * exactly the branch which calls the five judgement-number renderer.
     * Keep the counters behind the native dimmer/continue panel (states2..5).
     * Suppress only the leaving-result fade/black states6/7 and scene exit. */
    if(!fast_slow_enabled || !fs_counters_enabled || (u32)b>1 || !fs_counts_on((u32)b) || !a)return r;
    u32 result=fs_word(0x2ad310),side=(u32)b;
    if(!result || fs_word((u32)a+0x2c)!=3 || fs_word(result+0x13c4)>5 ||
       (s32)fs_word(result+0x88+side*4)<=0 || !fs_result_font_ready())return r;
    fs_counter(side,106+(s32)side*360,443,70,0);
    return r;
}
