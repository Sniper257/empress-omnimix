/* Native FREE retry lifecycle. Cached input snapshots only; no polls, heap,
 * file I/O, RNG rollback or changes to judgement/key history. */
#ifndef QUICK_RETRY_TEST
#define qr_kind() (((u32 (*)(void))0x1f5830)())
#define qr_style() (((u32 (*)(void))0x1d9db0)())
#define qr_diff() (((u32 (*)(void))0x205de0)())
#define qr_raw(s) (*(volatile u16 *)(unsigned long)(0x724c8a+(s)*0x1c0))
#define qr_failed() (((u32 (*)(void))0x203d20)())
#define qr_aborted() (((u32 (*)(void))0x203d40)())
#define qr_phase() fs_word(0x2ad534)
#define qr_play_native ((unsigned long long (*)(FS_ARGS))0x1f2170)
#define qr_decision_native ((unsigned long long (*)(FS_ARGS))0x1f4f20)
#define qr_choice_native ((unsigned long long (*)(FS_ARGS))0x21f4c0)
#define qr_continue_native ((unsigned long long (*)(FS_ARGS))0x1f57a0)
#define qr_song() (SONG_OVERRIDE>=0?SONG_OVERRIDE:song_now())
#define qr_base_song() song_now()
static void qr_source_pin(u32 row,u32 base)
{
    if(row==base)SONG_OVERRIDE=-1;
    else if(row<FIRST)SONG_OVERRIDE=(s32)row;
    else {sdb.slot_of[VARIANT]=row-FIRST;SONG_OVERRIDE=VARIANT;}
}
#endif
#define QR_START 0x800u
#define QR_SELECT 0x100u
#define QR_BUTTONS (QR_START|QR_SELECT)
enum {QR_NONE,QR_NEW,QR_SAME,QR_CONTINUE};
typedef struct {
    u32 row,base_row,style,diff,mask,dp;
    u32 flags[2],random[2],mirror[2],assist[2],mode[2],pattern[2];
    u8 chart[16];
} qr_key;
static struct {
    qr_key key;
    u8 map[2][8];
    u32 valid,pending,window,armed,held[2],queued;
} qr;

static u32 qr_mask(void){return fs_dp()?3u:gsm_mask()&3u;}
static u32 qr_capture_key(qr_key *key)
{
    s32 song=qr_song(),row=song_catalog_row(song);
    const u8 *id=chart_identity(song,fs_dp()!=0,qr_diff());
    if(qr_kind()!=4 || row<0 || !id)return 0;
    s32 base=song_catalog_row(qr_base_song());if(base<0)return 0;
    key->row=(u32)row;key->base_row=base;key->style=qr_style();key->diff=qr_diff();
    key->mask=qr_mask();key->dp=fs_dp()!=0;
    if(!key->mask)return 0;
    for(u32 i=0;i<16;i++){key->chart[i]=id[i];LOOP_PAD();}
    for(u32 s=0;s<2;s++){
        key->flags[s]=lq_flags(s);key->random[s]=lq_native(s)[1];
        key->mirror[s]=lq_native(s)[2];key->assist[s]=lq_native(s)[5];
        key->mode[s]=lq.mode[s];
        key->pattern[s]=lq.mode[s]>=LQ_EXACT && lq.mode[s]<=LQ_COLOUR?lq.pattern[s]:0;
        LOOP_PAD();
    }
    return 1;
}
static u32 qr_key_equal(const qr_key *a,const qr_key *b)
{
    const u8 *x=(const u8 *)a,*y=(const u8 *)b;
    for(u32 i=0;i<sizeof *a;i++){if(x[i]!=y[i])return 0;LOOP_PAD();}
    return 1;
}
static u32 qr_map_valid(const u8 *map)
{
    u32 mask=0;
    for(u32 i=0;i<8;i++){if(map[i]>7 || (mask&(1u<<map[i])))return 0;mask|=1u<<map[i];LOOP_PAD();}
    return mask==255;
}
static u32 qr_variant_restore(s32 song)
{
    if(qr.pending!=QR_SAME || !qr.valid || qr_kind()!=4 ||
       song_catalog_row(song)!=(s32)qr.key.base_row ||
       qr_style()!=qr.key.style || qr_diff()!=qr.key.diff)return 0;
    qr_source_pin(qr.key.row,qr.key.base_row);return 1;
}
static void qr_select_clear(void)
{
    qr.valid=qr.pending=qr.window=qr.armed=qr.queued=0;
    qr.held[0]=qr.held[1]=0;
}
/* Runs AFTER original map generation plus our trainer/R-RAN overrides, BEFORE
 * 1e5e80 returns to note construction and keysound routing. The row is a
 * source row in this immutable installation, never an eight-bit alias. */
static void qr_map_ready(void)
{
    qr_key key;
    u32 pending=qr.pending;qr.pending=qr.window=qr.armed=qr.queued=0;
    qr.held[0]=qr.held[1]=0;
    if(!qr_capture_key(&key)){qr.valid=0;return;}
    u32 same=pending==QR_SAME && qr.valid && qr_key_equal(&key,&qr.key);
    for(u32 s=0;s<2;s++)if((key.mask&(1u<<s)) &&
        !qr_map_valid(same?qr.map[s]:lq_maps(s))){qr.valid=0;return;}
    for(u32 s=0;s<2;s++){
        if(!(key.mask&(1u<<s))){LOOP_PAD();continue;}
        for(u32 i=0;i<8;i++){
            if(same)lq_maps(s)[i]=qr.map[s][i];else qr.map[s][i]=lq_maps(s)[i];
            lq.realised[s][lq_maps(s)[i]]=i;
            LOOP_PAD();
        }
    }
    qr.key=key;qr.valid=1;
}
static void qr_retry_input(void)
{
    u32 window=qr_kind()==4 && qr_phase()==5 && (qr_failed() || qr_aborted());
    if(!window){qr.window=qr.armed=qr.queued=0;return;}
    u32 mask=qr_mask();
    if(!qr.window){qr.window=1;qr.armed=qr.queued=0;qr.held[0]=qr.held[1]=0;}
    for(u32 s=0;s<2;s++){
        u32 bit=1u<<s,held=(mask&bit)?qr_raw(s)&QR_BUTTONS:0;
        u32 edge=held&~qr.held[s];qr.held[s]=held;
        if(!(mask&bit)){LOOP_PAD();continue;}
        if(!held)qr.armed|=bit;
        if(!qr.queued && (qr.armed&bit) && menu_control.options.quick_retry[fs_dp()?0:s] &&
           (held==QR_START || held==QR_SELECT) && edge==held)
            qr.queued=held==QR_SELECT?QR_SAME:QR_NEW;
        /* Chord/repeated key holds never arm or create a retry request. */
        if(held)qr.armed&=~bit;
        LOOP_PAD();
    }
}
unsigned long long qr_play_call(FS_ARGS)
{
    qr_retry_input();
    unsigned long long result=qr_play_native(FS_PASS);
    if(result || qr_kind()!=4){qr.window=qr.armed=qr.queued=0;}
    return result;
}
/* Called only after native failure/abort stop/fade has completed. Returning
 * true selects its phase6 drain/reset/rewind path, never a direct READY jump.
 * Unqueued/native retry controls retain the original decision function. */
unsigned long long qr_decision_call(FS_ARGS)
{
    if(qr_kind()==4 && qr.queued){qr.pending=qr.queued;qr.queued=qr.window=qr.armed=0;return 1;}
    unsigned long long result=qr_decision_native(FS_PASS);
    if(result && qr_kind()==4)qr.pending=QR_NEW;
    return result;
}
static u32 qr_select_held(void)
{
    u32 mask=qr_mask();
    for(u32 s=0;s<2;s++){
        /* START may confirm Continue while SELECT requests the same map.
         * Exclusivity is only for abort/failure taps, not a RESULT hold. */
        if((mask&(1u<<s)) && (qr_raw(s)&QR_SELECT))return 1;
        LOOP_PAD();
    }
    return 0;
}
/* Retail RESULT panel returns scene0x21=CONTINUE,13=music select,0c=menu,
 * 7fffffff=exit. Only this confirmed destination opens the transition window;
 * earlier gauge-view SELECT presses must not retain another attempt. */
unsigned long long qr_choice_call(FS_ARGS)
{
    unsigned long long choice=qr_choice_native(FS_PASS);
    qr.pending=QR_NONE;
    if(qr_kind()==4 && choice==0x21 && qr.valid)
        qr.pending=qr_select_held()?QR_SAME:QR_CONTINUE;
    return choice;
}
/* Continue initializer1a1730 restores the native options before this call to
 * 1f57a0. Sample again BEFORE it enters1f5df0's source/NO CRIME selection and
 * before the later map reset. Accept SELECT grabbed during the result fade.
 * This is a single transition check, not another input poll or PLAY action. */
unsigned long long qr_continue_call(FS_ARGS)
{
    if(qr.pending==QR_CONTINUE || qr.pending==QR_SAME){
        if(qr_kind()!=4 || !qr.valid ||
           song_catalog_row(qr_base_song())!=(s32)qr.key.base_row ||
           qr_style()!=qr.key.style || qr_diff()!=qr.key.diff)qr.pending=QR_NONE;
        else if(qr.pending==QR_CONTINUE)qr.pending=qr_select_held()?QR_SAME:QR_NONE;
    }
    return qr_continue_native(FS_PASS);
}
