#ifndef MMIX_SELECT_CAPTURE_H
#define MMIX_SELECT_CAPTURE_H
#include "protocol.h"
#include "config.h"
#define MSP_EVENTS 2048u
#define MSP_IO_EVENTS 64u
#define MSP_HEADER 256u
#define MSP_METRICS (16u*28u)
#define MSP_CAPTURE_BYTES (MSP_HEADER+MSP_METRICS+(MSP_EVENTS+MSP_IO_EVENTS)*48u)
typedef struct {unsigned kind,lo,hi,a,b,c,d,e,f,g,h,i;} msp_event;
extern msp_config select_profile;
static void sp_tick(void),sp_after_update(void),sp_draw(void),sp_leave(void);
static unsigned sp_span(void);
static void sp_art(unsigned,unsigned,unsigned,unsigned);
static void sp_note(unsigned,unsigned,unsigned,unsigned,unsigned,unsigned,unsigned,unsigned,unsigned,unsigned);
#endif
