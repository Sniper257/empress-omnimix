#ifndef MM_CUSTOM_ART_H
#define MM_CUSTOM_ART_H
/* Launcher-owned immutable catalog. IDs are pack identities, never row numbers.
 * One selected pack per owner/type is loaded by the ordinary PLAY worker. */
#define CUSTOM_ART_VERSION 1u
#define CUSTOM_ART_MAX 63u
#define CUSTOM_BOMB_BASE 32u
struct custom_art_pack {unsigned id,fid,frames;short x,y;char name[28];};
struct custom_art_catalog {unsigned version,count[2];struct custom_art_pack packs[];};
_Static_assert(sizeof(struct custom_art_pack)==44 && __builtin_offsetof(struct custom_art_catalog,packs)==12,"custom artwork catalog1 layout");
#endif
