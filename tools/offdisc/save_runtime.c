/* Included by songdb.c after alias storage helpers. Native SAVE/LOAD remains the
 * only writer; this code runs at its menu boundaries, never during a chart. */
#include "save_checkpoint.h"
#include "save_probe.h"
static s32 pc_enabled(void);
static s32 ps_compact_legacy;

typedef struct {cp_record record;u32 changed;} ps_candidate;
typedef struct {u32 schema,phase,error,detail,generation_lo,generation_hi,records,recovered,skipped,revision,committed,required_blocks;} ps_diag_t;
_Static_assert(sizeof(ps_diag_t)==48,"launcher save diagnostics ABI");
ps_diag_t save_diag={1};
u32 save_policy; /*0 native exits,1 FREE result-exit autosave*/
static u8 *ps_exit_scene;
static u32 ps_exit_phase,ps_exit_owns_aggregate;
static cp_record *ps_base,*ps_snapshot;
static u32 ps_base_n,ps_snapshot_n,ps_baseline_native_crc,ps_baseline_records_crc;
static cp_record *ps_pending_base;static u32 ps_pending_base_n;
static u8 ps_native_tracked[124],ps_pending_tracked[124];
static u8 *ps_read_packed;
static u32 ps_baseline_n,ps_baseline_valid,ps_after_play,ps_auto,ps_packed;
static cp_transaction *ps_transaction;
static s32 ps_probe;

#ifndef SAVE_HOST_TEST
#define ps_mc_init() (((s32 (*)(void))0x11e790)())
#define ps_mc_quit() (((void (*)(void))0x11ea28)())
#define ps_aggregate() (*(u8 **)0x2ad3a8)
#define ps_port() I32((u8 *)0x2ad3a4)
#define ps_state() I32((u8 *)0x796ddc)
#define ps_native_error() I32((u8 *)0x796de0)
#define ps_native_phase() I32((u8 *)0x796dd8)
#define ps_native_retries() I32((u8 *)0x796dd4)
#define ps_native_record(id,mode,diff) save_rec(id,mode,diff)
#define ps_score_object() (*(u8 **)0x7945d8)
#define ps_diff() (((s32 (*)(void))0x205de0)())
#define ps_mode() (((s32 (*)(void))0x1d9db0)())
#define ps_system_object() (*(u8 **)0x7945b0)
#define ps_autosave_on() (((u8 *(*)(void))0x1686e0)()[0xab])
#define ps_pack_prepare() (((void (*)(void))0x1688e0)())
#define ps_pack_counter() (((void (*)(u8 *))0x19e460)(*(u8 **)0x7945e8))
#define ps_pack_start(a) (((void (*)(u8 *))0x168860)(a))
#define ps_pack_object(a,i) (((void (*)(u8 *,s32))0x168830)(a,i))
#define ps_pack_crc(a) (((void (*)(u8 *))0x169040)(a))
#define ps_load_start(a) (((void (*)(u8 *))0x1687a0)(a))
#define ps_load_object(a,i) (((void (*)(u8 *,s32))0x168770)(a,i))
#define ps_load_finish() (((void (*)(void))0x168700)())
#include "save_text.h"
#define ps_game_kind() (((s32 (*)(void))0x1f5830)())
#define ps_exit_assets(s) (((void (*)(u8 *,s32,s32))0x155c40)(s,0xc6,0))
#define ps_exit_ready() ((((s32 (*)(void))0x14cf70)()!=0) && (((s32 (*)(void))0x13d450)()==0))
#define ps_exit_aggregate_begin() (((void (*)(void))0x1d7820)())
#define ps_exit_aggregate_end() (((void (*)(void))0x1d77f0)())
#define ps_exit_ui() (((void (*)(void))0x1d2290)())
#define ps_exit_fade_in() (((void (*)(s32))0x1561a0)(0))
#define ps_exit_fade_out() (((void (*)(s32))0x1560e0)(0))
extern s32 save_load_body(void),save_write_body(void),save_auto_body(s32),save_manual_body(s32);
extern void save_result_body(u8 *),save_message_body(void),save_result_exit_body(u8 *,unsigned long long,unsigned long long);
#endif

static void ps_copy(void *dest,const void *src,u32 n){memcpy_(dest,src,n);}
static s32 ps_equal(const void *a,const void *b,u32 n)
{
    const u8 *x=a,*y=b;while(n--){if(*x++!=*y++)return 0;LOOP_PAD();}return 1;
}
static const cp_record *ps_find(const cp_record *v,u32 n,const cp_record *key)
{
    u32 lo=0,hi=n;while(lo<hi){u32 mid=(lo+hi)>>1;s32 c=cp_record_compare(v+mid,key);if(c<0)lo=mid+1;else hi=mid;LOOP_PAD();}
    return lo<n && !cp_record_compare(v+lo,key)?v+lo:0;
}
static s32 ps_key(cp_record *r,s32 row,s32 mode,s32 diff)
{
    const u8 *id=chart_identity_row(row,mode==2,diff);if(!id)return 0;
    ps_copy(r->id,id,16);r->category[0]=mode==2?1:mode==3?2:0;r->category[1]=0;r->rules[0]=1;r->rules[1]=0;return 1;
}
/* Normalize only the native identity hints and lock bit. Scores, lamps, assist
 * provenance, counts and all other record bytes remain intact. */
static s32 ps_nondefault(const u8 *record)
{
    u8 unplayed[28];cp_record_normalize(unplayed,UNPLAYED);return !ps_equal(record,unplayed,28);
}
static s32 ps_emit(ps_candidate *out,u32 *count,u32 capacity,const cp_record *r)
{
    const cp_record *base=ps_find(ps_base,ps_base_n,r);
    if(!base && !ps_nondefault(r->data))return 1;
    if(out){if(*count>=capacity)return 0;out[*count].record=*r;out[*count].changed=!base || !ps_equal(r->data,base->data,28);}
    (*count)++;return 1;
}
static s32 ps_gather(ps_candidate *out,u32 *count,u32 capacity)
{
    u32 i,k; s32 mode,diff,row;cp_record r;
    u8 *imported=malloc_((sdb.identity_count+7)/8+1);if(!imported)return 0;
    for(i=0;i<(sdb.identity_count+7)/8+1;i++){imported[i]=0;LOOP_PAD();}
    for(k=0;k<sdb.n_new;k++)for(i=0;i<10;i++){
        u16 id=sdb.chart_index[(FIRST+k)*10+i];if(id!=NOSLOT && id<sdb.identity_count)imported[id>>3]|=1u<<(id&7);LOOP_PAD();}
    for(row=0;row<FIRST+(s32)sdb.n_new;row++){
        if((row>=99 && row<FIRST) || (ps_read_packed && row>=FIRST))continue;
        for(mode=0;mode<4;mode++)for(diff=0;diff<5;diff++){
            u8 *record;const cp_record *base;
            if(row<FIRST && mode==1)continue; /* native P1/P2 share one SP record */
            if(!ps_key(&r,row,mode,diff))continue;
            if(row<FIRST){u16 id=sdb.chart_index[row*10+(mode==2?5:0)+diff];
                base=ps_find(ps_base,ps_base_n,&r);
                if(!pc_enabled() && !base && !(imported[id>>3]&(1u<<(id&7))) && !(ps_native_tracked[(row*10+(mode==2?5:0)+diff)>>3]&(1u<<((row*10+(mode==2?5:0)+diff)&7))))continue;
                record=ps_native_record(row,mode,diff);
                /* Native BEGINNER owns only a lamp byte. The accessor's other
                 * scratch-object bytes can belong to a previous lookup. */
                if(mode==0 && diff==3){
                    u32 lamp=ps_read_packed?ps_read_packed[0x6a90+0x2f9dc-4+row]:ps_score_object()[0x2f9dc+row];
                    if(lamp>7){free_(imported);return 0;}
                    ps_copy(r.data,UNPLAYED,28);r.data[15]=lamp<<5;record=r.data;
                }else if(record && ps_read_packed){
                    u32 off=(u32)(record-ps_score_object());if(off<4 || off+28>4+0x2fb74)continue;record=ps_read_packed+0x6a90+off-4;
                }
            }else record=slot_rec(row-FIRST,mode,diff);
            if(!record)continue;cp_record_normalize(r.data,record);
            if(!ps_emit(out,count,capacity,&r)){free_(imported);return 0;}LOOP_PAD();
        }
        LOOP_PAD();
    }
    free_(imported);return 1;
}
static s32 ps_candidate_compare(const ps_candidate *a,const ps_candidate *b){return cp_record_compare(&a->record,&b->record);}
static void ps_swap(ps_candidate *a,ps_candidate *b){ps_candidate t=*a;*a=*b;*b=t;}
static void ps_sift(ps_candidate *v,u32 root,u32 end)
{
    while(root*2+1<end){u32 c=root*2+1;if(c+1<end && ps_candidate_compare(v+c,v+c+1)<0)c++;
        if(ps_candidate_compare(v+root,v+c)>=0)return;ps_swap(v+root,v+c);root=c;LOOP_PAD();}
}
static void ps_sort(ps_candidate *v,u32 n)
{
    u32 i=n/2;while(i){--i;ps_sift(v,i,n);LOOP_PAD();}i=n;
    while(i>1){--i;ps_swap(v,v+i);ps_sift(v,0,i);LOOP_PAD();}
}
#ifdef SAVE_PROFILE
#define ps_collect ps_collect_observed
#endif
static s32 ps_collect_ids(cp_record **records,u32 *n)
{
    ps_candidate *v;cp_record *result;u32 cap=ps_base_n,count=ps_base_n,i,j,chosen,changed,unique=0;
    if(sdb.identity_version!=1 || !sdb.chart_index || !ids_available() || !ensure_saves())return 0;
    for(i=FIRST;i<255;i++){if(i>VARIANT && i<128)continue;if(!stash_alias(i))return 0;LOOP_PAD();}
    if(!ps_gather(0,&cap,0) || cap>(pc_enabled()?(8u*1024u*1024u-4)/sizeof *v:CP_MAX_RECORDS*4u))return 0;
    v=malloc_(cap*sizeof *v+4);if(!v)return 0;
    for(i=0;i<ps_base_n;i++){v[i].record=ps_base[i];v[i].changed=0;LOOP_PAD();}
    if(!ps_gather(v,&count,cap)){free_(v);return 0;}ps_sort(v,count);
    for(i=0;i<count;i=j){chosen=i;changed=0;
        for(j=i;j<count && !cp_record_compare(&v[i].record,&v[j].record);j++){
            if(v[j].changed){if(changed && !ps_equal(v[chosen].record.data,v[j].record.data,28)){save_diag.detail=1;free_(v);return 0;}chosen=j;changed=1;}LOOP_PAD();}
        v[unique++].record=v[chosen].record;LOOP_PAD();}
    if(unique>(pc_enabled()?(4u*1024u*1024u)/sizeof(cp_record):CP_MAX_RECORDS)){free_(v);return 0;}
    result=malloc_(unique*sizeof *result+4);if(!result){free_(v);return 0;}
    for(i=0;i<unique;i++){result[i]=v[i].record;LOOP_PAD();}free_(v);*records=result;*n=unique;return 1;
}
/* every chart's identity is looked up: with them off the block, all are read into the heap once (songdb.c) */
static s32 ps_collect(cp_record **records,u32 *n)
{
    s32 r;ids_acquire();r=ps_collect_ids(records,n);ids_release();return r;
}
#ifdef SAVE_PROFILE
#undef ps_collect
static s32 ps_collect(cp_record **records,u32 *n)
{
    sp_u64 tick=sp_timer();sp_phase(SP_COLLECT);
    s32 result=ps_collect_observed(records,n);
    sp_cost(SP_COLLECT,tick);sp_restore(tick);return result;
}
#endif
/* Installed records already live in slot_saved/native objects. Retain only
 * unavailable chart/category records, plus a tiny native tracking bitmap. No
 * second full score library is kept resident during play. */
#ifdef SAVE_PROFILE
#define ps_prepare_retained ps_prepare_retained_observed
#endif
static s32 ps_prepare_retained(const cp_record *records,u32 n,cp_record **retained,u32 *retained_n,u8 tracked[124])
{
    u8 *seen=malloc_(n+1);u32 i,missing=0; s32 row,mode,d;cp_record key;const cp_record *r;
    if(!seen)return 0;for(i=0;i<=n;i++){seen[i]=0;LOOP_PAD();}
    for(i=0;i<124;i++){tracked[i]=ps_native_tracked[i];LOOP_PAD();}
    for(row=0;row<FIRST+(s32)sdb.n_new;row++){
        if(row>=99 && row<FIRST)continue;
        for(mode=0;mode<4;mode++)for(d=0;d<5;d++){
            if(row<FIRST && (mode==1 || !ps_native_record(row,mode,d)))continue;
            if(ps_key(&key,row,mode,d) && (r=ps_find(records,n,&key))){seen[r-records]=1;
                if(row<FIRST){u32 bit=row*10+(mode==2?5:0)+d;tracked[bit>>3]|=1u<<(bit&7);}
            }
            LOOP_PAD();
        }
        LOOP_PAD();
    }
    for(i=0;i<n;i++){if(!seen[i])missing++;LOOP_PAD();}
    *retained=missing?malloc_(missing*sizeof **retained):0;
    if(missing && !*retained){free_(seen);return 0;}
    *retained_n=missing;missing=0;
    for(i=0;i<n;i++){if(!seen[i])(*retained)[missing++]=records[i];LOOP_PAD();}
    free_(seen);return 1;
}
#ifdef SAVE_PROFILE
#undef ps_prepare_retained
static s32 ps_prepare_retained(const cp_record *records,u32 n,cp_record **retained,u32 *retained_n,u8 tracked[124])
{
    sp_u64 tick=sp_timer();sp_phase(SP_RETAIN);
    s32 result=ps_prepare_retained_observed(records,n,retained,retained_n,tracked);
    sp_cost(SP_RETAIN,tick);sp_restore(tick);return result;
}
#endif
static s32 ps_seed_native(u8 *packed,cp_record **records,u32 *n)
{
    cp_record *old=ps_base;u32 old_n=ps_base_n;s32 ok;
    ps_base=0;ps_base_n=0;ps_read_packed=packed;ok=ps_collect(records,n);
    ps_read_packed=0;ps_base=old;ps_base_n=old_n;return ok;
}
#ifdef SAVE_PROFILE
#define ps_apply_imports ps_apply_imports_observed
#endif
static void ps_apply_imports(const cp_record *records,u32 n)
{
    u32 k; s32 mode,d;cp_record key;const cp_record *r;
    /* Reset every slot first: a LOAD replaces the session, it never combines
     * the previous player's scores with the newly loaded profile. */
    for(k=0;k<(slot_base?slot_base[sdb.n_new]:0);k++){ps_copy(slot_saved+k*28,UNPLAYED,28);LOOP_PAD();}
    for(k=0;k<sdb.n_new;k++)for(mode=0;mode<4;mode++)for(d=0;d<5;d++){u8 *dest;
        if(mode!=1 && (dest=slot_rec(k,mode,d)) && ps_key(&key,FIRST+k,mode,d) && (r=ps_find(records,n,&key)))cp_record_restore(dest,r->data);LOOP_PAD();}
    for(k=FIRST;k<255;k++){u16 slot=sdb.slot_of[k];if(slot!=NOSLOT && slot<sdb.n_new)slot_expand(slot,save_of(k,0,0));LOOP_PAD();}
    /* Existing native contexts are rebuilt on re-entry after SAVE/LOAD. */
    window_reset();for(k=0;k<NPOOL;k++){sdb.pools[k].ctx=0;LOOP_PAD();}
}
#ifdef SAVE_PROFILE
#undef ps_apply_imports
static void ps_apply_imports(const cp_record *records,u32 n)
{
    sp_u64 tick=sp_timer();sp_phase(SP_APPLY);
    ps_apply_imports_observed(records,n);
    sp_cost(SP_APPLY,tick);sp_restore(tick);
}
#endif
/* The matching native prefix travels with the extension. Keep native song-ID,
 * category/difficulty hints and locks from its target record, not from an alias. */
#ifdef SAVE_PROFILE
#define ps_apply_native ps_apply_native_observed
#endif
static void ps_apply_native(const cp_record *records,u32 n,u8 *packed)
{
    s32 row,mode,d;cp_record key;const cp_record *r;u8 *record,*score=ps_score_object();
    for(row=0;row<99;row++)for(mode=0;mode<4;mode++)for(d=0;d<5;d++){
        if(mode==1 || !ps_key(&key,row,mode,d) || !(r=ps_find(records,n,&key)))continue;
        if(mode==0 && d==3){
            if(packed)packed[0x6a90+0x2f9dc-4+row]=r->data[15]>>5;
            else score[0x2f9dc+row]=r->data[15]>>5;
        }else if((record=ps_native_record(row,mode,d))){
            if(packed){u32 off=(u32)(record-score);if(off<4 || off+28>4+0x2fb74)continue;record=packed+0x6a90+off-4;}
            cp_record_restore(record,r->data);
        }
        LOOP_PAD();
    }
    if(packed)cp_write32(packed,cp_crc_full(packed+4,CP_NATIVE_BYTES-4));
}
#ifdef SAVE_PROFILE
#undef ps_apply_native
static void ps_apply_native(const cp_record *records,u32 n,u8 *packed)
{
    sp_u64 tick=sp_timer();sp_phase(SP_APPLY);
    ps_apply_native_observed(records,n,packed);
    sp_cost(SP_APPLY,tick);sp_restore(tick);
}
#endif
#ifdef SAVE_PROFILE
#define ps_pack ps_pack_observed
#endif
static s32 ps_pack(void)
{
    u8 *a=ps_aggregate();s32 i;if(!a || I32(a+16)!=CP_NATIVE_BYTES || !I32(a+4))return 0;
    ps_pack_prepare();ps_pack_counter();ps_pack_start(a);
    for(i=0;i<16;i++){ps_pack_object(a,i);LOOP_PAD();}ps_pack_crc(a);return 1;
}
#ifdef SAVE_PROFILE
#undef ps_pack
static s32 ps_pack(void)
{
    sp_u64 tick=sp_timer();sp_phase(SP_PACK);
    s32 result=ps_pack_observed();
    sp_cost(SP_PACK,tick);sp_restore(tick);return result;
}
#endif
static void ps_baseline(u8 *native,const cp_record *records,u32 n)
{
    ps_baseline_native_crc=cp_read32(native);ps_baseline_records_crc=cp_crc_full(records,n*sizeof *records);ps_baseline_n=n;ps_baseline_valid=1;
}
#ifndef SAVE_HOST_TEST
static int ps_mc_start(int op,int port,const char *path,int fd,void *data,u32 n,int arg)
{
    s32 r;
    switch(op){
    case CP_STAT:r=((s32 (*)(s32,s32,const char *,s32,s32,void *))0x11f5b8)(port,0,path,0,1,data);break;
    case CP_OPEN:r=((s32 (*)(s32,s32,const char *,s32))0x11ec68)(port,0,path,arg);break;
    case CP_READ:r=((s32 (*)(s32,void *,u32))0x11f018)(fd,data,n);break;
    case CP_WRITE:r=((s32 (*)(s32,const void *,u32))0x11f130)(fd,data,n);break;
    case CP_CLOSE:r=((s32 (*)(s32))0x11edc8)(fd);break;
    case CP_SEEK:r=((s32 (*)(s32,s32,s32))0x11ee88)(fd,n,arg);break;
    case CP_INFO:{cp_card_info *info=data;r=((s32 (*)(s32,s32,s32 *,s32 *,s32 *))0x11f3c8)(port,0,&info->type,&info->free_blocks,&info->formatted);break;}
    default:return -100;
    }
    return r==-200?1:r;
}
static int ps_mc_poll(int *result)
{
    s32 r=((s32 (*)(s32,s32 *,s32 *))0x11f2a8)(1,0,result);
    return r==1?1:r==0?0:-1;
}
static void *ps_allocate(u32 bytes){return malloc_(bytes);}
static const cp_ops ps_ops={ps_mc_start,ps_mc_poll,ps_allocate,free_};
#endif
static void ps_reset_native_io(s32 error)
{
    ps_native_error()=error;ps_native_retries()=0;ps_native_phase()=0;
}
#include "save_compact.c"
#ifndef profile_native_name
#define profile_native_name() ((char *)0x26a5b0)
#endif
void save_profile_role(s32 role)
{
    ps_port()=role;
    if(pc_enabled() && save_location.managed){
        const char *name=role?"BISLPM-55221BM2DX16":save_location.directory;
        char *out=profile_native_name();u32 i=0;
        while(i<19 && name[i]){out[i]=name[i];i++;LOOP_PAD();}
        while(i<20){out[i++]=0;LOOP_PAD();}
    }
}
#ifndef SAVE_HOST_TEST
extern s32 save_profile_presence_body(void);
extern s32 save_profile_startup_body(void);
#define profile_card_check() (((s32 (*)(void))0x1d4280)())
#endif
s32 save_profile_startup(void)
{
    /* Startup's original probe opens the obsolete retail data filename.
     * Retain its real type/format/removal checks, then let compact LOAD check
     * PROFILE.A/B. It must work even when no retail file was ever exported. */
    if(pc_enabled() && save_location.managed && ps_port()==0)return profile_card_check();
    return save_profile_startup_body();
}
/* Native LOAD normally checks for the old retail file before it reaches our
 * compact reader. A managed directory may contain only PROFILE.A/B. The actual
 * compact transaction performs the authoritative absence/CRC/UUID checks. */
s32 save_profile_presence(void)
{
    if(pc_enabled() && save_location.managed && ps_port()==0){ps_reset_native_io(0);return 1;}
    return save_profile_presence_body();
}
static s32 ps_finish_transaction(s32 loading)
{
    s32 error=cp_error(ps_transaction);u8 *a=ps_aggregate(),*native=(u8 *)(u32)I32(a+4);u32 n=0;cp_record *records=0;
    save_diag.required_blocks=cp_required_blocks(ps_transaction);save_diag.recovered=cp_recovered(ps_transaction);save_diag.skipped=cp_skipped(ps_transaction);
    if(loading && pc_enabled() && save_diag.recovered==2){error=6;save_diag.detail=4;save_diag.recovered=0;}
    if(error)save_diag.recovered=0;
    if(!error){cp_generation g=cp_current_generation(ps_transaction);save_diag.generation_lo=g.lo;save_diag.generation_hi=g.hi;
        if(loading){
            records=cp_take_records(ps_transaction,&n);
            if(!ensure_saves())error=7;
            if(!error && !g.lo && !g.hi && !n){if(records)free_(records);records=0;if(!ps_seed_native(native,&records,&n))error=7;}
            if(!error && !ps_prepare_retained(records,n,&ps_pending_base,&ps_pending_base_n,ps_pending_tracked))error=7;
            /* All validation and required allocations precede replacement of
             * the live profile. The original object callbacks remain native. */
            if(!error){s32 i;ps_load_start(a);for(i=0;i<16;i++){ps_load_object(a,i);LOOP_PAD();}ps_load_finish();}
        }else {records=ps_snapshot;n=ps_snapshot_n;ps_snapshot=0;}
        if(!error){
            ps_apply_native(records,n,0);ps_apply_imports(records,n);ps_baseline(native,records,n);
            if(ps_base)free_(ps_base);ps_base=ps_pending_base;ps_base_n=ps_pending_base_n;ps_pending_base=0;ps_pending_base_n=0;
            ps_copy(ps_native_tracked,ps_pending_tracked,124);
            ps_after_play=0;save_diag.records=n;save_diag.committed=save_diag.revision;
        }
    }
    if(records)free_(records);cp_end(ps_transaction);ps_transaction=0;
    if(ps_snapshot){free_(ps_snapshot);ps_snapshot=0;}ps_snapshot_n=0;ps_packed=0;
    if(ps_pending_base){free_(ps_pending_base);ps_pending_base=0;}ps_pending_base_n=0;
    if(loading){if(!error)pc_forget();ps_compact_legacy=0;}
    save_diag.phase=0;save_diag.error=error;ps_reset_native_io(error);return 1;
}
#ifdef SAVE_PROFILE
#define save_write save_write_observed
#endif
s32 save_write(void)
{
    u8 *a=ps_aggregate();
    if(ps_port()!=0)return save_write_body(); /* untouched native secondary-card import/export path */
    if(pc_enabled())return pc_write();
    if(!ps_transaction){
        save_diag.detail=0;save_diag.error=0;save_diag.recovered=0;save_diag.skipped=0;save_diag.phase=1;
        if(!a || I32(a+16)!=CP_NATIVE_BYTES || !I32(a+4) || (!ps_snapshot && !ps_collect(&ps_snapshot,&ps_snapshot_n))){save_diag.error=7;save_diag.phase=0;ps_reset_native_io(7);return 1;}
        if(!ps_prepare_retained(ps_snapshot,ps_snapshot_n,&ps_pending_base,&ps_pending_base_n,ps_pending_tracked)){
            free_(ps_snapshot);ps_snapshot=0;save_diag.error=7;save_diag.phase=0;ps_reset_native_io(7);return 1;
        }
        ps_apply_native(ps_snapshot,ps_snapshot_n,(u8 *)(u32)I32(a+4));
        ps_transaction=cp_begin(&ps_ops,0,1,!ps_auto,(u8 *)(u32)I32(a+4),ps_snapshot,ps_snapshot_n);
        if(!ps_transaction){free_(ps_snapshot);ps_snapshot=0;if(ps_pending_base){free_(ps_pending_base);ps_pending_base=0;}ps_pending_base_n=0;save_diag.error=7;save_diag.phase=0;ps_reset_native_io(7);return 1;}
        save_diag.revision++;
    }
    return cp_step(ps_transaction)?ps_finish_transaction(0):0;
}
#ifdef SAVE_PROFILE
#undef save_write
#endif
#ifdef SAVE_PROFILE
#define save_load save_load_observed
#endif
s32 save_load(void)
{
    u8 *a=ps_aggregate();
    if(ps_port()!=0)return save_load_body();
    if(pc_enabled() && !ps_compact_legacy){s32 result=pc_load();if(result!=2)return result;ps_compact_legacy=1;}
    if(!ps_transaction){save_diag.detail=0;save_diag.error=0;save_diag.recovered=0;save_diag.skipped=0;save_diag.phase=2;
        if(!a || I32(a+16)!=CP_NATIVE_BYTES || !I32(a+4) || !ensure_saves()){save_diag.error=7;save_diag.phase=0;ps_reset_native_io(7);return 1;}
        ps_transaction=cp_begin(&ps_ops,0,0,0,(u8 *)(u32)I32(a+4),0,0);
        if(!ps_transaction){save_diag.error=7;save_diag.phase=0;ps_reset_native_io(7);return 1;}
    }
    return cp_step(ps_transaction)?ps_finish_transaction(1):0;
}
#ifdef SAVE_PROFILE
#undef save_load
#endif
#ifdef SAVE_PROFILE
#define save_manual save_manual_observed
#endif
s32 save_manual(s32 enabling_autosave)
{
    if(!ps_state()){ps_auto=0;save_diag.recovered=0;save_diag.detail=0;}
    return save_manual_body(enabling_autosave);
}
#ifdef SAVE_PROFILE
#undef save_manual
#endif
static s32 ps_start_packed_auto(s32 arg)
{
    s32 result;
    ps_packed=1;ps_auto=1;
    /* Serialization is already complete. Native case0 uses this port field
     * only to choose packing versus an already-packed buffer; restore it before
     * any card operation on subsequent frames. */
    ps_port()=1;result=save_auto_body(arg);ps_port()=0;return result;
}
#ifdef SAVE_PROFILE
#define save_auto save_auto_observed
#endif
s32 save_auto(s32 arg)
{
    s32 result;
    if(ps_probe){
        s32 same;
        if(ps_probe==2){s32 result=pc_probe_step();if(!result)return 0;same=result==1;}
        else {if(!cp_step(ps_transaction))return 0;same=!cp_error(ps_transaction) && cp_skipped(ps_transaction);cp_end(ps_transaction);ps_transaction=0;}
        ps_probe=0;ps_mc_quit();save_diag.phase=0;
        if(same){free_(ps_snapshot);ps_snapshot=0;ps_snapshot_n=0;save_diag.skipped++;save_diag.error=0;ps_reset_native_io(0);ps_after_play=0;return 1;}
        result=ps_start_packed_auto(arg);
    }else if(!ps_state() && ps_autosave_on() && ps_port()==0){
        u8 *a=ps_aggregate(),*native;cp_record *records=0;u32 count=0;
        if(save_policy==1 && !ps_after_play)return 1;
        save_diag.detail=0;
        if(!ps_pack() || !ps_collect(&records,&count)){if(records)free_(records);ps_auto=1;return save_auto_body(arg);}
        native=(u8 *)(u32)I32(a+4);ps_apply_native(records,count,native);
        ps_snapshot=records;ps_snapshot_n=count;
        if(ps_baseline_valid && cp_read32(native)==ps_baseline_native_crc && count==ps_baseline_n && cp_crc_full(records,count*sizeof *records)==ps_baseline_records_crc){
            /* Never assume the same card is still inserted. Validate its A/B
             * images before suppressing an unchanged request. No writes or UI
             * success are possible from this probe. */
            if(ps_mc_init()>=0){
                if(pc_enabled()){if(pc_probe_begin()){ps_probe=2;save_diag.phase=3;return 0;}}
                else if((ps_transaction=cp_begin(&ps_ops,0,2,0,native,records,count))){ps_probe=1;save_diag.phase=3;return 0;}
            }
            ps_mc_quit();
        }
        result=ps_start_packed_auto(arg);
    }else {if(!ps_state())ps_auto=1;result=save_auto_body(arg);}
    if(result){ps_auto=0;if(!ps_transaction && ps_snapshot){free_(ps_snapshot);ps_snapshot=0;ps_snapshot_n=0;ps_packed=0;}}
    return result;
}
#ifdef SAVE_PROFILE
#undef save_auto
#endif
#ifdef SAVE_PROFILE
static sp_u64 ps_observe_enter(unsigned kind)
{return sp_enter(kind,ps_state(),ps_port(),save_profile_config[1],save_policy,FIRST+sdb.n_new);}
static void ps_observe_leave(sp_u64 tick,s32 result)
{sp_diag((const unsigned *)&save_diag);sp_leave(tick,result,ps_native_error());}
s32 save_write(void){sp_u64 t=ps_observe_enter(SP_WRITE_ONLY);s32 r=save_write_observed();ps_observe_leave(t,r);return r;}
s32 save_load(void){sp_u64 t=ps_observe_enter(SP_READ_ONLY);s32 r=save_load_observed();ps_observe_leave(t,r);return r;}
s32 save_manual(s32 arg){sp_u64 t=ps_observe_enter(SP_MANUAL);s32 r=save_manual_observed(arg);ps_observe_leave(t,r);return r;}
s32 save_auto(s32 arg){
    sp_u64 t=0;if(ps_state() || (ps_autosave_on() && (save_policy!=1 || ps_after_play)))t=ps_observe_enter(SP_AUTO);
    s32 r=save_auto_observed(arg);ps_observe_leave(t,r);return r;
}
#ifndef SAVE_HOST_TEST
extern s32 save_load_ui_body(void),save_load_auto_body(void);
s32 save_load_ui(void){sp_u64 t=ps_observe_enter(SP_LOAD_UI);s32 r=save_load_ui_body();ps_observe_leave(t,r);return r;}
s32 save_load_auto(void){sp_u64 t=ps_observe_enter(SP_LOAD_AUTO);s32 r=save_load_auto_body();ps_observe_leave(t,r);return r;}
#endif
#endif
static u8 *ps_primary_record(s32 song,s32 mode,s32 diff)
{
    s32 row=song_catalog_row(song),id=song&255;
    if(mode<0 || mode>3 || diff<0 || diff>4)return 0;
    if(row<0)return 0;
    if(row<FIRST)return ps_native_record(row,mode,diff);
    return save_of(id,mode,diff);
}
/* Share only an actually changed earned record after the native result writer.
 * This avoids a duplicate baseline library in gameplay RAM. Input, judgement,
 * gauge/lamp calculations and score policy are still the original routines. */
static void ps_share_record(s32 song,s32 mode,s32 diff)
{
    u8 *source=ps_primary_record(song,mode,diff);cp_record value,key;u16 slot; s32 row,m,d;
    if(!source)return;
    if((song&255)<FIRST)row=song&255;
    else {slot=sdb.slot_of[song&255];if(slot==NOSLOT)return;row=FIRST+slot;}
    if(!ps_key(&value,row,mode,diff))return;
    if(row<FIRST && mode<2 && diff==3){ps_copy(value.data,UNPLAYED,28);value.data[15]=ps_score_object()[0x2f9dc+row]<<5;}
    else cp_record_normalize(value.data,source);
    for(row=0;row<FIRST+(s32)sdb.n_new;row++){
        if(row>=99 && row<FIRST)continue;
        for(m=0;m<4;m++)for(d=0;d<5;d++){
            u8 *dest;
            if(!ps_key(&key,row,m,d) || cp_record_compare(&key,&value))continue;
            if(row<FIRST){u32 bit=row*10+(m==2?5:0)+d;ps_native_tracked[bit>>3]|=1u<<(bit&7);
                if(m==0 && d==3)ps_score_object()[0x2f9dc+row]=value.data[15]>>5;
                else if((dest=ps_native_record(row,m,d)))cp_record_restore(dest,value.data);
            }else {s32 k=row-FIRST,a=sdb.alias_of[k];
                {u8 *dest=slot_rec(k,m,d);if(dest)cp_record_restore(dest,value.data);}
                if(a)cp_record_restore(save_of(a,m,d),value.data);
            }
            LOOP_PAD();
        }
        LOOP_PAD();
    }
    save_diag.revision++;
}
#ifdef INPUT_LOG
void input_probe_stop(void),input_probe_flush(void);
#endif
#ifdef EXIT_TRACE
void exit_trace_mark(u32);
void exit_trace_arg(u32);
#define EXIT_ARG(p) exit_trace_arg((u32)(unsigned long)(p))
#define EXIT_MARK(n) exit_trace_mark(n)
#else
#define EXIT_MARK(n) ((void)0)
#define EXIT_ARG(p) ((void)0)
#endif
void save_result(u8 *result)
{
#ifdef PLAY_PREVIEW_ACTIVE
    if(PLAY_PREVIEW_ACTIVE())return;
#endif
    pc_new_load_used=1;
    EXIT_ARG(result);EXIT_MARK(0x20);
    s32 song=song_now(),mode=ps_mode(),diff=ps_diff();EXIT_MARK(0x70);
    u8 before[28],*record=ps_primary_record(song,mode,diff);s32 had_record=record!=0;EXIT_ARG(record);EXIT_MARK(0x71);
    EXIT_MARK(0x72);if(record)ps_copy(before,record,28);EXIT_ARG(result);EXIT_MARK(0x73);
    save_result_body(result);EXIT_MARK(0x21);
    record=ps_primary_record(song,mode,diff);
    if(had_record && record && !ps_equal(before,record,28))ps_share_record(song,mode,diff);
    if(ps_system_object()[0x17])ps_after_play=1;
#ifdef INPUT_LOG
    input_probe_stop();
    EXIT_MARK(0x22);
#endif
}
/* 17a390 is the RESULT scene update, not the PLAY-scene finish at 1a1800.
 * Its active routine 17a630 already stops result BGM and waits for 13f420
 * before fading into state5. Interpose only there, before its parent callback.
 * Other states must run the original update with all three native arguments.
 * Do not call 13e430 here: that resets a separate background loader state and
 * was the invalid barrier that prevented even entering the result screen. */
void save_result_exit(u8 *scene,unsigned long long arg1,unsigned long long arg2)
{
#ifdef EXIT_TRACE
    u32 trace_state=I32(scene+0x2c)&15;
#endif
    EXIT_MARK(0x30+(I32(scene+0x2c)&15));
#ifdef INPUT_LOG
    /* Actual RESULT state5, after original audio/fade; no chart I/O here.
     * Called once for each result exit, regardless of native autosave setting. */
    if(!ps_exit_phase && I32(scene+0x2c)==5) {
#ifdef LOAD_PROFILE
        lp_export();
#endif
        input_probe_flush();
#ifdef SAVE_PROFILE
        sp_flush();
#endif
    }
#endif
    if(!ps_exit_phase){
        if(I32(scene+0x2c)!=5 || save_policy!=1 || !ps_after_play || !ps_autosave_on() || ps_game_kind()!=4 || ps_port()!=0 || ps_state() || ps_transaction){save_result_exit_body(scene,arg1,arg2);EXIT_MARK(0x40+trace_state);return;}
        /* Result draw skips state5. Replace only its completed graphics with
         * the original card UI; result data/subpage objects stay alive until
         * the original parent/destructor sequence runs after saving. */
        ps_exit_scene=scene;ps_exit_assets(scene);ps_exit_phase=1;return;
    }
    if(scene!=ps_exit_scene){save_result_exit_body(scene,arg1,arg2);return;}
    if(ps_exit_phase==1){
        if(!ps_exit_ready())return;
        ps_exit_owns_aggregate=!ps_aggregate();
        if(ps_exit_owns_aggregate)ps_exit_aggregate_begin();
        ps_exit_ui();ps_exit_fade_in();ps_exit_phase=2;
        u8 *aggregate=ps_aggregate();
        if(!aggregate || I32(aggregate+16)!=CP_NATIVE_BYTES || !I32(aggregate+4)){
            /* Enter the native acknowledged error path without serializing a
             * null aggregate. A retry still reaches our checked writer. */
            save_diag.error=7;ps_reset_native_io(7);ps_state()=0xf;
        }
        return;
    }
    if(!save_auto(0))return;
    if(ps_exit_owns_aggregate)ps_exit_aggregate_end();
    ps_exit_owns_aggregate=0;ps_exit_phase=0;ps_exit_scene=0;
    ps_exit_fade_out();save_result_exit_body(scene,arg1,arg2);
}

void save_message(void)
{
    save_message_body();
    if(save_diag.recovered==2)ps_text("MEGAMIX: native-only recovery; imports unavailable");
    else if(save_diag.recovered)ps_text("MEGAMIX: previous checkpoint recovered");
    else if(save_diag.detail==1)ps_text("MEGAMIX: conflicting records; SAVE cancelled");
    else if(save_diag.detail==2)ps_text("MEGAMIX: LOAD compact profile before SAVE");
    else if(save_diag.detail==3)ps_text("MEGAMIX: profile creation ID missing");
    else if(save_diag.detail==4)ps_text("MEGAMIX: damaged legacy checkpoints; recover first");
    else if(save_diag.error==11){
        char text[52]="MEGAMIX needs ",digits[10];u32 n=save_diag.required_blocks,i=0,j=14;
        do {digits[i++]=(char)('0'+n%10);n/=10;LOOP_PAD();}while(n && i<10);
        while(i){text[j++]=digits[--i];LOOP_PAD();}
        const char *tail=" free blocks";while(*tail){text[j++]=*tail++;LOOP_PAD();}text[j]=0;ps_text(text);
    }
}
