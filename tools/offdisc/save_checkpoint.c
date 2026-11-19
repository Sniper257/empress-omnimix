#include "save_checkpoint.h"
#ifndef CP_HOST_TEST
#define CP_PAD() __asm__ volatile("nop\n\tnop\n\tnop")
#else
#define CP_PAD()
#endif

static void cp_copy(void *to,const void *from,cp_u32 n)
{
    cp_u8 *d=to;const cp_u8 *s=from;
    while(n--){*d++=*s++;CP_PAD();}
}
static void cp_zero(void *to,cp_u32 n)
{
    cp_u8 *d=to;while(n--){*d++=0;CP_PAD();}
}
static int cp_equal(const void *a,const void *b,cp_u32 n)
{
    const cp_u8 *x=a,*y=b;while(n--){if(*x++!=*y++)return 0;CP_PAD();}return 1;
}
cp_u32 cp_read32(const cp_u8 *p){return (cp_u32)p[0]|(cp_u32)p[1]<<8|(cp_u32)p[2]<<16|(cp_u32)p[3]<<24;}
void cp_write32(cp_u8 *p,cp_u32 v){p[0]=v;p[1]=v>>8;p[2]=v>>16;p[3]=v>>24;}
cp_u32 cp_crc(cp_u32 c,const void *data,cp_u32 n)
{
    static const cp_u32 tab[16]={0,0x1db71064,0x3b6e20c8,0x26d930ac,0x76dc4190,0x6b6b51f4,0x4db26158,0x5005713c,
        0xedb88320,0xf00f9344,0xd6d6a3e8,0xcb61b38c,0x9b64c2b0,0x86d3d2d4,0xa00ae278,0xbdbdf21c};
    const cp_u8 *p=data;
    while(n--){c^=*p++;c=(c>>4)^tab[c&15];c=(c>>4)^tab[c&15];CP_PAD();}return c;
}
cp_u32 cp_crc_full(const void *data,cp_u32 n){return ~cp_crc(~0u,data,n);}
int cp_record_compare(const cp_record *a,const cp_record *b)
{
    cp_u32 i;const cp_u8 *x=(const cp_u8 *)a,*y=(const cp_u8 *)b;
    for(i=0;i<20;i++){if(x[i]!=y[i])return x[i]<y[i]?-1:1;CP_PAD();}return 0;
}
int cp_record_valid(const cp_record *r)
{
    cp_u32 i,any=0;for(i=0;i<16;i++){any|=r->id[i];CP_PAD();}
    return any && r->category[0]<3 && !r->category[1] && r->rules[0]==1 && !r->rules[1]
        && (r->data[11]>>4)<=9 && !(cp_read32(r->data+20)&0x0fffc000u) && !(r->data[3]&0x80);
}
void cp_record_normalize(cp_u8 out[28],const cp_u8 in[28])
{
    cp_copy(out,in,28);out[3]&=0x7f;cp_write32(out+20,cp_read32(out+20)&~0x0fffc000u);
}
void cp_record_restore(cp_u8 out[28],const cp_u8 in[28])
{
    cp_u32 metadata=cp_read32(out+20)&0x0fffc000u;cp_u8 unlock=out[3]&0x80;
    cp_copy(out,in,28);out[3]=(out[3]&0x7f)|unlock;cp_write32(out+20,(cp_read32(out+20)&~0x0fffc000u)|metadata);
}
int cp_generation_compare(cp_generation a,cp_generation b)
{
    if(a.hi!=b.hi)return a.hi<b.hi?-1:1;
    if(a.lo!=b.lo)return a.lo<b.lo?-1:1;
    return 0;
}
int cp_generation_next(cp_generation in,cp_generation *out)
{
    if(in.lo==~0u && in.hi==~0u)return 0;
    out->lo=in.lo+1;out->hi=in.hi+(out->lo==0);return 1;
}
static const cp_u8 cp_magic[8]={'M','M','I','X','S','A','V','1'};
static const cp_u8 cp_commit[8]={'M','M','I','X','E','N','D','1'};
void cp_make_header(cp_u8 *h,const cp_meta *m)
{
    cp_zero(h,CP_HEADER_BYTES);cp_copy(h,cp_magic,8);cp_write32(h+8,1);cp_write32(h+12,CP_HEADER_BYTES);
    cp_write32(h+16,m->generation.lo);cp_write32(h+20,m->generation.hi);
    cp_write32(h+24,CP_NATIVE_BYTES);cp_write32(h+28,m->records);cp_write32(h+32,CP_RECORD_BYTES);
    cp_write32(h+36,m->capacity);cp_write32(h+40,m->payload_crc);cp_write32(h+44,1); /* native rules */
    cp_write32(h+60,cp_crc_full(h,60));
}
int cp_parse_header(const cp_u8 *h,cp_u32 bytes,cp_meta *m)
{
    if(!cp_equal(h,cp_magic,8))return 0;
    if(cp_crc_full(h,60)!=cp_read32(h+60))return 0;
    if(cp_read32(h+8)!=1 || cp_read32(h+12)!=CP_HEADER_BYTES || cp_read32(h+32)!=CP_RECORD_BYTES || cp_read32(h+44)!=1)return -1;
    if(cp_read32(h+24)!=CP_NATIVE_BYTES || cp_read32(h+28)>CP_MAX_RECORDS || bytes!=cp_read32(h+36) || bytes>CP_MAX_BYTES)return 0;
    if(bytes<CP_HEADER_BYTES+CP_NATIVE_BYTES+cp_read32(h+28)*CP_RECORD_BYTES+CP_FOOTER_BYTES)return 0;
    if(cp_read32(h+48)||cp_read32(h+52)||cp_read32(h+56))return -1;
    m->generation.lo=cp_read32(h+16);m->generation.hi=cp_read32(h+20);m->records=cp_read32(h+28);
    m->capacity=bytes;m->payload_crc=cp_read32(h+40);m->header_crc=cp_read32(h+60);return 1;
}
void cp_make_footer(cp_u8 *f,const cp_meta *m)
{
    cp_copy(f,cp_commit,8);cp_write32(f+8,m->generation.lo);cp_write32(f+12,m->generation.hi);
    cp_write32(f+16,m->payload_crc);cp_write32(f+20,m->header_crc);
}
int cp_check_footer(const cp_u8 *f,const cp_meta *m)
{
    return cp_equal(f,cp_commit,8) && cp_read32(f+8)==m->generation.lo && cp_read32(f+12)==m->generation.hi
        && cp_read32(f+16)==m->payload_crc && cp_read32(f+20)==m->header_crc;
}

/* Fixed names inside the native save directory. The legacy file remains an
 * ordinary EMPRESS save; neither file names nor paths depend on imported data. */
static const char *const cp_paths[]={
    "/BISLPM-55221BM2DX16/MMIXSAVE.A", "/BISLPM-55221BM2DX16/MMIXSAVE.B",
    "/BISLPM-55221BM2DX16/BISLPM-55221BM2DX16"};
/* phase: scan A/B, choose, optional legacy open/close, checkpoint write+verify,
 * native export+verify, load reread. A/B is authoritative after its footer is
 * durable. An export failure reports failure and leaves that checkpoint intact. */
enum {T_SCAN,T_CHOOSE,T_LEGACY_STAT,T_INFO,T_TOUCH,T_TOUCH_CLOSE,T_WRITE,T_VERIFY,T_EXPORT,T_EXPORT_VERIFY,T_LOAD,T_DONE};
enum {F_STAT,F_OPEN,F_HEADER,F_NATIVE,F_RECORDS,F_PADDING,F_FOOTER,F_EOF,F_CLOSE,F_DONE};
struct cp_transaction {
    const cp_ops *ops;
    int port,save,force,phase,file,pending,fd,stage,error,recovered,skipped,failed,fatal;
    int valid[3],present[3],best,target,reading,retain,scan_retry;
    cp_u32 sizes[3],off,crc,native_crc,native_prefix,record_fill,record_index,bytes;
    cp_meta meta[2],current,expected;
    cp_u8 *native,*scratch;
    const cp_record *input;
    cp_record *output;
    cp_u32 input_n,output_n,required_blocks;
    cp_record last,partial;
    cp_u8 header[64] __attribute__((aligned(64)));
    cp_u8 footer[24] __attribute__((aligned(16)));
    cp_u8 dir[64] __attribute__((aligned(64)));
    cp_card_info info;
};
static int cp_request(cp_transaction *t,int op,const char *path,void *p,cp_u32 n,int arg,int *result)
{
    int r;
    if(!t->pending){r=t->ops->start(op,t->port,path,t->fd,p,n,arg);if(r==1)return 0;if(r<0){*result=r;return 1;}t->pending=1;return 0;}
    r=t->ops->poll(result);if(!r)return 0;t->pending=0;if(r<0)*result=-1000;return 1;
}
static int cp_media_error(int r){return r<=-10 || r==-5;}
static void cp_file_start(cp_transaction *t,int file,int reading,int retain)
{
    t->file=file;t->reading=reading;t->retain=retain;t->stage=F_STAT;t->fd=-1;t->off=0;t->failed=0;t->fatal=0;
    t->crc=~0u;t->native_crc=~0u;t->native_prefix=0;t->record_fill=0;t->record_index=0;
}
static void cp_file_fail(cp_transaction *t,int fatal)
{
    t->failed=1;t->fatal|=fatal;t->stage=t->fd>=0?F_CLOSE:F_DONE;
}
/* Checkpoints are verified a second time after writing. Every operation,
 * including EOF, seek and close, is checked before a caller sees success. */
static int cp_file_step(cp_transaction *t)
{
    int r;cp_u32 n,i,part,pad_end;cp_u8 *p;
    const char *path=cp_paths[t->file];int legacy=t->file==2;
    switch(t->stage){
    case F_STAT:
        if(!cp_request(t,CP_STAT,path,t->dir,64,0,&r))return 0;
        if(r==0 || r==-4){t->present[t->file]=0;if(t->reading){cp_file_fail(t,0);return 0;}t->sizes[t->file]=0;}
        else if(r!=1){cp_file_fail(t,1);return 0;}
        else {t->present[t->file]=1;t->sizes[t->file]=cp_read32(t->dir+16);}
        if(t->reading && ((legacy && t->sizes[2]!=CP_NATIVE_BYTES) || (!legacy && (t->sizes[t->file]<CP_HEADER_BYTES+CP_NATIVE_BYTES+CP_FOOTER_BYTES || t->sizes[t->file]>CP_MAX_BYTES)))){cp_file_fail(t,0);return 0;}
        t->stage=F_OPEN;return 0;
    case F_OPEN:
        if(!cp_request(t,CP_OPEN,path,0,0,t->reading?1:0x203,&r))return 0;
        if(r<0){cp_file_fail(t,1);return 0;}t->fd=r;
        t->stage=legacy?F_NATIVE:F_HEADER;t->off=0;return 0;
    case F_HEADER:
        if(!cp_request(t,t->reading?CP_READ:CP_WRITE,0,t->header,64,0,&r))return 0;
        if(r!=64){cp_file_fail(t,cp_media_error(r));return 0;}
        if(t->reading){r=cp_parse_header(t->header,t->sizes[t->file],&t->current);
            if(r!=1){if(r<0)t->error=6;cp_file_fail(t,r<0);return 0;}
            if(t->retain && t->current.records){t->output=t->ops->alloc(t->current.records*CP_RECORD_BYTES);if(!t->output){t->error=7;cp_file_fail(t,1);return 0;}}
            t->output_n=t->retain?t->current.records:0;
        }
        t->stage=F_NATIVE;t->off=0;return 0;
    case F_NATIVE:
        n=CP_NATIVE_BYTES-t->off;if(n>CP_CHUNK)n=CP_CHUNK;
        p=t->reading?(t->retain?t->native+t->off:t->scratch):t->native+t->off;
        if(!cp_request(t,t->reading?CP_READ:CP_WRITE,0,p,n,0,&r))return 0;
        if(r!=(int)n){cp_file_fail(t,cp_media_error(r));return 0;}
        if(!t->off)t->native_prefix=cp_read32(p);
        part=t->off?0:4;t->native_crc=cp_crc(t->native_crc,p+part,n-part);t->crc=cp_crc(t->crc,p,n);
        t->off+=n;if(t->off==CP_NATIVE_BYTES){if(~t->native_crc!=t->native_prefix){cp_file_fail(t,0);return 0;}t->off=0;t->stage=legacy?F_EOF:F_RECORDS;}
        return 0;
    case F_RECORDS:
        n=t->current.records*CP_RECORD_BYTES-t->off;
        if(!n){t->stage=F_PADDING;t->off=CP_HEADER_BYTES+CP_NATIVE_BYTES+t->current.records*CP_RECORD_BYTES;return 0;}
        /* Whole records per transfer: preserve independent validation boundaries. */
        if(n>CP_CHUNK)n=(CP_CHUNK/CP_RECORD_BYTES)*CP_RECORD_BYTES;
        p=t->reading?(t->retain?(cp_u8 *)t->output+t->off:t->scratch):(cp_u8 *)t->input+t->off;
        if(!cp_request(t,t->reading?CP_READ:CP_WRITE,0,p,n,0,&r))return 0;
        if(r!=(int)n){cp_file_fail(t,cp_media_error(r));return 0;}
        t->crc=cp_crc(t->crc,p,n);
        if(t->reading)for(i=0;i<n;i+=CP_RECORD_BYTES){cp_record *v=(cp_record *)(p+i);
            if(!cp_record_valid(v) || (t->record_index && cp_record_compare(&t->last,v)>=0)){cp_file_fail(t,0);return 0;}
            cp_copy(&t->last,v,CP_RECORD_BYTES);t->record_index++;CP_PAD();}
        t->off+=n;return 0;
    case F_PADDING:
        pad_end=t->current.capacity-CP_FOOTER_BYTES;n=pad_end-t->off;
        if(!n){t->stage=F_FOOTER;return 0;}if(n>CP_CHUNK)n=CP_CHUNK;
        p=t->scratch;if(!t->reading)cp_zero(p,n);
        if(!cp_request(t,t->reading?CP_READ:CP_WRITE,0,p,n,0,&r))return 0;
        if(r!=(int)n){cp_file_fail(t,cp_media_error(r));return 0;}
        if(t->reading)for(i=0;i<n;i++){if(p[i]){cp_file_fail(t,0);return 0;}CP_PAD();}
        t->crc=cp_crc(t->crc,p,n);t->off+=n;return 0;
    case F_FOOTER:
        if(!cp_request(t,t->reading?CP_READ:CP_WRITE,0,t->footer,CP_FOOTER_BYTES,0,&r))return 0;
        if(r!=CP_FOOTER_BYTES || ~t->crc!=t->current.payload_crc || (t->reading && !cp_check_footer(t->footer,&t->current))){cp_file_fail(t,cp_media_error(r));return 0;}
        t->stage=t->reading?F_EOF:F_CLOSE;return 0;
    case F_EOF:
        if(!t->reading){t->stage=F_CLOSE;return 0;}
        if(!cp_request(t,CP_READ,0,t->scratch,1,0,&r))return 0;
        if(r!=0){cp_file_fail(t,cp_media_error(r));return 0;}t->stage=F_CLOSE;return 0;
    case F_CLOSE:
        if(!cp_request(t,CP_CLOSE,0,0,0,0,&r))return 0;
        t->fd=-1;
        if(r!=0){t->failed=1;t->fatal=1;}t->stage=F_DONE;return 0;
    case F_DONE:return 1;
    }
    return 0;
}
cp_transaction *cp_begin(const cp_ops *ops,int port,int save,int force,cp_u8 *native,const cp_record *records,cp_u32 count)
{
    cp_transaction *t;cp_u32 i;
    if(!ops || !native || port<0 || port>1 || count>CP_MAX_RECORDS || (count && !records))return 0;
    if(save){if(cp_crc_full(native+4,CP_NATIVE_BYTES-4)!=cp_read32(native))return 0;
        for(i=0;i<count;i++){if(!cp_record_valid(records+i) || (i && cp_record_compare(records+i-1,records+i)>=0))return 0;CP_PAD();}}
    t=ops->alloc(sizeof *t);if(!t)return 0;cp_zero(t,sizeof *t);t->ops=ops;t->scratch=ops->alloc(CP_CHUNK);
    if(!t->scratch){ops->release(t);return 0;}t->port=port;t->save=save;t->force=force;t->native=native;t->input=records;t->input_n=count;t->best=-1;
    cp_file_start(t,0,1,0);return t;
}
static void cp_finish(cp_transaction *t,int err){t->error=err;t->phase=T_DONE;}
static cp_u32 cp_payload_crc(cp_transaction *t,cp_u32 capacity)
{
    cp_u32 c=cp_crc(~0u,t->native,CP_NATIVE_BYTES),n=capacity-CP_HEADER_BYTES-CP_NATIVE_BYTES-t->input_n*CP_RECORD_BYTES-CP_FOOTER_BYTES;
    c=cp_crc(c,t->input,t->input_n*CP_RECORD_BYTES);cp_zero(t->scratch,CP_CHUNK);
    while(n){cp_u32 k=n>CP_CHUNK?CP_CHUNK:n;c=cp_crc(c,t->scratch,k);n-=k;CP_PAD();}return ~c;
}
int cp_step(cp_transaction *t)
{
    int r,i;cp_u32 capacity,needed,old_blocks;cp_generation previous={0,0};
    if(!t || t->phase==T_DONE)return 1;
    switch(t->phase){
    case T_SCAN:
        if(!cp_file_step(t))return 0;
        if(t->fatal){cp_finish(t,t->error?t->error:9);return 1;}
        if(t->save && t->failed && t->present[t->file] && !t->scan_retry){t->scan_retry=1;cp_file_start(t,t->file,1,0);return 0;}
        t->scan_retry=0;
        t->valid[t->file]=!t->failed;if(t->valid[t->file])t->meta[t->file]=t->current;
        if(t->file==0){cp_file_start(t,1,1,0);return 0;}t->phase=T_CHOOSE;return 0;
    case T_CHOOSE:
        for(i=0;i<2;i++)if(t->valid[i] && (t->best<0 || cp_generation_compare(t->meta[i].generation,t->meta[t->best].generation)>0))t->best=i;
        if(t->valid[0] && t->valid[1] && !cp_generation_compare(t->meta[0].generation,t->meta[1].generation) &&
            (t->meta[0].payload_crc!=t->meta[1].payload_crc || t->meta[0].records!=t->meta[1].records)){cp_finish(t,6);return 1;}
        if(!t->save){t->target=t->best>=0?t->best:2;t->recovered=t->best>=0?(t->present[1-t->best] && !t->valid[1-t->best]):(t->present[0]||t->present[1]?2:0);if(t->best>=0)t->expected=t->meta[t->best];
            cp_file_start(t,t->target,1,1);t->phase=T_LOAD;return 0;}
        if(t->save==2){
            if(t->best>=0 && t->meta[t->best].records==t->input_n &&
                t->meta[t->best].capacity>=CP_HEADER_BYTES+CP_NATIVE_BYTES+t->input_n*CP_RECORD_BYTES+CP_FOOTER_BYTES &&
                t->meta[t->best].payload_crc==cp_payload_crc(t,t->meta[t->best].capacity)){
                t->skipped=1;t->current=t->meta[t->best];cp_finish(t,0);
            }else cp_finish(t,12);
            return 1;
        }
        t->target=t->best>=0?1-t->best:0;if(t->sizes[t->target]>CP_MAX_BYTES){cp_finish(t,6);return 1;}
        capacity=(CP_HEADER_BYTES+CP_NATIVE_BYTES+t->input_n*CP_RECORD_BYTES+CP_FOOTER_BYTES+1023u)&~1023u;
        if(t->sizes[t->target]>capacity)capacity=t->sizes[t->target];
        t->current.records=t->input_n;t->current.capacity=capacity;t->current.payload_crc=cp_payload_crc(t,capacity);
        if(t->best>=0)previous=t->meta[t->best].generation;
        if(!cp_generation_next(previous,&t->current.generation)){cp_finish(t,6);return 1;}
        /* Compare the exact image, including records/count. Native CRC covers
         * changes inside native payload; no title/alias-based dirty tracking. */
        if(!t->force && t->best>=0 && t->meta[t->best].records==t->input_n && t->meta[t->best].capacity==capacity && t->meta[t->best].payload_crc==t->current.payload_crc){t->skipped=1;t->current=t->meta[t->best];cp_finish(t,0);return 1;}
        t->phase=T_LEGACY_STAT;return 0;
    case T_LEGACY_STAT:
        if(!cp_request(t,CP_STAT,cp_paths[2],t->dir,64,0,&r))return 0;
        if(r==0 || r==-4)t->sizes[2]=0;
        else if(r==1)t->sizes[2]=cp_read32(t->dir+16);
        else {cp_finish(t,9);return 1;}
        if(t->sizes[2]>CP_NATIVE_BYTES){cp_finish(t,6);return 1;}
        t->phase=T_INFO;return 0;
    case T_INFO:
        if(!cp_request(t,CP_INFO,0,&t->info,0,0,&r))return 0;
        if(r!=0 || t->info.type!=2 || t->info.formatted!=1 || t->info.free_blocks<0){cp_finish(t,9);return 1;}
        old_blocks=(t->sizes[t->target]+1023u)>>10;
        needed=(t->current.capacity+1023u)>>10;needed=needed>old_blocks?needed-old_blocks:0;
        old_blocks=(t->sizes[2]+1023u)>>10;
        needed+=((CP_NATIVE_BYTES+1023u)>>10)-old_blocks+8;
        t->required_blocks=needed;
        if((cp_u32)t->info.free_blocks<needed){cp_finish(t,11);return 1;}
        t->phase=T_TOUCH;t->fd=-1;return 0;
    case T_TOUCH:
        if(!cp_request(t,CP_OPEN,cp_paths[2],0,0,0x203,&r))return 0;
        if(r<0){cp_finish(t,7);return 1;}t->fd=r;t->phase=T_TOUCH_CLOSE;return 0;
    case T_TOUCH_CLOSE:
        if(!cp_request(t,CP_CLOSE,0,0,0,0,&r))return 0;
        t->fd=-1;if(r!=0){cp_finish(t,7);return 1;}
        cp_make_header(t->header,&t->current);t->current.header_crc=cp_read32(t->header+60);cp_make_footer(t->footer,&t->current);
        t->expected=t->current;
        cp_file_start(t,t->target,0,0);t->phase=T_WRITE;return 0;
    case T_WRITE:
        if(!cp_file_step(t))return 0;
        if(t->failed){cp_finish(t,7);return 1;}
        cp_file_start(t,t->target,1,0);t->phase=T_VERIFY;return 0;
    case T_VERIFY:
        if(!cp_file_step(t))return 0;
        if(t->failed || t->current.records!=t->input_n || cp_generation_compare(t->current.generation,t->expected.generation) || t->current.payload_crc!=t->expected.payload_crc || t->current.capacity!=t->expected.capacity){cp_finish(t,7);return 1;}
        cp_file_start(t,2,0,0);t->phase=T_EXPORT;return 0;
    case T_EXPORT:
        if(!cp_file_step(t))return 0;
        if(t->failed){cp_finish(t,7);return 1;}
        cp_file_start(t,2,1,0);t->phase=T_EXPORT_VERIFY;return 0;
    case T_EXPORT_VERIFY:
        if(!cp_file_step(t))return 0;
        if(t->failed || t->native_prefix!=cp_read32(t->native)){cp_finish(t,7);return 1;}
        cp_finish(t,0);return 1;
    case T_LOAD:
        if(!cp_file_step(t))return 0;
        if(t->failed || (t->file!=2 && (cp_generation_compare(t->current.generation,t->expected.generation) || t->current.payload_crc!=t->expected.payload_crc || t->current.capacity!=t->expected.capacity))){cp_finish(t,t->present[t->file]?(t->fatal?9:6):12);return 1;}
        if(t->file==2){t->output_n=0;t->current.generation.lo=0;t->current.generation.hi=0;}
        cp_finish(t,0);return 1;
    }
    return 0;
}
int cp_error(const cp_transaction *t){return t?t->error:7;}
int cp_recovered(const cp_transaction *t){return t?t->recovered:0;}
int cp_skipped(const cp_transaction *t){return t && t->skipped;}
cp_u32 cp_required_blocks(const cp_transaction *t){return t?t->required_blocks:0;}
cp_generation cp_current_generation(const cp_transaction *t){cp_generation z={0,0};return t?t->current.generation:z;}
cp_record *cp_take_records(cp_transaction *t,cp_u32 *count){cp_record *r=t->output;*count=t->output_n;t->output=0;t->output_n=0;return r;}
void cp_end(cp_transaction *t){if(!t)return;if(t->output)t->ops->release(t->output);t->ops->release(t->scratch);t->ops->release(t);}
