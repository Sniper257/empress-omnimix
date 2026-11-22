/* Hold-note rules (CN, HCN, BSS, HBSS). See holds.h. No libc, no engine references. */
#include "holds.h"

/* byte copy: the runtime has no libc, and struct assignment may compile to memcpy */
static void copy(void *dst, const void *src, hu32 n)
{
    volatile hu8 *d = (volatile hu8 *)dst;
    const hu8 *s = (const hu8 *)src;
    while (n--) *d++ = *s++;
}

static void emit(holds_t *h, hu32 kind, hu32 index, hu32 judge, hs32 offset)
{
    hold_event *e;
    if (h->nev >= HOLDS_EVENTS) { h->dropped++; return; }
    e = &h->ev[h->nev++];
    e->kind = (hu8)kind; e->judge = (hu8)judge; e->lane = h->defs[index].lane; e->pad = 0;
    e->offset = offset; e->index = index;
}

static hu32 judge_of(const holds_t *h, hs32 offset)
{
    hs32 a = offset < 0 ? -offset : offset;
    hu32 j;
    for (j = 0; j < 4; j++) if (a <= h->cfg.tail_window[j]) return j;
    return J_POOR;
}

static int is_hell(hu32 kind) { return kind == HOLD_HCN || kind == HOLD_HBSS; }
static int is_spin(hu32 kind) { return kind == HOLD_BSS || kind == HOLD_HBSS; }

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
    }
    h->defs = defs; h->n = n; h->next = 0; h->nev = 0; h->dropped = 0;
    copy(&h->cfg, cfg, sizeof h->cfg);
    if (h->cfg.tick <= 0) h->cfg.tick = 1;
    for (i = 0; i < HOLDS_LANES; i++) { h->active[i] = -1; h->spin[i] = 0; h->stood[i] = -1; h->next_tick[i] = 0; }
    return 0;
}

static void start_hold(holds_t *h, hu32 i, hu32 judge)
{
    const hold_def *d = &h->defs[i];
    hs32 old = h->active[d->lane];
    if (old >= 0 && old != (hs32)i) finish(h, (hu32)old, J_POOR, 0);   /* overlapping holds: close the old one */
    h->active[d->lane] = (hs32)i;
    h->spin[d->lane] = 0;
    h->stood[d->lane] = -1;
    h->next_tick[d->lane] = d->start + h->cfg.tick;
    if (judge <= J_GOOD) h->state[i] = HS_HOLDING;
    else h->state[i] = is_hell(d->kind) ? HS_LOOSE : HS_MISSED;
}

int holds_head(holds_t *h, hu32 lane, hs32 start, hu32 judge)
{
    hu32 i;
    for (i = 0; i < h->n; i++) {
        if (h->defs[i].start > start) break;
        if (h->defs[i].start == start && h->defs[i].lane == lane && h->state[i] == HS_PENDING) {
            start_hold(h, i, judge);
            return (int)i;
        }
    }
    return -1;
}

/* Is the hold on this lane being held this frame? BSS: spinning the head's way (with a short grace). */
static int held(holds_t *h, hu32 lane, hu32 kind, hs32 now, hu32 keys, const hs8 tt[2], int *reverse)
{
    hs8 dir;
    *reverse = 0;
    if (!is_spin(kind)) return (keys >> lane) & 1;
    dir = tt[lane >> 3];
    if (h->spin[lane] == 0) {
        if (dir == 0) {
            if (h->stood[lane] < 0) h->stood[lane] = now;
            return now - h->stood[lane] <= h->cfg.spin_grace;
        }
        h->spin[lane] = dir;
    }
    if (dir == -h->spin[lane]) { *reverse = 1; h->stood[lane] = -1; return 0; }
    if (dir == 0) {
        if (h->stood[lane] < 0) h->stood[lane] = now;
        return now - h->stood[lane] <= h->cfg.spin_grace;
    }
    h->stood[lane] = -1;
    return 1;
}

static void step(holds_t *h, hu32 lane, hs32 now, hu32 keys, const hs8 tt[2])
{
    hs32 i = h->active[lane];
    const hold_def *d;
    hs32 offset, bad;
    int down, reverse;
    if (i < 0) return;
    d = &h->defs[i];
    offset = now - d->end;
    bad = h->cfg.tail_window[J_BAD];
    down = held(h, lane, d->kind, now, keys, tt, &reverse);

    if (h->state[i] == HS_MISSED) {                 /* CN/BSS whose head was missed: tail is POOR too */
        if (offset >= 0) finish(h, (hu32)i, J_POOR, offset);
        return;
    }
    if (is_hell(d->kind)) {                         /* body ticks, from start to end */
        while (h->next_tick[lane] <= now && h->next_tick[lane] < d->end) {
            emit(h, h->state[i] == HS_HOLDING ? HE_TICK_GAIN : HE_TICK_LOSS, (hu32)i, 0, 0);
            h->next_tick[lane] += h->cfg.tick;
        }
    }
    if (h->state[i] == HS_LOOSE) {                  /* HCN/HBSS let go: grab again before the end */
        if (down && offset < 0) {
            h->state[i] = HS_HOLDING;
            if (is_spin(d->kind)) h->stood[lane] = -1;
        } else if (offset > bad) finish(h, (hu32)i, J_POOR, offset);
        return;
    }
    /* HS_HOLDING */
    if (!down) {
        if (offset < -bad) {
            if (is_hell(d->kind)) {               /* let go early: body keeps scoring as LOSS until grabbed again */
                h->state[i] = HS_LOOSE;
                emit(h, HE_BREAK, (hu32)i, 0, offset);
            } else finish(h, (hu32)i, J_POOR, offset);
        } else finish(h, (hu32)i, judge_of(h, offset), offset);
        return;
    }
    if (h->cfg.overhold == OVERHOLD_AUTO) {
        if (offset >= 0) finish(h, (hu32)i, J_PGREAT, 0);
    } else if (offset > bad) finish(h, (hu32)i, J_POOR, offset);
}

void holds_frame(holds_t *h, hs32 now, hu32 keys, const hs8 tt[2])
{
    hu32 lane;
    /* heads the engine never reported (no input at all) are misses */
    while (h->next < h->n && h->defs[h->next].start + h->cfg.head_late < now) {
        if (h->state[h->next] == HS_PENDING) start_hold(h, h->next, J_POOR);
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
        if (h->defs[i].lane == lane && h->defs[i].end == time) return 1;
    return 0;
}
