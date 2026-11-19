#include "bindings.h"
#include <limits.h>
#ifndef MK_FREESTANDING
#include <string.h>
#endif
#ifndef MK_VALIDATE_ONLY
uint32_t mh_fingerprint(const uint8_t *p, size_t n) {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < n; i++)
    h = (h ^ p[i]) * 16777619u;
  return h;
}
void mk_observe(const mh_model *m, const mh_values *v, unsigned report,
                mk_motion *s) {
  for (unsigned i = 0; i < m->field_count; i++) {
    const mh_field *f = m->fields + i;
    if (f->kind != MH_AXIS || f->report != report || !v->valid[i])
      continue;
    int64_t delta = 0;
    if (f->flags & MH_RELATIVE)
      delta = v->value[i];
    else if (s->initialized[i]) {
      delta = (int64_t)v->value[i] - s->previous[i];
      if (!(f->flags & MH_RELATIVE)) {
        int64_t span = (int64_t)f->maximum - f->minimum + 1;
        if (delta > span / 2)
          delta -= span;
        else if (delta < -span / 2)
          delta += span;
      }
    }
    s->previous[i] = v->value[i];
    s->initialized[i] = 1;
    int64_t sum = (int64_t)s->motion[i] + delta;
    s->motion[i] = sum > INT32_MAX   ? INT32_MAX
                   : sum < INT32_MIN ? INT32_MIN
                                     : (int32_t)sum;
  }
}
#endif
static int valid_binding(const mh_model *m, const mk_binding *b) {
  if (!b->kind)
    return 0;
  if (b->reserved || b->field >= m->field_count)
    return -1;
  const mh_field *f = m->fields + b->field;
  switch (b->kind) {
  case MK_BUTTON:
    return f->kind == MH_BUTTON ? 1 : -1;
  case MK_LOW:
  case MK_HIGH:
    return f->kind == MH_AXIS && !(f->flags & MH_RELATIVE) &&
                   b->threshold >= f->minimum && b->threshold <= f->maximum
               ? 1
               : -1;
  case MK_HAT:
    return f->kind == MH_HAT && b->threshold >= f->minimum &&
                   b->threshold <= f->maximum
               ? 1
               : -1;
  case MK_MOTION_NEG:
  case MK_MOTION_POS:
#ifdef MK_FREESTANDING
    LOOP_PAD();
#endif
    return f->kind == MH_AXIS && b->threshold > 0 ? 1 : -1;
  default:
    return -1;
  }
}
int mk_validate(const mh_model *m, const mk_profile *p, uint16_t *missing,
                uint16_t *duplicates) {
  *missing = *duplicates = 0;
  if(p->type>MK_5KEY)return -1;
  for (unsigned i = 0; i < MK_CONTROLS; i++) {
    const mk_binding *b = p->control + i;
    int valid = valid_binding(m, b);
    if (valid < 0)
      return -1;
    /* Keep dormant assignments when changing family. Sampling already masks
     * them out; only active controls contribute missing/duplicate warnings. */
    if(!(mk_control_mask(p->type)&(1u<<i)))continue;
    if (!valid)
      *missing |= 1u << i;
    for (unsigned j = 0; j < i; j++) {
      if(!(mk_control_mask(p->type)&(1u<<j)))continue;
      const mk_binding *a = p->control + j;
      /* Two thresholds in the SAME direction overlap too. Opposite
       * directions and individual hat positions are distinct inputs. */
      if (b->kind && a->field == b->field && a->kind == b->kind &&
          (b->kind != MK_HAT || a->threshold == b->threshold))
        *duplicates |= (1u << i) | (1u << j);
      if (a->field == b->field && ((a->kind == MK_LOW && b->kind == MK_HIGH &&
                                    a->threshold >= b->threshold) ||
                                   (a->kind == MK_HIGH && b->kind == MK_LOW &&
                                    b->threshold >= a->threshold)))
        *duplicates |= (1u << i) | (1u << j);
    }
  }
  return 0;
}
#ifndef MK_VALIDATE_ONLY
uint16_t mk_peek(const mh_model *m, const mh_values *v, const mk_profile *p,
                 const mk_motion *s) {
  uint16_t result = 0;
  for (unsigned i = 0; i < MK_CONTROLS; i++) {
    const mk_binding *b = p->control + i;
    if(!(mk_control_mask(p->type)&(1u<<i)))continue;
    if (valid_binding(m, b) != 1 || !v->valid[b->field])
      continue;
    int32_t value = v->value[b->field];
    const mh_field *f = m->fields + b->field;
    if (f->kind != MH_BUTTON && (value < f->minimum || value > f->maximum))
      continue;
    int active = 0;
    switch (b->kind) {
    case MK_BUTTON:
      active = value != 0;
      break;
    case MK_LOW:
      active = value <= b->threshold;
      break;
    case MK_HIGH:
      active = value >= b->threshold;
      break;
    case MK_HAT:
      active = value == b->threshold;
      break;
    case MK_MOTION_NEG:
      active = s->motion[b->field] <= -b->threshold;
      break;
    case MK_MOTION_POS:
      active = s->motion[b->field] >= b->threshold;
      break;
    }
    if (active)
      result |= 1u << i;
  }
  return result;
}
uint16_t mk_native_buttons(unsigned player, uint16_t logical) {
  /* Inverse of retail FUN_0013afd0 + tables 0x23c260/0x23c290.
   * Native CONCAT11(report[2],report[3]) order, NOT libpad's btns endian. */
  static const uint16_t key[2][MK_CONTROLS] = {
      {0x0008, 0x0040, 0x0004, 0x0080, 0x0002, 0x1000, 0x0001, 0x0800, 0x0100,
       0x0010, 0x0020},
      {0x0010, 0x0020, 0x0008, 0x0040, 0x0004, 0x0080, 0x0002, 0x0800, 0x0100,
       0x1000, 0x0001}};
  uint16_t bits = 0xe000;
  for (unsigned i = 0; i < 11; i++)
    if (logical & (1u << i))
      bits |= key[player & 1][i];
  return bits;
}
void mk_packet(unsigned player, uint16_t logical, uint8_t packet[32]) {
  memset(packet, 0, 32);
  packet[1] = 0x41;
  uint16_t wire = ~mk_native_buttons(player, logical);
  packet[2] = wire >> 8;
  packet[3] = wire;
}

uint16_t mk_sample(const mh_model *m, const mh_values *v, const mk_profile *p,
                   mk_motion *s) {
  uint16_t result = mk_peek(m, v, p, s);
  memset(s->motion, 0, sizeof s->motion);
  return result;
}

#endif /* MK_VALIDATE_ONLY */
