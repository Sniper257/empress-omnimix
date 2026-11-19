#ifndef MEGAMIX_HID_BINDINGS_H
#define MEGAMIX_HID_BINDINGS_H
#include "hid_report.h"
#define MK_CONTROLS 14
#define MK_7KEY 0u
#define MK_DUALSHOCK 1u
#define MK_5KEY 2u
#define MK_KEY1 0
#define MK_START 7
#define MK_SELECT 8
#define MK_CCW 9
#define MK_CW 10
#define MK_BUTTON 1
#define MK_LOW 2
#define MK_HIGH 3
#define MK_HAT 4
#define MK_MOTION_NEG 5
#define MK_MOTION_POS 6
/* Profiles are valid ONLY against the exact report descriptor fingerprint.
 * The field index then distinguishes repeated usages/collections/report IDs. */
typedef struct {
  uint16_t field;
  uint8_t kind, reserved;
  int32_t threshold;
} mk_binding;
typedef struct {
  mk_binding control[MK_CONTROLS];
  uint32_t type;
} mk_profile;
static inline unsigned mk_control_mask(unsigned type){return type==MK_DUALSHOCK?0x3fffu:type==MK_5KEY?0x79fu:0x7ffu;}
static inline unsigned mk_control_count(unsigned type){return type==MK_DUALSHOCK?14:type==MK_5KEY?9:11;}
static inline unsigned mk_control_slot(unsigned type,unsigned row){return type==MK_5KEY&&row>=5?row+2:row;}
static inline const char *mk_type_name(unsigned type){return type==MK_DUALSHOCK?"DUALSHOCK":type==MK_5KEY?"5KEY CONTROLLER":"7KEY CONTROLLER";}
static inline const char *mk_control_name(unsigned type,unsigned slot){
 static const char *const k[11]={"KEY 1","KEY 2","KEY 3","KEY 4","KEY 5","KEY 6","KEY 7","START","SELECT","SCRATCH CCW","SCRATCH CW"};
 static const char *const d[14]={"D-PAD UP","D-PAD DOWN","D-PAD LEFT","D-PAD RIGHT","TRIANGLE","CROSS","SQUARE","CIRCLE","L1","L2","R1","R2","START","SELECT"};
 return type==MK_DUALSHOCK?(slot<14?d[slot]:"INVALID"):(slot<11?k[slot]:"INVALID");
}
static inline unsigned mk_capture_target(unsigned type,unsigned slot){return type==MK_DUALSHOCK?(slot==0||slot==1||slot==11?9:0):slot;}
typedef struct {
  int32_t previous[MH_FIELDS], motion[MH_FIELDS];
  uint8_t initialized[MH_FIELDS];
} mk_motion;
void mk_observe(const mh_model *, const mh_values *, unsigned report_index,
                mk_motion *);
uint16_t mk_peek(const mh_model *, const mh_values *, const mk_profile *,
                 const mk_motion *);
uint16_t mk_sample(const mh_model *, const mh_values *, const mk_profile *,
                   mk_motion *);
int mk_validate(const mh_model *, const mk_profile *, uint16_t *missing,
                uint16_t *duplicates);
/* EMPRESS native wire order, before its history/delay/type/menu processing.
 * Includes the physical KOC signature; never emits analog/pressure reports. */
uint16_t mk_native_buttons(unsigned player, uint16_t logical);
void mk_packet(unsigned player, uint16_t logical, uint8_t packet[32]);
uint32_t mh_fingerprint(const uint8_t *, size_t);
#endif
