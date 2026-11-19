/* Shared envelope checks for the synchronous reference and resumable engine. */
#ifndef MEGAMIX_PROFILE_WIRE_H
#define MEGAMIX_PROFILE_WIRE_H
#include "profile_format.h"
void mp_wire_encode_entry(mp_u8 *,const mp_section *);
void mp_wire_decode_entry(const mp_u8 *,mp_section *);
int mp_wire_section_shape(const mp_section *);
int mp_wire_read_header(const mp_u8 *,mp_u64,mp_u64,mp_info *);
void mp_wire_make_header(mp_u8 *,const mp_info *,mp_u32);
int mp_wire_check_footer(const mp_u8 *,const mp_u8 *);
void mp_wire_make_footer(mp_u8 *,const mp_u8 *);
#endif
