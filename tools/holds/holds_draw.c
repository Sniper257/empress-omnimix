/* Hold graphics for EMPRESS (HOLDS runtime). Included after holds_empress.c (same TU: holds, chart_defs).
 *
 * Called from songdb.S note_hook for every note the drawer FUN_001e6f50 is about to draw
 * (jal 0x12fdd0 at 0x1e757c). For a hold's head or tail note it draws the cap and the body itself
 * and returns 1 (the native draw is skipped); for any other note it returns 0.
 *
 * Sprites: tools/holds/hold_sprites.py, 36 SPR1 sprites in catalogue order, appended by the launcher
 * to the note containers #109 (SP) and #146 (wide) through song.txt `S` lines
 * (tools/release/arcade_songfolder.py). All 36 lines of a container write their handle to the same
 * record field, so the field ends up holding the last one: SP at record +0x38, wide at +0x3c (the
 * tail of the 64-byte title field; titles stop at 0x37). Sprite k = last - 35 + k.
 *
 * Geometry: the body is drawn from the tail cap down to the head cap, or to the judge line once the
 * head has been hit; while the tail is still above the screen, from the head up to the lane top.
 * The body is one tile stretched with zoomY (pivot at its top). */

#define HD_SPRITES 36
#define HD_JUDGE_Y 0x140      /* a note's y on the judge line (FUN_001e6f50 clamps below 0x13f + h/2) */
#define HD_TOP_Y (-16)
#define HD_LAYER 0x10         /* the native note layer */
#define HD_REC_SP 0x38
#define HD_REC_WIDE 0x3c

/* catalogue order (hold_sprites.catalogue): first sprite of each group, states per body */
enum { HD_CN_WHITE = 0, HD_CN_BLACK = 5, HD_HCN_WHITE = 10, HD_HCN_BLACK = 17, HD_BSS = 24, HD_HBSS = 29 };
/* sprite sizes in frame rows (hold_sprites.py): white, black, turntable; caps and body tiles */
static const hs8 hd_w[3] = {16, 12, 28}, hd_cap_h[3] = {10, 9, 10}, hd_body_h[3] = {10, 9, 10};

#ifdef HOLDS_TEST
extern void test_draw(hu32 handle, hs32 x, hs32 y, hs32 zy, hs32 py);
extern hu8 *test_record(void);
#define HD_DRAW(h, x, y, zy, px, py, clip) ((void)(px), (void)(clip), test_draw((h), (x), (y), (zy), (py)))
#define HD_RECORD() test_record()
#define HD_LIFT(side) 0
#else
#define HD_NATIVE_DRAW ((void (*)(hu32, hs32, hs32, hs32, hs32, hs32, hs32, hs32, hs32, hs32, hu32, hs32))0x12fdd0)
#define HD_DRAW(h, x, y, zy, px, py, clip) HD_NATIVE_DRAW((h), (x), (y), 100, (zy), (px), (py), 100, 0, 0, (clip), HD_LAYER)
#define HD_RECORD() ((hu8 *(*)(hu8 *, hs32))0x1e19a0)(0, ((hs32 (*)(void))0x205e00)())
#ifdef PLAY_LIFT
extern hu32 lift_y[2];
#define HD_LIFT(side) ((hs32)lift_y[(side) & 1])
#else
#define HD_LIFT(side) 0
#endif
#endif

static hs32 hd_head_y[HOLDS_LANES], hd_head_at[HOLDS_LANES], hd_tail_at[HOLDS_LANES], hd_x[HOLDS_LANES];
static hu32 hd_clip[2], hd_cont[2];

/* a new chart: forget where notes were */
static void hd_reset(void)
{
    hu32 i;
    for (i = 0; i < HOLDS_LANES; i++) { hd_head_at[i] = hd_tail_at[i] = -100; hd_x[i] = -1; LOOP_PAD(); }
    hd_cont[0] = hd_cont[1] = 0;
}

/* hold index whose head (tail = 0) or tail (tail = 1) is this note, or -1 */
static hs32 hd_find(hu32 lane, hs32 t, int *tail)
{
    hu32 i;
    for (i = 0; i < chart_n; i++) {
        if (chart_defs[i].lane == lane) {
            if (chart_defs[i].start == t) { *tail = 0; return (hs32)i; }
            if (chart_defs[i].end == t) { *tail = 1; return (hs32)i; }
        }
        LOOP_PAD();
    }
    return -1;
}

static hu32 hd_group(const hold_def *d, hu32 lane, hu32 *kind)
{
    hu32 key = lane & 7;
    *kind = key == 7 ? 2u : (key & 1u);                       /* lanes 1,3,5,7 white (0,2,4,6), 2,4,6 blue */
    if (d->kind == HOLD_BSS) return HD_BSS;
    if (d->kind == HOLD_HBSS) return HD_HBSS;
    if (d->kind == HOLD_HCN) return *kind ? HD_HCN_BLACK : HD_HCN_WHITE;
    return *kind ? HD_CN_BLACK : HD_CN_WHITE;
}

/* body sprite: 0 not held, 1/2 held (alternating), 3/4 HCN let go (alternating) */
static hu32 hd_state(hu32 i, hu32 hell)
{
    hu32 blink = ((hu32)hd_now >> 1) & 1u;
    switch (holds.state[i]) {
    case HS_HOLDING: return 1 + blink;
    case HS_LOOSE: return hell ? 3 + blink : 0;
    default: return 0;
    }
}

/* sprite handles for the container a side draws from, 0 if the song has no hold sprites */
static hu32 hd_last(hu32 cont)
{
    hu8 *rec = HD_RECORD();
    hu32 last;
    if (!rec || (cont != 109 && cont != 146)) return 0;
    holds_dbg[6] = *(const hu32 *)(rec + HD_REC_SP);
    holds_dbg[7] = *(const hu32 *)(rec + HD_REC_WIDE);
    last = *(const hu32 *)(rec + (cont == 109 ? HD_REC_SP : HD_REC_WIDE));
    return last >> 16 == cont && (last & 0xffffu) >= HD_SPRITES - 1 ? last : 0;
}

static void hd_body(hu32 base, hu32 kind, hs32 x, hs32 top, hs32 bottom, hu32 clip)
{
    hs32 len = bottom - top, h = hd_body_h[kind];
    if (len <= 0) return;
    holds_dbg[10]++;
    HD_DRAW(base, x, top, len * 100 / h, hd_w[kind] / 2, 0, clip);
}

/* returns 1 if the note was drawn here (skip the native draw) */
int holds_note_hook(hu8 *note, hu32 handle, hs32 x, hs32 y, hu32 side, hu32 clip)
{
    const hold_def *d;
    hu32 last, base, group, kind, lane, cont = handle >> 16, body;
    hs32 i, t, judge;
    int tail = 0;
    holds_dbg[3]++;
    holds_dbg[11] = cont;
    if (!armed || note[9] > 7 || side > 1) return 0;
    lane = side * 8u + note[9];
    t = *(const short *)(note + 0x18);
    if ((i = hd_find(lane, t, &tail)) < 0) return 0;
    holds_dbg[4]++;
    if (!(last = hd_last(cont))) { holds_dbg[5]++; return 0; }   /* no hold sprites: native notes */
    d = &chart_defs[i];
    group = hd_group(d, lane, &kind);
    base = last - (HD_SPRITES - 1) + group;
    body = base + 2 + hd_state((hu32)i, d->kind == HOLD_HCN || d->kind == HOLD_HBSS);
    judge = HD_JUDGE_Y - HD_LIFT(side);
    hd_x[lane] = x;
    hd_clip[side] = clip;
    hd_cont[side] = cont;
    if (!tail) {
        hd_head_y[lane] = y;
        hd_head_at[lane] = hd_now;
        if (hd_tail_at[lane] < hd_now - 1)                     /* tail still above the screen */
            hd_body(body, kind, x, HD_TOP_Y, y, clip);
        HD_DRAW(base, x, y, 100, hd_w[kind] / 2, hd_cap_h[kind] / 2, clip);      /* start cap */
    } else {
        int head_seen = hd_head_at[lane] >= hd_now - 1;
        hs32 bottom = head_seen ? hd_head_y[lane] : judge;
        hd_tail_at[lane] = hd_now;
        if (bottom > judge) bottom = judge;
        hd_body(body, kind, x, y, bottom, clip);
        if (!head_seen && holds.state[i] != HS_PENDING && y < judge)
            HD_DRAW(base, x, judge, 100, hd_w[kind] / 2, hd_cap_h[kind] / 2, clip);   /* head hit: start cap on the line */
        HD_DRAW(base + 1, x, y, 100, hd_w[kind] / 2, hd_cap_h[kind] / 2, clip);       /* end cap */
    }
    return 1;
}

/* after the native tick: holds whose head has been hit and whose tail is still above the screen
 * have no note on screen, so their body (lane top to judge line) and start cap are drawn here */
static void hd_frame(void)
{
    hu32 lane, last, kind, group, side;
    hs32 i, judge;
    for (lane = 0; lane < HOLDS_LANES; lane++) {
        LOOP_PAD();
        i = holds.active[lane];
        side = lane >> 3;
        if (i < 0 || hd_x[lane] < 0 || !hd_cont[side]) continue;
        if (holds.state[i] != HS_HOLDING && holds.state[i] != HS_LOOSE) continue;
        if (hd_head_at[lane] >= hd_now || hd_tail_at[lane] >= hd_now) continue;   /* drawn with its notes */
        if (!(last = hd_last(hd_cont[side]))) continue;
        group = hd_group(&chart_defs[i], lane, &kind);
        judge = HD_JUDGE_Y - HD_LIFT(side);
        hd_body(last - (HD_SPRITES - 1) + group + 2 + hd_state((hu32)i, chart_defs[i].kind == HOLD_HCN || chart_defs[i].kind == HOLD_HBSS),
                kind, hd_x[lane], HD_TOP_Y, judge, hd_clip[side]);
        HD_DRAW(last - (HD_SPRITES - 1) + group, hd_x[lane], judge, 100, hd_w[kind] / 2, hd_cap_h[kind] / 2, hd_clip[side]);
    }
}
