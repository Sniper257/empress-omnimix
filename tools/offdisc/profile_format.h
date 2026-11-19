/* Portable compact-profile container. No heap, game IDs, file names or SDK ABI.
 * Backend callbacks must complete exactly; errors never imply a committed save.
 * See docs/PROFILE_FORMAT.md for the wire contract and recovery boundaries. */
#ifndef MEGAMIX_PROFILE_FORMAT_H
#define MEGAMIX_PROFILE_FORMAT_H
typedef unsigned char mp_u8;
typedef unsigned short mp_u16;
typedef unsigned int mp_u32;
typedef unsigned long long mp_u64;
_Static_assert(sizeof(mp_u32)==4 && sizeof(mp_u64)==8,"profile integer widths");
#define MP_PAGE 16384u
#define MP_HEADER 128u
#define MP_ENTRY 64u
#define MP_FOOTER 64u
#define MP_PAGE_HEADER 24u
#define MP_MANDATORY 1u
enum {MP_OK=0,MP_IO=-1,MP_CORRUPT=-2,MP_UNSUPPORTED=-3,MP_LIMIT=-4,MP_CALLBACK=-5};
enum {MP_METADATA=1,MP_CHARTS=2,MP_SONGS=3,MP_GRAPHS=4,MP_COURSES=5,
      MP_PROVENANCE=6,MP_NATIVE=0x10000,MP_V1_ENVELOPE=0x10001};
typedef struct {
    mp_u32 type,version,flags,stride;
    mp_u64 count,bytes,offset,stored;
    mp_u32 crc,stored_crc;
    mp_u64 pages;
} mp_section;
typedef struct {
    mp_u8 uuid[16];
    mp_u64 generation,sequence,bytes,sections;
} mp_info;
typedef struct {
    void *context;
    /* 0 success, nonzero failure. Offsets are checked before every callback. */
    int (*read)(void *,mp_u64,void *,mp_u32);
    int (*write)(void *,mp_u64,const void *,mp_u32);
    int (*sync)(void *);
    mp_u64 limit; /* Actual backend maximum; nonzero, never inferred from media. */
} mp_io;
typedef struct {
    void *context;
    int (*describe)(void *,mp_u64,mp_section *);
    int (*read)(void *,mp_u64,mp_u64,void *,mp_u32);
} mp_source;
typedef struct {
    void *context;
    /* Policy checks known mandatory section schemas. Optional unknown data
     * can be retained verbatim. NULL accepts structural data only. */
    int (*section)(void *,mp_u64,const mp_section *);
    /* Visitor data is UNCOMMITTED until the entire check returns MP_OK.
     * Never mutate live game state here: stage first, then publish. */
    int (*page)(void *,mp_u64,const mp_section *,mp_u64,const void *,mp_u32);
} mp_visitor;
typedef struct {
    mp_u8 raw[MP_PAGE],packed[MP_PAGE+MP_PAGE/255+32];
    mp_u16 dictionary[4096];
} mp_work;
mp_u32 mp_crc(mp_u32,const void *,mp_u32);
mp_u32 mp_crc_full(const void *,mp_u32);
mp_u32 mp_read32(const mp_u8 *);
mp_u64 mp_read64(const mp_u8 *);
void mp_write32(mp_u8 *,mp_u32);
void mp_write64(mp_u8 *,mp_u64);
/* Independent LZ4 blocks, bounded to MP_PAGE. Encoder returns 0 on no space;
 * caller uses a raw page when compression would expand it. */
mp_u32 mp_pack(const mp_u8 *,mp_u32,mp_u8 *,mp_u32,mp_u16 *);
int mp_unpack(const mp_u8 *,mp_u32,mp_u8 *,mp_u32);
int mp_check(const mp_io *,mp_u64,mp_info *,const mp_visitor *,mp_work *);
/* Caller owns a fresh/inactive file. The active generation is never passed as
 * output. in.bytes may retain the inactive file's larger allocation; the
 * unused tail is zeroed/verified, so MC transports need no truncate/rename.
 * Footer is published after a successful durable-payload boundary.
 * Success also requires footer sync; caller must then close and read back. */
int mp_write(const mp_io *,const mp_info *,const mp_source *,mp_info *,mp_work *);
#endif
