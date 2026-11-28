/* USB-only startup probe. No file I/O or SDK callbacks in native workers.
 * R142 MAIN-only allocation experiment queries the existing retail heap service
 * and optionally reserves the original760KiB block before audio initialization.
 * Other observations forward native calls; the row is SITE,E/A,ARG0/RETURN.
 * Each complete GIF packet fits the 16-QW CPU FIFO (14 QW maximum). Submit
 * only with every path idle; bounded readiness checks can skip a whole packet,
 * never abandon a half-submitted image. R16 initializes its own 60Hz display
 * after ExecPS2; R15 incorrectly relied on the launcher's reset GS state.
 * Native graphics initialization later replaces this diagnostic display.
 * GS BITBLTBUF/TRXPOS/TRXREG format follows PS2SDK ee/draw/src/draw.c.
 * Readiness fields follow PCSX2 pcsx2/Gif.h (GIF_STAT), not guessed busy bits.
 * This is diagnostic startup traffic, not an input timing baseline. */
#ifndef BOOT_TRACE_TEST
#define bt_word(a) (*(volatile u32 *)(a))
#define bt_long(a) (*(volatile unsigned long long *)(a))
#define bt_write(a,v) (bt_long(a)=(unsigned long long)(v))
#define bt_write_word(a,v) (bt_word(a)=(u32)(v))
#define bt_crt() (((void (*)(u32,u32,u32))0x10e860)(1,2,0))
#define bt_native(a) ((unsigned long long (*)(unsigned long long,unsigned long long,unsigned long long,unsigned long long,unsigned long long,unsigned long long,unsigned long long,unsigned long long))(a))
static void bt_send(const unsigned long long *q)
{
    register const void *src __asm__("$4")=q;
    register void *dst __asm__("$5")=(void *)0x10006000;
    /* lq v0,0(a0); sq v0,0(a1). LLVM's MIPS assembler lacks EE mnemonics. */
    __asm__ volatile(".word 0x78820000\n.word 0x7ca20000" :: "r"(src),"r"(dst):"$2","memory");
}
#endif
volatile u32 boot_trace_state[4]; /* call site, 0 entered/1 returned, value, skipped rows */
#ifdef USB_BOOT_JOURNAL
#include "boot_journal.c"
#elif defined(USB_BOOT_TEXT)
/* Main-thread-only bounded progress, not an on-disk log. */
volatile u32 boot_loop_state[16];
static u32 boot_loop_root;
/* Independent writers: sound thread lanes0/1, file/asset thread lane2. Neither
 * calls the renderer or touches main snapshot/cursor/DMA buffers. */
volatile u32 boot_workers[12]; /* each: entered, returned, current site, last result */
/* Per-call records have ONE writer each. Cursor0 belongs only to13f5f0;
 * cursor1 only to13e5a0. Main reads them; neither worker touches renderer. */
volatile u32 boot_sound[240]; /*30 x(site,after,arg0,result,entered,returned,enterF,returnF) */
volatile u32 boot_sound_cursor[12]; /*3 x(site,after,value,slot) */
volatile u32 boot_alloc[8]; /* init result,3IOP pointers,seen,failed,early failure,unused */
#include "iop_budget.h"
static void boot_trace_mark(u32,u32,u32);
static void bt_alloc_stop(void);
volatile u32 boot_iop_budget[IB_WORDS]={IB_MAGIC};
/* Only called in native MAIN init, before audio workers exist. Never from a
 * worker wrapper or the once/frame renderer. Queries use original native ABI. */
static void bt_heap_sample(u32 tag,u32 result){
 if(!ib_enabled(boot_iop_budget))return;
 u32 largest=(u32)bt_native(0x117408)(0,0,0,0,0,0,0,0);
 u32 total=(u32)bt_native(0x117478)(0,0,0,0,0,0,0,0);
 ib_record(boot_iop_budget,tag,total,largest,result);
}
static void bt_heap_begin(void){
 if(!ib_enabled(boot_iop_budget))return;
 bt_heap_sample(0x100,0);
 if(boot_iop_budget[1]!=2)return;
 if(boot_iop_budget[3]){boot_alloc[6]=2;boot_alloc[5]=1;boot_trace_mark(0x13e490,0,0);bt_alloc_stop();}
 u32 p=(u32)bt_native(0x117120)(IB_AUDIO_BYTES,0,0,0,0,0,0,0);
 boot_iop_budget[2]=p;boot_iop_budget[3]=ib_pointer(p)?1:3;
 bt_heap_sample(0x101,p);
 if(boot_iop_budget[3]!=1){boot_iop_budget[6]=1;boot_alloc[6]=1;boot_alloc[5]=1;boot_trace_mark(0x13e490,0,p);bt_alloc_stop();}
}
static void bt_heap_finish(void){
 if(!ib_enabled(boot_iop_budget))return;
 /* Only an unconsumed reservation belongs to us. Once handed to the native
  * 760KiB site, native owns it; never free a buffer used by sound transfers. */
 if(boot_iop_budget[3]==1||boot_iop_budget[3]==5){
  u32 r=(u32)bt_native(0x117210)(boot_iop_budget[2],0,0,0,0,0,0,0);
  boot_iop_budget[7]=r;if(!(int)r)boot_iop_budget[3]=4;
  bt_heap_sample(0x1ff,r);
 }
 bt_heap_sample(0x1f0,boot_alloc[0]);
}
#ifndef BOOT_TRACE_TEST
static void bt_alloc_stop(void){for(;;){LOOP_PAD();}}
#endif
volatile u32 boot_rpc[16]; /*2 x(packet,clientID,server,flags,packetID,busy,sema,valid) */
volatile u32 boot_detail[136]; /* IO12, native20, scene count/flags +16x6 records */
#ifndef BOOT_TRACE_TEST
extern void vd_boot_snapshot(u32 *);
#endif
#include "boot_text.c"
static int boot_loop_pointer(u32 p,u32 n){return !(p&3)&&p>=0x994700&&p<=0x2000000u-n;}
/* Verified native112D30/112F30: header0=packet,1=RPC id,2=semaphore,
 * client9=server (NOT word4); packet+10 flags and+18 RPC id. No RPC call,
 * SIF allocation, busy-loop or new lock. Best-effort sample only. */
static void boot_rpc_snapshot(void){
 for(u32 i=0;i<2;i++){
  u32 c=0x787600+i*0x30,k=i*8,p=btx_word(c),id=btx_word(c+4);
  boot_rpc[k]=p;boot_rpc[k+1]=id;boot_rpc[k+2]=btx_word(c+36);
  boot_rpc[k+6]=btx_word(c+8);boot_rpc[k+3]=~0u;boot_rpc[k+4]=~0u;
  boot_rpc[k+5]=p?~0u:0;boot_rpc[k+7]=0;
  u32 alias=p&0xe0000000u,physical=p&0x1fffffffu;
  /* Retail RPC packets use uncached202xxxxx. Check the physical range but
   * READ ORIGINAL alias: replacing it with cached RAM can invent stale busy. */
  if((!alias||alias==0x20000000u)&&!(p&3)&&physical>=0x100000&&physical<=0x2000000-0x40){
   u32 f=btx_word(p+16),r=btx_word(p+24);
   boot_rpc[k+3]=f;boot_rpc[k+4]=r;boot_rpc[k+5]=(id==r)&&(f&1);boot_rpc[k+7]=1;
  }LOOP_PAD();
 }
}
static void boot_loop_scene(void){
 u32 scene=~0u,state=~0u,root=boot_loop_root,n=0,scanned=0,flags=0;
 if(boot_loop_pointer(root,20)){
  u32 lists=btx_word(root+8);
  if(boot_loop_pointer(lists,48))for(u32 i=0;i<8;i++){
   u32 list=btx_word(lists+4+i*4);
   if(boot_loop_pointer(list,12)){
    u32 count=btx_word(list+4),entries=btx_word(list+8);
    if(count>128 || count>128-scanned){flags|=1;continue;}
    if(count&&!boot_loop_pointer(entries,count*16)){flags|=2;continue;}
    for(u32 j=0;j<count;j++){
     u32 e=entries+j*16,id=btx_word(e);scanned++;
     if(id==0x7fffffffu)continue;
     if(!n){scene=id;state=btx_word(e+4);}
     if(n<16){
      u32 k=40+n*6,obj=btx_word(e+12);
      boot_detail[k]=i;boot_detail[k+1]=id;boot_detail[k+2]=btx_word(e+4);
      boot_detail[k+3]=btx_word(e+8);boot_detail[k+4]=obj;
      boot_detail[k+5]=boot_loop_pointer(obj,4)?btx_word(obj):~0u;
     }else flags|=4;
     n++;LOOP_PAD();
    }
   }else if(list)flags|=2;LOOP_PAD();
  }
 }
 boot_detail[32]=n;boot_detail[33]=flags;
 boot_loop_state[4]=scene;boot_loop_state[5]=state;boot_loop_state[6]=btx_word(0x78af08);
 u32 io[12];vd_boot_snapshot(io);for(u32 i=0;i<12;i++){boot_detail[i]=io[i];LOOP_PAD();}
 for(u32 i=0;i<4;i++){boot_loop_state[7+i]=io[i];LOOP_PAD();}
 /* Exact native globals: queue callback head, sequence transition payload,
  * sound transfer busy flags and32-entry upload ring head/state. No RPC probe. */
 static const u32 addr[14]={0x78af00,0x2ad190,0x2ad194,0x2ad1a8,
  0x2ad120,0x2ad134,0x727380,0x727388,
  0x78f740,0x78f744,0x78f748,0x78f74c,0x78f750,0x78f754};
 for(u32 i=0;i<14;i++){boot_detail[12+i]=btx_word(addr[i]);LOOP_PAD();}
 u32 head=boot_detail[12];
 boot_detail[26]=boot_loop_state[6]&&head<72?btx_word(0x78af0c+head*28):~0u;
 boot_detail[27]=boot_loop_state[6]&&head<72?btx_word(0x78af0c+head*28+16):~0u;
 u32 upload=boot_detail[15];
 boot_detail[28]=upload<32?btx_word(0x757300+upload*24):~0u;
 boot_detail[29]=upload<32?btx_word(0x757304+upload*24):~0u;
 boot_alloc[1]=btx_word(0x2ad1b4);boot_alloc[2]=btx_word(0x2ad1b8);boot_alloc[3]=btx_word(0x2ad1bc);
 boot_rpc_snapshot();
 boot_loop_state[14]=root;boot_loop_state[15]=btx_word(0x2ad534);
}
static void boot_loop_note(u32 site,u32 after,u32 value){
 if(btx_retired)return;
 if(site==0x151990&&!after)boot_loop_state[0]++;
 if(site==0x1519c0&&!after)boot_loop_root=value;
 if(site==0x151a00&&after)boot_loop_state[12]++;
 boot_loop_state[1]=site;boot_loop_state[2]=after;boot_loop_state[3]=value;boot_loop_scene();
 if(boot_loop_state[15]>=4){btx_retired=1;boot_loop_state[13]=0;return;} /* no PLAY drawing */
 if(boot_loop_state[0]>1200){boot_trace_mark(0x1380000,1,1200);btx_retired=1;boot_loop_state[13]=0;return;}
 boot_loop_state[13]=1;
 /* Beyond initial two loops draw once per frame, on MAIN only. RAM still
  * identifies every boundary. This removes28 repeated panels per frame. */
 if(boot_loop_state[0]<=2 || (site==0x151a00&&after))boot_trace_mark(site,after,value);
}
#else
static u32 bt_display_initialized,bt_display_owned;
static unsigned long long bt_display_framebuffer;
static void bt_display_init(void)
{
    if(bt_display_initialized)return;
    bt_display_initialized=1;
    /* PS2SDK graph_mode.c: CSR reset, NTSC interlaced field mode,640x448.
     * This runs once, after CRT0 stack/GP setup and before native libc init.
     * No native renderer/queue exists yet. Do not reset it on later markers. */
    bt_write(0x12000000,0);
    bt_write(0x12001000,0x200);
    bt_write(0x12001000,0);
    bt_write(0x12001010,0x7f00); /* Mask GS interrupts; native setup owns them later. */
    bt_write_word(0x10003000,1); /* Fresh startup only: reset GIF, clear path3 mask. */
    bt_write_word(0x10003010,0);
    bt_write(0x12001040,0);
    bt_crt();
    bt_write(0x12000020,1); /* FIELD=0, as in PS2SDK graph_initialize. */
    bt_write(0x12000070,10u<<9); /* VRAM0,640-wide RGB32; no textures in use. */
    bt_write(0x12000080,652ull | 50ull<<12 | 3ull<<23 | 2559ull<<32 | 447ull<<44);
    bt_write(0x120000e0,0x102030);
    bt_write(0x12000000,0x8025); /* RC1,CRTMD1,constant alpha128. */
    bt_display_framebuffer=10u<<9;bt_display_owned=1;
}
static int bt_ready(void)
{
    for(u32 i=0;i<4096;i++) {
        if(!(bt_word(0x1000a000)&0x100) && !(bt_word(0x10009000)&0x100) &&
           !(bt_word(0x10003020)&0x1f001feb) && !bt_long(0x12001040))return 1;
        LOOP_PAD();
    }
    return 0;
}
static u32 bt_pixel(u32 x,u32 y,u32 site,u32 after,u32 value)
{
    static const u8 segments[16]={0x3f,6,0x5b,0x4f,0x66,0x6d,0x7d,7,0x7f,0x6f,0x77,0x7c,0x39,0x5e,0x79,0x71};
    u32 cell=x/12,xx=x%12,mask=0;
    if(cell<8)mask=segments[(site>>(28-cell*4))&15];
    else if(cell==9)mask=segments[after?10:14]; /* A=after, E=entered */
    else if(cell>=11 && cell<19)mask=segments[(value>>(28-(cell-11)*4))&15];
    int on=(xx>=2 && xx<=8 && ((y==2 && (mask&1)) || (y==8 && (mask&64)) || (y==14 && (mask&8)))) ||
           ((xx==1 || xx==9) && y>=3 && y<=7 && (mask&(xx==1?32:2))) ||
           ((xx==1 || xx==9) && y>=9 && y<=13 && (mask&(xx==1?16:4)));
    return on?(after?0x0050ff50:0x0050c0ff):0x00101010;
}
static void boot_trace_mark(u32 site,u32 after,u32 value)
{
    boot_trace_state[0]=site;boot_trace_state[1]=after;boot_trace_state[2]=value;
    bt_display_init();
    /* PMODE/DISPFB are write-only on real GS. Our early display has a known
     * software shadow; never infer its contents from emulator readback. Once
     * native graphics returns, retain RAM markers but give up screen ownership. */
    if(site==0x12f914 && after)bt_display_owned=0;
    if(!bt_display_owned)return;
    unsigned long long fb=bt_display_framebuffer;
    u32 psm=(u32)(fb>>15)&31,pitch=(u32)(fb>>9)&63;
    u32 dx=((u32)(fb>>32)&2047)+16,dy=((u32)(fb>>43)&2047)+16;
    if(psm>1 || !pitch || dx+256>pitch*64 || dy+16>2048)return;
    u32 bytes=psm==1?3:4,qwords=bytes*2;
    for(u32 y=0;y<16;y++)for(u32 x=0;x<256;x+=32) {
        unsigned long long q[14][2] __attribute__((aligned(16)));
        q[0][0]=0x1000000000000004ull;q[0][1]=14; /* PACKED, four A+D */
        q[1][0]=((fb&511)*32)<<32 | (unsigned long long)pitch<<48 | (unsigned long long)psm<<56;q[1][1]=0x50;
        q[2][0]=(unsigned long long)(dx+x)<<32 | (unsigned long long)(dy+y)<<48;q[2][1]=0x51;
        q[3][0]=32ull | 1ull<<32;q[3][1]=0x52;
        q[4][0]=0;q[4][1]=0x53;
        q[5][0]=2ull<<58 | 0x8000 | qwords;q[5][1]=0; /* IMAGE, EOP */
        u8 *pixels=(u8 *)&q[6];
        for(u32 i=0;i<32;i++) {
            u32 color=bt_pixel(x+i,y,site,after,value);
            for(u32 k=0;k<bytes;k++){pixels[i*bytes+k]=(u8)(color>>(k*8));LOOP_PAD();}
            LOOP_PAD();
        }
        if(!bt_ready()){boot_trace_state[3]++;return;}
        for(u32 i=0;i<6+qwords;i++){bt_send(q[i]);LOOP_PAD();}
        LOOP_PAD();
    }
}
#endif
#define BT_CALL(name,site,address) \
unsigned long long boot_trace_##name(unsigned long long a,unsigned long long b,unsigned long long c,unsigned long long d,unsigned long long e,unsigned long long f,unsigned long long g,unsigned long long h) \
{ \
    boot_trace_mark(site,0,(u32)a); \
    unsigned long long r=bt_native(address)(a,b,c,d,e,f,g,h); \
    boot_trace_mark(site,1,(u32)r);return r; \
}
BT_CALL(kernel,0x1001e8,0x119158)
BT_CALL(libc_semas,0x119160,0x118f08)
BT_CALL(libc_kernel,0x119168,0x119048)
BT_CALL(kernel_timer_patch,0x119170,0x119638)
BT_CALL(kernel_timer,0x119178,0x1197b0)
BT_CALL(kernel_alarm,0x119180,0x1199f0)
BT_CALL(kernel_threads,0x119188,0x10fa38)
BT_CALL(kernel_osd_patch,0x119190,0x119280)
BT_CALL(kernel_exec_patch,0x119198,0x118778)
BT_CALL(constructors,0x151888,0x14c8a0)
BT_CALL(arena,0x151894,0x1530c0)
BT_CALL(globals,0x1518ac,0x14c3a0)
BT_CALL(audio_reset,0x12f798,0x13a680)
BT_CALL(rpc_first,0x12f7a0,0x1123a8)
BT_CALL(cd_first,0x12f7a8,0x11d690)
BT_CALL(mode_first,0x12f7b0,0x11e0f8)
BT_CALL(rpc_second,0x12f7f8,0x1123a8)
BT_CALL(loadfile,0x12f800,0x117778)
BT_CALL(fileio,0x12f808,0x113f08)
BT_CALL(cd_second,0x12f810,0x11d690)
BT_CALL(mode_second,0x12f818,0x11e0f8)
BT_CALL(heap,0x12f820,0x117098)
BT_CALL(libsd,0x12f834,0x117fe8)
BT_CALL(sound,0x12f854,0x117fe8)
BT_CALL(sio2,0x12f874,0x117fe8)
BT_CALL(mcman,0x12f894,0x117fe8)
BT_CALL(mcserv,0x12f8b4,0x117fe8)
BT_CALL(padman,0x12f8d4,0x117fe8)
BT_CALL(cdvdstm,0x12f8f4,0x117fe8)
BT_CALL(filequeue,0x12f904,0x14d260)
BT_CALL(audio,0x12f90c,0x13e480)
BT_CALL(graphics,0x12f914,0x13a040)
BT_CALL(pad,0x12f91c,0x13bd00)
#ifdef USB_BOOT_TEXT
/* Initialization once; main-loop boundaries continue for a bounded boot run. */
#define BT_ONCE(name,site,address) \
unsigned long long boot_trace_##name(unsigned long long a,unsigned long long b,unsigned long long c,unsigned long long d,unsigned long long e,unsigned long long f,unsigned long long g,unsigned long long h) \
{ \
    static u32 seen;if(seen)return bt_native(address)(a,b,c,d,e,f,g,h);seen=1; \
    boot_trace_mark(site,0,(u32)a); \
    unsigned long long r=bt_native(address)(a,b,c,d,e,f,g,h); \
    boot_trace_mark(site,1,(u32)r);return r; \
}
BT_ONCE(init,0x151988,0x151880)
#define BT_LOOP(name,site,address) \
unsigned long long boot_trace_##name(unsigned long long a,unsigned long long b,unsigned long long c,unsigned long long d,unsigned long long e,unsigned long long f,unsigned long long g,unsigned long long h) \
{ \
    boot_loop_note(site,0,(u32)a); \
    unsigned long long r=bt_native(address)(a,b,c,d,e,f,g,h); \
    boot_loop_note(site,1,(u32)r);return r; \
}
BT_LOOP(loop_pad,0x151990,0x13bf70)
BT_LOOP(loop_wait,0x1519a0,0x10eca0)
BT_LOOP(render_reset,0x1519a8,0x139a20)
BT_LOOP(system_tick,0x1519b0,0x14c160)
BT_LOOP(service,0x1519b8,0x130730)
BT_LOOP(update,0x1519c0,0x14e670)
BT_LOOP(draw,0x1519c8,0x14e280)
BT_LOOP(submit,0x1519d0,0x136c30)
BT_LOOP(movie,0x1519d8,0x131aa0)
BT_LOOP(flip,0x1519e8,0x139be0)
BT_LOOP(timer,0x1519f8,0x13bf40)
BT_LOOP(tail,0x151a00,0x14e250)
/* Worker adapters use the same full-ABI assembly shell, but never draw/wait. */
#define BT_WORK(name,site,address,lane) \
unsigned long long boot_trace_##name(unsigned long long a,unsigned long long b,unsigned long long c,unsigned long long d,unsigned long long e,unsigned long long f,unsigned long long g,unsigned long long h) \
{ \
 volatile u32 *w=boot_workers+lane*4;int active=!btx_retired; \
 if(active){w[0]++;w[2]=site;} \
 unsigned long long r=bt_native(address)(a,b,c,d,e,f,g,h); \
 if(active){w[3]=(u32)r;w[1]++;}return r; \
}
BT_WORK(upload,0x13f600,0x145910,0)
BT_WORK(sound_status,0x13f6b8,0x13f870,1)
BT_WORK(asset_release,0x14cce8,0x13a530,2)
/* Sound workers preserve original arguments/results without added queries,
 * clock reads or waits. MAIN init/allocation slots13..16 additionally sample
 * the heap; MODE2 replaces only760KiB allocation with the owned reservation. */
#define BT_SOUND(name,site,address,slot,owner) \
unsigned long long boot_trace_##name(unsigned long long a,unsigned long long b,unsigned long long c,unsigned long long d,unsigned long long e,unsigned long long f,unsigned long long g,unsigned long long h) \
{ \
 volatile u32 *w=boot_sound+slot*8,*cur=boot_sound_cursor+owner*4;int active=!btx_retired; \
 if(active){w[0]=site;w[1]=0;w[2]=(u32)a;w[4]++;w[6]=boot_loop_state[0]; \
  cur[0]=site;cur[1]=0;cur[2]=(u32)a;cur[3]=slot;} \
 if(active&&slot==13)bt_heap_begin(); \
 if(active&&slot>=14&&slot<=16)bt_heap_sample(0x110+(slot-14)*2,0); \
 unsigned long long r; \
 if(active&&slot==16&&boot_iop_budget[1]==2&&ib_enabled(boot_iop_budget)){ \
  if(a==IB_AUDIO_BYTES&&boot_iop_budget[3]==1&&ib_pointer(boot_iop_budget[2])){r=boot_iop_budget[2];boot_iop_budget[3]=2;} \
  else{r=0;if(boot_iop_budget[3]==1)boot_iop_budget[3]=5;boot_iop_budget[6]=2;} \
 }else r=bt_native(address)(a,b,c,d,e,f,g,h); \
 if(active&&slot>=14&&slot<=16)bt_heap_sample(0x111+(slot-14)*2,(u32)r); \
 if(active){w[3]=(u32)r;w[5]++;w[7]=boot_loop_state[0];w[1]=1; \
  cur[0]=site;cur[1]=1;cur[2]=(u32)r;cur[3]=slot;} \
 if(slot==13&&active){boot_alloc[0]=(u32)r;boot_alloc[4]=1; \
  boot_alloc[1]=btx_word(0x2ad1b4);boot_alloc[2]=btx_word(0x2ad1b8);boot_alloc[3]=btx_word(0x2ad1bc);bt_heap_finish(); \
  if((int)(u32)r<0||!boot_alloc[1]||!boot_alloc[2]||!boot_alloc[3]){ \
   boot_alloc[5]=1;boot_trace_mark(site,1,(u32)r);bt_alloc_stop();}}return r; \
}
BT_SOUND(bank_submit,0x13e8a8,0x13fd20,0,1)
BT_SOUND(fade_bank,0x13f630,0x141a20,1,0)
BT_SOUND(fade_stream,0x13f670,0x141a10,2,0)
BT_SOUND(stop_stream,0x13f678,0x1439a0,3,0)
BT_SOUND(transfer_status,0x13f878,0x144fa0,4,0)
BT_SOUND(voice_residency,0x13f880,0x141d90,5,0)
BT_SOUND(voice_tick,0x13f888,0x141ba0,6,0)
BT_SOUND(stream_tick,0x13f890,0x143870,7,0)
BT_SOUND(command_build,0x13f898,0x1414b0,8,0)
BT_SOUND(command_send,0x13f8a0,0x146310,9,0)
BT_SOUND(transfer_publish,0x13f8d8,0x1450a0,10,0)
BT_SOUND(bank_poll_first,0x13e8b0,0x13fd10,11,1)
BT_SOUND(bank_poll_repeat,0x13e8d0,0x13fd10,12,1)
BT_SOUND(audio_init,0x13e490,0x13f900,13,2)
BT_SOUND(alloc_32k,0x13fa70,0x117120,14,2)
BT_SOUND(alloc_128k,0x13fa80,0x117120,15,2)
BT_SOUND(alloc_760k,0x13fa9c,0x117120,16,2)
BT_SOUND(status_bank,0x145030,0x1470a0,17,0)
BT_SOUND(status_stream,0x145064,0x1470a0,18,0)
BT_SOUND(bank_finish,0x13e8e0,0x13f310,19,1)
BT_SOUND(rpc_before,0x14713c,0x112f30,20,0)
BT_SOUND(rpc_after,0x1471f8,0x112f30,21,0)
BT_SOUND(queue_first,0x13f328,0x145630,22,1)
BT_SOUND(queue_repeat,0x13f340,0x145630,23,1)
BT_SOUND(upload_first,0x13f360,0x145c00,24,1)
BT_SOUND(upload_repeat,0x13f378,0x145c00,25,1)
BT_SOUND(stream_first,0x13f398,0x13fb80,26,1)
BT_SOUND(stream_repeat,0x13f3b8,0x13fb80,27,1)
BT_SOUND(stream_last_first,0x13f3d0,0x13fb80,28,1)
BT_SOUND(stream_last_repeat,0x13f3fc,0x13fb80,29,1)
#undef BT_SOUND
#undef BT_WORK
#undef BT_LOOP
#undef BT_ONCE
#endif
#undef BT_CALL
