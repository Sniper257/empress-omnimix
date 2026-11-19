#ifndef MEGAMIX_HID_REPORT_H
#define MEGAMIX_HID_REPORT_H
/* Portable descriptor-driven HID input core shared by IOP and host tests.
 * No VID/PID allowlist, allocation, USB calls, floats or output reports. */
#include <stddef.h>
#include <stdint.h>
#define MH_FIELDS 128
#define MH_REPORTS 16
#define MH_REPORT_BITS 2048
#define MH_STACK 16
#define MH_USAGES 128
#define MH_BUTTON 1
#define MH_AXIS 2
#define MH_HAT 3
#define MH_OK 0
#define MH_BAD_DESCRIPTOR -1
#define MH_LIMIT -2
#define MH_NOT_GAMEPAD -3
#define MH_BAD_REPORT -4
#define MH_SHORT_REPORT -5
#define MH_RELATIVE 4u
#define MH_WRAP 8u
#define MH_NULL 64u

typedef struct {
  uint16_t bits[3]; /* Input, Output and Feature are independent streams. */
  uint8_t id;
} mh_report;
typedef struct {
  int32_t minimum, maximum, selector;
  uint16_t offset, count, page, usage, flags;
  uint8_t size, report, collection, kind, array;
} mh_field;
typedef struct {
  mh_field fields[MH_FIELDS];
  mh_report reports[MH_REPORTS];
  uint16_t field_count;
  uint8_t report_count, collection_count, report_ids;
} mh_model;
typedef struct {
  int32_t value[MH_FIELDS];
  uint8_t valid[MH_FIELDS];
} mh_values;
int mh_parse(mh_model *model, const uint8_t *descriptor, size_t bytes);
/* Validate the entire report before updating any field. Other report IDs retain
 * their latest values. Disconnect/timeout ownership is the transport's job. */
int mh_decode(const mh_model *model, const uint8_t *report, size_t bytes,
              mh_values *values);
const char *mh_error(int code);
#endif
