#include "scratch.h"
#include <limits.h>
#include <string.h>
static const mh_field *axis_field(const mh_model *m, const mk_binding *b) {
  if (b->reserved || b->field >= m->field_count || b->threshold <= 0 ||
      (b->kind != MK_MOTION_NEG && b->kind != MK_MOTION_POS))
    return 0;
  const mh_field *f = m->fields + b->field;
  return f->kind == MH_AXIS ? f : 0;
}
static int value_valid(const mh_field *f, const mh_values *v, unsigned field) {
  return v->valid[field] && v->value[field] >= f->minimum &&
         v->value[field] <= f->maximum;
}
void mk_scratch_reset(const mh_model *m, const mh_values *v, const mk_profile *p,
                      mk_scratch *s) {
  memset(s, 0, sizeof *s);
  if(p->type==MK_DUALSHOCK)return;
  for (unsigned i = 0; i < 2; i++) {
    const mk_binding *b = p->control + MK_CCW + i;
    const mh_field *f = axis_field(m, b);
    if (f && value_valid(f, v, b->field) && !(f->flags & MH_RELATIVE)) {
      s->axis[i].previous = v->value[b->field];
      s->axis[i].initialized = 1;
    }
  }
}
static void expire(mk_scratch_axis *s, uint64_t now, uint64_t hold) {
  /* Also clear sub-threshold travel, so movements separated by a real stop
   * cannot accumulate into a later scratch. Full clock avoids stale revival
   * at the IOP low-word wrap, even while no EE target is installed. */
  if (!hold || now - s->moved >= hold) {
    s->held = 0;
    s->travel = 0;
  }
}
void mk_scratch_observe(const mh_model *m, const mh_values *v, unsigned report,
                        const mk_profile *p, mk_scratch *s, uint64_t now,
                        uint64_t hold) {
  if(p->type==MK_DUALSHOCK)return;
  for (unsigned i = 0; i < 2; i++) {
    const mk_binding *b = p->control + MK_CCW + i;
    const mh_field *f = axis_field(m, b);
    if (!f || f->report != report)
      continue;
    mk_scratch_axis *a = s->axis + i;
    if (!value_valid(f, v, b->field)) {
      memset(a, 0, sizeof *a);
      continue;
    }
    expire(a, now, hold);
    int32_t position = v->value[b->field];
    int64_t delta = 0;
    if (f->flags & MH_RELATIVE)
      delta = position;
    else if (a->initialized) {
      delta = (int64_t)position - a->previous;
      int64_t span = (int64_t)f->maximum - f->minimum + 1;
      if (delta > span / 2)
        delta -= span;
      else if (delta < -span / 2)
        delta += span;
    }
    a->previous = position;
    a->initialized = 1;
    if (!delta || !hold)
      continue; /* identical reports are not movement or a timeout refresh */
    int32_t threshold = b->threshold;
    const mk_binding *other = p->control + MK_CCW + (1u - i);
    if (other->field == b->field && other->kind != b->kind &&
        axis_field(m, other) && other->threshold > threshold)
      threshold = other->threshold;
    if ((delta > 0 && a->travel < 0) || (delta < 0 && a->travel > 0))
      a->travel = 0;
    int64_t travel = (int64_t)a->travel + delta;
    a->travel = travel > INT32_MAX ? INT32_MAX :
                travel < INT32_MIN ? INT32_MIN : (int32_t)travel;
    a->moved = now;
    if (a->travel >= threshold)
      a->held = 1;
    else if (a->travel <= -threshold)
      a->held = -1;
    /* Keep only a bounded directional threshold. A long spin must not make
     * reversal harder, and both directions must use the latest movement,
     * not the net movement accumulated over a whole video frame. */
    if (a->travel >= threshold || a->travel <= -threshold)
      a->travel = 0;
  }
}
uint16_t mk_scratch_peek(const mh_model *m, const mh_values *v,
                         const mk_profile *p, const mk_motion *motion,
                         mk_scratch *s, uint64_t now, uint64_t hold) {
  uint16_t logical = mk_peek(m, v, p, motion);
  if(p->type==MK_DUALSHOCK)return logical;
  for (unsigned i = 0; i < 2; i++) {
    const mk_binding *b = p->control + MK_CCW + i;
    const mh_field *f = axis_field(m, b);
    if (!f)
      continue; /* digital scratch buttons and static axis bindings unchanged */
    unsigned bit = 1u << (MK_CCW + i);
    logical &= ~bit;
    mk_scratch_axis *a = s->axis + i;
    expire(a, now, hold);
    if (value_valid(f, v, b->field) &&
        a->held == (b->kind == MK_MOTION_NEG ? -1 : 1))
      logical |= bit;
  }
  return logical;
}
