/* Host tests for holds.c: cc -Wall -Wextra -o test_holds test_holds.c holds.c && ./test_holds */
#include <stdio.h>
#include <string.h>
#include "holds.h"

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static holds_t H;
static hold_event ev[256];
static hu32 nev;
static const holds_cfg CFG = { {1, 2, 4, 6}, 6, 4, 2, OVERHOLD_RELEASE, {0, 0, 0} };

/* run frames t0..t1 inclusive with a key/turntable script; collect events */
typedef void (*input_fn)(hs32 t, hu32 *keys, hs8 tt[2]);
static void run(hs32 t0, hs32 t1, input_fn f)
{
    hs32 t;
    for (t = t0; t <= t1; t++) {
        hu32 keys = 0; hs8 tt[2] = {0, 0};
        f(t, &keys, tt);
        holds_frame(&H, t, keys, tt);
        nev += holds_pop(&H, ev + nev, 256 - nev);
    }
}
static int count(hu32 kind) { hu32 i; int n = 0; for (i = 0; i < nev; i++) n += ev[i].kind == kind; return n; }
static const hold_event *tail(void) { hu32 i; for (i = 0; i < nev; i++) if (ev[i].kind == HE_TAIL) return &ev[i]; return 0; }
static void setup(const hold_def *d, hu32 n, const holds_cfg *c) { nev = 0; CHECK(holds_reset(&H, d, n, c ? c : &CFG) == 0); }

static hs32 release_at;
static void hold_key0(hs32 t, hu32 *k, hs8 *tt) { (void)tt; if (t < release_at) *k = 1; }
static void hold_cn_gap(hs32 t, hu32 *k, hs8 *tt) { (void)tt; if (t < 20 || (t >= 28 && t < 40)) *k = 1; }
static void spin_then_back(hs32 t, hu32 *k, hs8 *tt) { (void)k; tt[0] = t < release_at ? 1 : -1; }
static void spin_stop(hs32 t, hu32 *k, hs8 *tt) { (void)k; tt[0] = t < 15 ? 1 : 0; }
static void spin_back_and_again(hs32 t, hu32 *k, hs8 *tt) { (void)k; tt[0] = t < 20 ? 1 : t < 24 ? -1 : t < 40 ? 1 : -1; }
static void nothing(hs32 t, hu32 *k, hs8 *tt) { (void)t; (void)k; (void)tt; }

int main(void)
{
    hold_def cn = {10, 40, 0, HOLD_CN, 0};
    hold_def hcn = {10, 40, 0, HOLD_HCN, 0};
    hold_def bss = {10, 40, 7, HOLD_BSS, 0};
    hold_def hbss = {10, 40, 7, HOLD_HBSS, 0};
    const hold_event *e;

    /* CN released exactly at the end -> PGREAT; 3 late -> GOOD; far early -> POOR */
    setup(&cn, 1, 0); CHECK(holds_head(&H, 0, 10, J_PGREAT) == 0);
    release_at = 40; run(10, 50, hold_key0); e = tail();
    CHECK(e && e->judge == J_PGREAT && e->offset == 0 && count(HE_TAIL) == 1);
    setup(&cn, 1, 0); holds_head(&H, 0, 10, J_GREAT);
    release_at = 43; run(10, 50, hold_key0); e = tail(); CHECK(e && e->judge == J_GOOD && e->offset == 3);
    setup(&cn, 1, 0); holds_head(&H, 0, 10, J_GREAT);
    release_at = 20; run(10, 50, hold_key0); e = tail(); CHECK(e && e->judge == J_POOR && e->offset == -20);

    /* overhold: RELEASE policy -> POOR after the BAD window; AUTO -> PGREAT at the end */
    setup(&cn, 1, 0); holds_head(&H, 0, 10, J_PGREAT);
    release_at = 99; run(10, 60, hold_key0); e = tail(); CHECK(e && e->judge == J_POOR && e->offset == 7);
    { holds_cfg c = CFG; c.overhold = OVERHOLD_AUTO; setup(&cn, 1, &c); }
    holds_head(&H, 0, 10, J_PGREAT); run(10, 60, hold_key0); e = tail(); CHECK(e && e->judge == J_PGREAT && e->offset == 0);

    /* missed head (BAD) or never reported -> tail POOR at the end, no early judgement */
    setup(&cn, 1, 0); holds_head(&H, 0, 10, J_BAD);
    release_at = 99; run(10, 39, hold_key0); CHECK(count(HE_TAIL) == 0);
    run(40, 40, hold_key0); e = tail(); CHECK(e && e->judge == J_POOR);
    setup(&cn, 1, 0); run(0, 45, nothing); e = tail(); CHECK(e && e->judge == J_POOR);
    CHECK(holds_head(&H, 0, 10, J_PGREAT) == -1);   /* too late: already a miss */

    /* HCN: ticks every 4 while held; letting go breaks once and loses; grabbing again gains */
    setup(&hcn, 1, 0); holds_head(&H, 0, 10, J_PGREAT); run(10, 50, hold_cn_gap);
    CHECK(count(HE_BREAK) == 1);
    CHECK(count(HE_TICK_GAIN) == 5 && count(HE_TICK_LOSS) == 2);   /* ticks 14..38: held 14,18 | 22,26 loose | 30,34,38 held */
    e = tail(); CHECK(e && e->judge == J_PGREAT && e->offset == 0);
    /* HCN head missed: body loses until grabbed */
    setup(&hcn, 1, 0); holds_head(&H, 0, 10, J_POOR); release_at = 0; run(10, 50, hold_key0);
    CHECK(count(HE_TICK_GAIN) == 0 && count(HE_TICK_LOSS) == 7); e = tail(); CHECK(e && e->judge == J_POOR);

    /* BSS: reversing at the end -> PGREAT; standing still past the grace early -> POOR */
    setup(&bss, 1, 0); holds_head(&H, 7, 10, J_PGREAT); release_at = 41; run(10, 50, spin_then_back);
    e = tail(); CHECK(e && e->judge == J_PGREAT && e->offset == 1 && e->lane == 7);
    setup(&bss, 1, 0); holds_head(&H, 7, 10, J_PGREAT); run(10, 50, spin_stop);
    e = tail(); CHECK(e && e->judge == J_POOR && e->offset < 0);
    /* HBSS: reversing early lets go (break, losses); spinning the head's way again re-grabs */
    setup(&hbss, 1, 0); holds_head(&H, 7, 10, J_PGREAT); run(10, 50, spin_back_and_again);
    CHECK(count(HE_BREAK) == 1 && count(HE_TICK_LOSS) >= 1 && count(HE_TICK_GAIN) >= 5);
    e = tail(); CHECK(e && e->judge == J_PGREAT);

    /* holds.bin records (tools/release/holds_format.py) are this struct, 12 bytes */
    CHECK(sizeof(hold_def) == 12);
    /* validation and tail lookup */
    { hold_def bad_bss = {10, 40, 0, HOLD_BSS, 0}; CHECK(holds_reset(&H, &bad_bss, 1, &CFG) == -1); }
    { hold_def two[2] = {{20, 30, 0, HOLD_CN, 0}, {10, 30, 1, HOLD_CN, 0}}; CHECK(holds_reset(&H, two, 2, &CFG) == -1); }
    setup(&cn, 1, 0); CHECK(holds_is_tail(&H, 0, 40) && !holds_is_tail(&H, 1, 40) && !holds_is_tail(&H, 0, 10));
    holds_head(&H, 0, 10, J_GREAT); { hs8 tt[2] = {0, 0}; holds_frame(&H, 11, 1, tt); } CHECK(holds_lane_held(&H, 0));

    if (failures) { printf("%d failures\n", failures); return 1; }
    printf("holds: all tests passed\n");
    return 0;
}
