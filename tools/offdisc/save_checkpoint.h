/* MEGAMIX checkpoint v1. Explicit little-endian wire format, no native pointers. */
#ifndef MEGAMIX_SAVE_CHECKPOINT_H
#define MEGAMIX_SAVE_CHECKPOINT_H

typedef unsigned char cp_u8;
typedef unsigned int cp_u32;
typedef int cp_i32;
#define CP_NATIVE_BYTES 833290u
#define CP_HEADER_BYTES 64u
#define CP_RECORD_BYTES 48u
#define CP_FOOTER_BYTES 24u
#define CP_MAX_RECORDS 65536u
#define CP_MAX_BYTES (CP_HEADER_BYTES+CP_NATIVE_BYTES+CP_RECORD_BYTES*CP_MAX_RECORDS+4096u)
#define CP_CHUNK 16384u
/* category 0: native modes0/1 SP; 1: mode2 DP; 2: mode3 SP battle.
 * Mode4 derived records and mode5 views are not additional earned categories.
 * Rule1 is EMPRESS's original record rules; native assist flags remain in data. */
typedef struct { cp_u8 id[16], category[2], rules[2], data[28]; } cp_record;
_Static_assert(sizeof(cp_record)==CP_RECORD_BYTES,"checkpoint record wire size");
typedef struct { cp_u32 lo,hi; } cp_generation;
typedef struct {
    cp_generation generation;
    cp_u32 records,capacity,payload_crc,header_crc;
} cp_meta;
cp_u32 cp_crc(cp_u32 running,const void *data,cp_u32 bytes);
cp_u32 cp_crc_full(const void *data,cp_u32 bytes);
cp_u32 cp_read32(const cp_u8 *p);
void cp_write32(cp_u8 *p,cp_u32 v);
int cp_record_compare(const cp_record *a,const cp_record *b);
int cp_record_valid(const cp_record *r);
void cp_record_normalize(cp_u8 out[28],const cp_u8 in[28]);
void cp_record_restore(cp_u8 out[28],const cp_u8 in[28]);
int cp_generation_compare(cp_generation a,cp_generation b);
int cp_generation_next(cp_generation in,cp_generation *out);
void cp_make_header(cp_u8 *header,const cp_meta *m);
/* 1 valid, 0 corrupt, -1 recognized but unsupported format. */
int cp_parse_header(const cp_u8 *header,cp_u32 file_bytes,cp_meta *m);
void cp_make_footer(cp_u8 *footer,const cp_meta *m);
int cp_check_footer(const cp_u8 *footer,const cp_meta *m);

/* Existing asynchronous libmc transport. The I/O buffers stay alive until poll
 * completes. start:0 accepted,1 busy/retry,<0 rejected. poll:0 pending,1 done,
 * <0 transport failure; operation completion is returned separately. */
enum {CP_STAT,CP_OPEN,CP_READ,CP_WRITE,CP_CLOSE,CP_SEEK,CP_INFO};
typedef struct { cp_i32 type,free_blocks,formatted; } cp_card_info;
typedef struct {
    int (*start)(int op,int port,const char *path,int fd,void *data,cp_u32 n,int arg);
    int (*poll)(int *result);
    void *(*alloc)(cp_u32 n);
    void (*release)(void *p);
} cp_ops;
typedef struct cp_transaction cp_transaction;
/* The native buffer is the coordinator-owned frozen aggregate, not live objects.
 * records belong to the caller and must remain immutable while the save runs. */
/* save=2 is a read-only unchanged-save probe: it can never create/write files. */
cp_transaction *cp_begin(const cp_ops *ops,int port,int save,int force,
                         cp_u8 *native,const cp_record *records,cp_u32 count);
/* 0 busy,1 complete. error:0 success,6 invalid,7 I/O,9 media,11 capacity,12 missing.
 * No live game object is mutated by this module. */
int cp_step(cp_transaction *t);
int cp_error(const cp_transaction *t);
int cp_recovered(const cp_transaction *t); /*0 none,1 previous checkpoint,2 native-only recovery*/
int cp_skipped(const cp_transaction *t);
cp_u32 cp_required_blocks(const cp_transaction *t);
cp_generation cp_current_generation(const cp_transaction *t);
cp_record *cp_take_records(cp_transaction *t,cp_u32 *count);
void cp_end(cp_transaction *t);
#endif
