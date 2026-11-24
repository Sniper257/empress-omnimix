/* Hold-note rules (CN, HCN, BSS, HBSS). See holds.h. No libc, no engine references. */
#include "holds.h"

/* byte copy: the runtime has no libc, and struct assignment may compile to memcpy */
static void copy(void *dst, const void *src, hu32 n)
{
    volatile hu8 *d = (volatile hu8 *)dst;
    const hu8 *s = (const hu8 *)src;
    while (n--) { *d++ = *s++; LOOP_PAD(); }
}

static void emit(holds_t *h, hu32 kind, hu32 index, hu32 judge, hs32 offset)
{
    hold_event *e;
    if (h->nev >= HOLDS_EVENTS) { h->dropped++; return; }
    e = &h->ev[h->nev++];
    e->kind = (hu8)kind; e->judge = (hu8)judge; e->lane = h->defs[index].lane; e->pad = 0;
    e->offset = offset; e->index = index;
}

static int is_hell(hu32 kind) { return kind == HOLD_HCN || kind == HOLD_HBSS; }
static int is_spin(hu32 kind) { return kind == HOLD_BSS || kind == HOLD_HBSS; }

static hs32 tick_of(const holds_t *h, hu32 i)
{
    hs32 t = h->defs[i].tick64 ? (hs32)h->defs[i].tick64 : h->cfg.tick64;
    return t > 0 ? t : HOLDS_FP;
}

/* judgement of a release inside the tail window */
static hu32 tail_judge(const holds_t *h, hs32 offset)
{
    hs32 a = offset < 0 ? -offset : offset;
    hu32 j;
    if (!h->cfg.tail_graded) return a <= h->cfg.tail_window[J_BAD] ? J_PGREAT : J_POOR;
    for (j = 0; j < 4; j++) if (a <= h->cfg.tail_window[j]) return j;
    return J_POOR;
}

static void finish(holds_t *h, hu32 i, hu32 judge, hs32 offset)
{
    emit(h, HE_TAIL, i, judge, offset);
    h->state[i] = HS_DONE;
    if (h->active[h->defs[i].lane] == (hs32)i) h->active[h->defs[i].lane] = -1;
}

int holds_reset(holds_t *h, const hold_def *defs, hu32 n, const holds_cfg *cfg)
{
    hu32 i;
    if (n > HOLDS_MAX) return -1;
    for (i = 0; i < n; i++) {
        if (defs[i].lane >= HOLDS_LANES || defs[i].kind > HOLD_HBSS || defs[i].end <= defs[i].start) return -1;
        if (i && defs[i].start < defs[i - 1].start) return -1;
        if (is_spin(defs[i].kind) != ((defs[i].lane & 7) == 7)) return -1;   /* BSS only on turntables */
        h->state[i] = HS_PENDING;
        h->broken[i] = 0;
    }
    h->defs = defs; h->n = n; h->next = 0; h->nev = 0; h->dropped = 0;
    copy(&h->cfg, cfg, sizeof h->cfg);
    if (h->cfg.overhold_judge != J_BAD) h->cfg.overhold_judge = J_POOR;
    for (i = 0; i < HOLDS_LANES; i++) { h->active[i] = -1; h->spin[i] = 0; h->stood[i] = -1; h->next_tick[i] = 0; h->up_at[i] = -1; }
    return 0;
}

static void let_go(holds_t *h, hu32 i, hs32 now, hu32 real_poor)
{
    hu32 lane = h->defs[i].lane;
    h->state[i] = HS_LOOSE;
    h->broken[i] = 1;
    if (real_poor) emit(h, HE_BREAK, i, J_POOR, now - h->defs[i].end);
    emit(h, HE_TICK_LOSS, i, J_POOR, now - h->defs[i].end);   /* the empty POOR at the moment of release */
    h->next_tick[lane] = now * HOLDS_FP + tick_of(h, i);
}

static void start_hold(holds_t *h, hu32 i, hu32 judge, hs32 now)
{
    const hold_def *d = &h->defs[i];
    hs32 old = h->active[d->lane];
    if (old >= 0 && old != (hs32)i) finish(h, (hu32)old, J_POOR, 0);   /* overlapping holds: close the old one */
    h->active[d->lane] = (hs32)i;
    h->spin[d->lane] = 0;
    h->stood[d->lane] = -1;
    h->up_at[d->lane] = -1;
    if (judge <= J_GOOD) {
        h->state[i] = HS_HOLDING;
        h->next_tick[d->lane] = d->start * HOLDS_FP + tick_of(h, i);
    } else if (is_hell(d->kind)) {
        let_go(h, i, now, 0);   /* the game already counted the head's POOR */
    } else {
        h->state[i] = HS_MISSED;
    }
}

int holds_head(holds_t *h, hu32 lane, hs32 start, hu32 judge, hs32 now)
{
    hu32 i;
    for (i = 0; i < h->n; i++) {
        if (h->defs[i].start > start) break;
        if (h->defs[i].start == start && h->defs[i].lane == lane && h->state[i] == HS_PENDING) {
            start_hold(h, i, judge, now);
            return (int)i;
        }
    }
    return -1;
}

int holds_head_near(holds_t *h, hu32 lane, hs32 now, hs32 window, hu32 judge)
{
    hu32 i;
    hs32 best = -1, gap = window + 1;
    for (i = 0; i < h->n; i++) {
        hs32 d = h->defs[i].start - now;
        if (d > window) break;
        if (d < 0) d = -d;
        if (h->defs[i].lane == lane && h->state[i] == HS_PENDING && d < gap) { gap = d; best = (hs32)i; }
    }
    if (best >= 0) start_hold(h, (hu32)best, judge, now);
    return best;
}

/* Is the lane held? BSS: spinning the head's way, with a short grace for standing still.
 * *reverse: spun back; *still: turntable not moving this tick. */
static int held(holds_t *h, hu32 lane, hu32 kind, hs32 now, hu32 keys, const hs8 tt[2], int *reverse, int *still)
{
    hs8 dir;
    *reverse = 0; *still = 0;
    if (!is_spin(kind)) return (keys >> lane) & 1;
    dir = tt[lane >> 3];
    if (dir == 0) {
        *still = 1;
        if (h->stood[lane] < 0) h->stood[lane] = now;
        return now - h->stood[lane] <= h->cfg.spin_grace;
    }
    h->stood[lane] = -1;
    if (h->spin[lane] == 0) h->spin[lane] = dir;
    if (dir == -h->spin[lane]) { *reverse = 1; return 0; }
    return 1;
}

static void step(holds_t *h, hu32 lane, hs32 now, hu32 keys, const hs8 tt[2])
{
    hs32 i = h->active[lane];
    const hold_def *d;
    hs32 offset, win, now64;
    int down, reverse, still, hell, pending = 0;
    if (i < 0) return;
    d = &h->defs[i];
    offset = now - d->end;
    win = h->cfg.tail_window[J_BAD];
    now64 = now * HOLDS_FP;
    hell = is_hell(d->kind);
    down = held(h, lane, d->kind, now, keys, tt, &reverse, &still);
    /* BSS: inside the tail window, stopping the turntable releases it like a backspin */
    if (is_spin(d->kind) && still && offset >= -win && h->state[i] == HS_HOLDING && h->spin[lane]) down = 0;
    /* keys: a release only counts once the key has stayed up for release_grace ticks; it is timed
     * from the first up tick */
    if (down || h->state[i] != HS_HOLDING) h->up_at[lane] = -1;
    else if (!is_spin(d->kind) && h->cfg.release_grace) {
        if (h->up_at[lane] < 0) h->up_at[lane] = now;
        if (now - h->up_at[lane] < h->cfg.release_grace) { down = 1; pending = 1; }
        else { now = h->up_at[lane]; offset = now - d->end; now64 = now * HOLDS_FP; }
    }

    if (h->state[i] == HS_MISSED) {                 /* CN/BSS whose head was missed: tail is POOR too */
        if (offset >= 0) finish(h, (hu32)i, J_POOR, offset);
        return;
    }
    /* state changes first, so a release or re-grab on a 16th boundary decides that tick */
    if (h->state[i] == HS_LOOSE) {                  /* HCN/HBSS let go: grab again before the end */
        if (offset >= 0) { finish(h, (hu32)i, J_NONE, offset); return; }   /* the end adds no POOR, no gauge loss */
        if (down) {
            h->state[i] = HS_HOLDING;
            h->next_tick[lane] = now64 + tick_of(h, (hu32)i);
        }
    } else if (!down) {                             /* HS_HOLDING, released */
        if (offset >= -win) { finish(h, (hu32)i, h->broken[i] ? J_NONE : tail_judge(h, offset), offset); return; }
        if (!hell) { finish(h, (hu32)i, J_POOR, offset); return; }
        let_go(h, (hu32)i, now, 1);
    } else if (h->cfg.overhold == OVERHOLD_AUTO) {  /* HS_HOLDING, still held */
        if (!pending && offset >= 0) { finish(h, (hu32)i, h->broken[i] ? J_NONE : J_PGREAT, 0); return; }
    } else if (!pending && offset > win) {
        finish(h, (hu32)i, h->broken[i] ? J_NONE : h->cfg.overhold_judge, offset);
        return;
    }
    if (hell) {                                     /* 16th-note ticks between start and end */
        while (h->next_tick[lane] <= now64 && h->next_tick[lane] < d->end * HOLDS_FP) {
            emit(h, h->state[i] == HS_HOLDING ? HE_TICK_GAIN : HE_TICK_LOSS, (hu32)i, J_POOR, offset);
            h->next_tick[lane] += tick_of(h, (hu32)i);
        }
    }
}

void holds_frame(holds_t *h, hs32 now, hu32 keys, const hs8 tt[2])
{
    hu32 lane;
    /* heads the engine never reported (no input at all) are misses */
    while (h->next < h->n && h->defs[h->next].start + h->cfg.head_late < now) {
        if (h->state[h->next] == HS_PENDING) start_hold(h, h->next, J_POOR, now);
        h->next++;
    }
    for (lane = 0; lane < HOLDS_LANES; lane++) step(h, lane, now, keys, tt);
}

hu32 holds_pop(holds_t *h, hold_event *out, hu32 max)
{
    hu32 n = h->nev < max ? h->nev : max, i;
    for (i = 0; i < n; i++) copy(&out[i], &h->ev[i], sizeof *out);
    for (i = n; i < h->nev; i++) copy(&h->ev[i - n], &h->ev[i], sizeof *out);
    h->nev -= n;
    return n;
}

int holds_lane_held(const holds_t *h, hu32 lane)
{
    hs32 i = lane < HOLDS_LANES ? h->active[lane] : -1;
    return i >= 0 && h->state[i] == HS_HOLDING;
}

int holds_is_tail(const holds_t *h, hu32 lane, hs32 time)
{
    hu32 i;
    for (i = 0; i < h->n; i++)
    { if (h->defs[i].lane == lane && h->defs[i].end == time) return 1; LOOP_PAD(); }
    return 0;
}
