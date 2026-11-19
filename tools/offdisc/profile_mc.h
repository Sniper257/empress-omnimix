/* Native libmc adapter for the resumable compact-profile transaction.
 * Uses the existing, audited retail cp_ops bridge; owns no heap or modules.
 * Caller exclusively owns the MC RPC interface until the transaction ends. */
#ifndef MEGAMIX_PROFILE_MC_H
#define MEGAMIX_PROFILE_MC_H
#include "profile_transaction.h"
#include "save_checkpoint.h"
typedef struct {
    const cp_ops *ops;
    void *output;
    int port,active,operation,poisoned;
    mp_u8 directory[64] __attribute__((aligned(64)));
    cp_card_info card __attribute__((aligned(64)));
} mp_mc;
/* Ports0/1, slot0: these are the capabilities of the retail bridge, not a
 * format limit. No mkdir, format, GameID switch or native save is performed.
 * A transport failure poisons this adapter; drain/reset libmc before re-init. */
int mp_mc_init(mp_mc *,mp_storage *,const cp_ops *,int port);
#endif
