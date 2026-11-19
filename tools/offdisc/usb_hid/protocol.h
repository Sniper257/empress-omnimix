#ifndef MEGAMIX_HID_PROTOCOL_H
#define MEGAMIX_HID_PROTOCOL_H
#include "bindings.h"
#define MU_RPC_ID 0x4d474831u
#define MU_VERSION 2u
#define MU_RPC_BYTES(type) ((sizeof(type) + 63u) & ~63u)
#define MU_DEVICES 4
#define MU_LIST 1
#define MU_MODEL 2
#define MU_VALUES 3
#define MU_CONFIGURE 4
#define MU_TARGET 5
#define MU_DISABLE 6
#define MU_DIAGNOSTICS 7
#define MU_GAME_TARGET 8
#define MU_BOOT_DIAG 9
#define MU_LIVE_INFO 10
#define MU_LIVE_CONFIGURE 11
/* Explicit replacement of one already-enabled player; CAS revision, fresh
 * connected target, disconnected previous identity, and no peer ownership. */
#define MU_LIVE_REPLACE 12
#define MU_LIVE_VERSION 1u
#define MU_LIVE_FRAME 0x3142494cu /* LIB1, optional reserved-word extension */
#define MU_PROBE 1
#define MU_CONFIGURING 2
#define MU_DESCRIPTOR 3
#define MU_ACCEPTED 4
#define MU_REJECTED 5
#define MU_CONFIG_ERROR 6
#define MU_DESCRIPTOR_ERROR 7
#define MU_NO_INTERFACE 8
typedef struct {
  uint32_t probes, connects, accepted, stage, vendor, product, interface;
  int32_t result;
  uint32_t host_control, host_frame, root_ports[2];
} mu_diagnostics;
/* Identity includes physical USB topology, so two identical devices do not
 * silently swap players. Rebind if moving a device to a different USB port. */
typedef struct {
  uint32_t descriptor_hash, descriptor_size;
  uint16_t vendor, product;
  uint8_t interface, path[8], reserved[3];
} mu_identity;
typedef struct {
  mu_identity identity;
  uint32_t generation, reports, errors;
  int32_t connected;
} mu_info;
typedef struct {
  uint32_t version, player, slot, generation, ee_target;
  mu_identity identity;
  mk_profile profile;
} mu_request;
/* Version2 adds typed profiles; version1 saved files migrate on read.
 * ee_target means expected revision ONLY for live apply;
 * it never changes the game target or arm gate. Slots are generation checked. */
typedef struct {
  uint32_t version, enabled, revision, slot, generation;
  mu_identity identity;
  mk_profile profile;
} mu_live_info;
typedef struct {
  int32_t status;
  uint32_t version;
  union {
    mu_info devices[MU_DEVICES];
    mu_diagnostics diagnostics;
    struct { uint32_t address, token; } game_gate;
    mu_live_info live;
    mh_model model;
    mh_values values;
  } data;
} mu_reply;
/* Two independent 128-byte DMA slots. Head and tail prevent torn reads; EE
 * accesses uncached and selects newest complete snapshot at its native phase.
 * IOP never reuses a source buffer until its prior DMA has completed. */
typedef struct {
  uint32_t head, version, connected[2], logical[2], reserved[25], tail;
} mu_frame;
/* Named volatile loads avoid aliasing assumptions and copy only live fields.
 * DMA writes head first and tail last. Re-read head to reject a wrap into a
 * newer transfer while the reader was sampling the remaining words. */
static inline int mu_snapshot(mu_frame *out, volatile const mu_frame *in) {
  out->head = in->head;
  out->version = in->version;
  out->connected[0] = in->connected[0];
  out->connected[1] = in->connected[1];
  out->logical[0] = in->logical[0];
  out->logical[1] = in->logical[1];
  out->reserved[3] = in->reserved[3];
  out->reserved[4] = in->reserved[4];
  out->tail = in->tail;
  return out->head && out->head == out->tail && out->head == in->head &&
         out->version == MU_VERSION;
}
#endif
