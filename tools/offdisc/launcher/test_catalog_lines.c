/* Host test for song-folder identity lines (song.txt `catalog ...`; tools/release/test_launcher_c.py). */
#define main launcher_main
#include "main.c"
#undef main
#include <assert.h>
#define H1 "0123456789abcdef0123456789abcdef"
#define H2 "fedcba9876543210fedcba9876543210"
int main(void) {
    assert(catalog_line("S id 5 1000 " H1 " " H2, 0) == 0);
    assert(catalog_line("C id 5 0 1 " H1, 0) == 0);
    /* a converted song's own lines */
    assert(catalog_line("S arcade 32083 2000 " H1 " " H2, 1) == 0);
    assert(catalog_line("C arcade 32083 2 1 " H2, 1) == 0);
    assert(catalog_find("arcade", 32083) == 1 && (catalog[1].has & 4));
    /* the same song's lines again (already merged into charts.txt): skipped, C lines too */
    assert(catalog_line("S arcade 32083 2000 " H1 " " H2, 1) == 0);
    assert(catalog_line("C arcade 32083 2 1 " H2, 1) == 0);
    assert(catalog_n == 2);
    /* the next S ends the skip */
    assert(catalog_line("S arcade 29025 2000 " H2 " " H1, 1) == 0);
    assert(catalog_line("C arcade 29025 0 1 " H1, 1) == 0 && catalog_n == 3 && (catalog[2].has & 1));
    /* malformed lines still fail; from charts.txt a duplicate S fails */
    assert(catalog_line("C arcade 29025 12 1 " H1, 1) != 0);
    assert(catalog_line("S id 5 1000 " H1 " " H2, 0) != 0);
    assert(catalog_line("X nonsense", 1) != 0);
    puts("catalog lines: ok");
    return 0;
}
