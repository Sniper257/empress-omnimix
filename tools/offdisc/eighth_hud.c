/* CS8 theme assets + original gauge/number layout; gameplay stays EMPRESS.
 * Included by songdb.c. The launcher skin options and modern additions remain
 * separate. Only the imported P1 wide-SP frame is affected. */
#include "eighth_hud_assets.h"
#ifndef E8_TEST
#define E8_PLAY (*(u8 **)0x2ad5e8)
#define E8_ENTRIES ((u8 *)0x34aa80)
#define E8_COUNT (*(s32 *)0x2ad030)
#define E8_SCORE() (((s32 (*)(s32))0x204180)(0))
#define E8_BPM() (((s32 (*)(void))0x1ea850)())
#define E8_GAUGE_VALUE() (((s32 (*)(s32))0x204cf0)(0))
#define E8_HARD() (((s32 (*)(s32))0x205b10)(0))
#define E8_DIFFICULTY() (((s32 (*)(void))0x205de0)())
#define E8_EFFECTOR() (((s32 (*)(u32))0x16a5e0)(((u32 (*)(void))0x1686e0)()))
#define E8_FRAME() I32((u8 *)0x2ad470)
#define E8_END() (((s32 (*)(void))0x1ebd90)())
#define E8_SCRATCH() (((s32 (*)(s32))0x13acd0)(0))
#define E8_JUDGEMENT(n) (((s32 (*)(s32,s32))0x204e20)(0,n))
#define E8_GAME_MODE() (((s32 (*)(void))0x1f5830)())
#define E8_STAGE() (((s32 (*)(void))0x205dc0)())
#define E8_VERSION() (rec_of(0,((s32 (*)(void))0x205e00)())[0x54])
#define E8_TICK() I32(E8_PLAY+0x23c)
#define E8_DRAW ((void (*)(u32,s32,s32,s32,s32,s32,s32,s32,s32,s32,u32,s32))0x12fdd0)
#define E8_BLIT ((void (*)(u32,s32,s32,s32,s32,s32,s32,s32,s32,s32,s32,s32,s32,s32,u32,s32))0x12fc30)
#endif

static s32 eighth_active(void)
{
    u8 *p=E8_PLAY;
    return skin.frame_pack==2 && p && I32(p)==2;
}

/* tint_hook dispatches both animation and direct sprites here, after checking
 * the source container. Other layouts and selected note sprites fall through. */
void eighth_sprite_draw(u8 *entry,u16 *rect,u8 *params)
{
    s32 old,cont,i,target=-1;
    u8 *dst=0;
    u16 *r;
    u8 local[64];
    if (!eighth_active()) { tint_body(entry,rect,params);return; }
    cont=I32(entry+0x210);
    old=(s32)((u8 *)rect-*(u8 **)(entry+0x214))/8;
    for (i=0;i<(s32)(sizeof e8_remap/sizeof e8_remap[0]);i++) {
        if (e8_remap[i][0]==cont && e8_remap[i][1]==old) { target=e8_remap[i][2];break; }
        LOOP_PAD();
    }
    if (target<0) { tint_body(entry,rect,params);return; }
    /* Find the currently loaded frame every time: the engine reuses heap
     * addresses after a song, so caching its former instance is unsafe. */
    for (i=0;i<E8_COUNT;i++) {
        u8 *e=E8_ENTRIES+i*0x228;
        if (I32(e+0x210)==69) { dst=e;break; }
        LOOP_PAD();
    }
    if (!dst) { tint_body(entry,rect,params);return; }
    r=*(u16 **)(dst+0x214)+target*4;
    memcpy_(local,params,sizeof local);
    if (rect[2]) I32(local+8)=I32(local+8)*r[2]/rect[2];
    if (rect[3]) I32(local+12)=I32(local+12)*r[3]/rect[3];
    if (cont==146 && old==600) {
        I32(local+4)=315;I32(local+20)=100;
    }
    tint_body(dst,r,local);
}

static void eighth_number(s32 value,s32 count,s32 x,s32 y,s32 percent)
{
    s32 divisor=1,i,started=0;
    if (value<0)value=0;
    for (i=1;i<count;i++) { divisor*=10;LOOP_PAD(); }
    if (value>=divisor*10)value=divisor*10-1;
    for (i=0;i<count;i++) {
        s32 digit=value/divisor;
        const short *r=e8_digits[digit];
        value%=divisor;
        if (digit || started || i==count-1) {
            E8_DRAW(0x450000u+r[0],x+r[1]+(percent?(i==0?2:i==1?-7:-8):0),y+r[2],100,100,0,0,100,0,0,0,27);
            started=1;
        }
        x+=32;divisor/=10;LOOP_PAD();
    }
}

static void eighth_flash(s32 maximum,s32 tick)
{
    s32 i,n;
    const short (*rows)[5];
    if (maximum) {
        rows=e8_max;n=sizeof e8_max/sizeof e8_max[0];tick%=E8_MAX_PERIOD;
    } else {
        rows=e8_rise;n=sizeof e8_rise/sizeof e8_rise[0];tick%=E8_RISE_PERIOD;
    }
    for (i=0;i<n;i++) {
        const short *r=rows[i];
        if (tick>=r[3] && tick<r[4])
            E8_DRAW(0x450000u+r[0],r[1],r[2],100,100,0,0,100,0,0,0,21);
        LOOP_PAD();
    }
}

/* CS8 1557f0/155750: the dim/lit boundary makes a bounded random walk
 * in the last four cells, with one bright cell at the actual gauge value.
 * Keep this visual RNG private so it cannot change gameplay/random songs. */
static u32 eighth_rng=1;
static s32 eighth_random(s32 n)
{
    eighth_rng=(eighth_rng*37u+1)&0xfffffu;
    return ((u32)n*(eighth_rng>>4))>>16;
}
static s32 eighth_gauge_edge(s32 cells,s32 tick,s32 reset,s32 good,s32 bad)
{
    static s32 actual,target,display,last_tick=-1,last_good,last_bad;
    if (reset || tick<last_tick) {
        actual=target=display=cells;last_tick=-1;eighth_rng=1;
        last_good=good;last_bad=bad;
    }
    if (tick==last_tick)return display;
    if(bad!=last_bad) {
        target-=eighth_random(5)+6;
        if(target<0)target=0;
        if(target>cells)target=cells;
        display=target;
    } else if(good!=last_good || cells!=actual) target=display=cells;
    actual=cells;last_good=good;last_bad=bad;
    if (display==target && actual) {
        target+=eighth_random(3)-1;
        if(target>actual)target=actual-eighth_random(1);
        else if(target<actual-4)target=actual-4+eighth_random(1);
        if(target<1)target=eighth_random(1)+1;
    }
    if(display<target)display++;
    else if(display>target)display--;
    last_tick=tick;return display;
}

static void eighth_stage(void)
{
    s32 mode=E8_GAME_MODE(),index=E8_STAGE(),which;
    const short *r;
    if(mode==5)which=6;                         /* TRAINING */
    else if(mode>=4)which=7;                    /* FREE / demo modes */
    else if(index>=6)which=5;                   /* EXTRA */
    else if(index>=4)which=4;                   /* FINAL */
    else which=index<0?0:index;                 /* 1st..4th */
    r=e8_stages[E8_VERSION()>=8][which];
    E8_DRAW(0x450000u+r[0],r[1],r[2],100,100,0,0,100,0,0,0,27);
}

/* A single callback replaces the former cluster of EMPRESS numeric/gauge/
 * effector callbacks, including the blue status flare that obscured CS8 art. */
s32 eighth_hud(u8 *param)
{
    static u8 *previous_play;
    static s32 previous_tick,previous_gauge,rise_tick=-1;
    s32 gauge,mode,fx,width,hard,tick,frame,end,progress,reset,edge;
    const short *r;
    (void)param;
    if (!eighth_active())return 0;
    gauge=E8_GAUGE_VALUE();
    if (gauge<0)gauge=0;
    if (gauge>100)gauge=100;
    tick=E8_TICK();
    reset=previous_play!=E8_PLAY || tick<previous_tick;
    if (reset) {
        rise_tick=-1;previous_gauge=gauge;previous_play=E8_PLAY;
    }
    if (tick>0 && gauge>previous_gauge)rise_tick=tick;
    previous_tick=tick;previous_gauge=gauge;
    hard=E8_HARD();
    edge=eighth_gauge_edge(gauge/2,tick,reset,E8_JUDGEMENT(0)+E8_JUDGEMENT(1)+E8_JUDGEMENT(2),E8_JUDGEMENT(3)+E8_JUDGEMENT(4));
    width=(hard?gauge/2:(edge>0?edge-1:0))*7;
    if (!hard && gauge>=2) E8_BLIT(0x450000u+E8_GAUGE_EDGE,39+(gauge/2-1)*7,390,100,100,0,0,(gauge/2-1)*7,0,7,22,100,0,0,0,27);
    if (width>0) E8_BLIT(0x450000u+(hard?E8_HARD_GAUGE:E8_GAUGE),39,390,100,100,0,0,0,0,width,22,100,0,0,0,27);
    /* Layers run back to front; newest packet within a layer runs FIRST.
     * Queue the opaque dim backing LAST so it cannot cover the lit cells. */
    E8_BLIT(0x450000u+(hard?E8_HARD_DIM:E8_GAUGE_DIM),39,390,100,100,0,0,0,0,350,22,100,0,0,0,27);
    frame=E8_FRAME();end=E8_END();
    if (end>0) {
        if (frame<0)frame=0;
        if (frame>end)frame=end;
        progress=frame*326/end;
        E8_DRAW(0x450000u+E8_PROGRESS,39+progress,386,100,100,0,0,100,0,0,0,24);
    }
    if (gauge==100)eighth_flash(1,tick);
    else if (hard && rise_tick>=0 && tick-rise_tick<60)eighth_flash(0,tick-rise_tick);
    eighth_stage();
    eighth_number(E8_SCORE(),6,115,424,0);
    eighth_number(E8_BPM(),3,115,446,0);
    eighth_number(gauge,3,272,348,1);
    if (E8_SCRATCH())
        E8_DRAW(0x450000u+E8_SCRATCH_RING,42,332,100,100,0,0,100,0,0,0,13);
    mode=E8_DIFFICULTY();if ((u32)mode>4)mode=0;
    r=e8_mode[mode];E8_DRAW(0x450000u+r[0],r[1],r[2],100,100,0,0,100,0,0,0,27);
    fx=E8_EFFECTOR();
    if (fx>0 && fx<7) {
        r=e8_fx[fx];E8_DRAW(0x450000u+r[0],r[1],r[2],100,100,0,0,100,0,0,0,27);
    }
    return 0;
}
