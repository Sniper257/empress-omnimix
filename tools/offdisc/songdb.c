#ifdef LOAD_PROFILE
#include "load_profile.h"
#endif
#ifdef SELECT_PROFILE
#include "select_profiler/capture.h"
#endif
/* Song database for the off-disc launcher (EMPRESS MEGAMIX): which EMPRESS songs are present, records and files of
 * the songs the launcher added, and our music-select folders: 59 = CUSTOM, 60.. = one per game version
 * (PREMIUM BEST's version folders); OLD VERSION is hidden; ALL VERSION uses the rolling alias window. The launcher fills `sdb` (songdb.h).
 *
 * Added songs are slots 0..n_new-1; the game sees them only as alias ids (songdb.h), bound in fold_copy when a
 * folder is opened. The game's own lists hold EMPRESS songs only; the added songs are counted per context in
 * fold_build (after the game builds its folders) and merged into a folder's list when it is opened: ours, LEVEL,
 * A-Z and NO PLAY. The hooks are in songdb.S. Built into the same blob as vdisc.c. */

typedef unsigned char u8;
typedef signed char s8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef int s32;
#include "songdb.h"
#include "song_resources.h"

#define LIST 1000                      /* the game's song list: +4 count, +8 lamp mask, +9 diff[495], +0x1f8 song[495] */
#define LIST_MAX 495
#define OFF(f) __builtin_offsetof(sdb_t, f)
_Static_assert(OFF(present) == 24 && OFF(next) == 232 && OFF(fold_n) == 488 && OFF(slot_of) == 536,
               "songdb.S O_* offsets");

sdb_t sdb = { 0x44474e53 };
u8 *play_preview_record; /* Explicit editor lifetime, never ordinary test kind8. */
#define PLAY_PREVIEW_ACTIVE() (play_preview_record!=0)
u8 unavailable_song_record[0x134];     /* invalid metadata is empty, never song0 */
_Static_assert(OFF(rec_version) == OFF(sort_rank) + 4, "records-off-block fields follow sort_rank");
const u32 chart_identity_layout[] = {OFF(identity_version),OFF(identity_count),OFF(chart_index),OFF(chart_ids),sizeof(sdb_t)};
skin_t skin = { 0x808080, 100, 100, { 0xffff, 0xffff }, 0, 0 };
_Static_assert(__builtin_offsetof(skin_t, note_zoom) == 4 && __builtin_offsetof(skin_t, note_zoom_skin) == 8 &&
               __builtin_offsetof(skin_t, skin_first) == 12 && __builtin_offsetof(skin_t, beam_dy) == 16,
               "songdb.S note_hook / beam_hook offsets");
_Static_assert(__builtin_offsetof(skin_t,field)==24 && sizeof(skin.field[0])==32 && __builtin_offsetof(skin_t,note_rows)==88,"per-field scalar hook ABI");
u32 cur_tint = 0x808080;               /* vertex colour the game's sprite packets use (3 loads in FUN_00139200) */

#ifdef HOST_TEST
#define LOOP_PAD()
#else
#define LOOP_PAD() __asm__ volatile("nop\n\tnop\n\tnop")
#endif
#define I32(p) (*(s32 *)(p))

#ifndef HOST_TEST                      /* test_songdb.c: stubs */
#define lamp_mask ((u32 (*)(s32, s32, s32))0x214e80)            /* FUN_00214e80(song, diff, mode) */
#define memcpy_ ((void (*)(void *, const void *, s32))0x12ad48)
#define malloc_ ((void *(*)(s32))0x13a4c0)
#define free_ ((void (*)(void *))0x13a4b0)
#define sort_list ((void (*)(u8 *, s32, s32, u8 *, s32))0x2151e0) /* FUN_002151e0(ctx, lo, hi, list, mode) */
#define chart_listed ((s32 (*)(u8 *, s32, s32))0x214f60)        /* FUN_00214f60(ctx, song, diff) */
#define lamp_of ((s32 (*)(u8 *, s32))0x213110)                  /* FUN_00213110(ctx, mask): folder lamp */
#define g_count ((s32 (*)(u8 *, s32, s32))0x212960)              /* the game's folder count / lamp / open */
#define g_lamp ((s32 (*)(u8 *, s32, s32))0x212c80)
#define g_open ((void (*)(u8 *, s32, s32, u8 *, u8 *))0x2118b0)
#define rec_of ((u8 *(*)(u8 *, s32))0x1e19a0)                    /* songlist record (rec_hook) */
#define song_ctx() (*(u8 **)0x7945d4)                           /* FUN_00168660 */
#define g_stats ((void (*)(u8 *, s32, s32, s32 *, s32 *, s32 *))0x210d80)
#define save_rec(s, m, d) (((u8 *(*)(u8 *, s32, s32, s32))0x19bca0)(*(u8 **)0x7945d8, s, m, d))  /* FUN_0019bca0 */
#define rival_idx() ((*(u8 **)0x7945e0)[4])                       /* FUN_00178380(FUN_00168630()) */
#define spr_set ((void (*)(u32 *, u32, s32))0x130d30)              /* sprite object: set animation */
#define LBL_BPM ((u32 *)0x2accd8)                                  /* gp-0x7f18 / -0x7f10 / -0x7f08 per colour: */
#define LBL_RANGE ((u32 *)0x2acce0)                                /* "BPM", "BPM" with a range, "MUSIC NUMBER" */
#define LBL_MUSIC ((u32 *)0x2acce8)
#define MS_STATE (*(u8 **)0x2ad328)
#define MS_WHEEL (*(u8 **)0x2ad32c)
#define wheel_scroll ((void (*)(s32,s32))0x194970)
#define wheel_random ((s32 (*)(s32))0x1d7aa0)
#endif

static const u8 UNPLAYED[0x1c] = { 0, 0, 0, 0x80, 0, 0, 0, 0, 0, 0, 0, 0x90, 0xff, 0x7f, 0, 0, 0, 0x40, 0, 0,
                                   0x99, 0x3a, 0, 0, 0x10, 0x67, 0xa6, 0x0e };

/* the pool of a context (make = take a free one, else the first) */
static __attribute__((noinline)) pool_t *pool_of(u8 *ctx, int make)
{
    s32 i;
    for (i = 0; i < NPOOL; i++) {
        if (sdb.pools[i].ctx == ctx) return &sdb.pools[i];
        LOOP_PAD();
    }
    if (!make) return 0;
    for (i = 0; i < NPOOL; i++) {
        if (!sdb.pools[i].ctx) break;
        LOOP_PAD();
    }
    if (i == NPOOL) i = 0;
    sdb.pools[i].ctx = ctx;
    return &sdb.pools[i];
}

/* virtual folder of a music-select folder id, -1 = the game's alone */
static s32 vf_of(s32 id)
{
    if (id == 0) return VF_ALL;
    if ((u32)(id - FOLD0) < NFOLD) return id - FOLD0;
    if (id >= 3 && id <= 14) return VF_LEVEL + id - 3;
    if (id >= 15 && id <= 21) return VF_ALPHA + id - 15;
    if (id == 30) return VF_NOPLAY;
    return -1;
}

static s32 one_list(s32 vf) { return (vf >= VF_LEVEL && vf < VF_ALPHA) || vf == VF_NOPLAY; }

/* the game's own list behind a virtual folder (EMPRESS songs), 0 = none */
static u8 *game_list(u8 *ctx, s32 vf, s32 d)
{
    if (vf == VF_ALL) return ctx + 1000 + d * LIST;
    if (vf < VF_LEVEL) return 0;
    if (vf < VF_ALPHA) return ctx + 16000 + (vf - VF_LEVEL) * LIST;
    if (vf < VF_NOPLAY) return ctx + 28000 + (vf - VF_ALPHA) * 5000 + d * LIST;
    return ctx + 88000;
}

static u8 *save_of(s32 id, s32 mode, s32 d)
{
    return sdb.saves + ((id & 0xff) - FIRST) * 0x230 + ((mode < 3 ? mode : 3) * 5 + d) * 0x1c;
}

/* Keep a changed alias's session scores attached to its stable song slot (docs/omnimix-capacity.md step 6).
 * Compact: only records that have a save key (save_runtime.c ps_key: a chart identity, category SP for modes
 * 0 and 1, DP for mode 2, the third category for mode 3), so per existing chart: SP two (modes 0/1 share one,
 * as their key does; the score writer keeps both equal and a save refuses two changed records of one key that
 * differ), DP one. slot_base[k] = slot k's first record; the game still sees 20 per alias (sdb.saves). */
static u8 *slot_saved;
static u32 *slot_base;
static s32 slot_saved_n;

#include "song_reference.c"

/* Future persistence consumes these bytes, NEVER the table index or alias.
 * dp is an explicit SP/DP flag; native save-mode/assist rules are separate. */
const u8 *chart_identity_row(s32 row, s32 dp, s32 diff)
{
    u32 index;
    if (sdb.identity_version!=1 || !sdb.chart_index || !sdb.chart_ids ||
        (u32)row>=FIRST+sdb.n_new || (u32)dp>1 || (u32)diff>=5)return 0;
    index=sdb.chart_index[row*10+dp*5+diff];
    if (index==NOSLOT || index>=sdb.identity_count)return 0;
    return sdb.chart_ids+index*16;
}

const u8 *chart_identity(s32 song, s32 dp, s32 diff)
{
    s32 row=song_catalog_row(song);
    return row<0?0:chart_identity_row(row,dp,diff);
}
/* bit i (0..9: SP N/H/A/B/X, DP N/H/A/B/X) = slot k has that chart (a save key) */
static u32 slot_charts(s32 k)
{
    u32 m=0,i;
    if (sdb.identity_version!=1 || !sdb.chart_index)return 0x3ff;
    for (i=0;i<10;i++) { if (sdb.chart_index[(FIRST+k)*10+i]!=NOSLOT)m|=1u<<i; LOOP_PAD(); }
    return m;
}

static u32 slot_records(u32 charts) { u32 n=0,i; for (i=0;i<10;i++){ if(charts>>i&1)n+=i<5?2:1; LOOP_PAD(); } return n; }

/* slot k's stored record for (mode, d), 0 if that chart has none (no save key) */
static u8 *slot_rec(s32 k, s32 mode, s32 d)
{
    u32 charts,i,at,c;
    if (k>=slot_saved_n || !slot_saved || (u32)d>=5)return 0;
    if (mode>3)mode=3;
    c=mode==2?5+d:d;
    charts=slot_charts(k);
    if (!(charts>>c&1))return 0;
    at=slot_base[k];
    for (i=0;i<c;i++) { if (charts>>i&1)at+=i<5?2:1; LOOP_PAD(); }
    return slot_saved+(at+(mode==3?1:0))*0x1c;
}

/* slot k's records as the 20 an alias holds (mode * 5 + d; modes 0 and 1 the same record) */
static void slot_expand(s32 k, u8 *dst)
{
    s32 m,d;
    for (m=0;m<4;m++) for (d=0;d<5;d++) {
        const u8 *r=slot_rec(k,m,d);
        memcpy_(dst+(m*5+d)*0x1c,r?r:UNPLAYED,0x1c);LOOP_PAD();
    }
}

/* an alias's 20 records back into slot k: for the shared SP record, the side that changed it */
static void slot_absorb(s32 k, const u8 *src)
{
    s32 m,d,i;
    for (m=0;m<4;m++) { if (m==1)continue; for (d=0;d<5;d++) {
        u8 *r=slot_rec(k,m,d);const u8 *v=src+(m*5+d)*0x1c;
        if (!r)continue;
        if (m==0) {
            const u8 *p2=src+(5+d)*0x1c;s32 same=1,p2same=1;
            for (i=0;i<0x1c;i++){ if(v[i]!=r[i])same=0; if(p2[i]!=r[i])p2same=0; LOOP_PAD(); }
            if (same && !p2same)v=p2;               /* only P2's record moved */
        }
        memcpy_(r,v,0x1c);LOOP_PAD();
    } }
}

static s32 ensure_saves(void)
{
    s32 i;u32 n=0;u8 *sv;u32 *base;
    if (slot_saved && slot_saved_n>=(s32)sdb.n_new)return 1;
    base=malloc_((sdb.n_new+1)*4);
    if (!base)return 0;
    for (i=0;i<(s32)sdb.n_new;i++) { base[i]=n;n+=slot_records(slot_charts(i));LOOP_PAD(); }
    base[sdb.n_new]=n;
    sv=malloc_(n*0x1c+4);
    if (!sv) { free_(base);return 0; }
    for (i=0;i<(s32)n;i++) { memcpy_(sv+i*0x1c,UNPLAYED,0x1c);LOOP_PAD(); }
    if (slot_saved) { free_(slot_saved);free_(slot_base); }
    slot_saved=sv;slot_base=base;slot_saved_n=sdb.n_new;return 1;
}

static u8 *slot_save(s32 k, s32 mode, s32 d)
{
    s32 a=sdb.alias_of[k];
    u8 *r;
    if (a && song_catalog_row(a)==FIRST+k)return save_of(a,mode,d);
    r=slot_rec(k,mode,d);
    return r?r:(u8 *)UNPLAYED;
}

static s32 stash_alias(s32 a)
{
    s32 k=sdb.slot_of[a];
    if (k==NOSLOT)return 1;
    if (k>=slot_saved_n)return 0;
    slot_absorb(k,save_of(a,0,0));return 1;
}

static void window_reset(void);
static s32 window_storage(void);

/* ---- records off the block (songdb.h; docs/omnimix-capacity.md step 3) ---- */
s32 vd_pread(u32 lba, u32 off, u32 n, void *buf);             /* vdisc.c */
static s32 rec_buf_first = -1, rec_buf_n;

/* music-select summary of slot k (SUMMARY_BYTES, SUM_*) */
const u8 *slot_summary(s32 k) { return sdb.summary + k * SUMMARY_BYTES; }

/* slot k's full record into dst, through rec_buf (53 records per read, aligned: a folder's songs, which the
 * launcher numbers together, come from one or two reads). 0 if the file cannot be read. */
static s32 slot_record_read(s32 k, u8 *dst)
{
    s32 per = REC_BUF / 0x134, first, n;
    if ((u32)k >= sdb.n_new || !sdb.rec_buf || !sdb.rec_lba) return 0;
    if (rec_buf_first < 0 || k < rec_buf_first || k >= rec_buf_first + rec_buf_n) {
        first = k / per * per;
        rec_buf_first = -1;
        n = vd_pread(sdb.rec_lba, (u32)first * 0x134, REC_BUF, sdb.rec_buf);
        if (n < (k - first + 1) * 0x134) return 0;
        rec_buf_first = first; rec_buf_n = n / 0x134;
    }
    memcpy_(dst, sdb.rec_buf + (k - rec_buf_first) * 0x134, 0x134);
    return 1;
}

/* the PROBE alias's record, built from slot k's summary: chart_listed (FUN_00214f60), which asks the probe about
 * every slot when a folder list is built, reads only the levels and the chart file id words (non-zero or not) */
static void summary_record(s32 k, u8 *r)
{
    const u8 *s = slot_summary(k);
    u32 bits = s[SUM_CHARTS] | s[SUM_CHARTS + 1] << 8, i;
    for (i = 0; i < 0x134; i++) { r[i] = 0; LOOP_PAD(); }
    r[0] = s[SUM_TITLE];
    for (i = 0; i < 4; i++) { r[0x42 + i] = s[SUM_BPM + i]; LOOP_PAD(); }
    for (i = 0; i < 10; i++) { r[0x4a + i] = s[SUM_LEVELS + i]; if (bits >> i & 1) r[0xc0 + 4 * i] = 1; LOOP_PAD(); }
}

/* rec_hook (songdb.S) for added songs: alias id -> its cached record, read again when the alias has been bound
 * to another slot since (bind_keys, the variant and quick-retry aliases) */
u8 *rec_added(u8 *ctx, s32 id)
{
    u32 a = (u32)id & 255, k;
    u8 *r;
    (void)ctx;
    if (a < FIRST || sdb.rec_version != REC_VERSION || !sdb.recs || !sdb.rec_key) return unavailable_song_record;
    k = sdb.slot_of[a];
    if (k >= sdb.n_new) return unavailable_song_record;
    r = sdb.recs + (a - FIRST) * 0x134;
    if (sdb.rec_key[a - FIRST] != k) {
        sdb.rec_key[a - FIRST] = NOSLOT;
        if (a == PROBE) summary_record(k, r);
        else if (!slot_record_read(k, r)) return unavailable_song_record;
        sdb.rec_key[a - FIRST] = k;
    }
    return r;
}

/* A-Z group of a title: ABCD EFGH IJKL MNOP QRST UVWXYZ others */
static s32 alpha(s32 c)
{
    if (c >= 'a' && c <= 'z') c -= 32;
    if (c < 'A' || c > 'Z') return 6;
    return c <= 'D' ? 0 : c <= 'H' ? 1 : c <= 'L' ? 2 : c <= 'P' ? 3 : c <= 'T' ? 4 : 5;
}

/* does pair (slot, d) of a context belong in virtual folder vf (diff = the folder's difficulty)? */
static s32 in_vf(pool_t *p, s32 k, s32 d, s32 vf, s32 diff, s32 dp, s32 mode)
{
    const u8 *rec = slot_summary(k);
    s32 lv;
    if (!(p->listed[k] >> d & 1)) return 0;
    if (vf < VF_LEVEL) return sdb.slot_fold[k] == vf && d == diff;
    if (vf == VF_ALL) return d == diff;
    if (vf < VF_ALPHA) {
        lv = rec[SUM_LEVELS + (dp ? 5 : 0) + d];
        return lv == vf - VF_LEVEL + 1;
    }
    if (vf < VF_NOPLAY) return d == diff && alpha(rec[SUM_TITLE]) == vf - VF_ALPHA;
    return slot_save(k, mode, d)[0xb] >> 4 == 9;
}

/* drop the songs shown in our folders (CUSTOM, SIRIUS, ...) from one of the game's lists, recompute its lamp mask */
static void filter(u8 *l, s32 mode)
{
    s32 n = I32(l + 4), i, k = 0;
    u8 mask = 0xff;
    for (i = 0; i < n; i++) {
        s32 s = l[0x1f8 + i], d = l[9 + i];
        if (s >= FIRST || sdb.emp_fold[s] == NOFOLD) {
            l[0x1f8 + k] = s; l[9 + k] = d; k++;
            mask &= lamp_mask((s8)s, d, mode);
        }
        LOOP_PAD();
    }
    I32(l + 4) = k;
    l[8] = mask;
}

/* after FUN_00213550 (the game's folder lists): which charts of the added songs this context lists, songs and lamp
 * masks per virtual folder; EMPRESS songs of our folders out of the EMPRESS folder */
void fold_build(u8 *ctx)
{
#ifdef SELECT_PROFILE
    unsigned profile_start=sp_span();
#endif
    s32 mode = I32(ctx + 0x20724), dp = I32(ctx + 0x20728), n = I32(ctx + 4), i, k, d, vf;
    pool_t *p = pool_of(ctx, 1);
    window_reset();
    for (vf = 0; vf < NVF; vf++)
        for (d = 0; d < 5; d++) { LOOP_PAD(); p->cnt[vf][d] = 0; p->mask[vf][d] = 0xff; LOOP_PAD(); }
    for (i = 0; i < n; i++) {                           /* EMPRESS songs of our folders (the game's song list) */
        s32 s = ctx[0x1f8 + i];
        d = ctx[9 + i];
        vf = s < FIRST ? sdb.emp_fold[s] : NOFOLD;
        if (vf < NFOLD && d < 5) {
            p->cnt[vf][d]++;
            p->mask[vf][d] &= lamp_mask(s, d, mode);
        }
        LOOP_PAD();
    }
    for (k = 0; k < (s32)sdb.n_new; k++) {              /* added songs, asked through the probe alias */
        u8 bits = 0;
        if (sdb.var_main && sdb.var_main[k] != NOSLOT) { p->listed[k] = 0; continue; }   /* variants: no folder */
        sdb.slot_of[PROBE] = k;
        for (d=0;d<20;d++) {memcpy_(save_of(PROBE,0,0)+d*0x1c,slot_save(k,d/5,d%5),0x1c);LOOP_PAD();}
        for (d = 0; d < 5; d++) { LOOP_PAD();
            if (d != 3 && chart_listed(ctx, PROBE, d)) bits |= 1 << d;
            LOOP_PAD();
        }
        p->listed[k] = bits;
        for (d = 0; d < 5; d++) { LOOP_PAD();
            if (bits >> d & 1) {
                u32 lm = lamp_mask(PROBE, d, mode);
                for (vf = 0; vf < NVF; vf++) {
                    s32 vd = one_list(vf) ? 0 : d;
                    if (in_vf(p, k, d, vf, d, dp, mode)) {
                        p->cnt[vf][vd]++;
                        p->mask[vf][vd] &= lm;
                    }
                    LOOP_PAD();
                }
            }
            LOOP_PAD();
        }
        LOOP_PAD();
    }
    sdb.slot_of[PROBE] = NOSLOT;
    for (d = 0; d < 5; d++) { LOOP_PAD();
        filter(ctx + 6000 + d * LIST, mode);          /* folder 1 = EMPRESS (version > 15) */
        filter(ctx + 11000 + d * LIST, mode);         /* folder 2 = OLD VERSION (hidden) */
        LOOP_PAD();
    }
#ifdef SELECT_PROFILE
    if(profile_start)sp_note(10,1,sp_span()-profile_start,sdb.n_new,mode,dp,n,0,0,0);
#endif
}

static s32 dix(s32 diff) { return (u32)diff < 5 ? diff : 0; }

/* thunk 0x2113b0: songs in the folder */
s32 fold_count(u8 *ctx, s32 id, s32 diff)
{
    pool_t *p = pool_of(ctx, 0);
    s32 vf = vf_of(id), n;
    if (vf < 0 || !p || !I32(ctx + 0x2072c)) return g_count(ctx, id, diff);
    n = p->cnt[vf][one_list(vf) ? 0 : dix(diff)];
    if (vf >= VF_LEVEL) n += g_count(ctx, id, diff);
    if (!window_storage())return 0;
    return n > SONG_VIEW ? SONG_VIEW : n;
}

/* thunk 0x210d70: folder lamp */
s32 fold_lamp(u8 *ctx, s32 id, s32 diff)
{
    pool_t *p = pool_of(ctx, 0);
    s32 vf = vf_of(id), d, n;
    u8 mask, *l;
    if (vf < 0 || !p) return g_lamp(ctx, id, diff);
    d = one_list(vf) ? 0 : dix(diff);
    n = p->cnt[vf][d];
    mask = p->mask[vf][d];
    if ((l = game_list(ctx, vf, dix(diff)))) {
        if (I32(l + 4)) mask &= l[8];
        n += I32(l + 4);
    }
    if (vf == VF_NOPLAY) return n ? 0 : 4;          /* like the game's NO PLAY folder */
    return n ? lamp_of(ctx, mask) : 0;
}

#include "song_window.c"

/* one chart into the folder stats, as FUN_00210d80 counts it: DJ level (9 = NO PLAY -> 8), clear lamp (7 -> 6,
 * 0 -> 1, stored from the end), against the rival (riv 0..9 one rival, 10 all; per-song score arrays [5][99]: EMPRESS
 * songs only, s = -1 for added songs) win 0 / lose 1 / unplayed 2 */
static void tally(u8 *ctx, s32 s, s32 d, u8 *sv, s32 riv, s32 *grade, s32 *lamp, s32 *rv)
{
    s32 g = sv[0xb] >> 4, l = sv[0xf] >> 5, l6 = l, u = 3, i, lo = riv, hi = riv + 1, off = s * 4 + d * 0x18c;
    if (g == 9) g = 8;
    if (l6 == 7) l6 = 6;
    if (l6 == 0) l6 = 1;
    if (riv == 10) { lo = 0; hi = 10; }
    if ((u32)s >= 99 || d >= 5 || riv < 0 || riv > 10) hi = lo;
    for (i = lo; i < hi; i++) {
        s32 r = I32(ctx + off + i * 0x7bc + 0x21e70);
        if (r >= 0) {
            u = I32(ctx + off + 0x20738) < r;
            if (u) break;
        }
        LOOP_PAD();
    }
    if (u < 2 && l == 0) u = 2;
    grade[g]++;
    lamp[6 - l6]++;
    if (u < 3) rv[u]++;
}

/* jal site 0x1918a4: folder stats panel FUN_00210d80(ctx, id, diff, grades[9], lamps[6], rivals[3]). The game counts
 * its own lists (EMPRESS songs) and gives -1s for ids >= 0x2b; ours are counted here, added songs added to LEVEL /
 * A-Z / NO PLAY too (bound aliases have their dummy saves, unbound slots are unplayed) */
void fold_stats(u8 *ctx, s32 id, s32 diff, s32 *grade, s32 *lamp, s32 *rv)
{
#ifdef SELECT_PROFILE
    unsigned profile_start=sp_span();
#endif
    pool_t *p = pool_of(ctx, 0);
    s32 vf = vf_of(id), d = dix(diff), smode = I32(ctx + 0x20724), dp = I32(ctx + 0x20728), riv, i, k, e;
    g_stats(ctx, id, diff, grade, lamp, rv);
    if (vf < 0 || !p || !I32(ctx + 0x2072c)) return;
    riv = rival_idx() - 2;
    if (vf < VF_LEVEL) {                                /* our folder: its EMPRESS songs from the song list */
        s32 m = I32(ctx + 4);
        for (i = 0; i < 9; i++) { grade[i] = 0; if (i < 6) lamp[i] = 0; if (i < 3) rv[i] = 0; LOOP_PAD(); }
        for (i = 0; i < m; i++) {
            s32 s = ctx[0x1f8 + i];
            if (s < FIRST && sdb.emp_fold[s] == vf && ctx[9 + i] == d)
                tally(ctx, s, d, save_rec(s, smode, d), riv, grade, lamp, rv);
            LOOP_PAD();
        }
    }
    for (k = 0; k < (s32)sdb.n_new; k++) {
        s32 a = sdb.alias_of[k];
        for (e = one_list(vf)?0:d; e < (one_list(vf)?5:d+1); e++) {
            if (in_vf(p, k, e, vf, d, dp, smode))
                tally(ctx, -1, e, slot_save(k, smode, e), riv, grade, lamp, rv);
            LOOP_PAD();
        }
        LOOP_PAD();
    }
    for (i = 0; i < 9; i++) {                           /* the panel has 3 digits */
        if (grade[i] > 999) grade[i] = 999;
        if (i < 6 && lamp[i] > 999) lamp[i] = 999;
        if (i < 3 && rv[i] > 999) rv[i] = 999;
        LOOP_PAD();
    }
#ifdef SELECT_PROFILE
    if(profile_start)sp_note(10,2,sp_span()-profile_start,id,d,vf,smode,dp,0,0);
#endif
}

#ifndef HOST_TEST
#ifdef PLAY_LIFT
static u32 lift_pixels(u32);static void lift_sync(void),lift_prepare(void),lift_draw(void);
static u32 lift_route_mask;
#endif
#include "customize_render.c"
#else
static void cs_apply(void){}
#endif

/* Called after the original play geometry setup, before any frame is queued.
 * The imported base frame uses its own intro, so bypass native frame themes
 * for this layout only. This changes the play object, never the saved choice. */
void frame_select(u8 *play)
{
    cs_apply();
#ifndef HOST_TEST
    ca_frame(play);
#endif
    if ((skin.frame_pack == 1 || skin.frame_pack == 2) && I32(play) == 2) {
        play[9] = 0;
        play[4] = 0;
    }
}

#ifndef HOST_TEST
/* ---- Skin settings: lane brightness ---- */
void tint_body(u8 *entry, u16 *rect, u8 *params);                  /* songdb.S: the original FUN_00135540 */

#include "eighth_hud.c"

/* lane interior (x, y, w, h) of the lane sprite of skins #67..#72 (lane_spr, songdb.S): #69 / #70 also carry the
 * progress bar frame (left / right) and a bottom edge in that sprite, which keep their colours */
static const u16 lane_in[6][4] = { { 0, 0, 144, 321 }, { 0, 0, 144, 321 }, { 13, 0, 233, 321 },
                                   { 2, 0, 233, 321 }, { 0, 0, 144, 321 }, { 0, 0, 233, 321 } };

/* tint_hook (every sprite draw) for a skin's lane sprite: the sprite in up to 5 pieces (borders untinted, the lane
 * interior with skin.lane_tint); a piece = the rect cut down + the pivot moved by the cut (FUN_00139200 places
 * left = x - pivot * zoom), so the pieces land where the whole sprite would */
void lane_draw(u8 *entry, u16 *rect, u8 *params, s32 k)
{
    u32 coloured=k>=6; k%=6;
    static const u16 colour_in[6][4]={{0,0,144,321},{0,0,144,321},{41,0,233,321},{366,0,233,321},{0,0,144,321},{0,0,233,321}};
    const u16 *in = coloured?colour_in[k]:lane_in[k];
    static const u16 djt_wide_in[4] = { 2, 0, 233, 321 };
    static const u16 eighth_wide_in[4] = { 0, 0, 233, 321 };
    if (!coloured && k == 2 && skin.frame_pack == 1) in = djt_wide_in;
    if (!coloured && k == 2 && skin.frame_pack == 2) in = eighth_wide_in;
    s32 px = I32(params + 8), py = I32(params + 12), i;
    s32 w = rect[2], h = rect[3], x1 = in[0] + in[2], y1 = in[1] + in[3];
    s32 piece[5][5] = { { in[0], in[1], in[2], in[3], 1 },          /* interior */
                        { 0, 0, in[0], h, 0 }, { x1, 0, w - x1, h, 0 },            /* left, right */
                        { in[0], 0, in[2], in[1], 0 }, { in[0], y1, in[2], h - y1, 0 } };   /* top, bottom */
    u16 r[4];
    for (i = 0; i < 5; i++) {
        s32 *p = piece[i];
        if (p[2] <= 0 || p[3] <= 0) continue;
        r[0] = rect[0] + p[0]; r[1] = rect[1] + p[1]; r[2] = p[2]; r[3] = p[3];
        I32(params + 8) = px - p[0];
        I32(params + 12) = py - p[1];
        cur_tint = p[4] ? skin.field[cs_lane_side(params)].tint : 0x808080;
        tint_body(entry, r, params);
        LOOP_PAD();
    }
    I32(params + 8) = px;
    I32(params + 12) = py;
    cur_tint = 0x808080;
}

/* bpm_hook, nothing to show (no song selected / a folder row): the panel's BPM label (scene +0xa8) becomes MUSIC
 * NUMBER like the game does when the cursor reaches a folder row. Needed on a clean entry: the panel setup chose
 * the BPM label there (selection not -1 yet at that point) */
void mn_label(u8 *scene, s32 colour)
{
    u32 *spr = *(u32 **)(scene + 0xa8);
    if (!spr || (u32)colour > 1 || (spr[0] != LBL_BPM[colour] && spr[0] != LBL_RANGE[colour])) return;
    spr_set(spr, LBL_MUSIC[colour], 0x10);       /* restarts its animation (the digits come in later frames); the
                                                    game keeps the frame (0x130ec0 / 0x130e60) only between BPM labels */
}
#endif

/* ---- title sprites of added songs, loaded on demand ---- */

s32 vd_pread(u32 lba, u32 off, u32 n, void *buf);             /* vdisc.c */

#define CONTS ((u8 *)0x34aa80)          /* loaded data1 containers, 0x228 bytes each: +0x10 sheet descs {u16 w / 2,
                                           h / 2; u32 pixels, palette}, +0x210 data1 index, +0x214 sprite rects */
#define N_CONTS (*(s32 *)0x2ad030)
#define TEXCACHE ((u8 *)0x38d9a0)       /* VRAM texture cache, 0x18 bytes each: +4 pixels, +0x10 TEX0 (0 = upload) */
#define N_TEX (*(short *)0x2ad044)

u8 spr_buf[12 + SPR_W * 48] __attribute__((aligned(64)));
static u32 ui_active; /* Exclusive code/read-window/result-title ownership. */
static void spr_forget_result(void);
/* A shared read window in the EXISTING scratch buffer. Adjacent title/genre/
 * artist sprites now share one read; bank+offset identity survives alias reuse.
 * No new buffer, heap allocation, background read or PLAY scheduling change. */
static u32 spr_read_bank,spr_read_off,spr_read_bytes;
static u32 spr_u16(const u8 *p){return p[0]|((u32)p[1]<<8);}
static u32 spr_u32(const u8 *p){return spr_u16(p)|(spr_u16(p+2)<<16);}
static const u8 *spr_source(s32 c,s32 k,u32 rows)
{
    if(ui_active)return 0;
    u32 off=sdb.spr_off[k*NSPR+c],start=off,i,w,h,need;
    for(i=0;i<NSPR;i++)if(sdb.spr_off[k*NSPR+i]<start)start=sdb.spr_off[k*NSPR+i];
    if(off-start>sizeof spr_buf-12)start=off;
    for(i=0;i<3;i++) {
        if(spr_read_bank==sdb.spr_lba && off>=spr_read_off &&
           off-spr_read_off<=spr_read_bytes && spr_read_bytes-(off-spr_read_off)>=12) {
            const u8 *p=spr_buf+(off-spr_read_off);
            if(spr_u32(p)!=0x31525053u)break;
            w=spr_u16(p+4);h=spr_u16(p+6);
            if(!w || !h || w>SPR_W || h>48)break;
            if(h>rows)h=rows;need=12+w*h;
            if(need<=spr_read_bytes-(off-spr_read_off))return p;
            start=off;
        }
        if(i==2)break;
        spr_read_bytes=0;spr_forget_result();
        s32 n=vd_pread(sdb.spr_lba,start,sizeof spr_buf,spr_buf);
        if(n<12)break;
        spr_read_bank=sdb.spr_lba;spr_read_off=start;spr_read_bytes=n;
        start=off;
        LOOP_PAD();
    }
    spr_read_bytes=0;return 0;
}    /* one complete title sprite, bounded by the strip geometry */
#include "random_title.h"

static void random_title_unpack(void)
{
    if(ui_active)return;
    spr_read_bytes=0;spr_forget_result();
    u32 i,n=0,j;
    for(i=0;i<sizeof random_title_rle;i+=2) {
        for(j=0;j<random_title_rle[i];j++) {spr_buf[n++]=random_title_rle[i+1];LOOP_PAD();}
        LOOP_PAD();
    }
}

#include "result_title.c"
#ifdef SPR_ASYNC
#include "select_artwork.c"
#endif

/* PSMT8 sheet byte of pixel (x, y), sheet width w (GS swizzle as the game uploads it) */
static u32 swz(u32 x, u32 y, u32 w)
{
    u32 r = y & 3, xr = x ^ ((((y >> 1) ^ (y >> 2)) & 1) << 2);
    return (y >> 2) * 4 * w + (r & 1) * 2 * w + (x >> 4) * 32 + 4 * (xr & 7) + 2 * ((xr >> 3) & 1) + (r >> 1);
}

/* handle of song slot k's sprite in container c: a cache slot, loaded from the bank on a miss */
/* the loaded copy of a title container, 0 = not loaded */
static u8 *spr_cont(spr_cache_t *sc)
{
    u32 i;
    for (i = 0; i < (u32)N_CONTS; i++) {
        if (*(u32 *)(CONTS + i * 0x228 + 0x210) == sc->cont) return CONTS + i * 0x228;
        LOOP_PAD();
    }
    return 0;
}

/* Each added song has its own sprite in every title container (the launcher's empty rects after the strips:
 * sprite first + n + k), so a handle the game keeps stays that song's. Its pixels live in one of the n strips while
 * resident (its rect points there); evicted or after the container is loaded again its rect is 0x0 (draws nothing)
 * until spr_ensure loads it again (called by the getters and before every draw of such a sprite). */
#define SONG_RECT(sc, rects, k) ((rects) + ((sc)->first + (sc)->n + (k)) * 8)
#ifdef SPR_ASYNC
static int sa_cached(u32 c,u32 k){
 spr_cache_t *sc=&sdb.spr[c];u8 *e=spr_cont(sc);
 if(!sc->n||!e)return 1; /* no loaded destination to prepare yet */
 for(u32 i=0;i<sc->n;i++)if(sc->key[i]==k){
  u8 *r=SONG_RECT(sc,(u8 *)(long)*(u32 *)(e+0x214),k);return *(u16 *)(r+4)!=0;
 }
 return 0;
}
#endif

/* song slot k's sprite of container c resident in the loaded container e */
static void spr_ensure(s32 c, u8 *e, s32 k)
{
#ifdef SELECT_PROFILE
    unsigned profile_start=sp_span();
#endif
    spr_cache_t *sc = &sdb.spr[c];
    u8 *rects = (u8 *)(long)*(u32 *)(e + 0x214), *rect = SONG_RECT(sc, rects, k), *strip, *desc, *px;
    u32 i, j, w, h, sw, y0, x, y, n;
    const u8 *source=spr_buf;
    int retained=c==0 && result_title_has(sdb.spr_lba,sdb.spr_off[k*NSPR+c]);
    for (j = 0; j < sc->n; j++) {
        if (sc->key[j] == k) break;
        LOOP_PAD();
    }
    if (j < sc->n && *(u16 *)(rect + 4)) { sc->used[j] = ++sc->tick;
#ifdef SELECT_PROFILE
        sp_art(2,c,k,profile_start?sp_span()-profile_start:0);
#endif
        return; }
    if(ui_active)return; /* A modal overlay owns this scratch until its call returns. */
#ifdef SPR_ASYNC
    /* A pending miss cannot evict an on-screen owner or issue FILEIO. */
    if(sa.data && (u32)k<sdb.n_new && !retained){source=sa_source(c,k);if(!source)return;}
    /* Pages make several cold labels available together. Keep metadata
     * getters from spending the upload budget before visible draws arrive. */
    if(sa.wheel_state==1&&!c){
        if((!sa.drawing&&(u32)k!=sa_focus())||sa.uploads>=4)return;
        sa.uploads++;
    }
#endif
#ifdef SELECT_PROFILE
    /* Count materializations, not repeated pending demands every draw. */
    sp_art(3,c,k,0);
#endif
    for (i = 0; i < sc->n; i++) { if (sc->key[i] == k) sc->key[i] = NOSLOT; LOOP_PAD(); }   /* stale (reloaded) */
    for (i = j = 0; i < sc->n; i++) {
        if (sc->key[i] == NOSLOT) { j = i; break; }
        if (sc->used[i] < sc->used[j]) j = i;
        LOOP_PAD();
    }
    if (sc->key[j] != NOSLOT) {                          /* evict: that song's sprite draws nothing until reloaded */
        u8 *old = SONG_RECT(sc, rects, sc->key[j]);
        *(u16 *)(old + 4) = 0; *(u16 *)(old + 6) = 0;
    }
    sc->key[j] = k;
    sc->used[j] = ++sc->tick;
    strip = rects + (sc->first + j) * 8;
    y0 = *(u16 *)(strip + 2);
    desc = e + 0x10 + (y0 >> 10) * 0x10;
    sw = *(u16 *)desc * 2;                              /* descs hold the PSMCT32 upload size: w / 2, h / 2 */
    px = (u8 *)(long)*(u32 *)(desc + 4);
    /* Disk cache changes/short reads must never become dimensions or pixels.
     * Keep the stable song handle empty so the next draw can retry safely. */
    if(retained){source=result_title.data;result_title_stats[1]++;}
    else if(c==1 && (u32)k==sdb.n_new)random_title_unpack();
    #ifdef SPR_ASYNC
    else if(sa.data && (u32)k<sdb.n_new){} /* already validated worker bundle */
#endif
    else if(!(source=spr_source(c,k,sc->rows)))goto bad_sprite;
    if(spr_u32(source)!=0x31525053u)goto bad_sprite;
    w = spr_u16(source+4); h = spr_u16(source+6);
    if (!w || !h || w>sw || h>48) goto bad_sprite;
    if(h>sc->rows)h=sc->rows;
    n = w * h;
    if (n > sizeof spr_buf - 12) goto bad_sprite;
    if(retained){if(12+n>result_title.bytes)goto bad_sprite;}
    else if(c==1 && (u32)k==sdb.n_new) {
        if(12+n>RANDOM_TITLE_BYTES)goto bad_sprite;
    }
    for (y = 0; y < sc->rows; y++)
        for (x = 0; x < sw; x++) {
            px[swz(x, (y0 & 1023) + y, sw)] = x < w && y < h ? source[12 + y * w + x] : source[10];
            LOOP_PAD();
        }
    *(u16 *)rect = 0; *(u16 *)(rect + 2) = y0; *(u16 *)(rect + 4) = w; *(u16 *)(rect + 6) = h;
    for (i = 0; i < (u32)N_TEX; i++) {                 /* re-upload the sheet */
        if (*(u32 *)(TEXCACHE + i * 0x18 + 4) == (u32)(long)px) *(unsigned long long *)(TEXCACHE + i * 0x18 + 0x10) = 0;
        LOOP_PAD();
    }
#ifdef SELECT_PROFILE
    sp_art(4,c,k,profile_start?sp_span()-profile_start:0);
#endif
    return;
bad_sprite:
    sc->key[j]=NOSLOT;
    *(u16 *)(rect+4)=0;*(u16 *)(rect+6)=0;
#ifdef SELECT_PROFILE
    sp_art(5,c,k,profile_start?sp_span()-profile_start:0);
#endif

}

#ifdef SPR_ASYNC
/* Called only on main at the existing sprite getter/draw boundary. Worker
 * completion isn't dequeue; never publish while any native request still owns
 * the buffer/container. Cache the wheel even if its native callback was early. */
static __attribute__((noinline)) void sa_publish(void){
 if(!sa.live||ui_active||sa.state!=2||sa_word(0x78af08)||sa_word(0x78b70c)||sa.bank!=sdb.spr_lba)return;
 if(sa.kind){sa_wheel_publish();return;}
 sa_barrier();sa_hold_focus();
 if(sa.published)return;
 sa.published=1;sa.ready++;
 if(sdb.spr_off[sa.job*NSPR]!=~0u){u8 *e=spr_cont(&sdb.spr[0]);if(e&&sdb.spr[0].n)spr_ensure(0,e,sa.job);}
}
#endif

/* handle of song slot k's sprite in container c (resident if the container is loaded) */
static u32 spr_get(s32 c, s32 k)
{
#ifdef SPR_ASYNC
    sa_publish();
#endif
    spr_cache_t *sc = &sdb.spr[c];
    u8 *e = spr_cont(sc);
    if (e) spr_ensure(c, e, k);
#ifdef SELECT_PROFILE
    if(e)sp_art(8,c,k,0);
#endif
    return (u32)sc->cont << 16 | (sc->first + sc->n + k);
}

/* FUN_00135720 (draw sprite: container, sprite): an added song's title sprite is made resident before it is drawn
 * (the game keeps handles across container reloads and evictions) */
void spr_draw(s32 cont, s32 idx)
{
    s32 c = cont - 0x12a;
    spr_cache_t *sc;
    u8 *e;
    u32 k;
    if ((u32)c >= NSPR) return;
    sc = &sdb.spr[c];
    k = idx - sc->first - sc->n;
    if (!sc->n || k > sdb.n_new || (k==sdb.n_new && c!=1) ||
        (k<sdb.n_new && sdb.spr_off[k * NSPR + c] == ~0u) || !(e = spr_cont(sc))) return;
#ifdef SPR_ASYNC
    sa_publish();sa.drawing=1;
#endif
    spr_ensure(c, e, k);
#ifdef SPR_ASYNC
    sa.drawing=0;
#endif
}

/* FUN_001e3410 / 33d0 / 3390 / 3350 (song id -> wheel title / big title / genre / artist sprite handle) */
static u32 title_spr(s32 id, s32 c, s32 field)
{
    s32 row=song_catalog_row(id),k=row-FIRST;
    if(row<0)return 0;
    if (row >= FIRST && sdb.spr_off && sdb.spr_off[k * NSPR + c] != ~0u &&
        sdb.spr[c].n)
        return spr_get(c, k);
    return *(u32 *)(rec_of(song_ctx(), id) + field);
}
u32 spr_wheel(s32 id) { return title_spr(id, 0, 0xb8); }
u32 spr_big(s32 id) { return title_spr(id, 1, 0xb0); }
u32 spr_genre(s32 id) { return title_spr(id, 2, 0xac); }
u32 spr_artist(s32 id) { return title_spr(id, 3, 0xb4); }

/* Only music-select panel call sites use these wrappers. Leave gameplay,
 * RESULT and native RANDOM's actual choice/difficulty logic untouched. The
 * extra stable rect is a RAM-generated title (big), or an empty rect (others). */
static u32 panel_spr(s32 id,s32 c,s32 field)
{
    if(MS_STATE && I32(MS_STATE+0x24)==124 && I32(MS_STATE+0x28)==id && sdb.spr[c].n) {
        spr_cache_t *sc=&sdb.spr[c];
        return c==1?spr_get(c,sdb.n_new):((u32)sc->cont<<16)|(sc->first+sc->n+sdb.n_new);
    }
    return title_spr(id,c,field);
}
u32 spr_panel_big(s32 id) {return panel_spr(id,1,0xb0);}
u32 spr_panel_genre(s32 id) {return panel_spr(id,2,0xac);}
u32 spr_panel_artist(s32 id) {return panel_spr(id,3,0xb4);}

/* ---- current folder: never the hidden ALL VERSION ---- */

#define vis_orig ((s32 (*)(u8 *, s32, s32, s32))0x1d8e30)        /* FUN_001d8e30(save, id, mode, x) */
/* MS_STATE: music-select state; +0x30 current folder. */

static s32 shown(s32 id)
{
    if (id == 2) return 0;
    if ((u32)(id - FOLD0) < NFOLD) return sdb.fold_n[id - FOLD0] != 0;
    return 1;
}

static s32 first_folder(void)
{
    s32 id = 0, n = 0;
    while (id != 0xff && !shown(id) && n++ < 256) { id = sdb.next[id]; LOOP_PAD(); }
    return id == 0xff ? 1 : id;
}

/* FUN_001d85c0 (saved folder; 0 / -1 when none): a folder in the menu */
s32 saved_folder(s32 id)
{
    if (id == 0x2a) return id;
    if (((u32)id < 0x2c || (u32)(id - FOLD0) < NFOLD) && shown(id)) return id;
    return first_folder();
}

/* 0x18bcfc: the current folder's re-check; the game resets an invisible one to 0 -> the first menu folder */
s32 vis_cur(u8 *save, s32 id, s32 mode, s32 x)
{
    if (id != 2 && ((u32)(id - FOLD0) < NFOLD ? shown(id) : vis_orig(save, id, mode, x))) return 1;
    *(s32 *)(MS_STATE + 0x30) = first_folder();
    return 1;
}

/* ---- song variants ---- */

#ifndef HOST_TEST
#define song_now ((s32 (*)(void))0x205e00)                     /* the song about to be played */
#define pad_select ((s32 (*)(s32))0x13ad30)                    /* SELECT held (port), NO CRIME J's check */
#define pad_cw ((s32 (*)(s32))0x13ae50)                        /* turntable clockwise (+0x59 of the pad state) */
#define pad_ccw ((s32 (*)(s32))0x13ae70)                       /* turntable counter-clockwise (+0x57) */
#define SONG_OVERRIDE (*(s32 *)0x2ace5c)                       /* -1, or the song loaded instead (99 = NO CRIME J) */
#define DICE(r) __asm__ volatile("mfc0 %0, $9" : "=r"(r))       /* COP0 Count: random enough for a dice roll */
static u32 qr_variant_restore(s32 song);
#endif

/* replaces the jal FUN_00205e00 of the NO CRIME check (0x1f5f18): an added song with variants loads one of them
 * when SELECT (+ turntable) is held, or by chance; the variant plays as alias VARIANT */
s32 variant_song(void)
{
    s32 s = song_now(), a = s & 0xff, row=song_catalog_row(s), i, sel, cw, ccw, pick = -1;
    u32 roll;
    u16 k;
#ifndef HOST_TEST
    /* SELECT on RESULT Continue means keep this attempt, not select another
     * chart/audio variant. This getter is used ONLY by the NO CRIME check;
     * a handled source returns the non-song sentinel to skip its held key. */
    if(qr_variant_restore(s))return -1;
#endif
    if (row < FIRST || a == VARIANT || !sdb.nvar) return s;
    k=row-FIRST;
    sel = pad_select(0) || pad_select(1);
    cw = pad_cw(0) || pad_cw(1);
    ccw = pad_ccw(0) || pad_ccw(1);
    DICE(roll);
    for (i = 0; i < (s32)sdb.nvar && i<MAXVAR && pick < 0; i++) {
        var_t *v = &sdb.vars[i];
        if (v->main != k) continue;
        if (sel ? (v->how == VAR_SELECT && !cw && !ccw) || (v->how == VAR_CW && cw) || (v->how == VAR_CCW && ccw)
                : v->pct && roll % 100 < v->pct)
            pick = v->var;
        LOOP_PAD();
    }
    if (pick >= 0 && (u32)pick<sdb.n_new) {
        sdb.slot_of[VARIANT] = pick;
        SONG_OVERRIDE = VARIANT;
    }
    return s;
}

#ifndef HOST_TEST
#include "gauge_shift.c"
static void bn_reset(void),bn_start(void),bn_draw(void);
static u32 fh_wait_field(void);
#ifdef HOLDS
#include "../holds/holds.c"
#include "../holds/holds_empress.c"
#endif
#include "fast_slow.c"
#include "customize_judgement.c"
#include "customize_assets.c"
#include "bpm_notice.c"
#include "floating_hispeed.c"
#ifdef PLAY_LIFT
#include "lift.c"
#endif
static void qr_select_clear(void);
#include "mod_menu.c"
#include "quick_retry.c"
#include "ranking.c"
#include "song_resources.c"
#if defined(USB_HID) || defined(PLAY_PREVIEW)
static int pve_asset_queue(u32 *);
#define ASSET_PREVIEW_QUEUE(q) pve_asset_queue(q)
#endif
#include "asset_memory.c"
#ifdef PREP_WATCH
#include "prep_watch.c"
#endif
#endif

#ifndef HOST_TEST
#include "save_runtime.c"
#endif

#if defined(INPUT_LOG) && !defined(HOST_TEST)
#include "input_probe.c"
#endif
#ifndef HOST_TEST
#ifdef USB_BOOT_TEXT
static void boot_loop_note(u32,u32,u32);
#endif
#include "audio_dispatch.c"
#endif

#if defined(EXIT_TRACE) && !defined(HOST_TEST)
#include "exit_trace.c"
#endif

#if defined(USB_BOOT_TRACE) && !defined(HOST_TEST)
#include "boot_trace.c"
#endif

#if defined(LOAD_PROFILE) && !defined(HOST_TEST)
#ifdef PREP_PROBE
#include "prep_probe.c"
#endif
#ifdef SELECT_PROFILE
#include "select_profiler/capture.c"
#else
#include "load_profile.c"
#endif
#endif

#ifdef USB_HID
#include "usb_hid/native.c"
#endif

#if !defined(HOST_TEST) && (defined(USB_HID) || defined(PLAY_PREVIEW))
#include "play_preview.c"
#endif

#ifdef PREVIEW_TRACE
#include "preview_trace.c"
#include "preview_fault.c"
#endif
