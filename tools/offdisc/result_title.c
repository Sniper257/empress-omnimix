/* One immutable wheel/title sprite retained across PLAY -> RESULT container
 * unloads. Filled at completed workspace exit, before RESULT queues sound-bank
 * reads. Bank LBA+offset are the key: aliases/strip addresses are not identities.
 * Fixed reserved-block storage, no native heap or PLAY read-policy changes. */
#define RESULT_TITLE_CAP (12 + SPR_W * 24)
/* The select read window and retained result title have exclusive ownership
 * of spr_buf. Preparing a title invalidates the window; any window refill or
 * RANDOM unpack invalidates retention before overwriting it. */
static struct {u32 bank,off,bytes;u8 *data;} result_title={0,0,0,spr_buf};
static void spr_forget_result(void){result_title.bytes=0;}
u32 result_title_stats[4]; /* prepare calls, RAM hits, cold/invalid prepares, read failures */
static int result_title_has(u32 bank,u32 off)
{
    return result_title.bytes>=12 && result_title.bank==bank && result_title.off==off;
}
static __attribute__((noinline)) int spr_prepare_result(s32 id)
{
    if(ui_active)return 0;
    s32 row=song_catalog_row(id);u32 off,w,h,n;
    result_title_stats[0]++;
    if(row<FIRST || (u32)(row-FIRST)>=sdb.n_new || !sdb.spr_off ||
       !sdb.spr_lba || !sdb.spr[0].n || !sdb.spr[0].rows || sdb.spr[0].rows>24 || (off=sdb.spr_off[(row-FIRST)*NSPR])==~0u) {
        result_title.bytes=0;return 0;
    }
    if(result_title_has(sdb.spr_lba,off))return 1;
    result_title.bytes=0;spr_read_bytes=0;result_title_stats[2]++;
    if(vd_pread(sdb.spr_lba,off,12,result_title.data)!=12 ||
       *(u32*)result_title.data!=0x31525053u)goto bad;
    w=*(u16*)(result_title.data+4);h=*(u16*)(result_title.data+6);
    if(!w || !h || w>SPR_W || h>48)goto bad;
    if(h>24)h=24;n=w*h;
    if(vd_pread(sdb.spr_lba,off+12,n,result_title.data+12)!=(s32)n)goto bad;
    result_title.bank=sdb.spr_lba;result_title.off=off;
    result_title.bytes=12+n;return 1;
bad:result_title_stats[3]++;return 0;
}
