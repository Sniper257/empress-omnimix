/* Large imported animations: native workspace sizing and fragmented-heap load.
 * Startup/loading only. Rendering, animation tracks and streaming are native. */
typedef struct { u32 count; u32 *trim; u32 split_loads, failures, failed_bytes;
    u32 stream_loads, trimmed_bytes;
    void *movie_work;u32 movie_capacity,movie_busy;
    void *texture_work;u32 texture_capacity,texture_busy; } asset_memory_t;
asset_memory_t asset_memory;
_Static_assert(__builtin_offsetof(asset_memory_t,movie_work)==28 &&
               __builtin_offsetof(asset_memory_t,texture_work)==40 && sizeof(asset_memory_t)==52,
               "launcher asset_stream3 ABI");

#ifndef ASSET_MEMORY_TEST
#define asset_alloc malloc_
#define asset_free free_
#define asset_input ((u8 *(*)(void *))0x14cf60)
#ifdef LOAD_PROFILE
static void asset_decode_profile(void *out,const void *in,u32 n)
{
    unsigned long long span=lp_begin();
    ((void (*)(void *,const void *,u32))0x1361e0)(out,in,n);
    lp_end(LP_DECODE,span,n,0,0);
}
#define asset_decode asset_decode_profile
#else
#define asset_decode ((void (*)(void *,const void *,u32))0x1361e0)
#endif
#define asset_register ((void (*)(u32 *))0x135860)
#define asset_song ((s32 (*)(void))0x205e00)
#define asset_native_workspace ((void (*)(s32))0x13a420)
#define asset_native_trim ((void (*)(u32))0x13a3e0)
#define ASSET_ENTRIES ((u8 *)0x34aa80)
#define ASSET_ENTRY_COUNT (*(u32 *)0x2ad030)
#endif
#ifndef ASSET_ENTRIES
#define ASSET_ENTRIES ((u8 *)0)
#define ASSET_ENTRY_COUNT 0
#endif

/* The primary metadata allocation owns this sidecar; native unload passes that
 * same primary pointer. Texture pointers in the native entry need not adjoin it. */
typedef struct asset_parts {
    struct asset_parts *next;
    void *base, *sheet[32];
    u32 n;
} asset_parts_t;
static asset_parts_t *asset_parts;
static asset_parts_t *asset_texture_owner;
static u32 asset_workspace_trimmed;

#ifdef ASSET_SPLIT_TEXTURE
/* Only one original sheet borrows the otherwise unusable reserved tail.
 * Its palette and first pixels stay together; the remaining original bytes
 * live in an ordinary allocation. Native metadata and sheet identity do not
 * change. All state outlives DMA and is cleared by that container's unload. */
static struct {
    asset_parts_t *owner;
    u8 *first,*second;
    u32 cut,bytes;
    u32 chain[12] __attribute__((aligned(16)));
} asset_split;
static void asset_split_clear(asset_parts_t *p)
{
    if(asset_split.owner!=p)return;
    asset_free(asset_split.second);
    asset_split.owner=0;asset_split.first=asset_split.second=0;
    asset_split.cut=asset_split.bytes=0;
}

/* The original uploader's two possible REF positions differ only when its
 * native VRAM allocator emits an initial reset tag. Redirect the IMAGE REF
 * through two REF tags and NEXT back to its original successor. This uses no
 * CALL stack, preserves the complete GIF IMAGE payload, and leaves CLUT and
 * every other native packet untouched. Called BEFORE the original cache flush
 * and native synchronous DMA wait/send/wait sequence. */
void asset_texture_flush(u32 mode)
{
    if(asset_split.owner){
        u32 *packet=(u32 *)0x38dcd0;
        for(u32 i=7;i<=8;i++){
            u32 *tag=packet+i*4,qwc=(asset_split.bytes-1024)>>4;
            if(tag[0]!=(0x30000000|qwc) || tag[1]!=(u32)(asset_split.first+1024) || tag[2] || tag[3]!=(0x50000000|qwc))continue;
            u32 *c=asset_split.chain,a=(asset_split.cut-1024)>>4,b=(asset_split.bytes-asset_split.cut)>>4;
            c[0]=0x30000000|a;c[1]=(u32)(asset_split.first+1024);c[2]=0;c[3]=0x50000000|a;
            c[4]=0x30000000|b;c[5]=(u32)asset_split.second;c[6]=0;c[7]=0x50000000|b;
            c[8]=0x20000000;c[9]=(u32)(tag+4);c[10]=c[11]=0;
            tag[0]=0x20000000;tag[1]=(u32)c;tag[2]=tag[3]=0;
            break;
        }
    }
#ifndef ASSET_MEMORY_TEST
    ((void (*)(u32))0x10eee0)(mode);
#else
    (void)mode;
#endif
}
#endif

#ifdef ASSET_FRAME_ORDER
/* One producer (the native asset worker), followed by the native queue-idle
 * barrier and one consumer (MAIN's MPEG constructor). Reserve the original
 * two240x240 frames before large texture sheets fragment the remaining heap.
 * The pointers are independent native allocations; handing one out transfers
 * ownership permanently to the original MPEG destructor. */
static void *asset_frames[2];
static void *asset_back_frames[2];
static u32 asset_frame_bytes,asset_frame_attempted;
/* MAIN sets geometry/eligibility before queue start; worker sets preparation
 * bits; MAIN consumes only after native queue idle. No shared hot-path cursor. */
static volatile u32 asset_frame_trace[10]; /* geometry, route bits, consumed;
 * pre-reserve free/largest, required texture bytes; failed FRONT/BACK free/largest */
#ifndef ASSET_MEMORY_TEST
#include "asset_heap.h"
#endif
#ifdef ASSET_GAP_TRACE
#include "asset_gap_trace.h"
#endif
static void asset_frame_sample(u32 at)
{
    u32 total,largest;
#ifdef ASSET_GAP_TRACE
    asset_gap_snapshot *s=&asset_gaps[at==3?0:at==6?1:2];
    s->ready=0;s->frames=!!asset_frames[0]+!!asset_frames[1];
    s->status=asset_heap_walk(&s->total,&s->largest,s->gap,ASSET_GAP_CAP,&s->count);
    total=s->total;largest=s->largest;
    __asm__ volatile("":::"memory");s->ready=1;
#else
    asset_heap_sample(&total,&largest);
#endif
    asset_frame_trace[at]=total;asset_frame_trace[at+1]=largest;
}
#ifndef ASSET_MEMORY_TEST
#define asset_dimensions ((void (*)(u32 *,u32 *))0x1e2df0)
#define asset_frame_back_alloc ((void *(*)(u32))0x100320)
#define asset_frame_back_free ((void (*)(void *))0x1002f0)
#endif
void asset_movie_frame_free(void *p)
{
    /* Native BACK nodes are kind4. The ordinary FRONT free searches kind3;
     * pair the original typed APIs, including pointers already handed out. */
    for(u32 i=0;i<2;i++)if(p && p==asset_back_frames[i]){
        asset_back_frames[i]=0;asset_frame_back_free(p);return;
    }
    asset_free(p);
}
static void asset_frames_cancel(void)
{
    for(u32 i=0;i<2;i++)if(asset_frames[i]){asset_movie_frame_free(asset_frames[i]);asset_frames[i]=0;}
}
static void asset_frames_reserve(u32 back)
{
    asset_frame_attempted=back?2:1;asset_frame_trace[1]|=back?16:2;
    for(u32 i=0;i<2;i++){
        asset_frames[i]=back?asset_frame_back_alloc(asset_frame_bytes):asset_alloc(asset_frame_bytes);
        if(back)asset_back_frames[i]=asset_frames[i];
        if(!asset_frames[i]){asset_frame_trace[1]|=128;asset_frames_cancel();return;}
    }
    asset_frame_trace[1]|=back?32:4;
}
static void asset_frames_prepare(void)
{
    if(!asset_frame_bytes || asset_frame_attempted)return;
#ifdef ASSET_GAP_TRACE
    if(!asset_gaps[0].ready)asset_frame_sample(3);
#else
    asset_frame_sample(3);
#endif
    asset_frames_reserve(0);
    if(!asset_frames[0])asset_frames_reserve(1);
}
void *asset_movie_frame_alloc(u32 bytes)
{
    if(bytes==asset_frame_bytes && asset_frames[0]){
        void *p=asset_frames[0];asset_frames[0]=asset_frames[1];asset_frames[1]=0;asset_frame_trace[2]++;return p;
    }
    /* A different decoder geometry cannot consume an old reservation. */
    if(asset_frames[0])asset_frame_trace[1]|=256;
    asset_frames_cancel();return asset_alloc(bytes);
}
#endif

/* Use only a launcher-proven unused TABLE tail, distinct from paths, the input
 * log and MPEG work. One container owns it until native unload. Pick whole
 * original sheets; no pixel/track changes and no gameplay allocation. The
 * bounded subset search skips equivalent equal-size exclusions. */
typedef struct {const u32 *size,*order;u32 n,budget,best,mask;} asset_subset_t;
static void asset_subset(asset_subset_t *s,u32 at,u32 left,u32 used,u32 mask)
{
    if(used>s->best){s->best=used;s->mask=mask;}
    if(at==s->n || !left || !s->budget)return;
    s->budget--;
    u32 i=s->order[at],n=(s->size[i]+127)&~127u,j=at+1;
    if(n && n<=left)asset_subset(s,at+1,left-n,used+n,mask|(1u<<i));
    while(j<s->n && ((s->size[s->order[j]]+127)&~127u)==n){j++;LOOP_PAD();}
    asset_subset(s,j,left,used,mask);
}

static void asset_sheet_free(void *p)
{
    u32 a=(u32)p,lo=(u32)asset_memory.texture_work;
    if(p && asset_texture_owner && a>=lo && a-lo<asset_memory.texture_capacity)return;
    asset_free(p);
}

/* The native allocator takes the first fitting free gap. Decreasing order
 * alone can strand a short sheet between smaller leftovers, even with enough
 * total free memory. Retry bounded, deterministic orders before decoding:
 * a decreasing prefix, then the remaining sheets in increasing order. Never
 * change the source sheet indices or retain an allocation from a failed try.
 * At most n orders / n*n allocations, on the loading thread only. This is
 * not a promise to fit every heap; real exhaustion still takes the safe path. */
static int asset_sheets_alloc(asset_parts_t *parts,const u32 *keep,u32 n,const u32 *order)
{
    u32 attempt,i,j,prefix,mask=0,offset;
    if(!asset_texture_owner && asset_memory.texture_work && asset_memory.texture_capacity) {
        asset_subset_t s={keep,order,n,8192,0,0};
        asset_subset(&s,0,asset_memory.texture_capacity,0,0);mask=s.mask;
        if(mask){asset_texture_owner=parts;asset_memory.texture_busy=1;}
    }
#ifdef ASSET_FRAME_ORDER
#ifdef ASSET_GAP_TRACE
    if(!asset_frame_trace[5]){
#else
    if(asset_frame_bytes && !asset_frame_trace[5]){
#endif
        u32 required=0;
        for(i=0;i<n;i++){if(!(mask&(1u<<i)))required+=(keep[i]+127)&~127u;LOOP_PAD();}
        asset_frame_trace[5]=required;
    }
#endif
    for(attempt=0;attempt<n;attempt++) {
        prefix=attempt?attempt-1:n;
        offset=0;
        for(i=0;i<n;i++)if(mask&(1u<<i)) {
            parts->sheet[i]=(u8 *)asset_memory.texture_work+offset;
            offset+=(keep[i]+127)&~127u;
        }
        for(i=0;i<n;i++) {
            j=order[i<prefix?i:n-1-(i-prefix)];
            if(!keep[j] || (mask&(1u<<j)))continue;
            parts->sheet[j]=asset_alloc(keep[j]);
            if(!parts->sheet[j])break;
            LOOP_PAD();
        }
        if(i==n)return 1;
        for(j=0;j<n;j++) {
            if(parts->sheet[j])asset_sheet_free(parts->sheet[j]);
            parts->sheet[j]=0;LOOP_PAD();
        }
    }
    if(asset_texture_owner==parts){asset_texture_owner=0;asset_memory.texture_busy=0;}
#ifdef ASSET_SPLIT_TEXTURE
    /* Try a two-piece original sheet only after all whole-sheet orders fail.
     * The second allocation can occupy a short native hole which cannot hold
     * any original whole sheet. Bounded n candidates/n orders; every failed
     * candidate returns ALL its allocations before another plan is tried. */
    u32 cut=asset_memory.texture_capacity&~127u;
    if(!asset_texture_owner && !asset_split.owner && asset_memory.texture_work && cut>1024){
        for(u32 k=n;k;k--){
            u32 split=order[k-1],bytes=keep[split];
            u32 meta_len=parts->base?(u32)((u8 *)parts-(u8 *)parts->base):0;
            if(bytes<=cut || (bytes&15) || ((bytes-1024)>>4)>=32768 ||
               !parts->base || meta_len<16+n*4 || *(u16 *)((u8 *)parts->base+2)<=split)continue;
            u32 w=*(u16 *)((u8 *)parts->base+16+split*4),h=*(u16 *)((u8 *)parts->base+18+split*4);
            if(!w||!h||(w&127)||(h&63)||bytes!=1024+w*h)continue;
            for(attempt=0;attempt<n;attempt++){
                void *tail=asset_alloc(bytes-cut);
                if(!tail)break;
                asset_texture_owner=parts;asset_memory.texture_busy=1;
                parts->sheet[split]=asset_memory.texture_work;
                prefix=attempt?attempt-1:n;
                for(i=0;i<n;i++){
                    j=order[i<prefix?i:n-1-(i-prefix)];
                    if(j==split || !keep[j])continue;
                    parts->sheet[j]=asset_alloc(keep[j]);
                    if(!parts->sheet[j])break;
                    LOOP_PAD();
                }
                if(i==n){
                    asset_split.owner=parts;asset_split.first=parts->sheet[split];
                    asset_split.second=tail;asset_split.cut=cut;asset_split.bytes=bytes;
                    return 1;
                }
                for(j=0;j<n;j++){asset_sheet_free(parts->sheet[j]);parts->sheet[j]=0;LOOP_PAD();}
                asset_free(tail);asset_texture_owner=0;asset_memory.texture_busy=0;
            }
            LOOP_PAD();
        }
    }
#endif
#ifdef ASSET_FRAME_ORDER
    /* If reserving movie buffers prevents a texture plan from fitting, return
     * them and retry the original bounded planner. Never drop BGA sheets to
     * force a successful movie, or retain allocations from an unsuccessful try. */
    if(asset_frames[0]){
        u32 back=asset_frame_attempted==1;
        asset_frame_trace[1]|=back?8:64;
        asset_frame_sample(back?6:8);
        asset_frames_cancel();
        /* Before surrendering the pair, try the same native heap from its
         * BACK end. Texture sheets retain their FRONT allocations. At most
         * front/back/legacy plans; every failed try releases all its owners. */
        if(back)asset_frames_reserve(1);
        return asset_sheets_alloc(parts,keep,n,order);
    }
#endif
#ifdef ASSET_GAP_TRACE
    /* Other geometries and failed reservations still get a gap map. The
     * explicit held-frame count prevents confusing this with a reserved pair. */
    if(n && !asset_gaps[2].ready)asset_frame_sample(8);
#endif
    return n==0;
}

static void asset_failed_entry(u32 *entry)
{
    entry[0]=entry[1]=entry[2]=entry[3]=0;
    /* Container slots survive scene changes; these may still address a freed
     * previous song. The renderer/length guards reject this unloaded entry. */
    entry[0x85]=entry[0x86]=0;
}

/* The native MPEG constructor needs work + TWO RGBA frame buffers. Borrow
 * unused reserved-block tail for work only; frame buffers and every decoder,
 * DMA, scheduling and movie-stream operation remain native. The launcher
 * publishes this tail only after all tables and strings are final. */
void *asset_movie_alloc(u32 bytes)
{
    if(bytes && asset_memory.movie_work && bytes<=asset_memory.movie_capacity && !asset_memory.movie_busy) {
        asset_memory.movie_busy=1;return asset_memory.movie_work;
    }
    return asset_alloc(bytes);
}

void asset_movie_free(void *base)
{
    if(base && base==asset_memory.movie_work) {
        asset_memory.movie_busy=0;return;
    }
    asset_free(base);
}

void asset_workspace(s32 big)
{
#ifdef ASSET_FRAME_ORDER
    /* Native completed-exit/queue barriers precede every new chart. Only
     * unconsumed pointers belong to us; never free a handed-out movie frame. */
    asset_frames_cancel();asset_frame_attempted=asset_frame_bytes=0;
#ifdef ASSET_GAP_TRACE
    for(u32 i=0;i<3;i++){asset_gaps[i].ready=0;LOOP_PAD();}
#endif
    for(u32 i=0;i<10;i++){asset_frame_trace[i]=0;LOOP_PAD();}
    u32 w=0,h=0;asset_dimensions(&w,&h);
    asset_frame_trace[0]=(w<<16)|(h&65535);
    /* Leave480x480 geometry/work allocation unchanged. The240 decoder work is
     * already guaranteed by the launcher's separate borrowed work area. */
    if(w==240 && h==240 && asset_memory.movie_work && asset_memory.movie_capacity>=0x41c80 && !asset_back_frames[0] && !asset_back_frames[1]){
        asset_frame_bytes=w*h*4;asset_frame_trace[1]=1;
    }
#endif
#ifdef SPR_ASYNC
    sa_retire(); /* before native PLAY workspace growth */
#endif
    s32 song=asset_song();
    s32 row=song_catalog_row(song);
    asset_workspace_trimmed=0;
    if (row>=0 && asset_memory.trim && (u32)row<asset_memory.count) {
        u32 plan=asset_memory.trim[row];
        if(big && (plan&ASSET_WORKSPACE_ABSOLUTE)) {
            u32 bytes=plan&ASSET_WORKSPACE_MASK;
            if(bytes>=ASSET_WORKSPACE_FLOOR && bytes<0x2000000u && !(bytes&0x7ff)) {
                /* Original13a3e0 frees/reallocates708000-minus-signed-delta.
                 * A negative delta grows beyond the ordinary floor. Keep the
                 * entire largest referenced WVB read, not the fixed D08000
                 * maximum. No bank, sample-cache or stream policy changes. */
                asset_native_trim(ASSET_WORKSPACE_FLOOR-bytes);return;
            }
        } else if(!big && plan && !(plan&ASSET_WORKSPACE_ABSOLUTE)) {
            asset_workspace_trimmed=1;asset_native_trim(plan);return;
        }
    }
    asset_native_workspace(big);
}

void asset_release(void *base)
{
    asset_parts_t **link=&asset_parts,*parts;
    u32 i;
    while ((parts=*link)) {
        if (parts->base==base) {
            *link=parts->next;
#ifdef ASSET_SPLIT_TEXTURE
            asset_split_clear(parts);
#endif
            /* Repeated immutable sheets retain distinct native indices but share one
             * allocation. Compare identities before freeing; the sidecar is live
             * throughout, including failures part-way through decode. */
            for(i=0;i<parts->n;i++) {
                u32 j;for(j=0;j<i && parts->sheet[j]!=parts->sheet[i];j++){LOOP_PAD();}
                if(j==i)asset_sheet_free(parts->sheet[i]);LOOP_PAD();
            }
            if(asset_texture_owner==parts){asset_texture_owner=0;asset_memory.texture_busy=0;}
            break; /* sidecar belongs to base: never access it after freeing base */
        }
        link=&parts->next;LOOP_PAD();
    }
    asset_free(base);
}

/* These three native calls are AFTER the movie and song-BGA object have been
 * destroyed, at completed play/preview/training exit. Native code grows its
 * audio workspace here, BEFORE the later all-container unload. A large import
 * can occupy the space needed for that growth. Retire our owned animation
 * allocations now, and clear their native slots so the later unload cannot
 * free them twice. Ordinary/native containers retain their original lifetime.
 * The original restore then runs, and its caller publishes the new audio base.
 * No gameplay, result writer, audio reader or input path is changed. */
void asset_workspace_exit(s32 big)
{
#ifdef ASSET_FRAME_ORDER
    asset_frames_cancel();asset_frame_bytes=0;
#endif
#ifdef EXIT_TRACE
    extern void exit_trace_mark(u32);exit_trace_mark(0x14);
#endif
    u32 i;asset_parts_t *parts;
    if(!big && asset_workspace_trimmed) {
        for(i=0;i<ASSET_ENTRY_COUNT;i++) {
            u32 *entry=(u32 *)(ASSET_ENTRIES+i*0x228);
            if(!entry[0])continue;
            for(parts=asset_parts;parts;parts=parts->next) {
                if(parts->base==(void *)entry[0]) {
                    asset_release(parts->base);asset_failed_entry(entry);break;
                }
                LOOP_PAD();
            }
            LOOP_PAD();
        }
        asset_workspace_trimmed=0;
    }
    asset_native_workspace(big);
#ifndef ASSET_MEMORY_TEST
    /* All three callers have retired the movie/BGA here. Keep the selected
     * wheel title's raw bytes while the reader is idle, before RESULT starts
     * asynchronous sound-bank loading. Other titles retain normal LRU behavior. */
    spr_prepare_result(asset_song());
#endif
#ifdef EXIT_TRACE
    exit_trace_mark(0x15);
#endif
}

void asset_unpack(void *request,u32 *entry)
{
    const u8 *input=asset_input(request),*packed;
    const u32 *sizes=(const u32 *)(input+8);
    u32 n=*(const u16 *)(input+2),total,meta=sizes[0],i,offset;
    u8 *base,*sheet;
    asset_parts_t *parts=0;
    if(n>32 || !meta || meta>0x2000000u)goto failed;
    total=meta;
    for(i=0;i<n;i++) {
        if(sizes[2+i*2]>0x2000000u-total)goto failed;
        total+=sizes[2+i*2];LOOP_PAD();
    }
    base=asset_alloc(total);
    if(!base) {
        offset=(meta+15)&~15u;
        base=asset_alloc(offset+sizeof(asset_parts_t));
        if(!base)goto failed;
        parts=(asset_parts_t *)(base+offset);
        parts->base=base;parts->n=0;parts->next=asset_parts;
        asset_parts=parts;
        for(i=0;i<n;i++) {
            sheet=asset_alloc(sizes[2+i*2]);
            if(!sheet) {asset_release(base);goto failed;}
            parts->sheet[parts->n++]=sheet;LOOP_PAD();
        }
        asset_memory.split_loads++;
    }
    packed=input+16+n*8;
    asset_decode(base,packed,meta);
    entry[0]=(u32)base;
    asset_register(entry); /* original sprite/animation/track pointer relocation */
    packed+=sizes[1];offset=meta;
    for(i=0;i<n;i++) {
        sheet=parts?(u8 *)parts->sheet[i]:base+offset;
        asset_decode(sheet,packed,sizes[2+i*2]);
        entry[5+i*4]=(u32)(sheet+0x400);
        entry[6+i*4]=entry[7+i*4]=(u32)sheet;
        packed+=sizes[3+i*2];offset+=sizes[2+i*2];LOOP_PAD();
    }
    return;
failed:
    /* Never let the native LZ writer scribble over address zero on exhaustion.
     * Leave diagnostic state; no partial allocation survives this callback. */
    asset_memory.failures++;asset_memory.failed_bytes=meta;
    asset_failed_entry(entry);
}

/* Only the CD thread uses this bounded reader. Keep its buffers off that
 * thread's 16 KiB stack. The chart's movies/audio keep their native readers. */
extern s32 vd_pread(u32 lba,u32 off,u32 n,void *buf);
static struct {
    /* Graphics-only staging, already used by the input-log build. Keep the
     * quiet resident no larger after adding exact sheet sharing/early HUD.
     * This does not change native audio/movie read windows or LZ's4KiB ring. */
#ifdef PREVIEW_TRACE
    u8 input[6144];
#else
    u8 input[10240];
#endif
    u8 ring[4096];
    u32 lba,offset,left,pos,end;
    int error;
} asset_reader;

static u32 asset_byte(void)
{
    u32 n;
    if(asset_reader.error)return 0;
    if(asset_reader.pos==asset_reader.end) {
        n=asset_reader.left;
        if(n>sizeof asset_reader.input)n=sizeof asset_reader.input;
        if(!n || vd_pread(asset_reader.lba,asset_reader.offset,n,asset_reader.input)!=(s32)n) {
            asset_reader.error=1;return 0;
        }
        asset_reader.offset+=n;asset_reader.left-=n;
        asset_reader.pos=0;asset_reader.end=n;
    }
    return asset_reader.input[asset_reader.pos++];
}

static void asset_stream_put(u8 *out,u32 at,u8 v)
{
#ifdef ASSET_SPLIT_TEXTURE
    if(asset_split.owner && out==asset_split.first && at>=asset_split.cut){
        asset_split.second[at-asset_split.cut]=v;return;
    }
#endif
    out[at]=v;
}

/* Same zero-initialised 4 KiB LZ ring as 1361e0. Decode every original byte;
 * only an unused, complete GS-page suffix may be discarded from the output. */
#ifdef LOAD_PROFILE
#define asset_stream_chunk asset_stream_chunk_body
#endif
static int asset_stream_chunk(u32 lba,u32 off,u32 packed,u8 *out,u32 raw,u32 keep)
{
    u32 i,k=0,w=0xfee,flags=0,n,a,v;
    asset_reader.lba=lba;asset_reader.offset=off;asset_reader.left=packed;
    asset_reader.pos=asset_reader.end=0;asset_reader.error=0;
    for(i=0;i<4096;i++){asset_reader.ring[i]=0;LOOP_PAD();}
    while(k<raw) {
        flags>>=1;
        if(!(flags&256))flags=asset_byte()|0xff00;
        if(flags&1) {
            v=asset_byte();
            if(k<keep)asset_stream_put(out,k,(u8)v);
            k++;asset_reader.ring[w]=(u8)v;w=(w+1)&4095;
        } else {
            a=asset_byte();v=asset_byte();a|=(v&240)<<4;n=(v&15)+3;
            if(n>raw-k)return 0;
            for(i=0;i<n;i++) {
                v=asset_reader.ring[(a+i)&4095];
                if(k<keep)asset_stream_put(out,k,(u8)v);
                k++;asset_reader.ring[w]=(u8)v;w=(w+1)&4095;LOOP_PAD();
            }
        }
        if(asset_reader.error)return 0;
        LOOP_PAD();
    }
    return 1;
}

#ifdef LOAD_PROFILE
#undef asset_stream_chunk
static int asset_stream_chunk(u32 lba,u32 off,u32 packed,u8 *out,u32 raw,u32 keep){unsigned long long span=lp_begin();int r=asset_stream_chunk_body(lba,off,packed,out,raw,keep);lp_end(LP_STREAM,span,raw,lba,!r);return r;}
#endif

/* Row stride stays identical. A 128x64-aligned PSMT8 height reduction is a
 * prefix of the game's PSMCT32 upload, so no re-swizzle or sprite move occurs.
 * Inspect ALL sprite rects, retain a sampling border, and keep unreferenced or
 * nonstandard sheets unchanged. Native sprite/track indices stay identical. */
static u32 asset_sheet_keep(u8 *meta,u32 meta_len,u32 sheet,u32 raw)
{
    u32 i,count=*(u16 *)(meta+6),rect=*(u32 *)(meta+8),sheets=*(u16 *)(meta+2);
    u32 w,h,y,bottom=0,nh;
    if(sheet>=sheets || 16+sheets*4>meta_len || rect>meta_len || count>(meta_len-rect)/8)return raw;
    w=*(u16 *)(meta+16+sheet*4);h=*(u16 *)(meta+18+sheet*4);
    if(!w || !h || (w&127) || (h&63) || raw!=1024+w*h)return raw;
    for(i=0;i<count;i++) {
        const u16 *r=(const u16 *)(meta+rect+i*8);
        y=r[1]&1023;
        if(r[1]/1024!=sheet || !r[2] || !r[3])continue;
        if((u32)r[0]+r[2]>w || y+r[3]>h)return raw;
        if(y+r[3]>bottom)bottom=y+r[3];
        LOOP_PAD();
    }
    if(!bottom)return raw;
    nh=(bottom+1+63)&~63u;
    if(nh>=h)return raw;
    *(u16 *)(meta+18+sheet*4)=(u16)nh;
    return 1024+w*nh;
}

/* Exact packed equality implies exact decoded palette/pixels because each
 * native LZ chunk starts with its own zeroed dictionary. Reuse the idle graphics
 * reader window: no extra resident buffer, heap allocation or PLAY I/O. A read
 * error fails the owning load, never publishes an unverified shared sheet. */
static int asset_same_chunk(u32 lba,u32 a,u32 b,u32 bytes)
{
    u32 half=sizeof asset_reader.input/2;
    while(bytes){
        u32 n=bytes<half?bytes:half;
        if(vd_pread(lba,a,n,asset_reader.input)!=(s32)n ||
           vd_pread(lba,b,n,asset_reader.input+half)!=(s32)n)return -1;
        for(u32 i=0;i<n;i++){if(asset_reader.input[i]!=asset_reader.input[half+i])return 0;LOOP_PAD();}
        a+=n;b+=n;bytes-=n;LOOP_PAD();
    }
    return 1;
}

/* Called at the native CD queue's yield boundary. Return1 consumes this one
 * request; return0 leaves the entire original read/callback path in control.
 * Imported normal large animations alone bypass the whole-packed-file buffer.
 * This also handles a packed file larger than 708000, which native a590 waits
 * forever to allocate. No source-name, chart, frame or difficulty exceptions. */
int asset_queue(u32 *q)
{
#ifdef SPR_ASYNC
    if(sa_worker(q))return 1;
#endif
#ifdef ASSET_PREVIEW_QUEUE
    if(ASSET_PREVIEW_QUEUE(q))return 1;
#endif
    u32 head[68],*sizes=head+2,n,i,j,total,meta,offset,keep[32],order[32],alias[32],chunk[32],saved=0;
    u32 *entry=(u32 *)q[1];u8 *base;
    asset_parts_t *parts;
    if(q[0]!=0x1359d0 || q[2] || !entry || entry[0x84]<463)return 0;
    if(vd_pread(q[4],0,8,head)!=8)return 0;
    n=((u16 *)head)[1];
    if(n>32 || (head[1]&65535) || ((u16 *)head)[0]!=16+n*8)return 0;
    if(vd_pread(q[4],8,8*(n+1),sizes)!=(s32)(8*(n+1)))return 0;
    meta=total=sizes[0];
    if(meta<16 || meta>0x2000000u)return 0;
    for(i=0;i<n;i++) {
        if(sizes[2+i*2]>0x2000000u-total)return 0;
        total+=sizes[2+i*2];LOOP_PAD();
    }
    if(total<=0x700000)return 0;
    offset=(meta+15)&~15u;
    base=asset_alloc(offset+sizeof(asset_parts_t));
    if(!base)goto failed_queue;
    parts=(asset_parts_t *)(base+offset);
    parts->base=base;parts->n=n;parts->next=asset_parts;asset_parts=parts;
    for(i=0;i<n;i++){parts->sheet[i]=0;LOOP_PAD();}
    offset=16+n*8;
    if(!asset_stream_chunk(q[4],offset,sizes[1],base,meta,meta))goto failed_parts;
    for(i=0;i<n;i++) {
        keep[i]=asset_sheet_keep(base,meta,i,sizes[2+i*2]);
        saved+=sizes[2+i*2]-keep[i];order[i]=i;LOOP_PAD();
    }
    /* Share only fully verified equal immutable sheets with identical native
     * geometry, retained prefix and independent compressed bytes. Do not merge
     * merely matching hashes, palettes, sprite appearances or source IDs.
     * Keep all native sheet/track indices and metadata unchanged. */
    u32 end=16+n*8,bytes=q[5]<=0x100000u?q[5]<<11:0;
    if(sizes[1]>bytes || end>bytes-sizes[1])goto failed_parts;
    end+=sizes[1];
    for(i=0;i<n;i++){
        u32 packed=sizes[3+i*2];alias[i]=i;chunk[i]=end;
        if(!packed || packed>bytes || end>bytes-packed)goto failed_parts;
        end+=packed;
        if(16+n*4>meta || *(u16 *)(base+2)!=n)continue;
        for(j=0;j<i;j++){
            if(alias[j]!=j || !keep[j] || keep[j]!=keep[i] ||
               sizes[2+j*2]!=sizes[2+i*2] || sizes[3+j*2]!=packed ||
               *(u32 *)(base+16+j*4)!=*(u32 *)(base+16+i*4))continue;
            int same=asset_same_chunk(q[4],chunk[j],chunk[i],packed);
            if(same<0)goto failed_parts;
            if(same){alias[i]=j;saved+=keep[i];keep[i]=0;break;}
            LOOP_PAD();
        }
        LOOP_PAD();
    }
    /* Try large first, then alternate bounded orders if gaps strand a sheet. */
    for(i=1;i<n;i++)for(j=i;j && keep[order[j]]>keep[order[j-1]];j--) {
        u32 t=order[j];order[j]=order[j-1];order[j-1]=t;LOOP_PAD();
    }
#ifdef ASSET_GAP_TRACE
    if(!asset_gaps[0].ready)asset_frame_sample(3);
#endif
    #ifdef ASSET_FRAME_ORDER
    asset_frames_prepare();
    #endif
    if(!asset_sheets_alloc(parts,keep,n,order))goto failed_parts;
    for(i=0;i<n;i++){
        if(alias[i]!=i)parts->sheet[i]=parts->sheet[alias[i]];
        else if(!asset_stream_chunk(q[4],chunk[i],sizes[3+i*2],parts->sheet[i],sizes[2+i*2],keep[i]))goto failed_parts;
        LOOP_PAD();
    }
    entry[0]=(u32)base;asset_register(entry);
    for(i=0;i<n;i++) {
        entry[5+i*4]=(u32)parts->sheet[i]+1024;
        entry[6+i*4]=entry[7+i*4]=(u32)parts->sheet[i];LOOP_PAD();
    }
    asset_memory.stream_loads++;asset_memory.trimmed_bytes+=saved;
    return 1;
failed_parts:asset_release(base);
#ifdef ASSET_FRAME_ORDER
    asset_frame_trace[1]|=512;
    asset_frames_cancel();
#endif
failed_queue:
    asset_memory.failures++;asset_memory.failed_bytes=meta;
    asset_failed_entry(entry);
    return 1;
}

/* Checked, private staged decode. Resident so UI scratch does not grow with
 * the supported32-sheet custom banks. Caller owns publication/dequeue. */
static __attribute__((noinline)) int asset_preview_decode(u32 *q,u32 *staged){
 u32 h[68],n,total,offset,meta;u8 *base=0;u32 bytes=q[5]<<11;
 if(q[2]||!q[5]||q[5]>1024||vd_pread(q[4],0,8,h)!=8)goto failed;
 n=h[0]>>16;if(!n||n>32||(h[0]&65535)!=16+n*8||h[1])goto failed;
 if(vd_pread(q[4],8,8*(n+1),h+2)!=(s32)(8*(n+1)))goto failed;
 meta=total=h[2];offset=16+n*8;if(meta<36||meta>65536)goto failed;
 for(u32 i=0;i<=n;i++){LOOP_PAD();if(!h[3+i*2]||h[3+i*2]>bytes-offset)goto failed;offset+=h[3+i*2];if(i){if(h[2+i*2]>0x110000-total)goto failed;total+=h[2+i*2];}}
 base=asset_alloc(total);if(!base)goto failed;offset=16+n*8;
 if(!asset_stream_chunk(q[4],offset,h[3],base,meta,meta))goto failed;offset+=h[3];
 /* Bound everything followed by135860 before relocating native pointers. */
 u32 rect=*(u32 *)(base+8),anim=*(u32 *)(base+12),sprites=*(u16 *)(base+6),anims=*(u16 *)(base+4);
 if(*(u16 *)base||*(u16 *)(base+2)!=n||16+n*4>meta||rect<16+n*4||rect>meta||sprites>(meta-rect)/8||anim<16+n*4||anim>meta||anims>(meta-anim)/4||(rect&1)||(anim&3))goto failed;
 for(u32 i=0;i<anims;i++){LOOP_PAD();u32 at=*(u32 *)(base+anim+i*4);
  for(;;){if(at>meta-36||(at&3))goto failed;if(*(short *)(base+at)<0)break;
   for(u32 j=20;j<=32;j+=4){LOOP_PAD();u32 ptr=*(u32 *)(base+at+j);if(ptr&&((ptr&3)||ptr>=meta))goto failed;}at+=36;}
 }
 u32 rawoff=meta;
 for(u32 i=0;i<n;i++){LOOP_PAD();u32 w=*(u16 *)(base+16+i*4),height=*(u16 *)(base+18+i*4),raw=h[4+i*2];
  if(!w||!height||(w&127)||(height&63)||raw!=1024+w*height)goto failed;
  if(!asset_stream_chunk(q[4],offset,h[5+i*2],base+rawoff,raw,raw))goto failed;offset+=h[5+i*2];rawoff+=raw;
 }
 for(u32 i=0;i<sprites;i++){LOOP_PAD();u16 *r=(u16 *)(base+rect+i*8);u32 sheet=r[1]>>10;
  if(sheet>=n||r[0]+r[2]>*(u16 *)(base+16+sheet*4)||(r[1]&1023)+r[3]>*(u16 *)(base+18+sheet*4))goto failed;
 }
 staged[0]=(u32)(unsigned long)base;asset_register(staged);rawoff=meta;
 for(u32 i=0;i<n;i++){LOOP_PAD();staged[5+i*4]=(u32)(unsigned long)(base+rawoff+1024);staged[6+i*4]=staged[7+i*4]=(u32)(unsigned long)(base+rawoff);rawoff+=h[4+i*2];}
 return 1;
failed:
 /* This unpublished buffer was one contiguous allocation, never a split-sheet
  * sidecar. Do not traverse other scenes' ownership list from the worker. */
 if(base)asset_free(base);staged[0]=0;return 0;
}
