/* EMPRESS glue for the hold rules (holds.c), built into the runtime with HOLDS=1.
 *
 * Engine facts used (docs/holds-design.md, rounds 1-3 of disasm_sites.py):
 *   confirmed
 *     0x1e5cb8  play loop: jal 0x1e6bb0(tick) once per tick      -> holds_frame_hook
 *     0x1ed5a0  judgement ring logger (code, side<<15 | lane); fast_slow.c's
 *               fs_ring() wraps it, fs_ring_body() is the original  -> holds_on_judgement
 *     ring codes (key/turntable judge): 4..8 PGREAT..POOR, 9 POOR (note used up),
 *               0xb empty POOR, 0xe/0xf missed POOR
 *     0x724cf0 + side*0x1c0, bits 0..6: keys held (fast_slow.c fs_keys)
 *   NOT confirmed yet (check in PCSX2 before enabling)
 *     0x13acd0(side): turntable; nonzero = moving, bit 0x80 = direction. It is
 *               what the scratch judge reads, but it may report spin edges only.
 *     HCN gain ticks: no ring code adds gauge without combo/score, so
 *               HE_TICK_GAIN calls HOLDS_GAIN_GAUGE (off by default).
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
#define H_KEYS(side) test_keys(side)
#define H_TT(side) test_turntable(side)
#define H_NATIVE_TICK(tick) ((void)(tick))
#else
static hu32 h_gp(void) { hu32 g; __asm__ volatile("move %0, $28" : "=r"(g)); return g; }
/* chart event cursor of the spawner (0x1e9790 reads gp-0x77dc): an address inside the decoded chart */
#define H_CHART_CURSOR() (*(volatile hu32 *)(unsigned long)(h_gp() - 0x77dc))
#ifndef FS_ARGS
#define FS_ARGS unsigned long long a,unsigned long long b,unsigned long long c,unsigned long long d,unsigned long long e,unsigned long long f,unsigned long long g,unsigned long long h
#endif
extern unsigned long long fs_ring_body(FS_ARGS);
#define H_RING(code, lane) fs_ring_body((code), (lane), 0, 0, 0, 0, 0, 0)
#define H_KEYS(side) (*(volatile hu32 *)(unsigned long)(0x724cf0 + (side) * 0x1c0) & 0x7fu)
#define H_TT(side) (((hu32 (*)(hu32))0x13acd0)(side) & 0xffu)
#define H_NATIVE_TICK(tick) (((void (*)(long long))0x1e6bb0)(tick))
#endif

#define CODE_PGREAT 4u
#define CODE_BAD 7u
#define CODE_POOR_USED 9u
#define CODE_EMPTY_POOR 0xbu
#define CODE_MISS 0xfu
#define HEAD_WINDOW 15   /* the BAD window in ticks: a head judgement further away is not a hold head */

static holds_t holds;
static const hold_def *chart_defs;
static hu32 chart_n, armed;
static hs32 last_tick = -1;

/* Settings until arcade values are measured: CN/BSS release PGREAT inside the
 * GOOD window (7 ticks), POOR after it; HCN 16ths come from holds.bin. */
static const holds_cfg default_cfg = { {1, 2, 4, 7}, 16, 6 * HOLDS_FP, 4, OVERHOLD_RELEASE, J_POOR, 0, 0 };

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

static hu32 lane_id(hu32 lane) { return (lane >> 3) << 15 | (lane & 7); }

static int judge_of_code(hu32 code)
{
    if (code >= 4 && code <= 8) return (int)(code - 4);          /* PGREAT..POOR */
    if (code == CODE_POOR_USED || code == 0xe || code == CODE_MISS) return J_POOR;
    return -1;                                                     /* empty POOR and others: not a head */
}

/* Called from fs_ring() for every native judgement, before it is logged. */
void holds_on_judgement(hu32 code, hu32 id)
{
    int judge;
    hu32 lane;
    if (!armed || last_tick < 0) return;
    judge = judge_of_code(code & 0xffffu);
    if (judge < 0) return;
    lane = ((id >> 15) & 1u) * 8u + (id & 7u);
    holds_head_near(&holds, lane, last_tick, HEAD_WINDOW, (hu32)judge);
}

static void apply(const hold_event *e)
{
    hu32 id = lane_id(e->lane);
    switch (e->kind) {
    case HE_TAIL:
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

/* Replaces the play loop's jal 0x1e6bb0(tick) at 0x1e5cb8. */
void holds_frame_hook(long long tick)
{
    hold_event ev[16];
    hu32 n, i, keys;
    hs8 tt[2];
    H_NATIVE_TICK(tick);
    if ((hs32)tick < last_tick) armed = 0;        /* a new play started (tick counter restarted) */
    if (!armed && tick < 600) {                   /* the cursor is set once the chart is loaded */
        hu32 n = 0;
        const hold_def *found = holds_in_chart(H_CHART_CURSOR(), &n);
        if (found) { chart_defs = found; chart_n = n; }
        armed = chart_n && holds_reset(&holds, chart_defs, chart_n, &default_cfg) == 0;
    }
    last_tick = (hs32)tick;
    if (!armed) return;
    keys = H_KEYS(0) | H_KEYS(1) << 8;
    tt[0] = spin(0);
    tt[1] = spin(1);
    holds_frame(&holds, (hs32)tick, keys, tt);
    while ((n = holds_pop(&holds, ev, 16)) != 0)
        for (i = 0; i < n; i++) apply(&ev[i]);
}
