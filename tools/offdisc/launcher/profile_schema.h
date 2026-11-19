/* Streaming consumer policy for the first canonical sections. Native-format
 * constants belong to these adapters, never the generic container parser. */
#ifndef MEGAMIX_PROFILE_SCHEMA_H
#define MEGAMIX_PROFILE_SCHEMA_H
#include "profile_format.h"
typedef struct {
    mp_u32 type,used,have_key,native_crc,native_expected;
    mp_u64 received;
    mp_u8 record[48],last_key[36];
    mp_u32 utf_remaining,utf_value,utf_minimum;
} __attribute__((aligned(8))) mp_schema_state;
_Static_assert(sizeof(mp_schema_state)==128,"streaming consumer state size");
void mp_schema_begin(mp_schema_state *,mp_visitor *);
int mp_schema_finish(mp_schema_state *);
int mp_check_profile(const mp_io *,mp_u64,mp_info *,mp_work *,mp_schema_state *);
#endif
