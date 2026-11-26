/* Hold graphics for EMPRESS (HOLDS runtime). Included after holds_empress.c (same TU: holds, chart_defs).
 *
 * Called from songdb.S note_hook for every note the drawer FUN_001e6f50 is about to draw
 * (jal 0x12fdd0 at 0x1e757c). For a hold's head or tail note it draws the cap and the body itself
 * and returns 1 (the native draw is skipped); for any other note it returns 0.
 *
 * Sprites: tools/holds/hold_sprites.py, 37 SPR1 sprites in catalogue order, appended by the launcher
 * to the note containers #109 (SP) and #146 (wide) through song.txt `S` lines
 * (tools/release/arcade_songfolder.py). All 36 lines of a container write their handle to the same
 * record field, so the field ends up holding the last one: SP at record +0x38, wide at +0x3c (the
 * tail of the 64-byte title field; titles stop at 0x37). Sprite k = last - 36 + k.
 *
 * Geometry: caps are placed like the game's own notes (songdb.S note_hook): pivot at the bottom,
 * at y + 3, so a cap sits on top of its timing point and lines up with a normal note of the same
 * time. The end cap hangs below the tail's timing point instead (pivot at its top), as in the
 * arcade, where a hold starting on the tick another ends sits on top of that end cap's line.
 * The body runs from the bottom of the end cap down to the top of the start cap, or of the cap
 * on the judge line once the head has been hit; while the tail is still above the screen, from
 * the lane top. The body is one tile stretched with zoomY (pivot at its top). */
#define HD_BASE(y) ((y) + 3)    /* a note's bottom edge (note_hook: pivot at the bottom, y + 3) */

#define HD_SPRITES 37
#define HD_JUDGE_Y 0x140      /* a note's y on the judge line (FUN_001e6f50 clamps below 0x13f + h/2) */
#define HD_TOP_Y (-16)
/* Draw layers run back to front with LOWER numbers in FRONT (the lane cover is drawn on 0xc, notes and
 * measure lines on 0x10), and within a layer the newest packet is drawn first, i.e. underneath
 * (eighth_hud.c). On the note layer the measure lines covered the bodies, and each cap, queued after
 * its body, went under it: the body hid the cap's inner half (silver ring and dark middle). Caps go
 * on 0xe, bodies on 0xf: both in front of notes and bars, caps over bodies, both behind the cover. */
#define HD_LAYER_CAP 0xe
#define HD_LAYER_BODY 0xf
#define HD_REC_SP 0x38
#define HD_REC_WIDE 0x3c

/* catalogue order (hold_sprites.catalogue): first sprite of each group, states per body */
enum { HD_CN_WHITE = 0, HD_CN_BLACK = 5, HD_HCN_WHITE = 10, HD_HCN_BLACK = 17, HD_BSS = 24, HD_HBSS = 29, HD_MSS_MIDDLE = 36 };
#define HD_MID_H 19           /* mss_middle rows (sprites.json) */
/* sprite sizes in frame rows (hold_sprites.py): white, black, turntable; caps and body tiles */
/* x pivots (hold_sprites.py PIVOT_X: widths 13 / 10 / 25, drawn about 1 px left of the note's x, which
 * sits right of the lane's centre) */
/* caps and bodies reach the lane lines (hold_sprites WIDTH 16 / 12 / 28, as in the arcade); x pivots half
 * a pixel left of the note's x */
static const hs8 hd_px[3] = {8, 6, 14}, hd_body_h[3] = {10, 9, 10};   /* bodies (sprites.json) */
static const hs8 hd_cpx[3] = {8, 6, 14}, hd_cap_h[3] = {12, 12, 12};   /* caps: the coloured band 6 rows, a note's height */
/* the beam meets each cap with a row of overlap, so no lane shows between them. It used to run half a cap
 * into each, drawn under the cap; in PCSX2 it covered the cap's inner half instead (the silver ring and
 * the dark middle), whatever the layers: so the caps no longer rely on draw order. */
#define HD_INTO(kind) ((void)(kind), 1)
/* a start cap with its bottom edge at `bottom`, placed from its top like the end cap (pivot 0): the
 * turntable's start cap drawn with its pivot at its own bottom never showed in PCSX2 */
#define HD_CAP(h, kind, x, bottom, clip) HD_DRAW((h), (x), (bottom) - hd_cap_h[kind], 100, hd_cpx[kind], 0, (clip))

#ifdef HOLDS_TEST
extern void test_draw(hu32 handle, hs32 x, hs32 y, hs32 zy, hs32 px, hs32 py);
extern hu8 *test_record(void);
#define HD_DRAW(h, x, y, zy, px, py, clip) ((void)(clip), test_draw((h), (x), (y), (zy), (px), (py)))
#define HD_DRAW_BODY HD_DRAW
#define HD_RECORD() test_record()
#define HD_LIFT(side) 0
#else
#define HD_NATIVE_DRAW ((void (*)(hu32, hs32, hs32, hs32, hs32, hs32, hs32, hs32, hs32, hs32, hu32, hs32))0x12fdd0)
#define HD_DRAW(h, x, y, zy, px, py, clip) HD_NATIVE_DRAW((h), (x), (y), 100, (zy), (px), (py), 100, 0, 0, (clip), HD_LAYER_CAP)
#define HD_DRAW_BODY(h, x, y, zy, px, py, clip) HD_NATIVE_DRAW((h), (x), (y), 100, (zy), (px), (py), 100, 0, 0, (clip), HD_LAYER_BODY)
#define HD_RECORD() ((hu8 *(*)(hu8 *, hs32))0x1e19a0)(0, ((hs32 (*)(void))0x205e00)())
#ifdef PLAY_LIFT
extern hu32 lift_y[2];
#define HD_LIFT(side) ((hs32)lift_y[(side) & 1])
#else
#define HD_LIFT(side) 0
#endif
#endif

static hs32 hd_x[HOLDS_LANES];
/* per hold: tick its head / tail note was last drawn, the head's y, and the frame the head was drawn
 * in (0 = not yet). Per hold, not per lane: CNs on one key follow each other closely, and an ended
 * hold's tail note stays on screen below the line for a while. */
static hs32 hd_head_at[HOLDS_MAX], hd_tail_at[HOLDS_MAX], hd_head_y[HOLDS_MAX];
static hu32 hd_head_frame[HOLDS_MAX], hd_frames;
/* per hold: its tail was judged POOR (let go early, or a missed head): the frame pass keeps its unlit
 * body until the tail is on screen */
static hu8 hd_poor[HOLDS_MAX];
static hu32 hd_from;                /* frame pass: first hold not yet over */
/* holds_ddbg: [1] head draws, [2] tail draws, [3] frame-pass bodies, [4] heads found gone, [9] empty POORs
 * dropped (tail notes, holds_empress.c tail_empty_poor),
 * [5] / [6] / [7] last tail drawn: tick, hold index, y; [8] bodies clamped */
volatile hu32 holds_ddbg[12] = {0x52444448u};
/* scroll speed per side in 1/256 pixel per tick, measured on tail notes above the line: the tail
 * note may sit a tick or two off the hold's end, and is drawn where the end is */
static hs32 hd_ppt[2];
static hu32 hd_clip[2], hd_cont[2];

/* a new chart: forget where notes were */
static void hd_reset(void)
{
    hu32 i;
    for (i = 0; i < HOLDS_LANES; i++) { hd_x[i] = -1; LOOP_PAD(); }
    for (i = 0; i < HOLDS_MAX; i++) { hd_head_at[i] = hd_tail_at[i] = -100; hd_head_frame[i] = 0; hd_poor[i] = 0; LOOP_PAD(); }
    hd_cont[0] = hd_cont[1] = 0;
    hd_from = 0;
}

/* hold index whose head (tail = 0) or tail (tail = 1) is this note, or -1 */
static hs32 hd_find(hu32 lane, hs32 t, int *tail)
{
    hu32 i;
    for (i = 0; i < chart_n; i++) {
        if (chart_defs[i].lane == lane && chart_defs[i].start == t) { *tail = 0; return (hs32)i; }
        LOOP_PAD();
    }
    for (i = 0; i < chart_n; i++) {
        if (chart_defs[i].lane == lane && holds_tail_tick(&chart_defs[i], t)) { *tail = 1; return (hs32)i; }
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

/* body sprite: 0 not held; held: 1 and 2 alternating every 4 ticks (the arcade beam pulses; every 2 ticks
 * was twice too fast); HCN let go: 3 and 4 alternating the same way */
#define HD_PULSE(t) (((hu32)(t) >> 2) & 1u)
static hu32 hd_state(hu32 i, hu32 hell)
{
    (void)i;
    switch (holds.state[i]) {
    case HS_HOLDING: return 1 + HD_PULSE(hd_now);
    case HS_LOOSE: return hell ? 3 + HD_PULSE(hd_now) : 0;
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
    hs32 len, h = hd_body_h[kind];
    if (top < HD_TOP_Y) { top = HD_TOP_Y; holds_ddbg[8]++; }                 /* never past the screen */
    if (bottom > HD_JUDGE_Y + 16) { bottom = HD_JUDGE_Y + 16; holds_ddbg[8]++; }
    len = bottom - top;
    if (len <= 0) return;
    holds_dbg[10]++;
    HD_DRAW_BODY(base, x, top, len * 100 / h, hd_px[kind], 0, clip);
}

/* rows from a hold's head line up to where its body starts (the start cap, or an MSS middle note centred
 * on the line), and from its tail line down to where its body starts (the end cap, or a middle note) */
static hs32 hd_head_edge(hs32 i, hu32 kind)
{ return i < HOLDS_MAX && (mss_chain[i] & MSS_PREV) ? HD_MID_H / 2 - 1 : hd_cap_h[kind] - HD_INTO(kind); }
static hs32 hd_tail_edge(hs32 i, hu32 kind)
{ return i < HOLDS_MAX && (mss_chain[i] & MSS_NEXT) ? HD_MID_H / 2 - 1 : hd_cap_h[kind] - HD_INTO(kind); }

/* returns 1 if the note was drawn here (skip the native draw) */
int holds_note_hook(hu8 *note, hu32 handle, hs32 x, hs32 y, hu32 side, hu32 clip)
{
    const hold_def *d;
    hu32 last, base, group, kind, lane, cont = handle >> 16, body;
    hs32 i, t, judge;
    int tail = 0;
    holds_dbg[3]++;
    holds_dbg[11] = cont;
    if (!armed || side > 1) return 0;
    if (note[9] > 7) return 0;
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
        holds_ddbg[1]++;
        hd_head_y[i] = y;
        hd_head_at[i] = hd_now;
        hd_head_frame[i] = hd_frames;
        if (hd_tail_at[i] < hd_now - 1)                        /* tail still above the screen */
            hd_body(body, kind, x, HD_TOP_Y, HD_BASE(y) - hd_head_edge(i, kind), clip);
        if (i < HOLDS_MAX && (mss_chain[i] & MSS_PREV))        /* MSS middle note: centred on the joint */
            HD_DRAW(last - (HD_SPRITES - 1) + HD_MSS_MIDDLE, x, HD_BASE(y) - HD_MID_H / 2, 100, hd_cpx[kind], 0, clip);
        else
            HD_CAP(base, kind, x, HD_BASE(y), clip);            /* start cap */
    } else {
        int head_seen, lit;
        if (t > hd_now + 4 && y < judge) hd_ppt[side & 1] = (judge - y) * 256 / (t - hd_now);
        if (hd_ppt[side & 1] > 16 * 256) hd_ppt[side & 1] = 16 * 256;           /* at most 16 px a tick */
        y -= (d->end - t) * hd_ppt[side & 1] / 256;
        holds_ddbg[2]++; holds_ddbg[5] = (hu32)t; holds_ddbg[6] = (hu32)i; holds_ddbg[7] = (hu32)y;
        /* the tail note is never judged, so the game keeps it (at or below the line) for up to 60
         * ticks after its time: once the hold has ended, nothing more is drawn for it */
        hd_tail_at[i] = hd_now;                                 /* on screen: the frame pass leaves it alone */
        if (hd_now > d->end || y > judge) return 1;
        head_seen = hd_head_at[i] >= hd_now - 1;
        /* the game may draw the tail before the head in a frame: then the head's y is a frame old, and the
         * body ended one frame's scroll above the start cap (a gap in PCSX2). Carry it on by the scroll speed. */
        hs32 bottom = !head_seen ? judge : hd_head_at[i] == hd_now ? hd_head_y[i] : hd_head_y[i] + hd_ppt[side & 1] / 256;
        if (bottom > judge) bottom = judge;
        lit = holds.state[i] == HS_HOLDING || holds.state[i] == HS_LOOSE;   /* head hit and not let go for good */
        hd_body(body, kind, x, HD_BASE(y) + hd_tail_edge(i, kind),
                HD_BASE(bottom) - (!head_seen && !lit ? 0 : head_seen ? hd_head_edge(i, kind) : hd_cap_h[kind] - HD_INTO(kind)), clip);
        if (!head_seen && lit && y < judge)
            HD_CAP(base, kind, x, HD_BASE(judge), clip);        /* head hit: start cap on the line */
        if (i < HOLDS_MAX && (mss_chain[i] & MSS_NEXT))         /* an MSS joint: the middle note, here as well as
                                                                  from the next section's head, so that it is
                                                                  drawn over both bodies whichever note the game
                                                                  draws last (the tail comes after that head) */
            HD_DRAW(last - (HD_SPRITES - 1) + HD_MSS_MIDDLE, x, HD_BASE(y) - HD_MID_H / 2, 100, hd_cpx[kind], 0, clip);
        else
        HD_DRAW(base + 1, x, HD_BASE(y), 100, hd_cpx[kind], 0, clip);   /* end cap: hangs below the tail's line (arcade:
                                                                             a head on the same tick sits on that line) */
    }
    return 1;
}

/* after the native tick: holds whose head time has passed and whose tail is still above the screen
 * have no note on screen, so their body (lane top to judge line) is drawn here, with the start cap
 * on the line while held. Unlit and capless: a missed head (also while the rules still wait for the
 * late rule: the game removes a missed head before that), and a CN let go too early (finished, no
 * longer the lane's active hold). Those vanished until the tail came on screen. */
static void hd_frame(void)
{
    hu32 i, lane, last, kind, group, side, lit;
    hs32 judge, top_cap;
    while (hd_from < chart_n && chart_defs[hd_from].end + 2 < hd_now) { hd_from++; LOOP_PAD(); }
    for (i = hd_from; i < chart_n; i++) {
        const hold_def *d = &chart_defs[i];
        LOOP_PAD();
        if (d->start > hd_now) break;
        lane = d->lane;
        side = lane >> 3;
        if (hd_x[lane] < 0 || !hd_cont[side]) continue;
        if (hd_head_at[i] >= hd_now || hd_tail_at[i] >= hd_now) continue;   /* drawn with its notes */
        if (hd_now >= d->end - HOLDS_TAIL_SHIFT) continue;   /* the end is at the line: nothing above it to
                                                               draw while the release is judged */
        if (holds.state[i] == HS_DONE && !hd_poor[i]) continue;              /* ended in its window */
        lit = holds.state[i] == HS_HOLDING || holds.state[i] == HS_LOOSE;
        holds_ddbg[3]++;
        if (!(last = hd_last(hd_cont[side]))) continue;
        group = hd_group(d, lane, &kind);
        judge = HD_JUDGE_Y - HD_LIFT(side);
        top_cap = lit ? hd_cap_h[kind] - HD_INTO(kind) : 0;
        hd_body(last - (HD_SPRITES - 1) + group + 2 + (lit ? hd_state(i, d->kind == HOLD_HCN || d->kind == HOLD_HBSS) : 0),
                kind, hd_x[lane], HD_TOP_Y, HD_BASE(judge) - top_cap, hd_clip[side]);
        if (lit)                                          /* the start cap rides the line only while held */
            HD_CAP(last - (HD_SPRITES - 1) + group, kind, hd_x[lane], HD_BASE(judge), hd_clip[side]);
    }
}

/* Called once per frame before the rules. The game stops drawing a note once it has judged it, and
 * keeps a missed note on screen until it reports the miss. So a pending hold whose head note was
 * drawn last frame, is gone now and has its key down was hit, even if no judgement reached
 * holds_on_judgement (seen in PCSX2: some hits are never reported). It counts as GREAT; the game
 * scores the head itself. */
static void hd_heads_gone(hu32 keys, const hs8 tt[2])
{
    hu32 i;
    hd_frames++;
    for (i = holds.next; i < chart_n; i++) {          /* pending heads not yet past the late rule */
        const hold_def *d = &chart_defs[i];
        LOOP_PAD();
        if (d->start > hd_now + 16) break;
        if (holds.state[i] != HS_PENDING || !hd_head_frame[i]) continue;
        if (hd_frames - hd_head_frame[i] < 2u) continue;                     /* drawn last frame */
        hd_head_frame[i] = 0;
        if (hd_now > d->start + default_cfg.head_late) continue;             /* the late rule decides */
        if ((d->lane & 7u) == 7u) {                                          /* BSS: the turntable moved lately */
            (void)tt;
            if (hd_now - spun_at[d->lane >> 3] > 8) continue;
        } else if (!((keys >> d->lane) & 1u)) continue;
        if (holds_head(&holds, d->lane, d->start, J_GREAT, hd_now) >= 0) { holds_ddbg[4]++; dbg_report(0xfe, lane_id(d->lane), (int)i); }
    }
}
