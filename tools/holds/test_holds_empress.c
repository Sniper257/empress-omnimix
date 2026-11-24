/* Host test for holds_empress.c with the engine faked:
 * cc -std=c99 -DHOLDS_TEST -Wall -Wextra -o t test_holds_empress.c holds_empress.c holds.c && ./t */
#include <stdio.h>
#include "holds.h"

void holds_set_chart(const hold_def *defs, hu32 n);
void holds_on_judgement(hu32 code, hu32 id);
void holds_frame_hook(long long tick);
void holds_spawn_hook(hu8 *note);

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static hu32 ring_code[64], ring_lane[64], nring;
static hu32 keys[2];
unsigned long long test_ring_body(hu32 code, hu32 lane) { if (nring < 64) { ring_code[nring] = code; ring_lane[nring++] = lane; } return 0; }
hu32 test_keys(hu32 side) { return keys[side]; }
hu32 test_turntable(hu32 side) { (void)side; return 0; }
int holds_note_hook(hu8 *note, hu32 handle, hs32 x, hs32 y, hu32 side, hu32 clip);
static hu32 draw_h[64], ndraw; static hs32 draw_y[64], draw_zy[64], draw_py[64];
void test_draw(hu32 handle, hs32 x, hs32 y, hs32 zy, hs32 py) { (void)x; if (ndraw < 64) { draw_h[ndraw] = handle; draw_y[ndraw] = y; draw_zy[ndraw] = zy; draw_py[ndraw++] = py; } }
static hu8 record[0x134] __attribute__((aligned(4)));
hu8 *test_record(void) { return record; }
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
    /* the game judges the P2 head 1 tick late (GREAT, code 5); key held until the end */
    holds_on_judgement(5, 1u << 15 | 2);
    keys[1] = 1u << 2;
    for (t = 100; t < 161; t++) holds_frame_hook(t);
    keys[1] = 0;
    for (t = 161; t < 200; t++) holds_frame_hook(t);
    CHECK(logged(4, 1u << 15 | 2) == 1);          /* tail PGREAT logged on P2 lane 3 */
    /* HCN head missed (code 0xf at its tick): an empty POOR at once, then one per 16th */
    nring = 0;
    holds_frame_hook(200);
    holds_on_judgement(0xf, 0);
    for (t = 201; t < 240; t++) holds_frame_hook(t);
    CHECK(logged(0xb, 0) == 5);                   /* empty POORs at 200 (release), 206, 212, 218, 224 */
    CHECK(logged(0xf, 0) == 0 && logged(4, 0) == 0);  /* the game's own miss is the real POOR; end adds nothing */
    /* empty POORs and judgements far from any hold are ignored */
    nring = 0;
    holds_on_judgement(0xb, 0);
    holds_on_judgement(4, 3);
    CHECK(nring == 0);
    /* a restart (tick counter back to 0) re-arms the chart */
    nring = 0;
    holds_frame_hook(0);
    for (t = 1; t < 100; t++) holds_frame_hook(t);
    holds_on_judgement(4, 1u << 15 | 2);
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
    /* drawing: SP container #109, last hold sprite 109:535 -> first 109:500 */
    {
        static hu8 n[0x20] __attribute__((aligned(4)));
        static const hold_def one[1] = {{300, 400, 2, HOLD_HCN, 6 * HOLDS_FP}};
        long long tt;
        *(hu32 *)(record + 0x38) = 109u << 16 | 535u;
        holds_set_chart(one, 1);
        for (tt = 0; tt < 250; tt++) holds_frame_hook(tt);
        /* a normal note is left to the game */
        make_note(n, 0, 3, 260); ndraw = 0;
        CHECK(holds_note_hook(n, 109u << 16 | 601, 50, 200, 0, 0) == 0 && ndraw == 0);
        /* head on screen, tail still above: body from the lane top to the head, then the start cap */
        make_note(n, 0, 2, 300); ndraw = 0;
        CHECK(holds_note_hook(n, 109u << 16 | 601, 80, 200, 0, 0) == 1);
        CHECK(ndraw == 2 && draw_h[0] == (109u << 16 | (500 + 10 + 2)) && draw_py[0] == 0 && draw_zy[0] == (200 + 16) * 10);
        CHECK(draw_h[1] == (109u << 16 | 510) && draw_y[1] == 200);   /* HCN white start cap */
        /* same tick: tail on screen too -> body from the tail to the head, end cap */
        make_note(n, 0, 2, 400); ndraw = 0;
        CHECK(holds_note_hook(n, 109u << 16 | 601, 80, 40, 0, 0) == 1);
        CHECK(ndraw == 2 && draw_y[0] == 40 && draw_zy[0] == (200 - 40) * 10 && draw_h[1] == (109u << 16 | 511));
        /* the wide container has no hold sprites in this record: native note */
        make_note(n, 0, 2, 300); ndraw = 0;
        CHECK(holds_note_hook(n, 146u << 16 | 601, 80, 200, 0, 0) == 0);
        /* head hit and held, both notes gone: the frame pass draws body + start cap on the judge line */
        for (tt = 250; tt <= 300; tt++) holds_frame_hook(tt);
        holds_on_judgement(4, 2);
        keys[0] = 1u << 2;
        for (tt = 301; tt < 305; tt++) holds_frame_hook(tt);
        ndraw = 0;
        holds_frame_hook(305);
        CHECK(ndraw == 2 && draw_y[0] == -16 && draw_y[1] == 0x140);
        CHECK(draw_h[0] == (109u << 16 | (512 + 1)) || draw_h[0] == (109u << 16 | (512 + 2)));   /* held: beam1/2 */
        keys[0] = 0;
    }
    if (failures) { printf("%d failures\n", failures); return 1; }
    printf("holds_empress: all tests passed\n");
    return 0;
}
