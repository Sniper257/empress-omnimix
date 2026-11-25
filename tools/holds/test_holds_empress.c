/* Host test for holds_empress.c with the engine faked:
 * cc -std=c99 -DHOLDS_TEST -Wall -Wextra -o t test_holds_empress.c holds_empress.c holds.c && ./t */
#include <stdio.h>
#include "holds.h"

void holds_set_chart(const hold_def *defs, hu32 n);
int holds_on_judgement(hu32 code, hu32 id, int dir);
void holds_frame_hook(long long tick);
void holds_spawn_hook(hu8 *note);

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static hu32 ring_code[64], ring_lane[64], nring;
static hu32 keys[2];
unsigned long long test_ring_body(hu32 code, hu32 lane) { if (nring < 64) { ring_code[nring] = code; ring_lane[nring++] = lane; } return 0; }
hu32 test_keys(hu32 side) { return keys[side]; }
static hu32 pressed[2];
hu32 test_pressed(hu32 side) { return pressed[side]; }
static hu32 tt_in[2];
hu32 test_turntable(hu32 side) { return tt_in[side]; }
static hu32 played_index[8], played_side[8], nplayed;
void test_play(hu32 index, hu32 side) { if (nplayed < 8) { played_index[nplayed] = index; played_side[nplayed++] = side; } }
int holds_note_hook(hu8 *note, hu32 handle, hs32 x, hs32 y, hu32 side, hu32 clip);
static hu32 draw_h[64], ndraw; static hs32 draw_y[64], draw_zy[64], draw_px[64], draw_py[64];
void test_draw(hu32 handle, hs32 x, hs32 y, hs32 zy, hs32 px, hs32 py) { (void)x; if (ndraw < 64) { draw_h[ndraw] = handle; draw_y[ndraw] = y; draw_zy[ndraw] = zy; draw_px[ndraw] = px; draw_py[ndraw++] = py; } }
static hu8 record[0x134] __attribute__((aligned(4)));
hu8 *test_record(void) { return record; }
static int gains;
void test_gain(hu32 side) { (void)side; gains++; }
static int spawned;
void test_native_spawn(hu8 *note) { (void)note; spawned++; }
static void make_note(hu8 *n, hu32 side, hu32 lane, short t) { hu32 i; for (i = 0; i < 0x20; i++) n[i] = 0; n[9] = (hu8)lane; n[0xa] = (hu8)side; n[0xb] = 1; *(short *)(n + 0x18) = t; }
hu32 test_chart_cursor(void) { return 0; }   /* no chart in host memory: holds_set_chart provides the records */
static int logged(hu32 code, hu32 lane) { hu32 i; int n = 0; for (i = 0; i < nring; i++) n += ring_code[i] == code && ring_lane[i] == lane; return n; }

int main(void)
{
    /* P2 key 3 CN 100..160, P1 key 1 HCN 200..230 with a 16th of 6 ticks */
    static const hold_def chart[2] = {{100, 160, 8 + 2, HOLD_CN, 0}, {200, 230, 0, HOLD_HCN, 6 * HOLDS_FP}};
    long long t;
    holds_set_chart(chart, 2);
    for (t = 0; t < 100; t++) holds_frame_hook(t);
    /* the game judges the P2 head 1 tick late; key held until the end */
    holds_on_judgement(7, 1u << 15 | 2, 0);   /* slow GOOD: still a hit */
    keys[1] = 1u << 2;
    for (t = 100; t < 161; t++) holds_frame_hook(t);
    keys[1] = 0;
    for (t = 161; t < 200; t++) holds_frame_hook(t);
    CHECK(logged(0xa, 1u << 15 | 2) == 1);        /* tail PGREAT (code 0xa) logged on P2 lane 3 */
    /* HCN head missed (code 0xf at its tick): an empty POOR at once, then one per 16th */
    nring = 0;
    holds_frame_hook(200);
    holds_on_judgement(0xf, 0, 0);
    for (t = 201; t < 240; t++) holds_frame_hook(t);
    CHECK(logged(0xb, 0) == 3);                   /* empty POORs at 200 (the miss), then 218, 224 (none at 206, 212) */
    CHECK(logged(0xf, 0) == 0 && logged(0xa, 0) == 0);  /* the game's own miss is the real POOR; end adds nothing */
    /* empty POORs and judgements far from any hold are ignored */
    nring = 0;
    holds_on_judgement(0xb, 0, 0);
    holds_on_judgement(0xa, 3, 0);
    CHECK(nring == 0);
    /* a restart (tick counter back to 0) re-arms the chart */
    nring = 0;
    holds_frame_hook(0);
    for (t = 1; t < 100; t++) holds_frame_hook(t);
    holds_on_judgement(0xa, 1u << 15 | 2, 0);   /* a PGREAT head */
    keys[1] = 1u << 2;
    for (t = 100; t < 150; t++) holds_frame_hook(t);
    keys[1] = 0;
    for (t = 150; t < 170; t++) holds_frame_hook(t);
    CHECK(logged(0xf, 1u << 15 | 2) == 1);       /* released 10 ticks early: missed POOR */
    /* spawn hook: the note at a hold's end tick on its lane is never judged; others are untouched */
    {
        static hu8 n[0x20] __attribute__((aligned(4)));
        make_note(n, 1, 2, 160);  holds_spawn_hook(n); CHECK(n[0xb] == 0);   /* P2 lane 3 CN tail */
        make_note(n, 1, 2, 100);  holds_spawn_hook(n); CHECK(n[0xb] == 1);   /* its head */
        make_note(n, 0, 2, 160);  holds_spawn_hook(n); CHECK(n[0xb] == 1);   /* same tick, other side */
        make_note(n, 0, 0, 230);  holds_spawn_hook(n); CHECK(n[0xb] == 0);   /* P1 lane 1 HCN tail */
        CHECK(spawned == 4);                                                 /* the game's spawn always runs */
    }
    /* drawing: SP container #109, last hold sprite 109:536 -> first 109:500 */
    {
        static hu8 n[0x20] __attribute__((aligned(4)));
        static const hold_def one[1] = {{300, 400, 2, HOLD_HCN, 6 * HOLDS_FP}};
        long long tt;
        *(hu32 *)(record + 0x38) = 109u << 16 | 536u;
        holds_set_chart(one, 1);
        for (tt = 0; tt < 250; tt++) holds_frame_hook(tt);
        /* a normal note is left to the game */
        make_note(n, 0, 3, 260); ndraw = 0;
        CHECK(holds_note_hook(n, 109u << 16 | 601, 50, 200, 0, 0) == 0 && ndraw == 0);
        /* head on screen, tail still above: body from the lane top to the head, then the start cap */
        make_note(n, 0, 2, 300); ndraw = 0;
        CHECK(holds_note_hook(n, 109u << 16 | 601, 80, 200, 0, 0) == 1);
        CHECK(ndraw == 2 && draw_h[0] == (109u << 16 | (500 + 10 + 2)) && draw_py[0] == 0 && draw_zy[0] == (203 - 9 + 4 + 16) * 100 / 9);
        CHECK(draw_h[1] == (109u << 16 | 510) && draw_y[1] == 203 - 9 && draw_py[1] == 0);   /* HCN white start cap, bottom at y+3 */
        CHECK(draw_px[0] == 8 && draw_px[1] == 8);   /* body and cap 15 px wide, a pixel left of the note's x (hold_sprites PIVOT_X) */
        /* same tick: tail on screen too -> body from the tail to the head, end cap */
        make_note(n, 0, 2, 400); ndraw = 0;
        CHECK(holds_note_hook(n, 109u << 16 | 601, 80, 40, 0, 0) == 1);
        CHECK(ndraw == 2 && draw_y[0] == 48 && draw_zy[0] == (203 - 9 + 4 - 48) * 100 / 9 && draw_h[1] == (109u << 16 | 511) && draw_y[1] == 43 && draw_py[1] == 0);
        /* a tail note a tick before the end (shifted off a shared row) is drawn where the end is:
         * 280 px over 151 ticks is ~1.85 px per tick, so one tick higher */
        make_note(n, 0, 2, 399); ndraw = 0;
        CHECK(holds_note_hook(n, 109u << 16 | 601, 80, 40, 0, 0) == 1);
        CHECK(ndraw == 2 && draw_y[1] == 42 && draw_h[1] == (109u << 16 | 511));
        /* the spawner never lets the game judge it */
        make_note(n, 0, 2, 399); holds_spawn_hook(n); CHECK(n[0xb] == 0);
        make_note(n, 0, 2, 300); holds_spawn_hook(n); CHECK(n[0xb] == 1);   /* the head stays judged */
        /* the wide container has no hold sprites in this record: native note */
        make_note(n, 0, 2, 300); ndraw = 0;
        CHECK(holds_note_hook(n, 146u << 16 | 601, 80, 200, 0, 0) == 0);
        /* head hit and held, both notes gone: the frame pass draws body + start cap on the judge line */
        for (tt = 250; tt <= 300; tt++) holds_frame_hook(tt);
        holds_on_judgement(0xa, 2, 0);
        keys[0] = 1u << 2;
        for (tt = 301; tt < 305; tt++) holds_frame_hook(tt);
        ndraw = 0;
        holds_frame_hook(305);
        CHECK(ndraw == 2 && draw_y[0] == -16 && draw_y[1] == 0x143 - 9 && draw_zy[0] == (0x143 - 9 + 4 + 16) * 100 / 9);
        CHECK(draw_h[0] == (109u << 16 | (512 + 2)));   /* held past the ramp: the brightest beam */
        { hu32 k, steady = 1;                           /* and it stays lit: no flashing */
          for (k = 0; k < 6; k++) { ndraw = 0; holds_frame_hook(306 + k); steady &= ndraw >= 1 && draw_h[0] == (109u << 16 | (512 + 2)); }
          CHECK(steady); }
        gains = 0;
        for (tt = 312; tt < 340; tt++) holds_frame_hook(tt);
        CHECK(gains >= 4);                                              /* HCN held: gauge gain every 16th */
        /* the tail at the line while the release is still being judged: nothing fills the lane */
        for (tt = 340; tt < 399; tt++) holds_frame_hook(tt);
        ndraw = 0;
        make_note(n, 0, 2, 400); holds_note_hook(n, 109u << 16 | 601, 80, 0x142, 0, 0);
        holds_frame_hook(399);
        CHECK(ndraw == 0);
        ndraw = 0;
        holds_frame_hook(400);                       /* tail note not drawn this frame: still nothing */
        CHECK(ndraw == 0);
        /* after the end, the leftover tail note draws nothing (and is not drawn natively) */
        for (tt = 401; tt < 410; tt++) holds_frame_hook(tt);
        make_note(n, 0, 2, 400); ndraw = 0;
        CHECK(holds_note_hook(n, 109u << 16 | 601, 80, 0x140, 0, 0) == 1 && ndraw == 0);
        CHECK(n[9] == 2);                                               /* left to the game (not marked consumed) */
        keys[0] = 0;
    }
    /* a missed CN keeps its unlit beam down to the line (no start cap) until its tail is on screen */
    {
        static hu8 n[0x20] __attribute__((aligned(4)));
        static const hold_def one[1] = {{300, 500, 2, HOLD_CN, 0}};
        long long tt;
        *(hu32 *)(record + 0x38) = 109u << 16 | 536u;
        holds_set_chart(one, 1);
        keys[0] = keys[1] = 0;
        for (tt = 0; tt < 290; tt++) holds_frame_hook(tt);
        make_note(n, 0, 2, 300); holds_note_hook(n, 109u << 16 | 601, 80, 300, 0, 0);   /* head seen: lane x, container */
        for (tt = 290; tt < 330; tt++) holds_frame_hook(tt);                           /* missed (late rule) */
        ndraw = 0;
        holds_frame_hook(330);
        CHECK(ndraw == 1 && draw_y[0] == -16 && draw_h[0] == (109u << 16 | (500 + 2)));  /* unlit CN beam, no cap */
        CHECK(draw_zy[0] == (0x143 + 16) * 100 / 9);                                         /* down to the line */
    }
    /* the game removed a missed head before the late rule decided: the unlit beam stays (it vanished) */
    {
        static hu8 n[0x20] __attribute__((aligned(4)));
        static const hold_def one[1] = {{300, 500, 2, HOLD_CN, 0}};
        long long tt;
        *(hu32 *)(record + 0x38) = 109u << 16 | 536u;
        holds_set_chart(one, 1);
        keys[0] = keys[1] = 0;
        for (tt = 0; tt <= 300; tt++) holds_frame_hook(tt);
        make_note(n, 0, 2, 300); holds_note_hook(n, 109u << 16 | 601, 80, 0x140, 0, 0);
        for (tt = 301; tt < 306; tt++) holds_frame_hook(tt);
        ndraw = 0;
        holds_frame_hook(306);                                                         /* still before the late rule */
        CHECK(ndraw == 1 && draw_y[0] == -16 && draw_h[0] == (109u << 16 | (500 + 2)));
    }
    /* a CN let go far too early (POOR, finished): the unlit beam stays until its tail is on screen */
    {
        static hu8 n[0x20] __attribute__((aligned(4)));
        static const hold_def one[1] = {{300, 500, 2, HOLD_CN, 0}};
        long long tt;
        *(hu32 *)(record + 0x38) = 109u << 16 | 536u;
        holds_set_chart(one, 1);
        keys[0] = 1u << 2;
        for (tt = 0; tt <= 300; tt++) holds_frame_hook(tt);
        make_note(n, 0, 2, 300); holds_note_hook(n, 109u << 16 | 601, 80, 0x140, 0, 0);
        holds_on_judgement(0xa, 2, 0);
        for (tt = 301; tt < 320; tt++) holds_frame_hook(tt);
        keys[0] = 0;
        for (tt = 320; tt < 340; tt++) holds_frame_hook(tt);                           /* POOR at 320 */
        ndraw = 0;
        holds_frame_hook(340);
        CHECK(ndraw == 1 && draw_y[0] == -16 && draw_h[0] == (109u << 16 | (500 + 2)));  /* unlit, no cap */
    }
    /* a BSS head the ring never reported: gone while the turntable moves = hit; lit beam and cap on the line */
    {
        static hu8 n[0x20] __attribute__((aligned(4)));
        static const hold_def one[1] = {{300, 500, 7, HOLD_BSS, 0}};
        long long tt;
        *(hu32 *)(record + 0x38) = 109u << 16 | 536u;
        holds_set_chart(one, 1);
        keys[0] = 0; tt_in[0] = 1;
        for (tt = 0; tt <= 300; tt++) holds_frame_hook(tt);
        make_note(n, 0, 7, 300); holds_note_hook(n, 109u << 16 | 601, 30, 0x140, 0, 0);
        for (tt = 301; tt < 310; tt++) holds_frame_hook(tt);
        ndraw = 0;
        holds_frame_hook(310);
        CHECK(ndraw == 2 && (draw_h[0] & 0xffff) >= 500 + 24 + 3 && (draw_h[0] & 0xffff) <= 500 + 24 + 4);   /* lit BSS beam */
        CHECK(draw_h[1] == (109u << 16 | (500 + 24)) && draw_y[1] == 0x143 - 9 && draw_py[1] == 0);        /* BSS start cap */
        CHECK(draw_px[0] == 14 && draw_px[1] == 14);
        tt_in[0] = 0;
    }
    /* multi-spin scratch: two touching BSS sections, reversed at the joint. Both ends PGREAT (one per
     * section, the second section's head from the scratch note at the joint); the middle note is drawn
     * centred on the joint, the first section has no end cap there */
    {
        static hu8 n[0x20] __attribute__((aligned(4)));
        static const hold_def mss[2] = {{300, 340, 7, HOLD_BSS, 0}, {340, 400, 7, HOLD_BSS, 0}};
        long long tt;
        hu32 before;
        *(hu32 *)(record + 0x38) = 109u << 16 | 536u;
        holds_set_chart(mss, 2);
        keys[0] = 0; tt_in[0] = 0; nring = 0;
        for (tt = 0; tt < 300; tt++) holds_frame_hook(tt);
        holds_on_judgement(0xa, 7, 0);                                   /* first head */
        tt_in[0] = 1;
        for (tt = 300; tt < 340; tt++) holds_frame_hook(tt);
        tt_in[0] = 0x81;                                                 /* reversed at the joint */
        holds_frame_hook(340);
        holds_on_judgement(0xa, 7, 0);                                   /* the middle note: second head */
        before = nring;
        for (tt = 341; tt < 400; tt++) holds_frame_hook(tt);
        tt_in[0] = 1;                                                    /* reversed again at the end */
        for (tt = 400; tt < 420; tt++) holds_frame_hook(tt);
        CHECK(logged(0xa, 7) == 2 && logged(8, 7) == 0 && logged(9, 7) == 0 && logged(0xe, 7) == 0 && logged(0xf, 7) == 0);
        (void)before;
        tt_in[0] = 0;
        /* drawing, a fresh play: the joint's head draws the middle note, the first section's tail no end cap */
        holds_set_chart(mss, 2);
        for (tt = 0; tt < 250; tt++) holds_frame_hook(tt);
        make_note(n, 0, 7, 340); ndraw = 0;
        CHECK(holds_note_hook(n, 109u << 16 | 601, 30, 100, 0, 0) == 1);
        CHECK(ndraw == 2 && draw_h[1] == (109u << 16 | (500 + 36)) && draw_y[1] == 103 - 14 / 2 && draw_py[1] == 0);
        make_note(n, 0, 7, 339); ndraw = 0;                              /* first section's tail note */
        CHECK(holds_note_hook(n, 109u << 16 | 601, 30, 110, 0, 0) == 1);
        CHECK(ndraw == 1 && (draw_h[0] & 0xffff) >= 500 + 24 + 2 && (draw_h[0] & 0xffff) <= 500 + 24 + 4);   /* body only */
    }
    /* the tail drawn before the head in a frame: the body runs to where the head is now (last frame's y
     * plus one tick of scroll), not where it was, which left a gap above the start cap */
    {
        static hu8 n[0x20] __attribute__((aligned(4)));
        static const hold_def one[1] = {{300, 400, 2, HOLD_CN, 0}};
        long long tt;
        hs32 ppt, bottom;
        *(hu32 *)(record + 0x38) = 109u << 16 | 536u;
        holds_set_chart(one, 1);
        keys[0] = 0;
        for (tt = 0; tt <= 250; tt++) holds_frame_hook(tt);
        make_note(n, 0, 2, 300); holds_note_hook(n, 109u << 16 | 601, 80, 100, 0, 0);   /* head at y 100 */
        holds_frame_hook(251);
        make_note(n, 0, 2, 400); ndraw = 0;
        CHECK(holds_note_hook(n, 109u << 16 | 601, 80, 0x140 - 149 * 2, 0, 0) == 1);       /* tail first: 2 px a tick */
        ppt = 2;
        bottom = 100 + ppt + 3 - 9 + 4;                                                   /* head y now, into the cap */
        CHECK(ndraw == 2 && draw_y[0] + draw_zy[0] * 9 / 100 >= bottom - 1 && draw_y[0] + draw_zy[0] * 9 / 100 <= bottom);
    }
    /* the empty POOR the game gives for a turntable reversal at a BSS end (it finds the never-judged tail
     * note) is dropped; one away from any hold end is the game's own */
    {
        static const hold_def one[1] = {{300, 400, 7, HOLD_BSS, 0}};
        long long tt;
        holds_set_chart(one, 1);
        for (tt = 0; tt <= 402; tt++) holds_frame_hook(tt);
        CHECK(holds_on_judgement(0xb, 7, -1) == 1);
        CHECK(holds_on_judgement(0xb, 3, -1) == 0);                      /* another lane */
        for (tt = 403; tt <= 500; tt++) holds_frame_hook(tt);
        CHECK(holds_on_judgement(0xb, 7, -1) == 0);                      /* long after the end */
    }
    /* CN held past the tail window: BAD (code 8) on that lane, like the arcade */
    {
        static const hold_def one[1] = {{300, 360, 3, HOLD_CN, 0}};
        long long tt;
        holds_set_chart(one, 1);
        keys[0] = keys[1] = 0;
        for (tt = 0; tt < 300; tt++) holds_frame_hook(tt);
        nring = 0;
        holds_on_judgement(0xa, 3, 0);
        keys[0] = 1u << 3;
        for (tt = 300; tt < 380; tt++) holds_frame_hook(tt);
        keys[0] = 0;
        for (tt = 380; tt < 400; tt++) holds_frame_hook(tt);
        CHECK(logged(8, 3) == 1 && logged(0xa, 3) == 0 && logged(0xf, 3) == 0);
    }
    /* BSS: the backspin at the end plays the tail note's keysound; a CN end plays nothing */
    {
        static hu8 tn[0x20] __attribute__((aligned(4))), cn[0x20] __attribute__((aligned(4)));
        static const hold_def two[2] = {{300, 360, 7, HOLD_BSS, 0}, {320, 380, 2, HOLD_CN, 0}};
        long long tt;
        holds_set_chart(two, 2);
        keys[0] = keys[1] = 0; tt_in[0] = 0; nplayed = 0;
        for (tt = 0; tt < 250; tt++) holds_frame_hook(tt);
        make_note(tn, 0, 7, 360); tn[8] = 0x33; holds_spawn_hook(tn);
        make_note(cn, 0, 2, 380); cn[8] = 0x21; holds_spawn_hook(cn);
        for (tt = 250; tt < 300; tt++) holds_frame_hook(tt);
        holds_on_judgement(0xa, 7, 0);
        tt_in[0] = 0x01;                                   /* spinning one way */
        for (tt = 300; tt < 320; tt++) holds_frame_hook(tt);
        holds_on_judgement(0xa, 2, 0); keys[0] = 1u << 2;
        for (tt = 320; tt < 360; tt++) holds_frame_hook(tt);
        tt_in[0] = 0x81;                                   /* backspin at the end */
        for (tt = 360; tt < 380; tt++) holds_frame_hook(tt);
        keys[0] = 0; tt_in[0] = 0;
        for (tt = 380; tt < 400; tt++) holds_frame_hook(tt);
        CHECK(nplayed == 1 && played_index[0] == 0x33 && played_side[0] == 0);
        CHECK(logged(0xa, 7) == 1 && logged(0xa, 2) == 1);   /* both ends PGREAT */
    }
    /* a hit the game never reports: the head note stops being drawn while its key is down */
    {
        static hu8 n[0x20] __attribute__((aligned(4)));
        static const hold_def one[1] = {{300, 400, 2, HOLD_HCN, 6 * HOLDS_FP}};
        long long tt;
        hu32 i;
        int bad = 0;
        holds_set_chart(one, 1);
        keys[0] = keys[1] = 0;
        for (tt = 0; tt < 280; tt++) holds_frame_hook(tt);
        nring = 0; gains = 0;
        for (tt = 280; tt < 298; tt++) { holds_frame_hook(tt); make_note(n, 0, 2, 300); holds_note_hook(n, 109u << 16 | 601, 80, 200, 0, 0); }
        keys[0] = 1u << 2;                         /* hit at 298: the note is gone from then on */
        for (tt = 298; tt < 360; tt++) holds_frame_hook(tt);
        for (i = 0; i < nring; i++) if (ring_code[i] == 0xf || ring_code[i] == 0xb) bad++;
        CHECK(bad == 0 && gains >= 8);
        /* the same with the key up: the late rule still makes it a miss */
        holds_set_chart(one, 1);
        keys[0] = 0;
        for (tt = 0; tt < 280; tt++) holds_frame_hook(tt);
        nring = 0;
        for (tt = 280; tt < 298; tt++) { holds_frame_hook(tt); make_note(n, 0, 2, 300); holds_note_hook(n, 109u << 16 | 601, 80, 200, 0, 0); }
        for (tt = 298; tt < 330; tt++) holds_frame_hook(tt);
        for (i = 0, bad = 0; i < nring; i++) if (ring_code[i] == 0xb) bad++;
        CHECK(bad >= 1);
    }
    /* the held mask lags the press by a tick: the press-tick mask keeps the HCN from breaking */
    {
        static const hold_def one[1] = {{300, 400, 2, HOLD_HCN, 6 * HOLDS_FP}};
        long long tt;
        hu32 i;
        int bad = 0;
        holds_set_chart(one, 1);
        keys[0] = keys[1] = 0;
        for (tt = 0; tt < 299; tt++) holds_frame_hook(tt);
        nring = 0;
        holds_on_judgement(5, 2, 0);
        pressed[0] = 1u << 2;                      /* pressed this tick, not yet in the held mask */
        holds_frame_hook(299);
        pressed[0] = 0;
        keys[0] = 1u << 2;
        for (tt = 300; tt < 400; tt++) holds_frame_hook(tt);
        keys[0] = 0;
        for (tt = 400; tt < 420; tt++) holds_frame_hook(tt);
        for (i = 0; i < nring; i++) if (ring_code[i] == 0xf || ring_code[i] == 0xb) bad++;
        CHECK(bad == 0);
        CHECK(nring >= 1 && ring_code[nring - 1] == 0xa);                    /* PGREAT at the end */
    }
    if (failures) { printf("%d failures\n", failures); return 1; }
    printf("holds_empress: all tests passed\n");
    return 0;
}
