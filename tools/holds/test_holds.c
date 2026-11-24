/* Host tests for holds.c: cc -std=c99 -Wall -Wextra -o test_holds test_holds.c holds.c && ./test_holds */
#include <stdio.h>
#include "holds.h"

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static holds_t H;
static hold_event ev[256];
static hu32 nev;
/* PGREAT window 6 ticks, 16th = 4 ticks, turntable may stand still 2 ticks, held past the window = POOR */
static const holds_cfg CFG = { {1, 2, 4, 6}, 6, 4 * HOLDS_FP, 2, OVERHOLD_RELEASE, J_POOR, 0, 0 };

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
static void setup(const hold_def *d, const holds_cfg *c) { nev = 0; CHECK(holds_reset(&H, d, 1, c ? c : &CFG) == 0); }
static int tail_is(hu32 judge, hs32 offset) { const hold_event *e = tail(); return e && e->judge == judge && e->offset == offset && count(HE_TAIL) == 1; }

static hs32 release_at;
static void hold_key0(hs32 t, hu32 *k, hs8 *tt) { (void)tt; if (t < release_at) *k = 1; }
static void hold_gap(hs32 t, hu32 *k, hs8 *tt) { (void)tt; if (t < 20 || (t >= 28 && t < 40)) *k = 1; }
static void spin_then_back(hs32 t, hu32 *k, hs8 *tt) { (void)k; tt[0] = t < release_at ? 1 : -1; }
static void spin_then_stop(hs32 t, hu32 *k, hs8 *tt) { (void)k; tt[0] = t < release_at ? 1 : 0; }
static void spin_back_and_again(hs32 t, hu32 *k, hs8 *tt) { (void)k; tt[0] = t < 20 ? 1 : t < 24 ? -1 : t < 40 ? 1 : -1; }
/* the held mask lags the press: up for two ticks right after the head */
static void lag_then_hold(hs32 t, hu32 *k, hs8 *tt) { (void)tt; if ((t < 11 || t >= 13) && t < release_at) *k = 1; }
static void nothing(hs32 t, hu32 *k, hs8 *tt) { (void)t; (void)k; (void)tt; }

int main(void)
{
    hold_def cn = {10, 40, 0, HOLD_CN, 0};
    hold_def hcn = {10, 40, 0, HOLD_HCN, 4 * HOLDS_FP};
    hold_def bss = {10, 40, 7, HOLD_BSS, 0};
    hold_def hbss = {10, 40, 7, HOLD_HBSS, 4 * HOLDS_FP};

    /* keyed fallback: the pending key hold whose key is down; BSS and keys that are up never match */
    setup(&cn, 0); CHECK(holds_head_keyed(&H, 0, 12, 15, J_GREAT) == -1); CHECK(holds_head_keyed(&H, 2, 12, 15, J_GREAT) == -1);
    CHECK(holds_head_keyed(&H, 1, 12, 15, J_GREAT) == 0 && holds_lane_held(&H, 0));
    setup(&bss, 0); CHECK(holds_head_keyed(&H, 0x80, 12, 15, J_GREAT) == -1);

    /* release_grace: a short up right after the head is no release; a real release is timed from its first up tick */
    { holds_cfg c = CFG; c.release_grace = 3;
      setup(&hcn, &c); holds_head(&H, 0, 10, J_GREAT, 10); release_at = 40; run(10, 50, lag_then_hold);
      CHECK(count(HE_BREAK) == 0 && count(HE_TICK_LOSS) == 0 && tail_is(J_PGREAT, 0));
      setup(&cn, &c); holds_head(&H, 0, 10, J_GREAT, 10); release_at = 40; run(10, 50, lag_then_hold); CHECK(tail_is(J_PGREAT, 0));
      setup(&cn, &c); holds_head(&H, 0, 10, J_GREAT, 10); release_at = 30; run(10, 50, hold_key0); CHECK(tail_is(J_POOR, -10));
      setup(&hcn, &c); holds_head(&H, 0, 10, J_GREAT, 10); release_at = 20; run(10, 50, hold_key0);
      { hu32 j = 0; while (j < nev && ev[j].kind != HE_BREAK) j++; CHECK(count(HE_BREAK) == 1 && j < nev && ev[j].offset == -20); }
      setup(&cn, &c); holds_head(&H, 0, 10, J_PGREAT, 10); release_at = 99; run(10, 60, hold_key0); CHECK(tail_is(J_POOR, 7)); }

    /* CN: a release anywhere in the wide window is PGREAT; early is POOR; held too long is POOR */
    setup(&cn, 0); CHECK(holds_head(&H, 0, 10, J_PGREAT, 10) == 0);
    release_at = 40; run(10, 50, hold_key0); CHECK(tail_is(J_PGREAT, 0));
    setup(&cn, 0); holds_head(&H, 0, 10, J_GREAT, 10); release_at = 45; run(10, 50, hold_key0); CHECK(tail_is(J_PGREAT, 5));
    setup(&cn, 0); holds_head(&H, 0, 10, J_GREAT, 10); release_at = 33; run(10, 50, hold_key0); CHECK(tail_is(J_POOR, -7));
    setup(&cn, 0); holds_head(&H, 0, 10, J_PGREAT, 10); release_at = 99; run(10, 60, hold_key0); CHECK(tail_is(J_POOR, 7));
    { holds_cfg c = CFG; c.overhold_judge = J_BAD; setup(&cn, &c); }
    holds_head(&H, 0, 10, J_PGREAT, 10); run(10, 60, hold_key0); CHECK(tail_is(J_BAD, 7));
    { holds_cfg c = CFG; c.overhold = OVERHOLD_AUTO; setup(&cn, &c); }
    holds_head(&H, 0, 10, J_PGREAT, 10); run(10, 60, hold_key0); CHECK(tail_is(J_PGREAT, 0));
    { holds_cfg c = CFG; c.tail_graded = 1; setup(&cn, &c); }
    holds_head(&H, 0, 10, J_PGREAT, 10); release_at = 43; run(10, 50, hold_key0); CHECK(tail_is(J_GOOD, 3));

    /* CN head missed (BAD, or never reported): tail POOR at the end, nothing earlier */
    setup(&cn, 0); holds_head(&H, 0, 10, J_BAD, 10); release_at = 99; run(10, 39, hold_key0); CHECK(count(HE_TAIL) == 0);
    run(40, 40, hold_key0); CHECK(tail_is(J_POOR, 0));
    setup(&cn, 0); run(0, 45, nothing); CHECK(tail() && tail()->judge == J_POOR);
    CHECK(holds_head(&H, 0, 10, J_PGREAT, 20) == -1);

    /* HCN held throughout: a gain every 16th (14, 18 .. 38), tail judged like a CN */
    setup(&hcn, 0); holds_head(&H, 0, 10, J_PGREAT, 10); release_at = 40; run(10, 50, hold_key0);
    CHECK(count(HE_TICK_GAIN) == 7 && count(HE_TICK_LOSS) == 0 && count(HE_BREAK) == 0 && tail_is(J_PGREAT, 0));
    /* let go at 20: real POOR + empty POOR at once, empty POOR at 24; re-grab at 28: gains at 32, 36;
       broken, so the tail is J_NONE */
    setup(&hcn, 0); holds_head(&H, 0, 10, J_PGREAT, 10); run(10, 50, hold_gap);
    CHECK(count(HE_BREAK) == 1 && count(HE_TICK_LOSS) == 2 && count(HE_TICK_GAIN) == 4);
    CHECK(tail_is(J_NONE, 0));
    /* HCN head missed: the game's POOR, an empty POOR at once, then every 16th; the end adds nothing */
    setup(&hcn, 0); holds_head(&H, 0, 10, J_POOR, 10); release_at = 0; run(10, 50, hold_key0);
    CHECK(count(HE_BREAK) == 0 && count(HE_TICK_LOSS) == 8 && count(HE_TICK_GAIN) == 0 && tail_is(J_NONE, 0));

    /* BSS: spinning back at the end is PGREAT; stopping inside the window too; stopping early is POOR */
    setup(&bss, 0); holds_head(&H, 7, 10, J_PGREAT, 10); release_at = 41; run(10, 50, spin_then_back);
    CHECK(tail_is(J_PGREAT, 1) && tail()->lane == 7);
    setup(&bss, 0); holds_head(&H, 7, 10, J_PGREAT, 10); release_at = 37; run(10, 50, spin_then_stop); CHECK(tail_is(J_PGREAT, -3));
    setup(&bss, 0); holds_head(&H, 7, 10, J_PGREAT, 10); release_at = 15; run(10, 50, spin_then_stop); CHECK(tail_is(J_POOR, -22));
    /* HBSS: spinning back early lets go; spinning the head's way again re-grabs */
    setup(&hbss, 0); holds_head(&H, 7, 10, J_PGREAT, 10); run(10, 50, spin_back_and_again);
    CHECK(count(HE_BREAK) == 1 && count(HE_TICK_LOSS) >= 1 && count(HE_TICK_GAIN) >= 4 && tail_is(J_NONE, 0));

    /* a head judged 2 ticks late is matched to the nearest pending hold on its lane only */
    { hold_def two[2] = {{10, 40, 0, HOLD_CN, 0}, {50, 60, 0, HOLD_CN, 0}};
      nev = 0; CHECK(holds_reset(&H, two, 2, &CFG) == 0);
      CHECK(holds_head_near(&H, 1, 12, 15, J_GREAT) == -1);
      CHECK(holds_head_near(&H, 0, 12, 15, J_GREAT) == 0 && holds_head_near(&H, 0, 13, 15, J_GREAT) == -1); }

    /* records, validation, helpers */
    CHECK(sizeof(hold_def) == 12);          /* holds.bin records (tools/release/holds_format.py) */
    { hold_def bad_bss = {10, 40, 0, HOLD_BSS, 0}; CHECK(holds_reset(&H, &bad_bss, 1, &CFG) == -1); }
    { hold_def two[2] = {{20, 30, 0, HOLD_CN, 0}, {10, 30, 1, HOLD_CN, 0}}; CHECK(holds_reset(&H, two, 2, &CFG) == -1); }
    setup(&cn, 0); CHECK(holds_is_tail(&H, 0, 40) && !holds_is_tail(&H, 1, 40) && !holds_is_tail(&H, 0, 10));
    holds_head(&H, 0, 10, J_GREAT, 10); { hs8 tt[2] = {0, 0}; holds_frame(&H, 11, 1, tt); } CHECK(holds_lane_held(&H, 0));

    if (failures) { printf("%d failures\n", failures); return 1; }
    printf("holds: all tests passed\n");
    return 0;
}
