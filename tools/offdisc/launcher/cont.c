/* Sprite containers (.if, EMPRESS data1): add sprites on one new sheet (off-disc launcher).
 *
 * Container: u16 data_off, u16 n; (n+1) x {u32 unpacked, u32 packed} at data_off-(n+1)*8; LZSS chunks back to back
 * from data_off. Chunk 0 = metadata: <HHHHII> (?, sheets, anims, sprites, rect table offset, anim table offset),
 * sheet sizes u16 w,h at 0x10+4*i, sprite rects u16 x, y (sheet*1024 + y), w, h. Chunks 1.. = sheets: 0x400 palette
 * + PSMT8 (GS-swizzled) indices.
 * The new sheet gets the palette of the sheet the sprites were drawn for; the rect table is copied to the end of the
 * metadata with the new rects appended (nothing else in the metadata moves, the header points at the copy).
 * Packed data is written as literal-only LZSS (+12.5%). Also built on the PC by test_cont.c. */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "cont.h"

#define RD16(p) ((p)[0] | (p)[1] << 8)
#define RD32(p) ((u32)(p)[0] | (u32)(p)[1] << 8 | (u32)(p)[2] << 16 | (u32)(p)[3] << 24)
static void wr16(u8 *p, u32 v) { p[0] = v; p[1] = v >> 8; }
static void wr32(u8 *p, u32 v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

int lzss_dec(const u8 *in, int inlen, u8 *out, int outlen)
{
    u8 ring[4096];
    int r = 0xfee, o = 0, i = 0;
    u32 ctl = 0;
    memset(ring, 0, sizeof ring);
    while (o < outlen && i < inlen) {
        ctl >>= 1;
        if (!(ctl & 0x100)) ctl = in[i++] | 0xff00;
        if (ctl & 1) {
            u8 c = in[i++];
            out[o++] = c; ring[r] = c; r = (r + 1) & 0xfff;
        } else {
            int off = in[i] | (in[i + 1] & 0xf0) << 4, n = (in[i + 1] & 0xf) + 3;
            i += 2;
            while (n-- && o < outlen) {
                u8 c = ring[off];
                out[o++] = c; ring[r] = c; r = (r + 1) & 0xfff; off = (off + 1) & 0xfff;
            }
        }
    }
    return o;
}

/* literal-only LZSS; out needs n + n/8 + 1 bytes */
static int lzss_lit(const u8 *in, int n, u8 *out)
{
    int o = 0, i = 0;
    while (i < n) {
        int k = n - i < 8 ? n - i : 8;
        out[o++] = 0xff;
        memcpy(out + o, in + i, k);
        o += k; i += k;
    }
    return o;
}

/* GS memory swizzle for an 8-bit sheet: indices (row-major) -> the byte order the game uploads as PSMCT32
 * (inverse of iidx-ps2tools' decode: write PSMT8, read back PSMCT32). w % 128 == 0, h % 64 == 0. */
static const u8 block32[32] = { 0, 1, 4, 5, 16, 17, 20, 21, 2, 3, 6, 7, 18, 19, 22, 23,
                                8, 9, 12, 13, 24, 25, 28, 29, 10, 11, 14, 15, 26, 27, 30, 31 };
static const u8 cword32[16] = { 0, 1, 4, 5, 8, 9, 12, 13, 2, 3, 6, 7, 10, 11, 14, 15 };
static const u8 cword8[2][64] = {
    { 0, 1, 4, 5, 8, 9, 12, 13, 0, 1, 4, 5, 8, 9, 12, 13, 2, 3, 6, 7, 10, 11, 14, 15, 2, 3, 6, 7, 10, 11, 14, 15,
      8, 9, 12, 13, 0, 1, 4, 5, 8, 9, 12, 13, 0, 1, 4, 5, 10, 11, 14, 15, 2, 3, 6, 7, 10, 11, 14, 15, 2, 3, 6, 7 },
    { 8, 9, 12, 13, 0, 1, 4, 5, 8, 9, 12, 13, 0, 1, 4, 5, 10, 11, 14, 15, 2, 3, 6, 7, 10, 11, 14, 15, 2, 3, 6, 7,
      0, 1, 4, 5, 8, 9, 12, 13, 0, 1, 4, 5, 8, 9, 12, 13, 2, 3, 6, 7, 10, 11, 14, 15, 2, 3, 6, 7, 10, 11, 14, 15 } };
static const u8 cbyte8[64] = { 0, 0, 0, 0, 0, 0, 0, 0, 2, 2, 2, 2, 2, 2, 2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 2, 2, 2, 2, 2, 2, 2, 2,
                               1, 1, 1, 1, 1, 1, 1, 1, 3, 3, 3, 3, 3, 3, 3, 3, 1, 1, 1, 1, 1, 1, 1, 1, 3, 3, 3, 3, 3, 3, 3, 3 };

static void swizzle8(const u8 *lin, u8 *raw, int w, int h)
{
    int x, y, dbw = w / 128, rw = w / 2, rh = h / 2, i = 0;
    u8 *gs = malloc(w * h);
    if (!gs) { memset(raw, 0, w * h); return; }
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            int page = x / 128 + y / 64 * dbw, px = x % 128, py = y % 64;
            int block = block32[px / 16 + py / 16 * 8], bx = px % 16, by = py % 16, col = by / 4, k = bx + by % 4 * 16;
            gs[(page * 2048 + block * 64 + col * 16 + cword8[col & 1][k]) * 4 + cbyte8[k]] = lin[y * w + x];
        }
    for (y = 0; y < rh; y++)
        for (x = 0; x < rw; x++, i++) {
            int page = x / 64 + y / 32 * dbw, px = x % 64, py = y % 32;
            int block = block32[px / 8 + py / 8 * 8], bx = px % 8, by = py % 8;
            memcpy(raw + i * 4, gs + (page * 2048 + block * 64 + by / 2 * 16 + cword32[bx + by % 2 * 8]) * 4, 4);
        }
    free(gs);
}

/* shelf packing, tallest first, onto sheets of W x SHEET_H (as many as needed); fills H[] with each sheet's height
 * (multiple of 64); returns the number of sheets or -1. SHEET_H = 512 like the game's own sheets: a 512x1024 8-bit
 * sheet is 0x8000 quadwords, one more than a GIF DMA transfer can carry (hung the GS in music select). */
#define SHEET_H 512
static int pack(spr_t *s, int n, int W, int *H, int maxsheets)
{
    int i, j, x = 0, y = 0, shelf = 0, sn = 0, *ord = malloc(n * sizeof(int));
    for (i = 0; i < n; i++) ord[i] = i;
    for (i = 1; i < n; i++)
        for (j = i; j > 0 && s[ord[j]].h > s[ord[j - 1]].h; j--) { int t = ord[j]; ord[j] = ord[j - 1]; ord[j - 1] = t; }
    for (i = 0; i < n; i++) {
        spr_t *p = &s[ord[i]];
        if (p->w + 2 > W || p->h + 2 > SHEET_H) { free(ord); return -1; }
        if (x + p->w + 2 > W) { y += shelf; x = 0; shelf = 0; }
        if (y + p->h + 2 > SHEET_H) {              /* sheet full: next one */
            H[sn] = SHEET_H;
            if (++sn >= maxsheets) { free(ord); return -1; }
            x = y = shelf = 0;
        }
        p->x = x + 1; p->y = y + 1; p->sn = sn;    /* 1 px transparent border */
        x += p->w + 2;
        if (p->h + 2 > shelf) shelf = p->h + 2;
    }
    free(ord);
    H[sn] = (y + shelf + 63) & ~63;
    return sn + 1;
}

/* the container with sprites s[0..n) added on new sheets of width W: placement by pack(), or (strip > 0) the first
 * nstrip as blank full-width strips of `strip` rows (SHEET_H / strip per sheet), the rest as empty 0x0 rects */
static u8 *build(const u8 *data, int len, int idx, spr_t *s, int n, int W, int strip, int nstrip, int *outlen,
                 char *err, int errlen)
{
    int data_off = RD16(data), cnt = RD16(data + 2), start = data_off - (cnt + 1) * 8, i, k, ns;
    int meta_len, new_meta_len, rect_off, sheets, sprites, ref, pos, mpk, total, ref_pk, H[32], slots;
    const u8 *tab = data + start, *p;
    u8 *meta, *meta2, *pal, *out, *o;

    if (start < 4 || data_off > len) { snprintf(err, errlen, "bad container header"); return 0; }
    ref = s[0].pal ? 0 : s[0].sheet;
    for (i = 1; i < n; i++)
        if (!s[i].pal != !s[0].pal || (s[0].pal && memcmp(s[i].pal, s[0].pal, 0x400)) || (!s[0].pal && s[i].sheet != ref)) {
            snprintf(err, errlen, "sprites use different palettes"); return 0;
        }
    meta_len = RD32(tab);
    if (!(meta = malloc(meta_len))) { snprintf(err, errlen, "out of memory (meta)"); return 0; }
    if (lzss_dec(data + data_off, RD32(tab + 4), meta, meta_len) != meta_len) {
        snprintf(err, errlen, "metadata unpack"); free(meta); return 0;
    }
    sheets = RD16(meta + 2); sprites = RD16(meta + 6); rect_off = RD32(meta + 8);
    slots = (rect_off - 0x10) / 4 - sheets;    /* free sheet-size slots before the rect table: the game reads only
                                                  `sheets` entries; some containers keep stale sizes after them (#146) */
    if (slots > 32) slots = 32;
    if (ref >= sheets || !slots) {
        snprintf(err, errlen, "no free sheet slot (sheets %d, rects at %x)", sheets, rect_off); free(meta); return 0;
    }
    if (strip) {
        int per = SHEET_H / strip;
        ns = (nstrip + per - 1) / per;
        for (i = 0; i < n; i++) {
            s[i].x = 0; s[i].y = i < nstrip ? i % per * strip : 0; s[i].sn = i < nstrip ? i / per : 0;
        }
        for (k = 0; k < ns; k++) H[k] = ((k < ns - 1 ? per : nstrip - k * per) * strip + 63) & ~63;
        if (ns > slots) ns = -1;
    } else
        ns = pack(s, n, W, H, slots);
    if (ns < 0) { snprintf(err, errlen, "sprites do not fit %d sheets of %dx%d", slots, W, SHEET_H); free(meta); return 0; }

    /* palette: the sprites' own, or the reference sheet's (unpack that chunk up to its palette) */
    pal = malloc(0x400);
    if (s[0].pal) memcpy(pal, s[0].pal, 0x400);
    else {
        for (i = 0, p = data + data_off; i <= ref; i++) p += RD32(tab + 8 * i + 4);
        ref_pk = RD32(tab + 8 * (ref + 1) + 4);
        lzss_dec(p, ref_pk, pal, 0x400);
    }

    /* metadata: sheet sizes, rect table copy + new rects at the end */
    pos = (meta_len + 15) & ~15;
    new_meta_len = (pos + (sprites + n) * 8 + 15) & ~15;
    meta2 = calloc(new_meta_len, 1);
    memcpy(meta2, meta, meta_len);
    memcpy(meta2 + pos, meta + rect_off, sprites * 8);
    for (i = 0; i < n; i++) {
        u8 *r = meta2 + pos + (sprites + i) * 8;
        wr16(r, s[i].x); wr16(r + 2, (sheets + s[i].sn) * 1024 + s[i].y); wr16(r + 4, s[i].w); wr16(r + 6, s[i].h);
        s[i].handle = (u32)idx << 16 | (sprites + i);
    }
    wr16(meta2 + 2, sheets + ns); wr16(meta2 + 6, sprites + n); wr32(meta2 + 8, pos);
    for (k = 0; k < ns; k++) { wr16(meta2 + 0x10 + 4 * (sheets + k), W); wr16(meta2 + 0x12 + 4 * (sheets + k), H[k]); }
    free(meta);

    /* container: header, table (+ns entries), packed chunks */
    total = start + (cnt + 1 + ns) * 8 + new_meta_len + new_meta_len / 8 + 1 + (len - data_off);
    for (k = 0; k < ns; k++) total += (0x400 + W * H[k]) * 9 / 8 + 1;
    if (!(out = malloc(total))) {
        snprintf(err, errlen, "out of memory (%d KB)", total >> 10); free(meta2); free(pal); return 0;
    }
    memcpy(out, data, start);
    wr16(out, start + (cnt + 1 + ns) * 8); wr16(out + 2, cnt + ns);
    o = out + start + (cnt + 1 + ns) * 8;
    mpk = lzss_lit(meta2, new_meta_len, o); o += mpk;
    wr32(out + start, new_meta_len); wr32(out + start + 4, mpk);
    p = data + data_off + RD32(tab + 4);
    for (i = 1; i <= cnt; i++) {
        int pk = RD32(tab + 8 * i + 4);
        memcpy(out + start + 8 * i, tab + 8 * i, 8);
        memcpy(o, p, pk); o += pk; p += pk;
    }
    for (k = 0; k < ns; k++) {                                     /* new sheets */
        int sheet_len = 0x400 + W * H[k], spk;
        u8 *lin = malloc(W * H[k]), *sheet = malloc(sheet_len);
        if (!lin || !sheet) {
            snprintf(err, errlen, "out of memory (sheet)"); free(lin); free(sheet); free(out); free(meta2); free(pal);
            return 0;
        }
        memset(lin, s[0].clear, W * H[k]);
        for (i = 0; i < n; i++) {
            int y;
            if (s[i].sn != k || !s[i].px) continue;
            for (y = 0; y < s[i].h; y++) memcpy(lin + (s[i].y + y) * W + s[i].x, s[i].px + y * s[i].w, s[i].w);
        }
        memcpy(sheet, pal, 0x400);
        swizzle8(lin, sheet + 0x400, W, H[k]);
        spk = lzss_lit(sheet, sheet_len, o);
        wr32(out + start + 8 * (cnt + 1 + k), sheet_len); wr32(out + start + 8 * (cnt + 1 + k) + 4, spk);
        o += spk;
        free(lin); free(sheet);
    }
    free(meta2); free(pal);
    *outlen = o - out;
    return out;
}

u8 *cont_add(const u8 *data, int len, int idx, spr_t *s, int n, int *outlen, char *err, int errlen)
{
    return build(data, len, idx, s, n, 512, 0, 0, outlen, err, errlen);
}

u8 *cont_strips(const u8 *data, int len, int idx, int sheet, int clear, int W, int strip, int n, int songs,
                u32 *first, int *outlen, char *err, int errlen)
{
    spr_t *s = calloc(n + songs, sizeof(spr_t));
    u8 *out;
    int i;
    if (!s) { snprintf(err, errlen, "out of memory (strips)"); return 0; }
    for (i = 0; i < n + songs; i++) {
        s[i].w = i < n ? W : 0; s[i].h = i < n ? strip : 0; s[i].sheet = sheet; s[i].clear = clear;
    }
    out = build(data, len, idx, s, n + songs, W, strip, n, outlen, err, errlen);
    if (out) *first = s[0].handle & 0xffff;
    free(s);
    return out;
}

/* PSMT8 byte offset of pixel (x, y) in a sheet of width w as the game stores it (songdb.c swz) */
static u32 swz8(u32 x, u32 y, u32 w)
{
    u32 xr = x ^ (4 * (((y >> 1) ^ (y >> 2)) & 1));
    return (y >> 2) * 4 * w + (y & 1) * 2 * w + (x >> 4) * 32 + 4 * (xr & 7) + 2 * ((xr >> 3) & 1) + ((y & 3) >> 1);
}

u32 *cont_sprite_rgba(const u8 *data, int len, int sprite, int *w, int *h)
{
    int off = RD16(data), n = RD16(data + 2), start = off - (n + 1) * 8, i, x, y, sheet, sw, sh, rx, ry, at = off;
    const u8 *tab = data + start, *r;
    u8 *meta, *sh8 = 0;
    u32 *out = 0, pal[256];
    if (start < 4 || off > len) return 0;
    if (!(meta = malloc(RD32(tab)))) return 0;
    lzss_dec(data + off, RD32(tab + 4), meta, RD32(tab));
    if (sprite >= RD16(meta + 6)) goto done;
    r = meta + RD32(meta + 8) + sprite * 8;
    rx = RD16(r); sheet = RD16(r + 2) / 1024; ry = RD16(r + 2) % 1024; *w = RD16(r + 4); *h = RD16(r + 6);
    sw = RD16(meta + 0x10 + sheet * 4); sh = RD16(meta + 0x12 + sheet * 4);
    for (i = 0; i <= sheet; i++) at += RD32(tab + i * 8 + 4);              /* chunk sheet + 1 */
    if (at > len || !(sh8 = malloc(0x400 + sw * sh)) || !(out = malloc(*w * *h * 4))) { free(out); out = 0; goto done; }
    lzss_dec(data + at, RD32(tab + (sheet + 1) * 8 + 4), sh8, 0x400 + sw * sh);
    for (i = 0; i < 256; i++) pal[i] = RD32(sh8 + (i ^ ((((i >> 3) ^ (i >> 4)) & 1) * 0x18)) * 4);   /* CSM1 */
    for (y = 0; y < *h; y++)
        for (x = 0; x < *w; x++) out[y * *w + x] = pal[sh8[0x400 + swz8(rx + x, ry + y, sw)]];
done:
    free(sh8);
    free(meta);
    return out;
}
