/* Hold-note rules for EMPRESS MEGAMIX: CN, HCN, BSS and HBSS.
 *
 * Engine independent: no libc, no game addresses, integer chart time only, so the
 * same file builds for the R5900 runtime and for host tests (test_holds.c).
 * The engine layer (holds_empress.c, written against SLPM_552.21) feeds it:
 *   - the hold list of the chart being played (holds_reset)
 *   - each judged head note (holds_head)
 *   - once per frame: chart time, held keys, turntable direction (holds_frame)
 * and applies what comes out of holds_pop (tail judgements, combo breaks, HCN ticks)
 * through the game's own judgement, combo and gauge routines.
 *
 * Lanes: 0..6 = P1 keys 1..7, 7 = P1 turntable, 8..14 = P2 keys, 15 = P2 turntable.
 */
#ifndef MEGAMIX_HOLDS_H
#define MEGAMIX_HOLDS_H

typedef signed int hs32;
typedef unsigned int hu32;
typedef unsigned short hu16;
typedef signed char hs8;
typedef unsigned char hu8;

#define HOLDS_MAX 1024          /* holds per chart (largest arcade charts stay well below) */
#define HOLDS_EVENTS 64         /* events buffered between two holds_pop drains */
#define HOLDS_LANES 16

enum hold_kind { HOLD_CN = 0, HOLD_HCN = 1, HOLD_BSS = 2, HOLD_HBSS = 3 };
enum hold_judge { J_PGREAT = 0, J_GREAT = 1, J_GOOD = 2, J_BAD = 3, J_POOR = 4 };
enum hold_event_kind {
    HE_TAIL = 1,       /* tail judged: judge, offset (release time - end; <0 early) */
    HE_BREAK = 2,      /* combo break without a tail judgement (HCN/HBSS let go) */
    HE_TICK_GAIN = 3,  /* HCN/HBSS body held for one tick: combo +1, gauge up */
    HE_TICK_LOSS = 4   /* HCN/HBSS body not held for one tick: gauge down */
};
enum overhold { OVERHOLD_RELEASE = 0, OVERHOLD_AUTO = 1 };

typedef struct {
    hs32 start, end;   /* chart time of head and tail */
    hu8 lane, kind;
    hu16 pad;
} hold_def;

typedef struct {
    hu8 kind, judge, lane, pad;
    hs32 offset;
    hu32 index;        /* hold_def index */
} hold_event;

typedef struct {
    hs32 tail_window[4];  /* |offset| <= tail_window[j] -> judge j (PGREAT..BAD), else POOR */
    hs32 head_late;       /* a head not reported this long after start counts as missed */
    hs32 tick;            /* HCN/HBSS tick interval in chart time */
    hs32 spin_grace;      /* BSS: turntable may stand still this long without letting go */
    hu8 overhold;         /* OVERHOLD_RELEASE: tail must be released; OVERHOLD_AUTO: held at end = PGREAT */
    hu8 pad[3];           /* letting go of an HCN/HBSS breaks combo once per release (HE_BREAK) */
} holds_cfg;

enum hold_state { HS_PENDING = 0, HS_HOLDING = 1, HS_LOOSE = 2, HS_MISSED = 3, HS_DONE = 4 };

typedef struct {
    const hold_def *defs;
    hu32 n, next;               /* holds sorted by start; next = first not yet activated */
    hs32 active[HOLDS_LANES];   /* hold index per lane or -1 */
    hu8 state[HOLDS_MAX];
    hs8 spin[HOLDS_LANES];      /* BSS: direction of the head spin, 0 = not yet seen */
    hs32 stood[HOLDS_LANES];    /* BSS: time the turntable stopped, or -1 */
    hs32 next_tick[HOLDS_LANES];
    holds_cfg cfg;
    hold_event ev[HOLDS_EVENTS];
    hu32 nev, dropped;
} holds_t;

/* defs must stay valid and be sorted by start; returns -1 if n > HOLDS_MAX or unsorted */
int holds_reset(holds_t *h, const hold_def *defs, hu32 n, const holds_cfg *cfg);
/* the engine judged a head note on lane at chart time start: returns the hold index or -1 (not a hold) */
int holds_head(holds_t *h, hu32 lane, hs32 start, hu32 judge);
/* once per frame. keys: bit per lane (turntable bits ignored); tt[side] = -1, 0, +1 */
void holds_frame(holds_t *h, hs32 now, hu32 keys, const hs8 tt[2]);
/* copy out up to max events (oldest first); returns the count copied */
hu32 holds_pop(holds_t *h, hold_event *out, hu32 max);
/* 1 while a hold body is drawn as held on this lane (for the renderer) */
int holds_lane_held(const holds_t *h, hu32 lane);
/* tail notes the engine must not judge itself (they are judged here) */
int holds_is_tail(const holds_t *h, hu32 lane, hs32 time);

#endif
