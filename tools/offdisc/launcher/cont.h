#ifndef CONT_H
#define CONT_H
#ifdef _EE
#include <tamtypes.h>
#else
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
#endif

typedef struct {
    int w, h, sheet, clear;            /* size, palette sheet, transparent index */
    const u8 *px;                      /* w*h indices, row-major */
    const u8 *pal;                     /* own palette (0x400 bytes as stored in a sheet chunk, CSM1 order) instead of
                                          `sheet`'s; all sprites added in one call share it (same contents) */
    int x, y, sn;                      /* placement: new sheet number and position (set by cont_add) */
    u32 handle;                        /* container << 16 | sprite (set by cont_add) */
} spr_t;

int lzss_dec(const u8 *in, int inlen, u8 *out, int outlen);
/* returns a malloc'd container with the sprites added on a new sheet (0 + err on failure) */
u8 *cont_add(const u8 *data, int len, int idx, spr_t *s, int n, int *outlen, char *err, int errlen);
/* the container with n blank sprite slots added: full-width strips of `strip` rows (multiple of 8) on new sheets of
 * width W (multiple of 128), palette of `sheet`, filled with index `clear`, followed by `songs` empty (0x0) sprites,
 * one per song (the runtime points them at a strip); *first = the first slot's sprite index (songs from first + n) */
u8 *cont_strips(const u8 *data, int len, int idx, int sheet, int clear, int W, int strip, int n, int songs,
                u32 *first, int *outlen, char *err, int errlen);
/* one sprite as RGBA (PS2 alpha 0..0x80), malloc'd, w x h; 0 on failure */
u32 *cont_sprite_rgba(const u8 *data, int len, int sprite, int *w, int *h);
#endif
