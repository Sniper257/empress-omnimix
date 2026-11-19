#ifndef MEGAMIX_HID_NATIVE_H
#define MEGAMIX_HID_NATIVE_H
#include "protocol.h"
#define MU_NATIVE_VERSION 2u
typedef struct {
  uint32_t version, owned[2], gate_address, gate_token;
} mu_native_config;
#endif
