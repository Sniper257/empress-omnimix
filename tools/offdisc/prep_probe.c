/* Private pre-READY diagnostic, external debug opt-in. No allocation, extra
 * input polls or readiness/timeout changes. Sparse closed checkpoints try the
 * existing FILEIO lock and defer on contention. Never enabled in active PLAY. */
#include "prep_probe.h"
#ifndef PREP_PROBE_TEST
#define pp_word(a) (*(volatile u32 *)(unsigned long)(a))
#define pp_rect ((void (*)(s32,s32,s32,s32,u32,s32))0x12ff00)
#define pp_native(a) ((unsigned long long (*)(unsigned long long))(unsigned long)(a))
#define pp_path() ((const char *)(unsigned long)input_probe.path)
#define pp_crc(p,n) cp_crc_full(p,n)
#define pp_append vd_exit_append
extern s32 vd_exit_append(const char *,u32 *,const void *,u32);
extern volatile u32 exit_io[6];
static u32 pp_clock(void){u32 n;__asm__ volatile("mfc0 %0,$9":"=r"(n));return n;}
#endif
static u32 pp_active,pp_in_chart,pp_serial,pp_sequence,pp_frames,pp_written,pp_error;
static u32 pp_alloc_bytes,pp_alloc_result,pp_alloc_failures;
static u32 pp_pending[16][32],pp_head,pp_queued,pp_dropped,pp_last_pair;
volatile u32 prep_probe_snapshot[32];

static void pp_snapshot(u32 stage)
{
    volatile u32 *p=prep_probe_snapshot;
    p[0]=0x50584d4d;p[1]=0x31504552;p[2]=1;p[3]=pp_sequence;
    p[4]=stage;p[5]=pp_clock();p[6]=pp_word(0x2ad534);
    p[7]=pp_word(0x2ad538);p[8]=pp_frames;p[9]=pp_word(0x2ad06c);
    p[10]=pp_word(0x2ad100);p[11]=pp_word(0x2ad120);
    p[12]=pp_word(0x2ad024);p[13]=pp_word(0x7431f0);
    p[14]=pp_word(0x78af08);p[15]=pp_word(0x78af00);
    p[16]=p[17]=p[18]=0;
    if(p[14] && p[15]<72) {
        u32 a=0x78af0c+p[15]*28;
        p[16]=pp_word(a+16);p[17]=pp_word(a+20);p[18]=pp_word(a+8);
    }
    p[19]=pp_word(0x2ad07c);p[20]=pp_alloc_bytes;
    p[21]=pp_alloc_result;p[22]=pp_alloc_failures;
    p[23]=exit_io[0];p[24]=exit_io[1];p[25]=exit_io[2];
    p[26]=exit_io[3];p[27]=exit_io[4];p[28]=pp_written;
    p[29]=pp_error;p[30]=pp_dropped;p[31]=0;
}
static int pp_prefix(char out[160])
{
    const char *s=pp_path();u32 n=0;
    if(!s)return 0;
    while(n<140 && s[n]){out[n]=s[n];n++;LOOP_PAD();}
    if(n<6 || n==140 || out[n-6]!='i' || out[n-5]!='n' ||
       out[n-4]!='p' || out[n-3]!='u' || out[n-2]!='t' || out[n-1]!='-')return 0;
    out[n-6]='p';out[n-5]='r';out[n-4]='e';out[n-3]='p';
    out[n-2]='-';out[n-1]=0;return 1;
}
static void pp_flush(void)
{
    if(!pp_active || pp_error || (!pp_in_chart && pp_word(0x2ad534)>=4) || !pp_queued)return;
    char path[160];if(!pp_prefix(path)){pp_error=1;return;}
    s32 r=pp_append(path,&pp_serial,pp_pending[pp_head],128);
    if(r==-8)return; /* busy: retry on a later diagnostic boundary */
    if(r<0){pp_error=(u32)-r;return;}
    pp_head=(pp_head+1)%16;pp_queued--;pp_written++;
}
void prep_probe_mark(u32 stage)
{
    if(!pp_active || (!pp_in_chart && pp_word(0x2ad534)>=4))return;
    pp_snapshot(stage);
    if(pp_queued==16){pp_dropped++;return;}
    u32 *r=pp_pending[(pp_head+pp_queued)%16];
    for(u32 i=0;i<31;i++){r[i]=prep_probe_snapshot[i];LOOP_PAD();}
    r[3]=pp_sequence++;r[31]=pp_crc(r,124);pp_queued++;pp_flush();
    if(stage==0x11)pp_in_chart=0;
}
void prep_probe_start(void)
{
    /* Old phase9 may still be visible before the native chart constructor. */
    pp_active=0;pp_serial=pp_sequence=pp_frames=pp_written=pp_error=0;
    pp_head=pp_queued=pp_dropped=pp_alloc_bytes=pp_alloc_result=pp_alloc_failures=0;
    pp_last_pair=~0u;pp_in_chart=1;pp_active=1;
}
void prep_probe_finish(void){pp_active=pp_in_chart=0;}

unsigned long long prep_workspace_allocate(unsigned long long n)
{
    if(!pp_active || (!pp_in_chart && pp_word(0x2ad534)>=4))return pp_native(0x100320)(n);
    pp_alloc_bytes=(u32)n;pp_alloc_result=0;prep_probe_mark(0x30);
    unsigned long long p=pp_native(0x100320)(n);
    pp_alloc_result=(u32)p;if(n && !p)pp_alloc_failures++;
    prep_probe_mark(0x31);return p;
}
unsigned long long prep_movie_work_allocate(unsigned long long n)
{
    if(!pp_active || (!pp_in_chart && pp_word(0x2ad534)>=4))return (unsigned long)asset_movie_alloc((u32)n);
    pp_alloc_bytes=(u32)n;pp_alloc_result=0;prep_probe_mark(0x40);
    void *p=asset_movie_alloc((u32)n);
    pp_alloc_result=(u32)(unsigned long)p;if(n && !p)pp_alloc_failures++;
    prep_probe_mark(0x41);return (unsigned long)p;
}
unsigned long long prep_movie_frame_allocate(unsigned long long n)
{
    if(!pp_active || (!pp_in_chart && pp_word(0x2ad534)>=4))return pp_native(0x13a4c0)(n);
    pp_alloc_bytes=(u32)n;pp_alloc_result=0;prep_probe_mark(0x42);
    unsigned long long p=pp_native(0x13a4c0)(n);
    pp_alloc_result=(u32)p;if(n && !p)pp_alloc_failures++;
    prep_probe_mark(0x43);return p;
}
static void pp_digit(s32 x,s32 y,u32 digit)
{
    static const u8 mask[16]={63,6,91,79,102,109,125,7,127,111,119,124,57,94,121,113};
    static const u8 box[7][4]={{1,0,5,1},{5,1,6,6},{5,7,6,12},{1,12,5,13},{0,7,1,12},{0,1,1,6},{1,6,5,7}};
    for(u32 i=0;i<7;i++) {
        if(mask[digit&15]&(1u<<i))pp_rect(x+box[i][0],y+box[i][1],x+box[i][2],y+box[i][3],0xffffff,0);
        LOOP_PAD();
    }
}
void prep_probe_render(void)
{
    if(!pp_active)return;
    u32 phase=pp_word(0x2ad534);
    if(phase>=4){prep_probe_finish();return;}
    pp_frames++;
    u32 pair=phase|(pp_word(0x2ad538)<<8);
    if(pair!=pp_last_pair){pp_last_pair=pair;prep_probe_mark(0x20);}
    pp_flush();pp_snapshot(0x21);
    u32 packet=pp_word(0x2ad040);
    if(packet<0x34d100 || packet>0x38d100 || 0x38d100-packet<0x24000)return;
    /* At most1198 rectangles*80B; retain >48KiB for the native submit. No
     * font/texture references: queued or freed HUD containers are irrelevant. */
    pp_rect(38,38,365,184,0,1);pp_digit(48,42,3);pp_digit(56,42,6);
    static const u8 indices[21]={6,7,8,14,11,12,9,10,13,16,17,18,20,21,22,23,24,26,28,29,30};
    for(u32 row=0;row<7;row++)for(u32 j=0;j<3;j++)for(u32 i=0;i<8;i++) {
        pp_digit(48+j*100+i*8,60+row*17,prep_probe_snapshot[indices[row*3+j]]>>(28-i*4));LOOP_PAD();
    }
}
