/* Hold-note rules for EMPRESS MEGAMIX: CN, HCN, BSS and HBSS.
 *
 * Engine independent: no libc, no game addresses, integer chart time only, so the
 * same file builds for the R5900 runtime and for host tests (test_holds.c).
 * The engine layer (holds_empress.c, written against SLPM_552.21) feeds it:
 *   - the hold list of the chart being played (holds_reset)
 *   - each judged head note (holds_head)
 *   - once per tick: chart time, held keys, turntable direction (holds_frame)
 * and applies what comes out of holds_pop through the game's own judgement,
 * combo and gauge routines.
 *
 * Rules (docs/holds-design.md has the sources):
 *   CN/BSS  head judged by the game. Release inside the tail window: PGREAT
 *           (one wide window unless tail_graded). Release earlier: POOR.
 *           Still held after the window: overhold_judge (POOR by default).
 *           BSS: spinning back, or stopping, inside the window releases it.
 *   HCN/HBSS while held, every 16th note (the hold's tick64): HE_TICK_GAIN
 *           (gauge as for a hit, no combo). Letting go or missing the head:
 *           HE_BREAK (real POOR, combo break) plus HE_TICK_LOSS (empty POOR)
 *           at once, then HE_TICK_LOSS every 16th from that moment until the
 *           lane is held again. The end never adds a POOR or gauge loss. Once
 *           broken, the tail is J_NONE (no score), even after re-grabbing.
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
#define HOLDS_FP 64             /* tick64: 16th-note length in 1/64 chart ticks */

enum hold_kind { HOLD_CN = 0, HOLD_HCN = 1, HOLD_BSS = 2, HOLD_HBSS = 3 };
enum hold_judge { J_PGREAT = 0, J_GREAT = 1, J_GOOD = 2, J_BAD = 3, J_POOR = 4, J_NONE = 5 };
enum hold_event_kind {
    HE_TAIL = 1,       /* tail judged: judge (J_NONE = no score/gauge/combo), offset (release - end) */
    HE_BREAK = 2,      /* HCN/HBSS let go or head missed: real POOR, combo break */
    HE_TICK_GAIN = 3,  /* HCN/HBSS held for one 16th: gauge as for a hit, no combo */
    HE_TICK_LOSS = 4   /* HCN/HBSS not held for one 16th: empty POOR */
};
enum overhold { OVERHOLD_RELEASE = 0, OVERHOLD_AUTO = 1 };

typedef struct {
    hs32 start, end;   /* chart time of head and tail */
    hu8 lane, kind;
    hu16 tick64;       /* HCN/HBSS: 16th-note length at the head's BPM, 1/64 ticks; 0 = cfg.tick64 */
} hold_def;

typedef struct {
    hu8 kind, judge, lane, pad;
    hs32 offset;
    hu32 index;        /* hold_def index */
} hold_event;

typedef struct {
    hs32 tail_window[4];  /* tail_graded: |offset| <= tail_window[j] -> judge j; else only [J_BAD] is used, as the PGREAT window */
    hs32 head_late;       /* a head not reported this long after start counts as missed */
    hs32 tick64;          /* fallback 16th length for holds without one, 1/64 ticks */
    hs32 spin_grace;      /* BSS: turntable may stand still this long without letting go */
    hu8 overhold;         /* OVERHOLD_RELEASE: held past the window = overhold_judge; OVERHOLD_AUTO: held at end = PGREAT */
    hu8 overhold_judge;   /* J_POOR (default) or J_BAD (later arcade versions) */
    hu8 tail_graded;      /* 0: one PGREAT window (arcade); 1: grade releases PGREAT..BAD by offset */
    hu8 pad;
} holds_cfg;

enum hold_state { HS_PENDING = 0, HS_HOLDING = 1, HS_LOOSE = 2, HS_MISSED = 3, HS_DONE = 4 };

typedef struct {
    const hold_def *defs;
    hu32 n, next;               /* holds sorted by start; next = first not yet activated */
    hs32 active[HOLDS_LANES];   /* hold index per lane or -1 */
    hu8 state[HOLDS_MAX];
    hu8 broken[HOLDS_MAX];      /* HCN/HBSS: let go at least once */
    hs8 spin[HOLDS_LANES];      /* BSS: direction of the head spin, 0 = not yet seen */
    hs32 stood[HOLDS_LANES];    /* BSS: time the turntable stopped, or -1 */
    hs32 next_tick[HOLDS_LANES];/* HCN/HBSS: next tick, 1/64 ticks */
    holds_cfg cfg;
    hold_event ev[HOLDS_EVENTS];
    hu32 nev, dropped;
} holds_t;

/* defs must stay valid and be sorted by start; returns -1 if n > HOLDS_MAX or a record is invalid */
int holds_reset(holds_t *h, const hold_def *defs, hu32 n, const holds_cfg *cfg);
/* the engine judged a head note on lane at chart time start: returns the hold index or -1 (not a hold) */
int holds_head(holds_t *h, hu32 lane, hs32 start, hu32 judge, hs32 now);
/* the engine logged a judgement on lane at tick now but cannot name the note: take the pending
 * hold on that lane whose start is nearest now, within window ticks. Returns the index or -1. */
int holds_head_near(holds_t *h, hu32 lane, hs32 now, hs32 window, hu32 judge);
/* once per tick. keys: bit per lane (turntable bits ignored); tt[side] = -1, 0, +1 */
void holds_frame(holds_t *h, hs32 now, hu32 keys, const hs8 tt[2]);
/* copy out up to max events (oldest first); returns the count copied */
hu32 holds_pop(holds_t *h, hold_event *out, hu32 max);
/* 1 while a hold body is drawn as held on this lane (for the renderer) */
int holds_lane_held(const holds_t *h, hu32 lane);
/* tail notes the engine must not judge itself (they are judged here) */
int holds_is_tail(const holds_t *h, hu32 lane, hs32 time);

#endif
