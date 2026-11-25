/* Host test for build_containers' shared sprite runs (tools/release/test_launcher_c.py builds it 32-bit). */
#define main launcher_main
#include "main.c"
#undef main
#include <assert.h>
static u8 A[3][8] = {{1,2,3},{4,5,6},{7,8,9}}, B[8] = {9,9,9};
static void q(int cont, int set, u8 *buf, int len) {
    sq_t *s = sq_new(); s->cont = cont; s->set = set; s->buf = buf; s->len = len; s->hash = sq_hash(buf, len);
}
int main(void) {
    int i, r;
    /* song 1: three sprites to #109 then the same to #146; song 2 the same; song 3 one sprite differs;
       a folder label (set 0) in between; song 4 again like song 1 */
    for (i = 0; i < 3; i++) q(109, 1, A[i], 8);
    for (i = 0; i < 3; i++) q(146, 1, A[i], 8);
    for (i = 0; i < 3; i++) q(109, 2, A[i], 8);
    for (i = 0; i < 3; i++) q(146, 2, A[i], 8);
    q(0, 0, A[0], 8);
    q(109, 3, A[0], 8); q(109, 3, B, 8); q(109, 3, A[2], 8);
    for (i = 0; i < 3; i++) q(109, 4, A[i], 8);
    r = sq_share_runs();
    assert(r == 3 + 3 + 3);
    for (i = 0; i < 6; i++) assert(sq[i].from == -1);                 /* song 1 kept */
    for (i = 0; i < 3; i++) assert(sq[6 + i].from == i);              /* song 2 #109 -> song 1 #109 */
    for (i = 0; i < 3; i++) assert(sq[9 + i].from == 3 + i);          /* song 2 #146 -> song 1 #146 */
    assert(sq[12].from == -1);                                          /* folder label never shared */
    for (i = 13; i < 16; i++) assert(sq[i].from == -1);               /* song 3: one sprite differs: own run */
    for (i = 0; i < 3; i++) assert(sq[16 + i].from == i);             /* song 4 -> song 1, not song 3 */
    puts("share runs: ok");
    return 0;
}
