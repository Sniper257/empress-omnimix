/* Capacities of the original EMPRESS resources, not catalog/library limits.
 * Imported data needs its own identity mapping before these can be extended. */
#ifndef SONG_RESOURCES_H
#define SONG_RESOURCES_H
#define E16_RESOURCE_SONGS 99
#define E16_RESOURCE_STYLES 2
#define E16_RESOURCE_DIFFS 5
#define E16_RANKING_DIFFS 3
#define E16_RANKING_SECTORS 30
#ifndef __ASSEMBLER__
int song_catalog_row(int song);
#endif
#endif
