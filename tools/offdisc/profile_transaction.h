/* Asynchronous A/B coordinator, shared by the native MC and file adapters.
 * Paths and frozen source/staging callbacks remain alive until terminal step.
 * No directory creation, native export, selector or implicit format downgrade. */
#ifndef MEGAMIX_PROFILE_TRANSACTION_H
#define MEGAMIX_PROFILE_TRANSACTION_H
#include "profile_async.h"
enum {MP_TSTAT,MP_TOPEN,MP_TREAD,MP_TWRITE,MP_TCLOSE,MP_TSEEK,MP_TSPACE};
enum {MP_CONFLICT=-6,MP_MISSING=-7,MP_SPACE=-8};
enum {MP_TX_SAVE=1,MP_TX_CREATE=2,MP_TX_SKIP_SAME=4,MP_TX_PROBE=8};
typedef struct {mp_u64 bytes;mp_u32 exists,reserved;} mp_file_stat;
typedef struct {mp_u64 free_bytes;mp_u32 block_bytes,writable;} mp_file_space;
typedef struct {
    void *context;
    /* start:0 accepted,1 busy,<0 rejection; poll:0 pending,1 terminal,<0
     * terminal transport failure. Exactly one owner/operation at a time.
     * STAT/SPACE/CLOSE: result0. OPEN: nonnegative fd. SEEK: exact offset.
     * READ/WRITE: exact count. STAT sets exists=0 for a missing file.
     * CLOSE must finish the backend's acknowledged flush; final readback is
     * still required. Adapters reject unsupported durability/capacity modes. */
    int (*start)(void *,int,const char *,int,void *,mp_u32,mp_u64);
    int (*poll)(void *,int *);
    mp_u64 limit,reserve_bytes;
} mp_storage;
typedef struct mp_transaction mp_transaction;
mp_u32 mp_tx_bytes(void); /* Allocate with64-byte alignment; no internal heap. */
int mp_tx_begin(mp_transaction *,const mp_storage *,const char *const paths[2],
                const mp_u8 expected_uuid[16],int flags,const mp_info *,
                const mp_source *,const mp_visitor *,mp_work *);
enum {MP_TX_PUMP_STEPS=64,MP_TX_PUMP_TICKS=294912,MP_TX_READ_WINDOW=4096};
/*0 busy,1 terminal. At most64 small/page steps per call and ~1ms EE Count
 * budget, checked between steps. Return immediately on transport busy/pending;
 * retained buffers remain exclusively owned until terminal completion. */
int mp_tx_step(mp_transaction *);
int mp_tx_error(const mp_transaction *);
int mp_tx_recovered(const mp_transaction *);
int mp_tx_skipped(const mp_transaction *);
/* Both paths were explicitly absent. Damage is never reported as absence. */
int mp_tx_absent(const mp_transaction *);
mp_u64 mp_tx_required_bytes(const mp_transaction *);
const mp_info *mp_tx_info(const mp_transaction *);
#endif
