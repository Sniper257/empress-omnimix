#include "profile_async.h"
#include "profile_wire.h"
#include "save_probe.h"
#if defined(__mips__)
#define AS_PAD() __asm__ volatile("nop\n\tnop\n\tnop")
#else
#define AS_PAD() ((void)0)
#endif
enum {R_HEADER=1,R_FOOTER,R_ENTRY,R_PAGE_HEADER,R_PAGE_DATA,R_TAIL,
      W_ZERO=16,W_DIRECTORY,W_SECTION,W_PREPARE,W_PAGE_HEADER,W_PAGE_DATA,W_ENTRY,W_TAIL,W_HEADER,W_SYNC1,W_FOOTER,W_SYNC2,W_PAGE_JOIN,DONE=32};
static void copy(void *to,const void *from,mp_u32 n){mp_u8 *d=to;const mp_u8 *s=from;while(n--){*d++=*s++;AS_PAD();}}
static void zero(void *to,mp_u32 n){mp_u8 *d=to;while(n--){*d++=0;AS_PAD();}}
static int empty(const mp_u8 *p,mp_u32 n){while(n--){if(*p++)return 0;AS_PAD();}return 1;}
static int within(mp_u64 at,mp_u32 n,mp_u64 limit){return at<=limit && n<=limit-at;}
static int done(mp_async *a,int error){a->error=error;a->state=DONE;return 1;}
static int request(mp_async *a,int op,mp_u64 at,void *buffer,mp_u32 n,mp_u64 end)
{
    int r,result;if(!within(at,n,end) || !within(at,n,a->io->limit)){a->error=MP_LIMIT;return -1;}
    if(!a->pending){
        r=a->io->start(a->io->context,op,at,buffer,n);
        if(r==1)return 0;if(r<0){a->error=MP_IO;return -1;}a->pending=1;return 0;
    }
    r=a->io->poll(a->io->context,&result);if(!r)return 0;a->pending=0;
    if(r<0 || result!=(int)n){a->error=MP_IO;return -1;}return 1;
}
static int begin(mp_async *a,const mp_async_io *io,mp_work *work)
{
    if(!a || !io || !io->start || !io->poll || !io->limit || !work)return MP_CALLBACK;
    zero(a,sizeof *a);a->io=io;a->work=work;a->directory_crc=~0u;mp_schema_begin(&a->schema,&a->validator);return MP_OK;
}
int mp_async_read(mp_async *a,const mp_async_io *io,mp_u64 bytes,const mp_visitor *sink,mp_work *work)
{
    int r=begin(a,io,work);if(r)return r;if(bytes<MP_HEADER+MP_FOOTER)return MP_CORRUPT;if(bytes>io->limit)return MP_LIMIT;
    a->info.bytes=bytes;a->sink=sink;a->state=R_HEADER;return MP_OK;
}
int mp_async_write(mp_async *a,const mp_async_io *io,const mp_info *info,const mp_source *source,mp_work *work)
{
    int r=begin(a,io,work);if(r)return r;
    if(!info || !source || !source->describe || !source->read)return MP_CALLBACK;
    if(!info->generation || empty(info->uuid,16))return MP_CORRUPT;
    if(io->limit<MP_HEADER+MP_FOOTER || info->sections>(io->limit-MP_HEADER-MP_FOOTER)/MP_ENTRY || info->bytes>io->limit)return MP_LIMIT;
    copy(&a->info,info,sizeof *info);a->capacity=info->bytes;a->source=source;a->writing=1;
    a->at=MP_HEADER+info->sections*MP_ENTRY;a->state=W_ZERO;return MP_OK;
}
static int visit_section(mp_async *a)
{
    int r=a->validator.section(a->validator.context,a->index,&a->section);if(r)return r;
    if(a->sink && a->sink->section)return a->sink->section(a->sink->context,a->index,&a->section);return MP_OK;
}
static int visit_page(mp_async *a)
{
    int r=a->validator.page(a->validator.context,a->index,&a->section,a->raw,a->work->raw,a->n);if(r)return r;
    if(a->sink && a->sink->page)return a->sink->page(a->sink->context,a->index,&a->section,a->raw,a->work->raw,a->n);return MP_OK;
}
int mp_async_step(mp_async *a)
{
    int r;mp_u32 budget=8;mp_work *w;if(!a)return 1;if(a->state==DONE)return 1;w=a->work;
    while(budget--){
        switch(a->state){
        case R_HEADER:
            r=request(a,MP_AREAD,0,a->header,MP_HEADER,a->info.bytes);if(r<=0)return r<0?done(a,a->error):0;
            if((r=mp_wire_read_header(a->header,a->info.bytes,a->io->limit,&a->info)))return done(a,r);
            a->next=MP_HEADER+a->info.sections*MP_ENTRY;a->tail=a->info.bytes-MP_FOOTER;a->state=R_FOOTER;break;
        case R_FOOTER:
            r=request(a,MP_AREAD,a->tail,a->footer,MP_FOOTER,a->info.bytes);if(r<=0)return r<0?done(a,a->error):0;
            if(!mp_wire_check_footer(a->header,a->footer))return done(a,MP_CORRUPT);a->state=R_ENTRY;break;
        case R_ENTRY:
            if(a->index==a->info.sections){a->at=a->next;a->state=R_TAIL;break;}
            r=request(a,MP_AREAD,MP_HEADER+a->index*MP_ENTRY,a->entry,MP_ENTRY,a->tail);if(r<=0)return r<0?done(a,a->error):0;
            a->directory_crc=mp_crc(a->directory_crc,a->entry,MP_ENTRY);mp_wire_decode_entry(a->entry,&a->section);
            if((r=mp_wire_section_shape(&a->section)))return done(a,r);
            if(a->section.type<=a->previous || a->section.offset!=a->next || a->next>a->tail ||
               a->section.stored>a->tail-a->next || a->section.pages!=a->section.bytes/MP_PAGE+(a->section.bytes%MP_PAGE!=0))return done(a,MP_CORRUPT);
            a->previous=a->section.type;a->at=a->next;a->next+=a->section.stored;
            a->raw=a->page=0;a->raw_crc=a->stored_crc=~0u;
            if((r=visit_section(a)))return done(a,r);a->state=R_PAGE_HEADER;break;
        case R_PAGE_HEADER:
            if(a->page==a->section.pages){
                if(a->at!=a->next || a->raw!=a->section.bytes || ~a->raw_crc!=a->section.crc || ~a->stored_crc!=a->section.stored_crc)return done(a,MP_CORRUPT);
                a->index++;a->state=R_ENTRY;break;
            }
            r=request(a,MP_AREAD,a->at,a->page_header,MP_PAGE_HEADER,a->next);if(r<=0)return r<0?done(a,a->error):0;
            a->stored_crc=mp_crc(a->stored_crc,a->page_header,MP_PAGE_HEADER);a->at+=MP_PAGE_HEADER;
            a->codec=mp_read32(a->page_header);a->n=mp_read32(a->page_header+4);a->k=mp_read32(a->page_header+8);
            if(a->n!=(a->section.bytes-a->raw>MP_PAGE?MP_PAGE:(mp_u32)(a->section.bytes-a->raw)) ||
               !a->k || a->k>a->n || mp_read64(a->page_header+16)!=a->page || (!a->codec && a->k!=a->n))return done(a,MP_CORRUPT);
            if(a->codec>1)return done(a,MP_UNSUPPORTED);a->state=R_PAGE_DATA;break;
        case R_PAGE_DATA:
            r=request(a,MP_AREAD,a->at,w->packed,a->k,a->next);if(r<=0)return r<0?done(a,a->error):0;
            a->stored_crc=mp_crc(a->stored_crc,w->packed,a->k);a->at+=a->k;
            sp_u64 decode_tick=sp_timer();
            if(a->codec){if((r=mp_unpack(w->packed,a->k,w->raw,a->n))){sp_cost(SP_DECODE,decode_tick);return done(a,r);}}else copy(w->raw,w->packed,a->n);
            sp_cost(SP_DECODE,decode_tick);
            if(mp_crc_full(w->raw,a->n)!=mp_read32(a->page_header+12))return done(a,MP_CORRUPT);
            a->raw_crc=mp_crc(a->raw_crc,w->raw,a->n);if((r=visit_page(a)))return done(a,r);
            a->raw+=a->n;a->page++;a->state=R_PAGE_HEADER;return 0; /* one decoded page per call */
        case R_TAIL:
            if(a->at<a->tail){
                a->n=a->tail-a->at>MP_PAGE?MP_PAGE:(mp_u32)(a->tail-a->at);
                r=request(a,MP_AREAD,a->at,w->raw,a->n,a->tail);if(r<=0)return r<0?done(a,a->error):0;
                if(!empty(w->raw,a->n))return done(a,MP_CORRUPT);a->at+=a->n;return 0;
            }
            if(~a->directory_crc!=mp_read32(a->header+80))return done(a,MP_CORRUPT);
            return done(a,mp_schema_finish(&a->schema));
        case W_ZERO:
            r=request(a,MP_AWRITE,0,a->header,MP_HEADER,a->io->limit);if(r<=0)return r<0?done(a,a->error):0;
            /* MC files need not support sparse writes. Materialize the entire
             * directory reservation before seeking to the first payload. */
            zero(w->raw,MP_PAGE);a->next=MP_HEADER;a->state=W_DIRECTORY;break;
        case W_DIRECTORY:
            if(a->next==a->at){a->state=W_SECTION;break;}
            a->n=a->at-a->next>MP_PAGE?MP_PAGE:(mp_u32)(a->at-a->next);
            r=request(a,MP_AWRITE,a->next,w->raw,a->n,a->io->limit);if(r<=0)return r<0?done(a,a->error):0;
            a->next+=a->n;return 0;
        case W_SECTION:
            if(a->index==a->info.sections){
                if((r=mp_schema_finish(&a->schema)))return done(a,r);
                if(!within(a->at,MP_FOOTER,a->io->limit))return done(a,MP_LIMIT);
                a->tail=a->at;if(a->capacity>a->at+MP_FOOTER)a->tail=a->capacity-MP_FOOTER;
                zero(w->raw,MP_PAGE);a->state=W_TAIL;break;
            }
            if(a->source->describe(a->source->context,a->index,&a->section))return done(a,MP_CALLBACK);
            if((r=mp_wire_section_shape(&a->section)))return done(a,r);
            if(a->section.type<=a->previous)return done(a,MP_CORRUPT);a->previous=a->section.type;
            a->section.offset=a->at;a->section.pages=0;a->raw=0;a->raw_crc=a->stored_crc=~0u;
            if((r=visit_section(a)))return done(a,r);a->state=W_PREPARE;break;
        case W_PREPARE:
            if(a->raw==a->section.bytes){
                a->section.crc=~a->raw_crc;a->section.stored_crc=~a->stored_crc;a->section.stored=a->at-a->section.offset;
                mp_wire_encode_entry(a->entry,&a->section);a->directory_crc=mp_crc(a->directory_crc,a->entry,MP_ENTRY);a->state=W_ENTRY;break;
            }
            a->n=a->section.bytes-a->raw>MP_PAGE?MP_PAGE:(mp_u32)(a->section.bytes-a->raw);
            if(a->source->read(a->source->context,a->index,a->raw,w->raw,a->n))return done(a,MP_CALLBACK);
            if((r=visit_page(a)))return done(a,r);a->raw_crc=mp_crc(a->raw_crc,w->raw,a->n);
            sp_u64 encode_tick=sp_timer();
            /* Reserve the existing scratch buffer's leading24 bytes for the
             * wire header. Compressed payload bytes and codec choice stay the
             * same; an incompressible page still falls back to raw storage. */
            a->k=mp_pack(w->raw,a->n,w->packed+MP_PAGE_HEADER,sizeof w->packed-MP_PAGE_HEADER,w->dictionary);a->codec=a->k && a->k<a->n;
            sp_cost(SP_ENCODE,encode_tick);
            if(!a->codec)a->k=a->n;
            mp_write32(a->page_header,a->codec);mp_write32(a->page_header+4,a->n);mp_write32(a->page_header+8,a->k);
            mp_write32(a->page_header+12,mp_crc_full(w->raw,a->n));mp_write64(a->page_header+16,a->section.pages);
            a->stored_crc=mp_crc(a->stored_crc,a->page_header,MP_PAGE_HEADER);
            a->stored_crc=mp_crc(a->stored_crc,a->codec?w->packed+MP_PAGE_HEADER:w->raw,a->k);
            if(a->k<=MP_PAGE-MP_PAGE_HEADER){
                if(!a->codec)copy(w->packed+MP_PAGE_HEADER,w->raw,a->k);
                copy(w->packed,a->page_header,MP_PAGE_HEADER);a->state=W_PAGE_JOIN;
            }else a->state=W_PAGE_HEADER; /* Keep every physical write<=16KiB. */
            return 0; /* one encoded page per call */
        case W_PAGE_JOIN:
            /* Own scratch bytes unchanged through terminal acknowledgement.
             * No deferred writes: errors occur before this page advances.
             * Directory/header/footer writes and both syncs stay separate. */
            r=request(a,MP_AWRITE,a->at,w->packed,MP_PAGE_HEADER+a->k,a->io->limit);if(r<=0)return r<0?done(a,a->error):0;
            a->at+=MP_PAGE_HEADER+a->k;a->raw+=a->n;a->section.pages++;a->state=W_PREPARE;break;
        case W_PAGE_HEADER:
            r=request(a,MP_AWRITE,a->at,a->page_header,MP_PAGE_HEADER,a->io->limit);if(r<=0)return r<0?done(a,a->error):0;
            a->at+=MP_PAGE_HEADER;a->state=W_PAGE_DATA;break;
        case W_PAGE_DATA:
            r=request(a,MP_AWRITE,a->at,a->codec?w->packed+MP_PAGE_HEADER:w->raw,a->k,a->io->limit);if(r<=0)return r<0?done(a,a->error):0;
            a->at+=a->k;a->raw+=a->n;a->section.pages++;a->state=W_PREPARE;break;
        case W_ENTRY:
            r=request(a,MP_AWRITE,MP_HEADER+a->index*MP_ENTRY,a->entry,MP_ENTRY,a->io->limit);if(r<=0)return r<0?done(a,a->error):0;
            a->index++;a->state=W_SECTION;break;
        case W_TAIL:
            if(a->at<a->tail){
                a->n=a->tail-a->at>MP_PAGE?MP_PAGE:(mp_u32)(a->tail-a->at);
                r=request(a,MP_AWRITE,a->at,w->raw,a->n,a->io->limit);if(r<=0)return r<0?done(a,a->error):0;
                a->at+=a->n;return 0;
            }
            a->info.bytes=a->at+MP_FOOTER;mp_wire_make_header(a->header,&a->info,~a->directory_crc);
            mp_wire_make_footer(a->footer,a->header);a->state=W_HEADER;break;
        case W_HEADER:
            r=request(a,MP_AWRITE,0,a->header,MP_HEADER,a->io->limit);if(r<=0)return r<0?done(a,a->error):0;a->state=W_SYNC1;break;
        case W_SYNC1:case W_SYNC2:
            r=request(a,MP_ASYNC,0,0,0,a->io->limit);if(r<=0)return r<0?done(a,a->error):0;
            if(a->state==W_SYNC2)return done(a,MP_OK);a->state=W_FOOTER;break;
        case W_FOOTER:
            r=request(a,MP_AWRITE,a->at,a->footer,MP_FOOTER,a->io->limit);if(r<=0)return r<0?done(a,a->error):0;
            a->state=W_SYNC2;break;
        default:return done(a,MP_CALLBACK);
        }
        AS_PAD();
    }
    return 0;
}
