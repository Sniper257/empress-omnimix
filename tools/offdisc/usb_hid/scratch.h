#ifndef MEGAMIX_HID_SCRATCH_H
#define MEGAMIX_HID_SCRATCH_H
#include "bindings.h"
/* A stop timeout for converting an encoder to digital KOC directions, not a
 * key debounce window or a change to EMPRESS's native sample history. The
 * 80-ms candidate comes from a PS2-capable USBemani layout; original KOC
 * firmware timing has not been measured. No gameplay/scene-specific policy. */
#define MK_SCRATCH_HOLD_US 80000u
typedef struct {
  uint64_t moved;
  int32_t previous, travel;
  int8_t held;
  uint8_t initialized;
} mk_scratch_axis;
typedef struct { mk_scratch_axis axis[2]; } mk_scratch;
void mk_scratch_reset(const mh_model *, const mh_values *, const mk_profile *,
                      mk_scratch *);
void mk_scratch_observe(const mh_model *, const mh_values *, unsigned report,
                        const mk_profile *, mk_scratch *, uint64_t now,
                        uint64_t hold);
uint16_t mk_scratch_peek(const mh_model *, const mh_values *, const mk_profile *,
                         const mk_motion *, mk_scratch *, uint64_t now,
                         uint64_t hold);
#endif
