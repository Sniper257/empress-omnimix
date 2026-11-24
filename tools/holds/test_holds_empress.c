/* Host test for holds_empress.c with the engine faked:
 * cc -std=c99 -DHOLDS_TEST -Wall -Wextra -o t test_holds_empress.c holds_empress.c holds.c && ./t */
#include <stdio.h>
#include "holds.h"

void holds_set_chart(const hold_def *defs, hu32 n);
void holds_on_judgement(hu32 code, hu32 id);
void holds_frame_hook(long long tick);

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static hu32 ring_code[64], ring_lane[64], nring;
static hu32 keys[2];
unsigned long long test_ring_body(hu32 code, hu32 lane) { if (nring < 64) { ring_code[nring] = code; ring_lane[nring++] = lane; } return 0; }
hu32 test_keys(hu32 side) { return keys[side]; }
hu32 test_turntable(hu32 side) { (void)side; return 0; }
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
    if (failures) { printf("%d failures\n", failures); return 1; }
    printf("holds_empress: all tests passed\n");
    return 0;
}
