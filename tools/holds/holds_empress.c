/* EMPRESS glue for the hold rules (holds.c), built into the runtime with HOLDS=1.
 *
 * Engine facts used (docs/holds-design.md, rounds 1-3 of disasm_sites.py):
 *   confirmed
 *     0x1e5cb8  play loop: jal 0x1e6bb0(tick) once per tick      -> holds_frame_hook
 *     0x1ed5a0  judgement ring logger (code, side<<15 | lane); fast_slow.c's
 *               fs_ring() wraps it, fs_ring_body() is the original  -> holds_on_judgement
 *     ring codes (key/turntable judge): 4..8 PGREAT..POOR, 9 POOR (note used up),
 *               0xb empty POOR, 0xe/0xf missed POOR
 *     0x130970() (gp-0x7bec): keys held, P1 bits 0..6, P2 bits 7..13
 *   NOT confirmed yet (check in PCSX2 before enabling)
 *     0x13acd0(side): turntable; nonzero = moving, bit 0x80 = direction. It is
 *               what the scratch judge reads, but it may report spin edges only.
 *     HCN gain ticks: 0x204d30(gauge side, kind) is the ring reader's gauge step per judgement
 *               (kind 1 PGREAT, 2 GREAT, 3 GOOD, 0 BAD, 8 POOR, 9 missed, 0xa empty POOR), separate
 *               from score (0x204310) and combo; gauge side = 0x205d50() ? 0 : side, as the reader
 *               does. HE_TICK_GAIN applies a PGREAT's gain, nothing else.
 *
 * Results go back through the ring, so the game itself updates the judgement
 * counters, score, combo, gauge and judgement display:
 *   tail PGREAT -> 4, tail BAD -> 7, tail POOR / HE_BREAK -> 0xf, HE_TICK_LOSS -> 0xb,
 *   J_NONE -> nothing.
 * Chart totals: the converter counts each hold twice (holds_format.note_total),
 * so scores and gauge recovery include the tails.
 * Holds come from the chart itself (holds_in_chart). Build with HOLDS=1
 * (tools/offdisc/build_vdisc.py), which includes this file from songdb.c.
 */
#include "holds.h"

#ifdef HOLDS_TEST
extern hu32 test_chart_cursor(void);
#define H_CHART_CURSOR() test_chart_cursor()
extern unsigned long long test_ring_body(hu32 code, hu32 lane);
extern hu32 test_keys(hu32 side);
extern hu32 test_turntable(hu32 side);
#define H_RING(code, lane) test_ring_body((code), (lane))
extern void test_gain(hu32 side);
#define HOLDS_GAIN_GAUGE(side) test_gain(side)
extern hu32 test_pressed(hu32 side);
#define H_KEYS(side) (test_keys(side) | test_pressed(side))
#define H_TT(side) test_turntable(side)
#define H_NATIVE_TICK(tick) ((void)(tick))
extern void test_native_spawn(hu8 *note);
#define H_NATIVE_SPAWN(note) test_native_spawn(note)
extern void test_play(hu32 index, hu32 side);
#define H_PLAY_NOTE(note, side) test_play((note)[8], (side))
extern void test_judge_keep(hu32 side);
#define H_JUDGE_KEEP(side) test_judge_keep(side)
#else
static hu32 h_gp(void) { hu32 g; __asm__ volatile("move %0, $28" : "=r"(g)); return g; }
/* chart event cursor of the spawner (0x1e9790 reads gp-0x77dc): an address inside the decoded chart */
#define H_CHART_CURSOR() (*(volatile hu32 *)(unsigned long)(h_gp() - 0x77dc))
#ifndef FS_ARGS
#define FS_ARGS unsigned long long a,unsigned long long b,unsigned long long c,unsigned long long d,unsigned long long e,unsigned long long f,unsigned long long g,unsigned long long h
#endif
extern unsigned long long fs_ring_body(FS_ARGS);
#define H_RING(code, lane) fs_ring_body((code), (lane), 0, 0, 0, 0, 0, 0)
/* held keys: the game's 14-bit mask (P1 bits 0..6, P2 bits 7..13) at gp-0x7bec, read by 0x130970
 * (0x130950 / gp-0x7be8 is the pressed-this-frame mask the key judge uses; the replay recorder at
 * 0x1cc9a8 saves both every frame). The earlier 0x724cf0 guess behaved like a press flag. */
#ifndef HOLDS_GAIN_GAUGE
#define HOLDS_GAIN_GAUGE(side) (((void (*)(hs32, hs32))0x204d30)(((hs32 (*)(void))0x205d50)() ? 0 : (hs32)(side), 1))
#endif
/* a key pressed this tick counts as held: the held mask can lag the press (and the head judgement) by
 * one tick, which broke HCNs right after a good head in PCSX2 (POOR flash after GREAT) */
#define H_HELD_MASK() (((hu32 (*)(void))0x130970)() | ((hu32 (*)(void))0x130950)())   /* 0x130950 held (PCSX2: key1 1, key3 4, key5 0x10); 0x130970 kept for the press frame */
#define H_KEYS(side) ((H_HELD_MASK() >> ((side) * 7u)) & 0x7fu)
#define H_TT(side) (((hu32 (*)(hu32))0x13acd0)(side) & 0xffu)
#define H_NATIVE_TICK(tick) (((void (*)(long long))0x1e6bb0)(tick))
#define H_NATIVE_SPAWN(note) (((void (*)(hu8 *))0x1ebf90)(note))
/* the key handler's keysound request (0x1e8834..0x1e8948): the word at 0x79da50 + note[8] * 4, with
 * the side flag when 0x1ebdc0() == 2 and not 0x205e20(), 0x20000000 when 0x205d40(), skipped while
 * gp-0x776c is set; then 0x140260(request) */
static void h_play_note(const hu8 *note, hu32 side)
{
    hu32 req = *(volatile hu32 *)(unsigned long)(0x79da50u + note[8] * 4u);
    if (!req || *(volatile hu32 *)(unsigned long)(h_gp() - 0x776c)) return;
    if (((hu32 (*)(void))0x1ebdc0)() == 2 && !((hu32 (*)(void))0x205e20)()) req |= side ? 0x2000000u : 0x1000000u;
    if (((hu32 (*)(void))0x205d40)()) req |= 0x20000000u;
    ((void (*)(hu32))0x140260)(req);
}
#define H_PLAY_NOTE(note, side) h_play_note((note), (side))
/* the judgement display (FUN_001feaf0, called for every judgement the ring reports): per side, +0xfc the
 * judgement, +0xec frames left, set to 0x1e (half a second). A note's judgement stays until the next one; a
 * hold has none from its head to its end, so the head's faded. Held, the frames left stop at HD_JUDGE_KEEP,
 * past the 0x1e..0x1b pop-in, so it stays as the game shows it, until the end's judgement replaces it. */
#define HD_JUDGE_KEEP 20
static void h_judge_keep(hu32 side)
{
    hu32 o = *(volatile hu32 *)(unsigned long)(h_gp() - 0x7608);
    volatile hs32 *left;
    if (o < 0x100000u || o >= 0x2000000u || (o & 3u)) return;
    left = (volatile hs32 *)(unsigned long)(o + 0xec + side * 4u);
    if (*left < HD_JUDGE_KEEP) *left = HD_JUDGE_KEEP;
}
#define H_JUDGE_KEEP(side) h_judge_keep(side)
#endif

#define CODE_PGREAT 0xau
#define CODE_BAD 8u
#define CODE_POOR_USED 9u
#define CODE_EMPTY_POOR 0xbu
#define CODE_MISS 0xfu
#define HEAD_WINDOW 15   /* the BAD window in ticks: a head judgement further away is not a hold head */

static holds_t holds;
static hs32 hd_now;
/* diagnostics, readable in the PCSX2 debugger (symbol holds_dbg): 'HDBG', armed, holds in chart,
 * note_hook calls, hold notes seen, hold notes with no sprites, record +0x38, record +0x3c,
 * tails made unjudged, ticks, bodies drawn, last note container */
volatile hu32 holds_dbg[32] = {0x47424448u};
/* holds_dbg[12] = events logged so far; [16 + 2k], [17 + 2k] for the last 8 (k = count % 8):
 * tick << 8 | event kind (1 tail, 2 break, 3 loss tick, 4 gain), and
 * judge << 28 | hold index << 16 | lane (side*8 + key) << 8 | P1 keys seen this tick */
static hu32 dbg_keys;
static void dbg_event(const hold_event *e)
{
    hu32 k = holds_dbg[12]++ & 7u;
    holds_dbg[16 + 2 * k] = (hu32)hd_now << 8 | e->kind;
    holds_dbg[17 + 2 * k] = (hu32)e->judge << 28 | (e->index & 0xfffu) << 16 | (hu32)e->lane << 8 | (dbg_keys & 0xffu);
}                 /* tick being processed (holds_draw.c stamps note draws with it) */
static void hd_reset(void);
/* multi-spin scratch: per hold, MSS_PREV = a section on the same turntable ends where this one starts
 * (its head is a middle note), MSS_NEXT = one starts where this one ends (arcade_data.read_charts:
 * MSS sections are touching BSS holds; plain holds on one lane never touch) */
enum { MSS_PREV = 1, MSS_NEXT = 2 };
static hu8 mss_chain[HOLDS_MAX];
static void mss_link(void);
static hu8 hd_poor[HOLDS_MAX];                 /* holds_draw.c */
/* last tick each turntable was moving (BSS heads the ring never reported) */
static hs32 spun_at[2] = {-100, -100};
static void hd_heads_gone(hu32 keys, const hs8 tt[2]);
static void hd_frame(void);
static const hold_def *chart_defs;
static hu32 chart_n, armed;
static hs32 last_tick = -1;

/* Settings until arcade values are measured: CN/BSS release PGREAT inside the
 * GOOD window (7 ticks), POOR after it; HCN 16ths come from holds.bin. */
static const holds_cfg default_cfg = { {1, 2, 4, 7}, 16, 6 * HOLDS_FP, 4, OVERHOLD_RELEASE, J_BAD, 0, 3 };   /* overhold BAD: atwiki bemani2sp 4662 */

/* Converted charts carry their holds after the 0x7fff terminator: an 8-byte
 * terminator event, then holds.bin ('MMHL', u16 1, u16 count, 12-byte records).
 * The game stops reading at the terminator, so native charts are unaffected. */
#define CHART_SCAN 32768u
static const hold_def *holds_in_chart(hu32 cursor, hu32 *count)
{
    hu32 at, i;
    const volatile hu8 *b;
    if (cursor < 0x100000u || cursor >= 0x2000000u || (cursor & 3u)) return 0;
    for (i = 0, at = cursor; i < CHART_SCAN && at + 24 < 0x2000000u; i++, at += 8) {
        if (*(const volatile hu32 *)(unsigned long)at != 0x7fffu) { LOOP_PAD(); continue; }
        b = (const volatile hu8 *)(unsigned long)(at + 8);
        if (b[0] != 'M' || b[1] != 'M' || b[2] != 'H' || b[3] != 'L' || (b[4] | b[5] << 8) != 1) return 0;
        *count = b[6] | b[7] << 8;
        return *count <= HOLDS_MAX ? (const hold_def *)(unsigned long)(at + 16) : 0;
    }
    return 0;
}

/* Tests and other loaders can hand over a chart's records directly. */
void holds_set_chart(const hold_def *defs, hu32 n)
{
    chart_defs = defs;
    chart_n = n;
    armed = 0;
}

/* each hold's tail note object, from its spawn (for the BSS end sound; tail_live checks it is still that note) */
static hu8 *tail_note[HOLDS_MAX];
extern volatile hu32 holds_ddbg[12];

static void mss_link(void)
{
    hu32 i, j;
    for (i = 0; i < HOLDS_MAX; i++) { mss_chain[i] = 0; LOOP_PAD(); }
    for (i = 0; i < chart_n && i < HOLDS_MAX; i++) {
        if ((chart_defs[i].lane & 7u) != 7u) continue;
        for (j = i + 1; j < chart_n && j < HOLDS_MAX && chart_defs[j].start <= chart_defs[i].end; j++) {
            LOOP_PAD();
            if (chart_defs[j].lane == chart_defs[i].lane && chart_defs[j].start == chart_defs[i].end) {
                mss_chain[i] |= MSS_NEXT;
                mss_chain[j] |= MSS_PREV;
            }
        }
    }
}

static hold_def play_defs[HOLDS_MAX];
static hu8 chart_lane[HOLDS_MAX];

/* per side: the judgement on screen is a hold head's (the held hold keeps it shown); any other note's
 * judgement on that side takes the display back, and it fades as the game's own do */
static hu8 judge_owned[2];

static void arm(void)
{
    hu32 n = 0, i;
    const hold_def *found = holds_in_chart(H_CHART_CURSOR(), &n);
    /* a chart without holds.bin has no holds: never keep the previous play's list */
#ifdef HOLDS_TEST
    if (!found && chart_defs != play_defs) { found = chart_defs; n = chart_n; }   /* holds_set_chart */
#endif
    if (found) {
        /* Lane options (RANDOM, R-RANDOM, MIRROR, S-RANDOM...) move notes to other lanes when they spawn:
         * work on a copy whose lanes become the head's lane on screen (holds_spawn_hook); chart_lane keeps
         * the chart's lane, which is what the spawned note's chart event still says. */
        for (i = 0; i < n && i < HOLDS_MAX; i++) { play_defs[i] = found[i]; chart_lane[i] = found[i].lane; LOOP_PAD(); }
        chart_defs = play_defs; chart_n = n;
    }
#ifndef HOLDS_TEST
    else chart_n = 0;
#endif
    armed = chart_n && holds_reset(&holds, chart_defs, chart_n, &default_cfg) == 0;
    hd_reset();
    mss_link();
    spun_at[0] = spun_at[1] = -100;
    for (n = 0; n < HOLDS_MAX; n++) { tail_note[n] = 0; LOOP_PAD(); }
    judge_owned[0] = judge_owned[1] = 0;
    holds_dbg[1] = (hu32)armed;
    holds_dbg[2] = chart_n;
}

/* Replaces the spawner's jal 0x1ebf90(note) at 0x1e7d14. The note's time (+0x18, ticks), lane (+9)
 * and side (+0xa) are set and +0xb (must judge) has just been decided. A note at a hold's end tick
 * on its lane (within HOLDS_TAIL_SHIFT) is the hold's tail (arcade_convert.py writes one per hold): never judged by the game,
 * only scrolled and drawn, so the renderer knows where the tail is. */
/* the chart lane (side * 8 + lane) of a spawned note: its chart event (note +0, 8 bytes: u32 tick, then
 * the command byte, lane << 4 | side), before lane options moved it; the note's own lane if unreadable */
static hu32 chart_lane_of(const hu8 *note, hu32 lane)
{
#ifndef HOLDS_TEST
    hu32 ev = *(const hu32 *)note, cmd;
    if (ev < 0x100000u || ev >= 0x2000000u || (ev & 3u)) return lane;
    cmd = *(const volatile hu8 *)(unsigned long)(ev + 4);
    if ((cmd & 15u) > 1u) return lane;
    return (cmd & 15u) * 8u + (cmd >> 4);
#else
    (void)note;
    return lane;
#endif
}

void holds_spawn_hook(hu8 *note)
{
    hu32 i, lane, clane;
    hs32 t;
    if (!armed) arm();
    if (armed && note[9] < 8 && note[0xa] < 2) {
        t = *(const short *)(note + 0x18);
        lane = note[0xa] * 8u + note[9];
        clane = chart_lane_of(note, lane);
        for (i = 0; i < chart_n; i++) {
            if (chart_lane[i] == clane && chart_defs[i].start == t) {                     /* a head */
                play_defs[i].lane = (hu8)lane;                                           /* its lane on screen */
                break;
            }
            if (chart_lane[i] == clane && holds_tail_tick(&chart_defs[i], t)) {          /* its tail: on the head's lane */
                if (chart_defs[i].lane != lane) {                                      /* S-RANDOM and the like */
                    note[9] = chart_defs[i].lane & 7u; note[0xa] = chart_defs[i].lane >> 3;
#ifndef HOLDS_TEST
                    /* its x, as the spawner set it from the lane (0x1e7bec: 0x797440 + side * 18 + lane * 2) */
                    *(short *)(note + 0x1a) = *(const volatile short *)(unsigned long)(0x797440u + note[0xa] * 18u + note[9] * 2u);
#endif
                }
                note[0xb] = 0; tail_note[i] = note; holds_dbg[8]++; break;
            }
            LOOP_PAD();
        }
    }
    H_NATIVE_SPAWN(note);
}

static hu32 lane_id(hu32 lane) { return (lane >> 3) << 15 | (lane & 7); }

static int judge_of_code(hu32 code)
{
    /* ring codes (fast_slow.c fs_band; bands 0..3 FAST, 4 PGREAT, 5..8 SLOW): 0xa PGREAT, 6 GREAT,
     * 5 / 7 GOOD fast / slow, 4 / 8 BAD fast / slow, 9, 0xe, 0xf POOR, 0xb empty POOR */
    if (code == 0xa) return J_PGREAT;
    if (code == 6) return J_GREAT;
    if (code == 5 || code == 7) return J_GOOD;
    if (code == 4 || code == 8) return J_BAD;
    if (code == CODE_POOR_USED || code == 0xe || code == CODE_MISS) return J_POOR;
    return -1;                                                     /* empty POOR and others: not a head */
}

/* holds_jdbg: every judgement report seen while armed. [1] = count; [2 + 2k], [3 + 2k] for the last
 * 16 (k = count % 16): tick << 16 | code << 8 | ring lane id low byte, then side << 31 | fallback << 30 |
 * matched hold index (0xfff = none). */
volatile hu32 holds_jdbg[34] = {0x47444a48u};
static void dbg_report(hu32 code, hu32 id, int r)
{
    hu32 k = holds_jdbg[1]++ & 15u;
    holds_jdbg[2 + 2 * k] = ((hu32)last_tick & 0xffffu) << 16 | (code & 0xffu) << 8 | (id & 0xffu);
    holds_jdbg[3 + 2 * k] = ((id >> 15) & 1u) << 31 | (r >= 0 && (r & 0x100) ? 1u << 30 : 0) | (r < 0 ? 0xfffu : (hu32)r & 0xfffu);
}

/* Called from fs_ring() for every native judgement, before it is logged. */
/* where a judged note can be, as start - now in ticks: the judgement's own window (IIDX: PGREAT ~1
 * frame, GREAT ~2, GOOD ~7, BAD ~15, plus a tick of slack), cut to one side by FAST/SLOW where the
 * code or the timed delta says. Short CNs sit a few ticks from other notes on the same key; one
 * window for every judgement took a CN head from a neighbouring note's hit (then POOR on release). */
static void head_range(hu32 code, int judge, int dir, int scratch, hs32 *lo, hs32 *hi)
{
    static const hs8 w[5] = {3, 4, 9, HEAD_WINDOW, HEAD_WINDOW};
    hs32 a = w[judge];
    *lo = -a; *hi = a;
    if (judge == J_POOR) { *lo = -(HEAD_WINDOW + 3); *hi = 1; return; }   /* misses come after the window */
    if (scratch) { *lo = -HEAD_WINDOW; *hi = HEAD_WINDOW; return; }        /* the turntable's reports came outside
                                                                     the key windows (BSS heads went missed) */
    if (code == 5 || code == 4 || (code == 6 && dir < 0)) *lo = -1;       /* FAST: the note is still ahead */
    if (code == 7 || code == 8 || (code == 6 && dir > 0)) *hi = 1;        /* SLOW: the note has passed */
}

/* An empty POOR the game gave because of a tail note: the tail is a real note to the game, never
 * judgeable, so a turntable reversal at a BSS end (or a press near a CN end) found it and the game
 * showed an empty POOR the arcade does not have. True if a hold on the lane ends within the window. */
static int tail_empty_poor(hu32 lane)
{
    hu32 i;
    for (i = 0; i < chart_n; i++) {
        const hold_def *d = &chart_defs[i];
        LOOP_PAD();
        if (d->start > last_tick + HEAD_WINDOW) break;
        if (d->lane == lane && d->end - last_tick <= HEAD_WINDOW && last_tick - d->end <= HEAD_WINDOW) return 1;
    }
    return 0;
}

/* Called from fs_timed() / fs_ring() for every native judgement, before it is logged.
 * dir: sign of the timed hit delta (-1 early, +1 late), 0 if the ring gave none.
 * Returns 1 to drop the judgement (an empty POOR caused by a tail note, see tail_empty_poor). */
int holds_on_judgement(hu32 code, hu32 id, int dir)
{
    int judge, r;
    hu32 lane;
    hs32 lo, hi;
    if (!armed || last_tick < 0) return 0;
    code &= 0xffffu;
    if (code == CODE_EMPTY_POOR) {
        lane = ((id >> 15) & 1u) * 8u + (id & 7u);
        if (tail_empty_poor(lane)) { holds_ddbg[9]++; dbg_report(code, id, -1); return 1; }
        judge_owned[(id >> 15) & 1u] = 0;          /* an empty POOR of its own replaces the display */
        return 0;
    }
    judge = judge_of_code(code);
    if (judge < 0) return 0;
    lane = ((id >> 15) & 1u) * 8u + (id & 7u);
    head_range(code, judge, dir, (lane & 7u) == 7u, &lo, &hi);
    r = holds_head_in(&holds, lane, last_tick, lo, hi, (hu32)judge);
    /* a hit that names no pending hold on its lane: the hold whose key is down (chords where the
     * report's lane is not the head's) */
    if (r < 0 && judge <= J_GOOD) {
        r = holds_head_keyed(&holds, H_KEYS(0) | H_KEYS(1) << 8, last_tick, lo, hi, (hu32)judge);
        if (r >= 0) r |= 0x100;
    }
    dbg_report(code, id, r);
    judge_owned[(id >> 15) & 1u] = r >= 0;
    return 0;
}

/* the tail note object of hold i, if it is still that note (not freed and reused) */
static int tail_live(hu32 i)
{
    const hu8 *n = tail_note[i];
    return n && holds_tail_tick(&chart_defs[i], *(const short *)(n + 0x18)) && (n[9] & 0x3fu) == (chart_defs[i].lane & 7u)
        && n[0xa] == (chart_defs[i].lane >> 3);
}

static void apply(const hold_event *e)
{
    hu32 id = lane_id(e->lane);
    if (e->kind != HE_TICK_GAIN) dbg_event(e);
    switch (e->kind) {
    case HE_TAIL:
        if (e->judge == J_POOR && e->index < HOLDS_MAX) hd_poor[e->index] = 1;   /* holds_draw.c: unlit to its tail */
        /* BSS / HBSS: the backspin at the end plays the turntable's keysound at that point of the chart,
         * the sound the tail note carries (the game never judges the tail, so it never plays it) */
        /* not at an MSS middle note: there the next section's head (a scratch note) plays its sound */
        if ((e->judge == J_PGREAT || e->judge == J_BAD) && (e->lane & 7) == 7 && tail_live(e->index)
            && !(e->index < HOLDS_MAX && (mss_chain[e->index] & MSS_NEXT)))
            H_PLAY_NOTE(tail_note[e->index], e->lane >> 3);
        if (e->judge == J_PGREAT) H_RING(CODE_PGREAT, id);
        else if (e->judge == J_BAD) H_RING(CODE_BAD, id);
        else if (e->judge == J_POOR) H_RING(CODE_MISS, id);
        break;                                   /* J_NONE: the end adds nothing */
    case HE_BREAK: H_RING(CODE_MISS, id); break;
    case HE_TICK_LOSS: H_RING(CODE_EMPTY_POOR, id); break;
    case HE_TICK_GAIN:
#ifdef HOLDS_GAIN_GAUGE
        HOLDS_GAIN_GAUGE(e->lane >> 3);
#endif
        break;
    }
}

static hs8 spin(hu32 side)
{
    hu32 v = H_TT(side);
    return v == 0 ? 0 : (v & 0x80u) ? -1 : 1;
}

/* AUTO PLAY (native side mask, input_probe.c): the game hits the heads by itself but presses nothing, so
 * hold its holds for it: keys down and the turntable spinning from the head until the end tick, then let
 * go there (a release on the end: PGREAT; a BSS stops on it, which releases it the same way). MSS
 * sections each start a new spin, in the other direction, as a joint asks for. */
#ifdef HOLDS_TEST
extern hu32 test_autoplay(void);
#define H_AUTOPLAY() test_autoplay()
#else
#define H_AUTOPLAY() (*(volatile hu8 *)(unsigned long)0x2ad440 & 3u)
#endif
static hs32 auto_hold[2] = {-1, -1};
static hs8 auto_spin[2] = {1, 1};
static void autoplay_holds(hs32 now, hu32 *keys, hs8 tt[2])
{
    hu32 sides = H_AUTOPLAY(), lane;
    if (!sides) return;
    for (lane = 0; lane < HOLDS_LANES; lane++) {
        hs32 i = holds.active[lane];
        hu32 side = lane >> 3;
        LOOP_PAD();
        if (!(sides >> side & 1u) || i < 0) continue;
        if (holds.state[i] != HS_HOLDING && holds.state[i] != HS_LOOSE) continue;
        if (now >= chart_defs[i].end) {                     /* the end: let go */
            if ((lane & 7u) == 7u) tt[side] = 0; else *keys &= ~(1u << lane);
            continue;
        }
        if ((lane & 7u) == 7u) {
            if (auto_hold[side] != i) {                     /* a new BSS / MSS section */
                auto_spin[side] = (mss_chain[i] & MSS_PREV) ? (hs8)-auto_spin[side] : 1;
                auto_hold[side] = i;
            }
            tt[side] = auto_spin[side];
        } else *keys |= 1u << lane;
    }
}

/* Replaces the play loop's jal 0x1e6bb0(tick) at 0x1e5cb8. */
void holds_frame_hook(long long tick)
{
    hold_event ev[16];
    hu32 n, i, keys;
    hs8 tt[2];
    if ((hs32)tick < last_tick) armed = 0;        /* a new play started (tick counter restarted); before the
                                                     native tick, which spawns this play's first notes */
    hd_now = (hs32)tick;
    holds_dbg[9]++;
    H_NATIVE_TICK(tick);
    if (!armed && tick < 600) arm();              /* the cursor is set once the chart is loaded */
    last_tick = (hs32)tick;
    if (!armed) return;
    keys = H_KEYS(0) | H_KEYS(1) << 8;
    dbg_keys = keys;
#ifndef HOLDS_TEST
    holds_dbg[13] = ((hu32 (*)(void))0x130970)();           /* raw masks, to check their bit order */
    holds_dbg[14] = ((hu32 (*)(void))0x130950)();
    holds_dbg[15] = *(volatile hu32 *)(unsigned long)0x724cf0;
#endif
    tt[0] = spin(0);
    tt[1] = spin(1);
    autoplay_holds((hs32)tick, &keys, tt);
    if (tt[0]) spun_at[0] = (hs32)tick;
    if (tt[1]) spun_at[1] = (hs32)tick;
    hd_heads_gone(keys, tt);
    holds_frame(&holds, (hs32)tick, keys, tt);
    while ((n = holds_pop(&holds, ev, 16)) != 0)
        for (i = 0; i < n; i++) apply(&ev[i]);
    {                                             /* a held hold keeps the side's last judgement on screen */
        hu32 held = 0, lane;
        for (lane = 0; lane < HOLDS_LANES; lane++) {
            hs32 a = holds.active[lane];
            if (a >= 0 && (holds.state[a] == HS_HOLDING || holds.state[a] == HS_LOOSE)) held |= 1u << (lane >> 3);
            LOOP_PAD();
        }
        if ((held & 1u) && judge_owned[0]) H_JUDGE_KEEP(0);
        if ((held & 2u) && judge_owned[1]) H_JUDGE_KEEP(1);
    }
    hd_frame();
}

#include "holds_draw.c"
