/* Resumable, bounded profile streams. One pending transport operation owns its
 * buffers until poll reports terminal completion. No filesystem/kernel calls. */
#ifndef MEGAMIX_PROFILE_ASYNC_H
#define MEGAMIX_PROFILE_ASYNC_H
#include "profile_schema.h"
enum {MP_AREAD,MP_AWRITE,MP_ASYNC};
typedef struct {
    void *context;
    /* start:0 accepted,1 busy,<0 rejected. poll:0 pending,1 terminal,<0 terminal
     * transport failure. result is exact bytes or zero for a completed sync.
     * A sync must establish the backend's acknowledged durable boundary. */
    int (*start)(void *,int,mp_u64,void *,mp_u32);
    int (*poll)(void *,int *);
    mp_u64 limit;
} mp_async_io;
typedef struct {
    const mp_async_io *io;
    const mp_source *source;
    const mp_visitor *sink;
    mp_work *work;
    mp_schema_state schema;
    mp_visitor validator;
    mp_info info;
    mp_section section;
    mp_u64 index,at,next,raw,page,capacity,tail;
    mp_u32 previous,directory_crc,raw_crc,stored_crc,n,k,codec;
    int state,pending,error,writing;
    mp_u8 header[MP_HEADER] __attribute__((aligned(64)));
    mp_u8 footer[MP_FOOTER] __attribute__((aligned(64)));
    mp_u8 entry[MP_ENTRY] __attribute__((aligned(64)));
    mp_u8 page_header[64] __attribute__((aligned(64)));
} mp_async;
int mp_async_read(mp_async *,const mp_async_io *,mp_u64,const mp_visitor *,mp_work *);
int mp_async_write(mp_async *,const mp_async_io *,const mp_info *,const mp_source *,mp_work *);
/* 0 busy,1 terminal. Check error==MP_OK before publishing info or staged data.
 * Each call does at most one page encode/decode and a bounded number of small
 * transitions. No busy polling loop, allocation, drawing or PLAY integration. */
int mp_async_step(mp_async *);
#endif
