/* Song database shared by the off-disc runtime (songdb.c / songdb.S, EE side) and the launcher (launcher/main.c),
 * which fills it before starting the game. Field offsets used by songdb.S are the O_* constants there (checked by
 * static asserts in songdb.c). Needs u8 / u16 / u32 typedefs.
 *
 * Added songs (not on the EMPRESS disc) are numbered by slot 0..n_new-1 (any number). The game only ever sees an
 * 8-bit ALIAS of a slot: alias ids are bound when a folder is opened (songdb.c bind), least recently used first,
 * The canonical folder list uses stable 16-bit keys; only a moving window is
 * exposed to the original byte-based wheel. */
#ifndef SONGDB_H
#define SONGDB_H
#include "custom_art.h"

#define FIRST 104                      /* first alias id (EMPRESS ids are 0..103) */
#define PROBE 123                      /* alias the runtime itself uses to ask the game about any slot */
#define VARIANT 122                    /* alias of the song variant being played (hold SELECT at load, songdb.c) */
#define NALIAS 145                     /* aliases 104..121 and 128..254 (wheel rows: RANDOM 124, folders 125/126;
                                          ids >= 128 are negative in the game's signed byte loads, -1 stays unused) */
#define NSAVE 152                      /* dummy save records per id 104..255 (index id - FIRST) */
#define FOLD0 59                       /* music-select folder ids FOLD0.. = our folders: 59 CUSTOM, 60.. versions */
#define NFOLD 45                       /* CUSTOM + up to 44 style folders (ids 59..103). The folder menu holds 88 entries:
                                          ids 0, 1 and 3..0x2b (43) + ours; build_vdisc.py sizes its arrays for 96 */
#define NOFOLD 0xff
#define NOSLOT 0xffff
/* asset_workspace1: ordinary words remain positive cuts from708000; flagged
 * words encode an absolute big-bank workspace span, including sector tails. */
#define ASSET_WORKSPACE_ABSOLUTE 0x80000000u
#define ASSET_WORKSPACE_MASK 0x7fffffffu
#define ASSET_WORKSPACE_FLOOR 0x708000u
#define SONG_KEY 256                   /* canonical key of added slot k = SONG_KEY+k */
#define SONG_VIEW 95                   /* song rows, plus at most two native special rows */

/* "virtual folders" the runtime adds songs to: ours (59..103), then the game's LEVEL 1-12 (ids 3..14, one list for
 * all difficulties), A-Z groups (ids 15..21, per difficulty), NO PLAY (id 30, one list) */
#define VF_LEVEL NFOLD
#define VF_ALPHA (VF_LEVEL + 12)
#define VF_NOPLAY (VF_ALPHA + 7)
#define VF_ALL (VF_NOPLAY + 1)
#define NVF (VF_ALL + 1)

/* title sprites of added songs (wheel 0x12a, big 0x12b, genre 0x12c, artist 0x12d): loaded on demand into a few
 * slots per container (blank full-width strips the launcher adds, cont_strips) from the sprite bank file */
#define NSPR 4
#define SPR_W 384                      /* slot sheet width */
#define SPR_SLOTS 32                   /* most slots per container (wheel) */

typedef struct {
    u16 cont, first, n, rows;          /* data1 container, first slot sprite, slots, slot height (rows) */
    u32 inst;                          /* rect table of the loaded container the slots below describe */
    u32 tick, used[SPR_SLOTS];         /* LRU */
    u16 key[SPR_SLOTS];                /* slot -> song slot, NOSLOT = empty */
} spr_cache_t;

#define NPOOL 3                        /* music-select contexts (scene +0xc8/+0xcc/+0xd0: SP, DP, style 3) */

/* song variants (another version of a song, picked while it loads, like NO CRIME / NO CRIME J): the variant has
 * its own slot but is in no folder */
#define MAXVAR 32
enum { VAR_SELECT, VAR_CCW, VAR_CW };   /* hold SELECT / SELECT + turntable counter-clockwise / clockwise */
typedef struct {
    u16 main, var;                     /* slots */
    u8 how, pct;                       /* VAR_*, chance in % without input (CROSSROAD's Right Story: 20) */
    u8 pad[2];
} var_t;

typedef struct {                       /* per music-select context */
    u8 *ctx;                           /* 0 = free */
    u8 *listed;                        /* per slot: bit d = chart (song, diff d) listed in this context */
    u16 cnt[NVF][5];                   /* songs per virtual folder and difficulty (diff 0 for one-list folders) */
    u8 mask[NVF][5];                   /* lamp mask per virtual folder and difficulty */
} pool_t;

/* skin settings (launcher Skin settings menu); the launcher finds this through vdisc.txt "skin <addr>" */
typedef struct {
    u32 lane_tint;                     /* lane sprites' vertex colour 0x00BBGGRR, 0x80 = 1.0 (draw_hook, songdb.S) */
    u32 note_zoom;                     /* +4  note zoomY % for the game's own note sprites (note_hook) */
    u32 note_zoom_skin;                /* +8  ... for note skin sprites (their own height) */
    u16 skin_first[2];                 /* +12 first note skin sprite in #109 / #146 (0xffff = none) */
    int beam_dy;                       /* +16 keybeam y shift (beam_hook): 256 * (1 - length) */
    u32 frame_pack;                    /* +20 0 original, 1 DJT, 2 8th; P1 wide SP (#69, layout 2) */
    struct {u32 tint,zoom,zoom_skin;int beam_dy;u32 beam_zoom,judge,pad[2];} field[2]; /* +24, +56 physical fields */
    u32 note_rows;                     /* selected note-art source height */
    struct custom_art_catalog *custom_art; /* +92 catalog, immutable after launch */
} skin_t;

typedef struct {
    u32 magic;                         /* 'SNGD' */
    u32 n_new;                         /* added songs (slots) */
    u8 *recs;                          /* n_new * 0x134 songlist records */
    u8 *saves;                         /* NSAVE * 20 * 0x1c dummy save records (style 0-3 x diff 0-4) per alias */
    u32 *fid_tab;                      /* added files, fid 1062..: {offset from BM2DX16C.DAT, sectors, size} */
    u32 custom_n;                      /* songs in the CUSTOM folder */
    u8 present[FIRST];                 /* +24  EMPRESS ids 0..103: 1 = song folder present */
    u8 emp_fold[FIRST];                /* +128 EMPRESS id -> our folder index (id - FOLD0), NOFOLD = game folders */
    u8 next[256];                      /* +232 folder menu order: id -> next id, 0xff = end */
    u8 fold_n[NFOLD];                  /* +488 songs per folder (shown if > 0; counts stop at 255) */
    u8 pad[3];                         /* slot_of at +536 (songdb.S O_SLOT) */
    u16 slot_of[256];                  /* +536 alias id -> slot, NOSLOT = unbound */
    u8 *slot_fold;                     /* +1020 slot -> our folder index */
    u8 *alias_of;                      /* +1024 slot -> alias id, 0 = unbound */
    u32 clock, stamp[256];             /* alias LRU: stamp = clock of the folder open that last used it */
    pool_t pools[NPOOL];
    u32 spr_lba;                       /* sprite bank (.spr files back to back) at this virtual LBA */
    u32 *spr_off;                      /* per slot x NSPR: byte offset of its .spr in the bank, ~0 = none */
    spr_cache_t spr[NSPR];
    u16 *var_main;                     /* per slot: NOSLOT, or the slot this one is a variant of */
    u32 nvar;
    var_t vars[MAXVAR];
    /* v1 persistent chart identity. Index/alias/slot numbers are transient.
     * Row = native id, or FIRST+added slot; ten columns SP N/H/A/B/X, DP N/H/A/B/X.
     * IDs are sixteen canonical bytes, independent of native byte order. */
    u32 identity_version, identity_count;
    u16 *chart_index;                  /* (FIRST+n_new)*10, NOSLOT = no chart */
    u8 *chart_ids;                     /* identity_count*16 bytes */
    u32 sort_version;                   /* 1 = complete per-row alphabetical ranks */
    u16 *sort_rank;                     /* FIRST+n_new; independent of scores/source priority */
} sdb_t;

const u8 *chart_identity(int song, int dp, int diff);
const u8 *chart_identity_row(int row, int dp, int diff);

#endif
