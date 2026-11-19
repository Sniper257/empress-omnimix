/* Native EMPRESS gauge auto shift. Inspired by aixxe/2dx-gsm (MIT;
 * licenses/2dx-gsm.txt). Options in the user's save are never modified.
 * Only FREE mode is eligible; course, STANDARD and training gauges are native.
 * Compile with GSM_TEST to supply native entry points from the host fixture. */
#define GSM_SAMPLES 0x480
#ifndef GSM_TEST
#include "menu_settings.h"
extern struct mm_control menu_control;
#define gsm_player(s) mm_gas(&menu_control.options,((u32 (*)(void))0x205d50)()?0:(s))
#define gsm_native(s) ((s32 *)(0x7a3420 + (s)*0x40))
#define gsm_opt(s) ((s32 *)(0x7a32a0 + (s)*0x2c))
#define gsm_mode() (((s32 (*)(void))0x1f5830)())
#define gsm_mask() (((u32 (*)(void))0x1f7070)())
#define gsm_death (*(volatile u32 *)0x2ad528)
#define gsm_count (*(volatile u16 *)0x2ad634)
#define gsm_calc ((void (*)(s32 *,s32,long long))0x22fa20)
#define gsm_coeff ((void (*)(s32 *))0x230080)
#define gsm_apply ((void (*)(s32 *,s32))0x22f830)
#define gsm_native_sample ((s32 (*)(s32,s32))0x204920)
#define gsm_native_score_sample ((s32 (*)(s32,s32))0x2048b0)
#define gsm_native_rate_sample ((s32 (*)(s32,s32))0x204830)
#define gsm_select ((s32 (*)(s32))0x13ad30)
#define gsm_rect ((void (*)(s32,s32,s32,s32,u32,s32))0x12ff00)
#endif
#ifndef gsm_player
#define gsm_player(s) 1u
#endif
u32 gauge_shift_enabled;
static u32 gsm_active;
static s32 gsm_start[2][16], gsm_gauge[2][3][16];
static s32 gsm_options[2][2], gsm_best[2], gsm_view[2];
static u16 gsm_graph[2][3][GSM_SAMPLES];
static u32 gsm_n, gsm_seen[2], gsm_held[2];

static void gsm_copy(s32 *dst, const s32 *src)
{
    s32 i;
    for (i=0;i<16;i++) { dst[i]=src[i]; LOOP_PAD(); }
}
/* The native cache stores EASY and HARD separately; HARD is 1 (2=HAZARD). */
static void gsm_publish(s32 side)
{
    s32 k=gsm_best[side];
    gsm_copy(gsm_native(side),gsm_gauge[side][k]);
    gsm_opt(side)[7]=k==1;
    gsm_opt(side)[8]=k==2;
}
void gsm_reset(void)
{
    s32 s;
    for (s=0;s<2;s++) {
        if (gsm_active & (1u<<s)) {
            gsm_copy(gsm_native(s),gsm_start[s]);
            gsm_opt(s)[7]=gsm_options[s][0];
            gsm_opt(s)[8]=gsm_options[s][1];
        }
        gsm_seen[s]=gsm_held[s]=0;
        LOOP_PAD();
    }
    gsm_active=0; gsm_n=0;
}
/* Called after the original gain calculator. Its note counts already combine
 * both lanes in DP and already reflect battle/P2 style routing. */
void gsm_init(s32 n0,s32 n1)
{
    s32 s,k,j,notes,kind;
    if (!gauge_shift_enabled || gsm_mode()!=4) return;
    for (s=0;s<2;s++) {
        kind=gsm_native(s)[2]; notes=s ? n1:n0;
        if (!gsm_player(s) || !(gsm_mask() & (1u<<s)) || kind<0 || kind>2 || !notes || gsm_opt(s)[8]==2) continue;
        gsm_copy(gsm_start[s],gsm_native(s));
        gsm_options[s][0]=gsm_opt(s)[7]; gsm_options[s][1]=gsm_opt(s)[8];
        for (k=0;k<3;k++) {
            s32 *g=gsm_gauge[s][k];
            gsm_copy(g,gsm_native(s)); g[2]=k;
            g[3]=g[4]=k==2 ? 5000:1100;
            gsm_coeff(g);
            for (j=0;j<5;j++) { gsm_calc(g,j,(long long)notes); LOOP_PAD(); }
            LOOP_PAD();
        }
        gsm_best[s]=gsm_view[s]=kind;
        gsm_active|=1u<<s;
        gsm_publish(s);
        LOOP_PAD();
    }
}
static s32 gsm_choose(s32 s)
{
    s32 start=gsm_start[s][2];
    if (start==2 && gsm_gauge[s][2][4]>0) return 2;
    if (start!=1 && gsm_gauge[s][0][4]>=4000) return 0;
    return 1;
}
void gsm_update(s32 side,s32 judge)
{
    s32 k,old_easy,old_hard;
    u32 death;
    if ((u32)side>1 || !(gsm_active & (1u<<side))) { gsm_apply(gsm_native(side),judge); return; }
    old_easy=gsm_opt(side)[7]; old_hard=gsm_opt(side)[8]; death=gsm_death;
    for (k=0;k<3;k++) {
        /* Reuse the original updater, including HARD's <30% damage relief and
         * irreversible zero. Isolate the virtual death mask from stage flow. */
        gsm_opt(side)[7]=k==1; gsm_opt(side)[8]=k==2;
        gsm_apply(gsm_gauge[side][k],judge);
        gsm_death=death;
        LOOP_PAD();
    }
    gsm_opt(side)[7]=old_easy; gsm_opt(side)[8]=old_hard;
    gsm_best[side]=gsm_choose(side); gsm_publish(side);
}
/* Getter overrides describe the EARNED gauge, independent of result browsing. */
s32 gsm_hard(s32 s) { return (u32)s<2 && (gsm_active & (1u<<s)) ? gsm_best[s]==2:-1; }
s32 gsm_easy(s32 s) { return (u32)s<2 && (gsm_active & (1u<<s)) ? gsm_best[s]==1:-1; }
void gsm_history(void)
{
    s32 s,k;
    u32 n=gsm_count;
    if (!gsm_active || n<=gsm_n || n>GSM_SAMPLES) return;
    /* Native history advances only on a 32-frame boundary. Never fabricate a
     * new sample for repeated draws, pauses, or a full native buffer. */
    for (s=0;s<2;s++) {
        for (k=0;k<3;k++) { gsm_graph[s][k][n-1]=(u16)gsm_gauge[s][k][4]; LOOP_PAD(); }
        LOOP_PAD();
    }
    gsm_n=n;
}
/* Native result records have 384 slots, while play histories hold 1152.
 * This boundary applies with GAS off too: sample384 otherwise overwrites the
 * destination's own count, and later writes escape the result object. Reduce
 * ALL seven curves at the same indices, preserving both ends. Native PLAY
 * sampling, stored scores and the full private GAS histories are unchanged. */
s32 gsm_result_count(void)
{
    u32 n=gsm_count;
    return n>384 ? 384:n;
}
static s32 gsm_result_index(s32 idx)
{
    u32 n=gsm_count;
    return n>384 ? (s32)((n-1)*(u32)idx/383):idx;
}
s32 gsm_result_sample(s32 s,s32 idx)
{
    s32 at=gsm_result_index(idx);
    if ((u32)s<2 && (gsm_active & (1u<<s)) && gsm_n) {
        if ((u32)at>=gsm_n) at=gsm_n-1;
        return gsm_graph[s][gsm_best[s]][at];
    }
    return gsm_native_sample(s,at);
}
s32 gsm_result_score_sample(s32 s,s32 idx)
{
    return gsm_native_score_sample(s,gsm_result_index(idx));
}
s32 gsm_result_rate_sample(s32 s,s32 idx)
{
    return gsm_native_rate_sample(s,gsm_result_index(idx));
}
void gsm_result_input(s32 s)
{
    u32 held;
    if ((u32)s>1 || !(gsm_active & (1u<<s))) return;
    held=gsm_select(s)!=0;
    if (!gsm_seen[s]) { gsm_view[s]=gsm_best[s]; gsm_seen[s]=1; }
    else if (held && !gsm_held[s]) gsm_view[s]=(gsm_view[s]+1)%3;
    gsm_held[s]=held;
}
/* Only the native result BACKGROUND draw consults the browsed gauge.
 * Gameplay, result lamps and records keep using gsm_best through the getters. */
s32 gsm_view_hard(s32 s)
{
    return (u32)s<2 && (gsm_active & (1u<<s)) ? gsm_view[s]==2:-1;
}
/* Return 1 when handled, 0 for untouched native/course/replay graphs. */
s32 gsm_segment(s32 s,s32 idx)
{
    s32 a,b,k,x,y1,y2,t;
    if ((u32)s>1 || !(gsm_active & (1u<<s))) return 0;
    if (!gsm_n || (u32)idx>=215) return 1;
    k=gsm_view[s];
    a=gsm_n*idx/215; b=gsm_n*(idx+1)/215;
    if (a>=(s32)gsm_n) a=gsm_n-1;
    if (b>=(s32)gsm_n) b=gsm_n-1;
    a=gsm_graph[s][k][a]/100; b=gsm_graph[s][k][b]/100;
    if (a<1) a=1;
    if (b<1) b=1;
    y1=199-a*170/50; y2=199-b*170/50;
    if (y2<y1) {t=y1;y1=y2;y2=t;}
    x=s*360+33+idx;
    if (k==2 || (y1<64 && y2<64)) gsm_rect(x,y1,x+1,y2+3,0xff4000,16);
    else if (y1<63) {
        gsm_rect(x,y1,x+1,63,0xff6000,16);
        gsm_rect(x,63,x+1,y2+3,0xff00,16);
    } else gsm_rect(x,y1,x+1,y2+3,0xff00,16);
    return 1;
}
