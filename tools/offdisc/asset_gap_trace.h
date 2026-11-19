#ifndef MEGAMIX_ASSET_GAP_TRACE_H
#define MEGAMIX_ASSET_GAP_TRACE_H
#define ASSET_GAP_CAP 32u
typedef struct {
    volatile u32 ready;
    u32 status,count,total,largest,frames;
    u32 gap[ASSET_GAP_CAP][2];
} asset_gap_snapshot;
/* BEFORE frame reservation, failed FRONT plan, failed BACK plan. Each worker
 * publication is immutable until the next chart's native completed barrier. */
static asset_gap_snapshot asset_gaps[3];
#endif
