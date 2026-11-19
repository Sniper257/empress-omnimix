#ifndef MEGAMIX_IO_PROBE_H
#define MEGAMIX_IO_PROBE_H
/* Fixed little-endian trailer. Raw EE Count ticks, no clock-rate assumption.
 * Ring = last 64 completed requests; slow = 16 longest completed requests.
 * It is a bounded sample, not a complete file-access trace. */
typedef struct {
    /* kind: low 2 bits are request kind; high 30 = signed chart frame. */
    unsigned sequence,tick,ticks,lba,requested,delivered,kind,error;
} io_probe_event;
typedef struct {
    unsigned h[40];
    io_probe_event ring[64],slow[16];
} io_probe_snapshot;
enum {
    IO_SCHEMA,IO_BYTES,IO_EVENT_BYTES,IO_RING_CAP,IO_RING_COUNT,IO_RING_NEXT,IO_SLOW_CAP,IO_SLOW_COUNT,
    IO_REQUESTS,IO_SECTOR_READS,IO_PREADS,IO_WANT_LO,IO_WANT_HI,IO_GOT_LO,IO_GOT_HI,
    IO_TICKS_LO,IO_TICKS_HI,IO_MAX_TICKS,IO_HITS,IO_MISSES,IO_EVICTIONS,IO_OPEN_FAIL,
    IO_SEEK_FAIL,IO_SHORT_READ,IO_READ_FAIL,IO_EOF,IO_READ_CALLS,IO_CLOSE_FAIL,IO_ERRORS,
    IO_BEGIN,IO_END,IO_RESERVED,IO_HIST
};
_Static_assert(sizeof(io_probe_event)==32,"IO event wire layout");
_Static_assert(sizeof(io_probe_snapshot)==2720,"IO snapshot wire layout");
#endif
