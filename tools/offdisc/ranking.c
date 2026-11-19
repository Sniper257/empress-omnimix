/* EMPRESS ROMRS has99songs x2styles x3difficulties,30sectors per table.
 * Imported byte aliases are not ROMRS indexes. Do not queue their out-of-range
 * reads or feed unrelated file bytes to the native backward ranking scan.
 * Unavailable output matches original22ccc0 with a zero-entry table. Native
 * score/lamp processing and valid native ranking calculations stay unchanged. */
#ifndef RANKING_TEST
#define rk_key (*(volatile s32 *)0x2ad69c)
#define rk_pending (*(volatile u8 *)0x2ad698)
#define rk_count (*(volatile u32 *)0x7b50c0)
extern void ranking_load_body(void);
extern unsigned long long ranking_evaluate_body(s32 *,unsigned long long);
#endif
#include "song_resources.h"
#define RANKING_TABLES (E16_RESOURCE_SONGS*E16_RESOURCE_STYLES*E16_RANKING_DIFFS)
#define RANKING_MAX_RECORDS ((E16_RANKING_SECTORS*2048u-12u)/4u)
static s32 ranking_key_valid(void){return (u32)rk_key<RANKING_TABLES;}
void ranking_load(void)
{
    if(!ranking_key_valid()) {
        rk_count=0;rk_pending=0;
        return;
    }
    ranking_load_body();
}
unsigned long long ranking_evaluate(s32 *out,unsigned long long score)
{
    if(!ranking_key_valid() || rk_count>RANKING_MAX_RECORDS) {
        out[0]=-1;out[1]=-1;out[2]=0;return 0;
    }
    return ranking_evaluate_body(out,score);
}
