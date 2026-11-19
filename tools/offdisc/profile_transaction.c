#include "profile_transaction.h"
#include "profile_wire.h"
#include "save_probe.h"
#if defined(__mips__)
#define TX_PAD() __asm__ volatile("nop\n\tnop\n\tnop")
#else
#define TX_PAD() ((void)0)
#endif
enum {T_STAT=1,T_OPEN_SCAN,T_SCAN,T_CLOSE_SCAN,T_CHOOSE,T_OPEN_READ,T_READ,T_CLOSE_READ,
      T_MEASURE,T_SPACE,T_OPEN_WRITE,T_WRITE,T_VERIFY_STAT,T_FINISH,T_ERROR_CLOSE,T_DONE};
struct mp_transaction {
    const mp_storage *storage;
    const char *paths[2];
    const mp_source *source;
    const mp_visitor *sink;
    mp_work *work;
    mp_async stream;
    mp_async_io io,measure_io;
    mp_visitor guard;
    mp_info input,meta[2],expected,output;
    mp_section incoming;
    mp_u64 sizes[2],fd_position,pipe_position,source_index,required;
    mp_u32 header_crc[2],directory_crc[2],expected_header,expected_directory;
    mp_u8 uuid[16];
    int state,flags,error,file,best,target,valid[2],present[2],fd,low_pending;
    int scanned_error,scan_retry,read_kind,pipe_active,pipe_op,measure_result,same,skipped,recovered,compare_page;
    void *pipe_buffer;mp_u32 pipe_bytes;
    mp_file_stat stat __attribute__((aligned(64)));
    mp_file_space space __attribute__((aligned(64)));
    int blocked;
    /* One file/open/pass only. Never populate from writes: readback verification
     * must refill from the reopened media. DMA owns this aligned buffer until
     * its exact-count READ completes. No read goes beyond the checked extent. */
    mp_u64 read_limit,read_at;
    mp_u32 read_bytes,pipe_copied;
    mp_u8 read_buffer[MP_TX_READ_WINDOW] __attribute__((aligned(64)));
};
static void copy(void *a,const void *b,mp_u32 n){mp_u8 *x=a;const mp_u8 *y=b;while(n--){*x++=*y++;TX_PAD();}}
static void zero(void *a,mp_u32 n){mp_u8 *x=a;while(n--){*x++=0;TX_PAD();}}
static int equal(const void *a,const void *b,mp_u32 n){const mp_u8 *x=a,*y=b;while(n--){if(*x++!=*y++)return 0;TX_PAD();}return 1;}
static int empty(const mp_u8 *a,mp_u32 n){while(n--){if(*a++)return 0;TX_PAD();}return 1;}
static int terminal(mp_transaction *t,int error)
{
    t->error=error;t->state=t->fd>=0?T_ERROR_CLOSE:T_DONE;return t->state==T_DONE;
}
static int request(mp_transaction *t,int op,const char *path,void *p,mp_u32 n,mp_u64 arg,int *result)
{
    int r;if(!t->low_pending){
        if(op==MP_TOPEN || op==MP_TCLOSE || op==MP_TWRITE || op==MP_TSEEK)t->read_bytes=0;
        r=t->storage->start(t->storage->context,op,path,t->fd,p,n,arg);
        if(r==1){t->blocked=1;return 0;}if(r<0){*result=r;return 1;}t->low_pending=1;return 0;}
    r=t->storage->poll(t->storage->context,result);if(!r){t->blocked=1;return 0;}t->low_pending=0;if(r<0)*result=-1000;return 1;
}
/* Adapt absolute stream requests into seek/transfer/close+reopen operations.
 * The file stays exclusively owned across a sync. Never reuse buffers early. */
static int pipe_start(void *context,int op,mp_u64 at,void *p,mp_u32 n)
{
    mp_transaction *t=context;if(t->pipe_active || t->low_pending)return -1;
    t->pipe_active=1;t->pipe_op=op;t->pipe_position=at;t->pipe_buffer=p;t->pipe_bytes=n;t->pipe_copied=0;return 0;
}
static int pipe_done(mp_transaction *t,int result,int *out){t->pipe_active=0;*out=result;return 1;}
static int pipe_read(mp_transaction *t,int *out)
{
    if(t->pipe_position>t->read_limit || t->pipe_bytes>t->read_limit-t->pipe_position)
        return pipe_done(t,-1000,out);
    mp_u64 at=t->pipe_position+t->pipe_copied;
    if(t->read_bytes && at>=t->read_at && at-t->read_at<t->read_bytes){
        mp_u32 offset=(mp_u32)(at-t->read_at),n=t->read_bytes-offset;
        if(n>t->pipe_bytes-t->pipe_copied)n=t->pipe_bytes-t->pipe_copied;
        copy((mp_u8 *)t->pipe_buffer+t->pipe_copied,t->read_buffer+offset,n);
        t->pipe_copied+=n;
        return t->pipe_copied==t->pipe_bytes?pipe_done(t,(int)t->pipe_bytes,out):0;
    }
    /* Aligned windows also combine a page header with its compressed payload.
     * A logical codec request can be16KiB; each read fill is only4KiB. */
    mp_u64 base=at&~(mp_u64)(MP_TX_READ_WINDOW-1),left=t->read_limit-base;
    mp_u32 n=left>MP_TX_READ_WINDOW?MP_TX_READ_WINDOW:(mp_u32)left;int r;
    if(!n)return pipe_done(t,-1000,out);
    t->read_bytes=0;
    if(t->fd_position!=base){
        if(!request(t,MP_TSEEK,0,0,0,base,&r))return 0;
        if(r<0 || (mp_u64)(mp_u32)r!=base)return pipe_done(t,-1000,out);
        t->fd_position=base;return 0;
    }
    if(!request(t,MP_TREAD,0,t->read_buffer,n,0,&r))return 0;
    if(r<0 || (mp_u32)r!=n)return pipe_done(t,-1000,out);
    t->fd_position+=n;t->read_at=base;t->read_bytes=n;return 0;
}
static int pipe_poll(void *context,int *out)
{
    mp_transaction *t=context;int r;if(!t->pipe_active)return -1;
    if(t->pipe_op==MP_ASYNC){
        if(t->fd<0)return pipe_done(t,0,out);
        if(!request(t,MP_TCLOSE,0,0,0,0,&r))return 0;if(!r)t->fd=-1;return pipe_done(t,r,out);
    }
    if(t->fd<0){
        if(!request(t,MP_TOPEN,t->paths[t->target],0,0,t->pipe_op==MP_AWRITE?2:1,&r))return 0;
        if(r<0)return pipe_done(t,r,out);t->fd=r;t->fd_position=0;return 0;
    }
    if(t->pipe_op==MP_AREAD)return pipe_read(t,out);
    if(t->fd_position!=t->pipe_position){
        if(!request(t,MP_TSEEK,0,0,0,t->pipe_position,&r))return 0;
        if(r<0 || (mp_u64)(mp_u32)r!=t->pipe_position)return pipe_done(t,-1000,out);
        t->fd_position=t->pipe_position;return 0;
    }
    if(!request(t,MP_TWRITE,0,t->pipe_buffer,t->pipe_bytes,0,&r))return 0;
    if(r>=0 && (mp_u32)r<=t->pipe_bytes)t->fd_position+=(mp_u32)r;
    return pipe_done(t,r,out);
}
static int measure_start(void *context,int op,mp_u64 at,void *p,mp_u32 n)
{
    mp_transaction *t=context;(void)op;(void)at;(void)p;t->measure_result=(int)n;return 0;
}
static int measure_poll(void *context,int *out){mp_transaction *t=context;*out=t->measure_result;return 1;}
static int editable(const mp_section *s)
{
    return s->version==1 && ((s->type==MP_METADATA && !s->stride) ||
        ((s->type==MP_CHARTS || s->type==MP_SONGS) && s->stride==48) || (s->type==MP_NATIVE && s->stride==1));
}
static int guard_section(void *context,mp_u64 index,const mp_section *old)
{
    mp_transaction *t=context;int r;(void)index;
    while(t->source_index<t->input.sections){
        if(t->source->describe(t->source->context,t->source_index,&t->incoming))return MP_CALLBACK;
        if((r=mp_wire_section_shape(&t->incoming)))return r;
        if(t->incoming.type>=old->type)break;t->source_index++;TX_PAD();
    }
    if(t->source_index==t->input.sections || t->incoming.type!=old->type)return MP_CONFLICT;
    int same=t->incoming.version==old->version && t->incoming.flags==old->flags && t->incoming.stride==old->stride &&
             t->incoming.count==old->count && t->incoming.bytes==old->bytes;
    if(!same){t->same=0;if(t->read_kind==2 || !editable(old))return MP_CONFLICT;}
    t->compare_page=same;return MP_OK;
}
static int guard_page(void *context,mp_u64 index,const mp_section *old,mp_u64 at,const void *p,mp_u32 n)
{
    mp_transaction *t=context;(void)index;if(!t->compare_page)return MP_OK;
    /* Packed input has already been decoded/CRC'd before the callback. It is
     * now safe to use this same bounded buffer for source comparison. */
    if(t->source->read(t->source->context,t->source_index,at,t->work->packed,n))return MP_CALLBACK;
    if(!equal(p,t->work->packed,n)){t->same=0;if(t->read_kind==2 || !editable(old))return MP_CONFLICT;}
    return MP_OK;
}
mp_u32 mp_tx_bytes(void){return sizeof(mp_transaction);}
int mp_tx_begin(mp_transaction *t,const mp_storage *storage,const char *const paths[2],const mp_u8 expected_uuid[16],
                int flags,const mp_info *input,const mp_source *source,const mp_visitor *sink,mp_work *work)
{
    if(!t || !storage || !storage->start || !storage->poll || !work || !paths || !paths[0] || !paths[1] ||
       !storage->limit || storage->limit>0x7fffffffu || flags&~15 || (!(flags&MP_TX_SAVE) && flags) ||
       ((flags&MP_TX_PROBE) && ((flags&MP_TX_CREATE) || !(flags&MP_TX_SKIP_SAME))))return MP_CALLBACK;
    if(flags&MP_TX_SAVE){
        TX_PAD();
        if(!input || !source || !source->describe || !source->read || empty(input->uuid,16)){TX_PAD();return MP_CALLBACK;}
    }
    sp_tx((unsigned)flags);
    zero(t,sizeof *t);t->storage=storage;t->paths[0]=paths[0];t->paths[1]=paths[1];t->work=work;
    t->source=source;t->sink=sink;t->flags=flags;t->best=t->fd=-1;t->state=T_STAT;
    if(expected_uuid)copy(t->uuid,expected_uuid,16);if(input)copy(&t->input,input,sizeof *input);
    t->io.context=t;t->io.start=pipe_start;t->io.poll=pipe_poll;t->io.limit=storage->limit;
    t->measure_io.context=t;t->measure_io.start=measure_start;t->measure_io.poll=measure_poll;t->measure_io.limit=storage->limit;
    t->guard.context=t;t->guard.section=guard_section;t->guard.page=guard_page;return MP_OK;
}
static int matches(mp_transaction *t,int slot)
{
    const mp_info *a=&t->stream.info,*b=&t->meta[slot];
    return equal(a->uuid,b->uuid,16) && a->generation==b->generation && a->sequence==b->sequence && a->bytes==b->bytes &&
        a->sections==b->sections && mp_read32(t->stream.header+124)==t->header_crc[slot] &&
        mp_read32(t->stream.header+80)==t->directory_crc[slot];
}
static int start_measure(mp_transaction *t)
{
    mp_info m;copy(&m,&t->input,sizeof m);m.generation=t->best<0?1:t->meta[t->best].generation+1;
    if(!m.generation)return MP_LIMIT;m.bytes=t->sizes[t->target];
    if(t->best>=0 && m.sequence<t->meta[t->best].sequence)return MP_CONFLICT;
    t->state=T_MEASURE;return mp_async_write(&t->stream,&t->measure_io,&m,t->source,t->work);
}
#ifdef SAVE_PROFILE
#define mp_tx_step_once sp_tx_step_body
#endif
static int mp_tx_step_once(mp_transaction *t)
{
    int r;if(!t || t->state==T_DONE)return 1;
    switch(t->state){
    case T_STAT:
        if(!request(t,MP_TSTAT,t->paths[t->file],&t->stat,sizeof t->stat,0,&r))return 0;
        if(r)return terminal(t,MP_IO);t->present[t->file]=!!t->stat.exists;t->sizes[t->file]=t->stat.exists?t->stat.bytes:0;
        if(t->sizes[t->file]>t->storage->limit)return terminal(t,MP_LIMIT);
        if(t->present[t->file] && t->sizes[t->file]>=MP_HEADER+MP_FOOTER){t->state=T_OPEN_SCAN;return 0;}
        if(t->present[t->file] && (t->flags&MP_TX_SAVE) && !t->scan_retry){t->scan_retry=1;return 0;}
        t->scan_retry=0;if(++t->file==2)t->state=T_CHOOSE;return 0;
    case T_OPEN_SCAN:
        if(!request(t,MP_TOPEN,t->paths[t->file],0,0,1,&r))return 0;if(r<0)return terminal(t,MP_IO);
        t->fd=r;t->fd_position=0;t->target=t->file;t->read_limit=t->sizes[t->file];
        if((r=mp_async_read(&t->stream,&t->io,t->sizes[t->file],0,t->work)))return terminal(t,r);
        t->state=T_SCAN;return 0;
    case T_SCAN:
        if(!mp_async_step(&t->stream))return 0;t->scanned_error=t->stream.error;
        if(!t->scanned_error){t->valid[t->file]=1;copy(&t->meta[t->file],&t->stream.info,sizeof(mp_info));
            t->header_crc[t->file]=mp_read32(t->stream.header+124);t->directory_crc[t->file]=mp_read32(t->stream.header+80);}
        t->state=T_CLOSE_SCAN;return 0;
    case T_CLOSE_SCAN:
        if(!request(t,MP_TCLOSE,0,0,0,0,&r))return 0;if(r)return terminal(t,MP_IO);t->fd=-1;
        if(t->scanned_error && t->scanned_error!=MP_CORRUPT)return terminal(t,t->scanned_error);
        if(t->scanned_error && (t->flags&MP_TX_SAVE) && !t->scan_retry){t->scan_retry=1;t->state=T_STAT;return 0;}
        t->scan_retry=0;if(++t->file==2)t->state=T_CHOOSE;else t->state=T_STAT;return 0;
    case T_CHOOSE:
        for(int i=0;i<2;i++)if(t->valid[i]){
            if(!empty(t->uuid,16) && !equal(t->uuid,t->meta[i].uuid,16))return terminal(t,MP_CONFLICT);
            if(t->best<0 || t->meta[i].generation>t->meta[t->best].generation)t->best=i;TX_PAD();
        }
        if(t->valid[0] && t->valid[1]){
            if(!equal(t->meta[0].uuid,t->meta[1].uuid,16))return terminal(t,MP_CONFLICT);
            if(t->meta[0].generation==t->meta[1].generation &&
               (t->header_crc[0]!=t->header_crc[1] || t->directory_crc[0]!=t->directory_crc[1] || t->meta[0].bytes!=t->meta[1].bytes))return terminal(t,MP_CONFLICT);
            if(t->meta[1-t->best].sequence>t->meta[t->best].sequence)return terminal(t,MP_CONFLICT);
        }
        if(!(t->flags&MP_TX_SAVE)){
            if(t->best<0)return terminal(t,MP_MISSING);
            t->target=t->best;t->read_kind=0;t->state=T_OPEN_READ;return 0;
        }
        if(t->best<0){
            if(!(t->flags&MP_TX_CREATE) || t->present[0] || t->present[1])return terminal(t,MP_MISSING);
            if(!empty(t->uuid,16) && !equal(t->uuid,t->input.uuid,16))return terminal(t,MP_CONFLICT);
            t->target=0;if((r=start_measure(t)))return terminal(t,r);return 0;
        }
        if(t->flags&MP_TX_CREATE || !equal(t->input.uuid,t->meta[t->best].uuid,16))return terminal(t,MP_CONFLICT);
        t->target=t->best;t->read_kind=1;t->state=T_OPEN_READ;return 0;
    case T_OPEN_READ:
        if(!request(t,MP_TOPEN,t->paths[t->target],0,0,1,&r))return 0;if(r<0)return terminal(t,MP_IO);
        t->fd=r;t->fd_position=0;t->source_index=0;
        t->read_limit=t->read_kind==2?t->expected.bytes:t->sizes[t->target];
        if(t->read_kind==2)t->same=1;
        else if(t->read_kind==1)t->same=t->input.sections==t->meta[t->best].sections && t->input.sequence==t->meta[t->best].sequence;
        if((r=mp_async_read(&t->stream,&t->io,t->read_kind==2?t->expected.bytes:t->sizes[t->target],
                           t->read_kind?&t->guard:t->sink,t->work)))return terminal(t,r);
        t->state=T_READ;return 0;
    case T_READ:
        if(!mp_async_step(&t->stream))return 0;if(t->stream.error)return terminal(t,t->stream.error);
        if(t->read_kind<2 && !matches(t,t->best))return terminal(t,MP_CONFLICT);
        if(t->read_kind==2 && (t->stream.info.generation!=t->expected.generation ||
           t->stream.info.sequence!=t->expected.sequence || t->stream.info.sections!=t->expected.sections ||
           !equal(t->stream.info.uuid,t->expected.uuid,16) || mp_read32(t->stream.header+124)!=t->expected_header ||
           mp_read32(t->stream.header+80)!=t->expected_directory || !t->same))return terminal(t,MP_CONFLICT);
        t->state=T_CLOSE_READ;return 0;
    case T_CLOSE_READ:
        if(!request(t,MP_TCLOSE,0,0,0,0,&r))return 0;if(r)return terminal(t,MP_IO);t->fd=-1;
        if(t->read_kind==0){copy(&t->expected,&t->stream.info,sizeof(mp_info));t->recovered=t->present[1-t->best]&&!t->valid[1-t->best];t->state=T_VERIFY_STAT;return 0;}
        if(t->read_kind==2){t->state=T_VERIFY_STAT;return 0;}
        if(t->same && (t->flags&MP_TX_SKIP_SAME)){copy(&t->output,&t->meta[t->best],sizeof(mp_info));t->skipped=1;t->state=T_DONE;return 1;}
        if(t->flags&MP_TX_PROBE)return terminal(t,MP_MISSING); /* Read-only comparison; never open output. */
        t->target=1-t->best;if((r=start_measure(t)))return terminal(t,r);return 0;
    case T_MEASURE:
        if(!mp_async_step(&t->stream))return 0;if(t->stream.error)return terminal(t,t->stream.error);
        copy(&t->expected,&t->stream.info,sizeof(mp_info));t->expected_header=mp_read32(t->stream.header+124);
        t->expected_directory=mp_read32(t->stream.header+80);t->state=T_SPACE;return 0;
    case T_SPACE:{
        if(!request(t,MP_TSPACE,0,&t->space,sizeof t->space,0,&r))return 0;
        mp_u32 unit=t->space.block_bytes;
        if(r || !t->space.writable || !unit || (unit&(unit-1)) || unit>0x100000)return terminal(t,MP_IO);
        mp_u64 new_blocks=(t->expected.bytes+unit-1)&~(mp_u64)(unit-1),old_blocks=(t->sizes[t->target]+unit-1)&~(mp_u64)(unit-1);
        mp_u64 needed=new_blocks>old_blocks?new_blocks-old_blocks:0;
        if(t->best<0)needed+=new_blocks; /* Reserve BOTH generations at Create. */
        if(t->storage->reserve_bytes>~(mp_u64)0-needed)return terminal(t,MP_LIMIT);
        t->required=needed+t->storage->reserve_bytes;
        if(t->space.free_bytes<t->required)return terminal(t,MP_SPACE);
        t->state=T_OPEN_WRITE;return 0;
    }
    case T_OPEN_WRITE:
        if(!request(t,MP_TOPEN,t->paths[t->target],0,0,0x203,&r))return 0;if(r<0)return terminal(t,MP_IO);
        t->fd=r;t->fd_position=0;
        if((r=mp_async_write(&t->stream,&t->io,&t->expected,t->source,t->work)))return terminal(t,r);
        t->state=T_WRITE;return 0;
    case T_WRITE:
        if(!mp_async_step(&t->stream))return 0;if(t->stream.error)return terminal(t,t->stream.error);
        if(t->stream.info.bytes!=t->expected.bytes || mp_read32(t->stream.header+124)!=t->expected_header ||
           mp_read32(t->stream.header+80)!=t->expected_directory)return terminal(t,MP_CONFLICT);
        /* Both stream syncs close successfully. Open afresh for full verify. */
        t->read_kind=2;t->state=T_OPEN_READ;return 0;
    case T_VERIFY_STAT:
        if(!request(t,MP_TSTAT,t->paths[t->target],&t->stat,sizeof t->stat,0,&r))return 0;
        if(r || !t->stat.exists || t->stat.bytes!=t->expected.bytes)return terminal(t,MP_IO);
        copy(&t->output,&t->expected,sizeof(mp_info));t->state=T_DONE;return 1;
    case T_ERROR_CLOSE:
        if(!request(t,MP_TCLOSE,0,0,0,0,&r))return 0;t->fd=-1;t->state=T_DONE;return 1;
    default:return terminal(t,MP_CALLBACK);
    }
}
#ifdef SAVE_PROFILE
#undef mp_tx_step_once
static int mp_tx_step_once(mp_transaction *t)
{
    unsigned phase=SP_SETUP;
    if(t)switch(t->state){
    case T_STAT:case T_OPEN_SCAN:case T_SCAN:case T_CLOSE_SCAN:case T_CHOOSE:phase=SP_SCAN;break;
    case T_OPEN_READ:case T_READ:case T_CLOSE_READ:
        phase=t->read_kind==2?SP_VERIFY:t->read_kind==1?SP_GUARD:SP_READ;break;
    case T_MEASURE:phase=SP_MEASURE;break;
    case T_SPACE:case T_OPEN_WRITE:case T_WRITE:phase=SP_WRITE;break;
    case T_VERIFY_STAT:phase=t->read_kind==2?SP_VERIFY:SP_READ;break;
    case T_ERROR_CLOSE:phase=SP_CLEANUP;break;
    }
    sp_phase(phase);sp_u64 tick=sp_timer();
    int r=sp_tx_step_body(t);sp_cost(phase,tick);return r;
}
#endif
/* Keep the existing small transitions/page boundaries, but do not make each
 * one a video-frame boundary. A real pending poll or rejected-busy start ends
 * this update immediately: never poll it twice in the same pump. The hard
 * step cap also bounds hosts without an EE clock. On EE, stop after about1ms
 * (CP0 Count294.912MHz), checked between indivisible bounded steps. A single
 * codec page or native call can exceed that target; it is not preempted. */
#ifndef MP_TX_CLOCK
#if defined(__mips__)
static mp_u32 mp_tx_clock(void){mp_u32 n;__asm__ volatile("mfc0 %0,$9":"=r"(n));return n;}
#define MP_TX_CLOCK() mp_tx_clock()
#else
#define MP_TX_CLOCK() 0u
#endif
#endif
int mp_tx_step(mp_transaction *t)
{
    mp_u32 began=MP_TX_CLOCK();
    if(!t)return 1;
    for(unsigned step=0;step<MP_TX_PUMP_STEPS;step++){
        t->blocked=0;
        int done=mp_tx_step_once(t);
        if(done || t->blocked)return done;
        if((mp_u32)(MP_TX_CLOCK()-began)>=MP_TX_PUMP_TICKS)return 0;
    }
    return 0;
}
int mp_tx_error(const mp_transaction *t){return t?t->error:MP_CALLBACK;}
int mp_tx_recovered(const mp_transaction *t){return t && t->recovered;}
int mp_tx_skipped(const mp_transaction *t){return t && t->skipped;}
int mp_tx_absent(const mp_transaction *t){return t && t->state==T_DONE && t->error==MP_MISSING && !t->present[0] && !t->present[1];}
mp_u64 mp_tx_required_bytes(const mp_transaction *t){return t?t->required:0;}
const mp_info *mp_tx_info(const mp_transaction *t){return t?&t->output:0;}
