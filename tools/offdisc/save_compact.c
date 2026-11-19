/* Included by save_runtime.c. The existing native UI owns libmc and the frozen
 * aggregate. This layer never writes legacy files or runs during PLAY. */
#include "profile_mc.h"
#include "profile_location.h"

/* Launcher ABI2: version, format (1 legacy / 2 compact), fresh-create UUID.
 * Apply after prepared-cache restore. Actual profile UUID is read from LOAD. */
u32 save_profile_config[6]={2,1,0,0,0,0};
ml_target save_location={ML_SCHEMA,0,0,0,{0},{0},{0}};
static s32 pc_new_load_used;
#define PC_RECORD_BUDGET (4u*1024u*1024u)
#define PC_OPAQUE_BUDGET (512u*1024u)
typedef struct pc_domain {struct pc_domain *next;mp_section section;u8 *data;} pc_domain;
typedef struct {
    mp_transaction *transaction;mp_work *work;mp_mc card;mp_storage storage;
    mp_info input;mp_source source;mp_visitor visitor;
    cp_record *records;u32 count;u8 *native;
    pc_domain *domains,**tail;u32 opaque_bytes;
    s32 loading,phase,created,probe,native_seen,charts_seen;
} pc_job_t;
static pc_job_t *pc_job;
static pc_domain *pc_domains;
static u8 pc_uuid[16];static mp_u64 pc_sequence;
static s32 pc_selected;
static char pc_path_a[64],pc_path_b[64];
static const char *const pc_paths[]={pc_path_a,pc_path_b};
static const u8 pc_name[]="MEGAMIX";
static s32 pc_enabled(void){return save_profile_config[0]==2 && save_profile_config[1]==2;}
static void *pc_allocate(u32 n)
{
    if(n>0x7fffff80u)return 0;
    void *raw=malloc_(n+64);if(!raw)return 0;
    void *p=(void *)(((unsigned long)raw+64)&~63ul);((void **)p)[-1]=raw;return p;
}
static void pc_release(void *p){if(p)free_(((void **)p)[-1]);}
static void pc_domains_free(pc_domain *d)
{while(d){pc_domain *next=d->next;if(d->data)free_(d->data);free_(d);d=next;LOOP_PAD();}}
static void pc_forget(void)
{pc_domains_free(pc_domains);pc_domains=0;pc_selected=0;pc_sequence=0;}
static void pc_job_free(void)
{
    if(!pc_job)return;
    if(pc_job->loading){pc_domains_free(pc_job->domains);if(pc_job->records)free_(pc_job->records);}
    pc_release(pc_job->transaction);pc_release(pc_job->work);pc_release(pc_job);pc_job=0;
}
static s32 pc_error(s32 r)
{
    return r==MP_OK?0:r==MP_SPACE?11:r==MP_MISSING?12:
           (r==MP_CORRUPT || r==MP_UNSUPPORTED || r==MP_CONFLICT)?6:7;
}
static s32 pc_empty(const u8 *p,u32 n){while(n--){if(*p++)return 0;LOOP_PAD();}return 1;}
static void pc_zero(void *p,u32 n){u8 *q=p;while(n--){*q++=0;LOOP_PAD();}}
static int pc_load_section(void *context,mp_u64 index,const mp_section *s)
{
    pc_job_t *j=context;(void)index;
    if(s->type==MP_NATIVE){
        if(s->version!=1 || s->stride!=1 || s->bytes!=CP_NATIVE_BYTES)return MP_UNSUPPORTED;
        j->native_seen=1;return MP_OK;
    }
    if(s->type==MP_CHARTS){
        if(s->version!=1 || s->stride!=sizeof(cp_record))return MP_UNSUPPORTED;
        if(s->bytes>PC_RECORD_BUDGET || s->count>PC_RECORD_BUDGET/sizeof(cp_record))return MP_LIMIT;
        j->count=(u32)s->count;j->charts_seen=1;
        j->records=j->count?malloc_((u32)s->bytes):0;
        return j->count && !j->records?MP_CALLBACK:MP_OK;
    }
    if(j->opaque_bytes>PC_OPAQUE_BUDGET-sizeof(pc_domain) || s->bytes>PC_OPAQUE_BUDGET-sizeof(pc_domain)-j->opaque_bytes)return MP_LIMIT;
    pc_domain *d=malloc_(sizeof *d);if(!d)return MP_CALLBACK;
    d->next=0;d->section=*s;d->data=s->bytes?malloc_((u32)s->bytes):0;
    if(s->bytes && !d->data){free_(d);return MP_CALLBACK;}
    *j->tail=d;j->tail=&d->next;j->opaque_bytes+=(u32)s->bytes+sizeof *d;return MP_OK;
}
static int pc_load_page(void *context,mp_u64 index,const mp_section *s,mp_u64 at,const void *p,mp_u32 n)
{
    pc_job_t *j=context;u8 *out;(void)index;
    if(s->type==MP_NATIVE)out=j->native;
    else if(s->type==MP_CHARTS)out=(u8 *)j->records;
    else {pc_domain *d=j->domains;while(d && d->section.type!=s->type){d=d->next;LOOP_PAD();}if(!d)return MP_CALLBACK;out=d->data;}
    ps_copy(out+(u32)at,p,n);return MP_OK;
}
/* Merge resident opaque domains with the three editable domains, by section
 * type. Unknown data and native source provenance never disappear on SAVE. */
static const u8 *pc_source_at(pc_job_t *j,mp_u64 index,mp_section *out)
{
    pc_domain *d=j->domains;u32 known=0;
    while(known<3 || d){
        u32 type=known==0?MP_METADATA:known==1?MP_CHARTS:known==2?MP_NATIVE:~0u;
        if(d && d->section.type<=type){
            if(d->section.type==type)known++;
            if(!index--){*out=d->section;return d->data;}d=d->next;
        }else {
            known++;
            if(!index--){
                pc_zero(out,sizeof *out);out->type=type;out->version=1;
                out->flags=type==MP_NATIVE?0:MP_MANDATORY;
                out->stride=type==MP_METADATA?0:type==MP_CHARTS?sizeof(cp_record):1;
                out->count=type==MP_METADATA?1:type==MP_CHARTS?j->count:CP_NATIVE_BYTES;
                out->bytes=type==MP_METADATA?sizeof(pc_name)-1:type==MP_CHARTS?(mp_u64)j->count*sizeof(cp_record):CP_NATIVE_BYTES;
                return type==MP_METADATA?pc_name:type==MP_CHARTS?(u8 *)j->records:j->native;
            }
        }
        LOOP_PAD();
    }
    return 0;
}
static int pc_source_describe(void *context,mp_u64 index,mp_section *s)
{
    pc_job_t *j=context;
    /* An empty chart section legitimately has a NULL data pointer. */
    pc_source_at(j,index,s);return index<j->input.sections?MP_OK:MP_CALLBACK;
}
static int pc_source_read(void *context,mp_u64 index,mp_u64 at,void *p,mp_u32 n)
{
    pc_job_t *j=context;mp_section s;const u8 *data=pc_source_at(j,index,&s);
    if(index>=j->input.sections || !data || at>s.bytes || n>s.bytes-at)return MP_CALLBACK;
    ps_copy(p,data+(u32)at,n);return MP_OK;
}
#ifdef SAVE_PROFILE
#define pc_make_job pc_make_job_observed
#endif
static s32 pc_make_job(s32 loading,s32 probe)
{
    u8 *a=ps_aggregate();
    if(!a || I32(a+16)!=CP_NATIVE_BYTES || !I32(a+4) || !ensure_saves())return 0;
    pc_job=pc_allocate(sizeof *pc_job);if(!pc_job)return 0;
    pc_zero(pc_job,sizeof *pc_job);
    pc_job->transaction=pc_allocate(mp_tx_bytes());pc_job->work=pc_allocate(sizeof(mp_work));
    pc_job->loading=loading;pc_job->probe=probe;pc_job->native=(u8 *)(unsigned long)(u32)I32(a+4);pc_job->tail=&pc_job->domains;
    if(!pc_job->transaction || !pc_job->work || ml_paths(&save_location,pc_path_a,pc_path_b) ||
       mp_mc_init(&pc_job->card,&pc_job->storage,&ps_ops,save_location.managed?save_location.port:0)){pc_job_free();return 0;}
    pc_job->source=(mp_source){pc_job,pc_source_describe,pc_source_read};
    pc_job->visitor=(mp_visitor){pc_job,pc_load_section,pc_load_page};
    if(!loading){
        pc_job->records=ps_snapshot;pc_job->count=ps_snapshot_n;pc_job->domains=pc_domains;
        pc_job->input.sections=3;pc_job->input.sequence=pc_sequence;
        for(pc_domain *d=pc_domains;d;d=d->next){if(d->section.type!=MP_METADATA)pc_job->input.sections++;LOOP_PAD();}
        ps_copy(pc_job->input.uuid,pc_selected?pc_uuid:(u8 *)(save_profile_config+2),16);
    }
    return 1;
}
#ifdef SAVE_PROFILE
#undef pc_make_job
static s32 pc_make_job(s32 loading,s32 probe)
{
    sp_u64 tick=sp_timer();sp_phase(SP_SETUP);
    s32 result=pc_make_job_observed(loading,probe);
    sp_cost(SP_SETUP,tick);sp_restore(tick);return result;
}
#endif
static s32 pc_start(s32 flags)
{
    pc_job_t *j=pc_job;
    return mp_tx_begin(j->transaction,&j->storage,pc_paths,
           flags?j->input.uuid:save_location.managed?save_location.uuid:0,flags,flags?&j->input:0,flags?&j->source:0,
           !flags && j->loading?&j->visitor:0,j->work);
}
static void pc_diagnostics(void)
{
    mp_u64 n=mp_tx_required_bytes(pc_job->transaction);
    save_diag.required_blocks=n>0xfffffc00u?~0u:((u32)n+1023)/1024;
    save_diag.recovered=mp_tx_recovered(pc_job->transaction);
    save_diag.skipped=mp_tx_skipped(pc_job->transaction);
}
#ifdef SAVE_PROFILE
#define pc_finish pc_finish_observed
#endif
static s32 pc_finish(s32 error)
{
    pc_job_t *j=pc_job;cp_record *records=j->records;u32 count=j->count;
    if(!error && j->loading && (!j->native_seen || !j->charts_seen))error=MP_UNSUPPORTED;
    if(!error && j->loading && !ps_prepare_retained(records,count,&ps_pending_base,&ps_pending_base_n,ps_pending_tracked))error=MP_CALLBACK;
    if(!error){
        const mp_info *info=mp_tx_info(j->transaction);
        save_diag.generation_lo=(u32)info->generation;save_diag.generation_hi=(u32)(info->generation>>32);
        if(j->loading){
            ps_apply_native(records,count,j->native);
            ps_load_start(ps_aggregate());for(s32 i=0;i<16;i++){ps_load_object(ps_aggregate(),i);LOOP_PAD();}ps_load_finish();
            pc_domains_free(pc_domains);pc_domains=j->domains;j->domains=0;
        }
        ps_apply_native(records,count,0);ps_apply_imports(records,count);ps_baseline(j->native,records,count);
        if(ps_base)free_(ps_base);ps_base=ps_pending_base;ps_base_n=ps_pending_base_n;ps_pending_base=0;ps_pending_base_n=0;
        ps_copy(ps_native_tracked,ps_pending_tracked,124);ps_copy(pc_uuid,info->uuid,16);pc_sequence=info->sequence;pc_selected=1;
        ps_after_play=0;save_diag.records=count;save_diag.committed=save_diag.revision;
    }
    pc_job_free();if(ps_snapshot){free_(ps_snapshot);ps_snapshot=0;}ps_snapshot_n=0;ps_packed=0;
    if(ps_pending_base){free_(ps_pending_base);ps_pending_base=0;}ps_pending_base_n=0;
    save_diag.error=pc_error(error);save_diag.phase=0;ps_reset_native_io(save_diag.error);return 1;
}
#ifdef SAVE_PROFILE
#undef pc_finish
static s32 pc_finish(s32 error)
{
    sp_u64 tick=sp_timer();sp_phase(SP_PUBLISH);
    s32 result=pc_finish_observed(error);
    sp_cost(SP_PUBLISH,tick);sp_restore(tick);return result;
}
#endif
/* LOAD returns2 only when BOTH compact paths are absent. Corrupt or future
 * compact data must never silently downgrade to an older native/v1 save. */
static s32 pc_load(void)
{
    if(!pc_job){
        save_diag.detail=save_diag.error=save_diag.recovered=save_diag.skipped=0;save_diag.phase=2;
        if(!pc_make_job(1,0)){save_diag.error=7;save_diag.phase=0;ps_reset_native_io(7);return 1;}
        s32 r=pc_start(0);if(r)return pc_finish(r);
    }
    if(!mp_tx_step(pc_job->transaction))return 0;
    if(mp_tx_absent(pc_job->transaction)){
        if(save_location.managed){
            if(save_location.action==ML_IMPORT){pc_job_free();return 2;}
            /* New Profile is explicit for this fresh launcher session only.
             * Leave native freshly initialized objects alone; a later LOAD
             * must report missing data rather than retaining unsaved progress
             * and pretending that it was read from a card. */
            if(save_location.action==ML_CREATE && !pc_new_load_used && !pc_selected){
                pc_new_load_used=1;pc_job_free();save_diag.phase=save_diag.error=0;ps_reset_native_io(0);return 1;
            }
            return pc_finish(MP_MISSING);
        }
        pc_job_free();return 2;
    }
    pc_diagnostics();s32 error=mp_tx_error(pc_job->transaction);
    return pc_finish(error==MP_MISSING?MP_CORRUPT:error);
}
static s32 pc_write(void)
{
    s32 r;pc_job_t *j=pc_job;
    if(!j){
        save_diag.detail=save_diag.error=save_diag.recovered=save_diag.skipped=0;save_diag.phase=1;
        u8 *a=ps_aggregate();if(!a || I32(a+16)!=CP_NATIVE_BYTES || !I32(a+4))goto allocation_error;
        if(!ps_snapshot && !ps_collect(&ps_snapshot,&ps_snapshot_n))goto allocation_error;
        if(!ps_prepare_retained(ps_snapshot,ps_snapshot_n,&ps_pending_base,&ps_pending_base_n,ps_pending_tracked))goto allocation_error;
        ps_apply_native(ps_snapshot,ps_snapshot_n,(u8 *)(unsigned long)(u32)I32(ps_aggregate()+4));
        if(!pc_make_job(0,0))goto allocation_error;j=pc_job;
        if(pc_empty(j->input.uuid,16)){save_diag.detail=3;return pc_finish(MP_CONFLICT);}
        /* A new boot cannot overwrite an existing compact player's unseen
         * orphan/opaque data. LOAD establishes ownership first. */
        j->phase=pc_selected?1:0;r=pc_start(j->phase?(MP_TX_SAVE|(ps_auto?MP_TX_SKIP_SAME:0)):0);
        if(r)return pc_finish(r);save_diag.revision++;
    }
    if(!mp_tx_step(j->transaction))return 0;r=mp_tx_error(j->transaction);
    if((j->phase==0 || (j->phase==1 && !j->created)) && mp_tx_absent(j->transaction)){
        if(save_location.managed && save_location.action!=ML_CREATE && save_location.action!=ML_IMPORT)return pc_finish(MP_MISSING);
        j->created=1;j->phase=1;r=pc_start(MP_TX_SAVE|MP_TX_CREATE);return r?pc_finish(r):0;
    }
    if(j->phase==0){if(!r){save_diag.detail=2;r=MP_CONFLICT;}return pc_finish(r==MP_MISSING?MP_CORRUPT:r);}
    if(!r && j->created==1){j->created=2;r=pc_start(MP_TX_SAVE);return r?pc_finish(r):0;} /* Seed BOTH. */
    pc_diagnostics();return pc_finish(r);
allocation_error:
    if(ps_snapshot){free_(ps_snapshot);ps_snapshot=0;}ps_snapshot_n=0;
    if(ps_pending_base){free_(ps_pending_base);ps_pending_base=0;}ps_pending_base_n=0;
    save_diag.error=7;save_diag.phase=0;ps_reset_native_io(7);return 1;
}
static s32 pc_probe_begin(void)
{
    if(!pc_selected || !pc_make_job(0,1))return 0;
    if(pc_start(MP_TX_SAVE|MP_TX_SKIP_SAME|MP_TX_PROBE)){pc_job_free();return 0;}return 1;
}
static s32 pc_probe_step(void)
{
    if(!mp_tx_step(pc_job->transaction))return 0;
    s32 same=!mp_tx_error(pc_job->transaction) && mp_tx_skipped(pc_job->transaction);
    pc_job_free();return same?1:2;
}
