/* Private pre-READY RAM observer. No files, RPC, input polls, allocations or
 * readiness overrides. Main owns all fields; the reader exposes only its
 * existing locked breadcrumbs. Disable before READY and leave PLAY unchanged. */
#ifndef PREP_WATCH_TEST
#define pw_word(a) (*(volatile u32 *)(unsigned long)(a))
#define pw_native(a) ((unsigned long long (*)(unsigned long long,unsigned long long,unsigned long long,unsigned long long,unsigned long long,unsigned long long,unsigned long long,unsigned long long))(unsigned long)(a))
#define pw_rect ((void (*)(s32,s32,s32,s32,u32,s32))0x12ff00)
extern unsigned long long pw_chart_body(void);
extern volatile u32 exit_io[6];
#endif
volatile u32 prep_watch_config[2]={1,0};
/* version, chart#, active, frame, phase, substate, queue, bank, movie, BGM,
 * allocation bytes/result/failures/first failed bytes, workspace, asset failures,
 * reader stage/error, movie buffers, total/largest free, heap status, row,
 * queue head/request bytes/destination, initial total/largest, chart returned. */
volatile u32 prep_watch_state[32]={147};
static u32 pw_asset_base;
#include "asset_heap.h"
static void pw_heap(void)
{
    u32 total,largest,status=asset_heap_sample(&total,&largest);
    prep_watch_state[20]=total;prep_watch_state[21]=largest;prep_watch_state[22]=status;
}
static int pw_active(void){return prep_watch_config[1] && prep_watch_state[2];}
#define PW_ARGS unsigned long long a,unsigned long long b,unsigned long long c,unsigned long long d,unsigned long long e,unsigned long long f,unsigned long long g,unsigned long long h
#define PW_PASS a,b,c,d,e,f,g,h
unsigned long long pw_chart(PW_ARGS)
{
    if(prep_watch_config[1]){
        prep_watch_state[1]++;
        for(u32 i=2;i<32;i++){prep_watch_state[i]=0;LOOP_PAD();}
        prep_watch_state[2]=1;pw_asset_base=asset_memory.failures;
        prep_watch_state[23]=(u32)song_catalog_row(asset_song());
        pw_heap();prep_watch_state[28]=prep_watch_state[20];prep_watch_state[29]=prep_watch_state[21];
    }
    unsigned long long r=pw_native((unsigned long)pw_chart_body)(PW_PASS);
    if(pw_active())prep_watch_state[30]=1;
    return r;
}
static unsigned long long pw_alloc(u32 target,PW_ARGS)
{
    unsigned long long r=pw_native(target)(PW_PASS);
    if(pw_active()){
        prep_watch_state[10]=(u32)a;prep_watch_state[11]=(u32)r;
        if(a&&!r){prep_watch_state[12]++;if(!prep_watch_state[13])prep_watch_state[13]=(u32)a;}
        pw_heap();
    }
    return r;
}
unsigned long long pw_back(PW_ARGS){return pw_alloc(0x100320,PW_PASS);}
unsigned long long pw_frame(PW_ARGS){
#ifdef ASSET_FRAME_ORDER
    unsigned long long r=(unsigned long)asset_movie_frame_alloc((u32)a);
    if(pw_active()){
        prep_watch_state[10]=(u32)a;prep_watch_state[11]=(u32)r;
        if(a&&!r){prep_watch_state[12]++;if(!prep_watch_state[13])prep_watch_state[13]=(u32)a;}
        pw_heap();
    }
    return r;
#else
    return pw_alloc(0x13a4c0,PW_PASS);
#endif
}
static void pw_digit(s32 x,s32 y,u32 value)
{
    static const u8 mask[16]={63,6,91,79,102,109,125,7,127,111,119,124,57,94,121,113};
    static const u8 box[7][4]={{1,0,5,1},{5,1,6,6},{5,7,6,12},{1,12,5,13},{0,7,1,12},{0,1,1,6},{1,6,5,7}};
    for(u32 i=0;i<7;i++){
        if(mask[value&15]&(1u<<i))pw_rect(x+box[i][0],y+box[i][1],x+box[i][2],y+box[i][3],0xffffff,0);
        LOOP_PAD();
    }
}
#ifdef ASSET_GAP_TRACE
static int pw_gap_cell(u32 row,u32 col,u32 *value)
{
    u32 pages[3],total=0;
    for(u32 i=0;i<3;i++){
        pages[i]=0;
        if(asset_gaps[i].ready){
            __asm__ volatile("":::"memory");
            u32 n=asset_gaps[i].count;if(n>ASSET_GAP_CAP)n=ASSET_GAP_CAP;
            pages[i]=(n+6)/7;if(!pages[i])pages[i]=1;total+=pages[i];
        }
        LOOP_PAD();
    }
    if(!total)return 0;
    u32 page=prep_watch_state[3]/45%total;
    /* Show BACK first. Capture copied worker data even while other native
     * loading is still busy; do not wait until the later movie failure. */
    for(u32 i=3;i--;){
        if(page<pages[i]){
            asset_gap_snapshot *s=&asset_gaps[i];
            if(!row){
                *value=col==0?(0x15100000|(i<<8)|page):col==1?s->count:
                    s->status|(s->count>ASSET_GAP_CAP?0x10000:0)|(s->frames<<24);
            }else{
                u32 k=page*7+row-1;
                *value=k<s->count && k<ASSET_GAP_CAP?(col==0?k+1:s->gap[k][col-1]):0;
            }
            return 1;
        }
        page-=pages[i];LOOP_PAD();
    }
    return 0;
}
#endif
static u32 pw_cell(u32 row,u32 col)
{
#ifdef ASSET_GAP_TRACE
    u32 value;if(pw_gap_cell(row,col,&value))return value;
#endif
    static const u8 field[21]={4,5,3,6,7,8,10,11,12,13,15,17,20,21,22,14,18,19,16,1,23};
#ifdef ASSET_FRAME_ORDER
    if(row==7)return asset_frame_trace[col];
    u32 page=prep_watch_state[3]/120%3;
    if(prep_watch_state[12] && !prep_watch_state[6] && !prep_watch_state[7] && page){
        if(row==5)return asset_frame_trace[(page==1?3:6)+col];
        if(row==6){
            if(col==2)return 0x150000+page;
            return asset_frame_trace[(page==1?(col?8:6):8)+col*(page==2)];
        }
    }
#endif
    return prep_watch_state[field[row*3+col]];
}
static void pw_render(void)
{
    if(!pw_active())return;
    u32 phase=pw_word(0x2ad534);if(phase>=4){prep_watch_state[2]=0;return;}
    prep_watch_state[3]++;prep_watch_state[4]=phase;prep_watch_state[5]=pw_word(0x2ad538);
    static const u32 address[4]={0x78af08,0x2ad120,0x2ad024,0x7431f0};
    for(u32 i=0;i<4;i++)prep_watch_state[6+i]=pw_word(address[i]);
    prep_watch_state[14]=pw_word(0x2ad06c);
    prep_watch_state[15]=asset_memory.failures-pw_asset_base;
    prep_watch_state[16]=exit_io[0];prep_watch_state[17]=exit_io[4];
    prep_watch_state[18]=pw_word(0x2be880);prep_watch_state[19]=pw_word(0x2be888);
    u32 head=pw_word(0x78af00);prep_watch_state[24]=head;
    prep_watch_state[25]=prep_watch_state[26]=0;
    if(prep_watch_state[6] && head<72){prep_watch_state[25]=pw_word(0x78af0c+head*28+16);prep_watch_state[26]=pw_word(0x78af0c+head*28+8);}
    if((prep_watch_state[3]&31)==1)pw_heap();
    u32 packet=pw_word(0x2ad040);
    if(packet<0x34d100 || packet>0x38d100 || 0x38d100-packet<0x28000)return;
    /* Same guarded primitive budget as the hardware-qualified R36 panel. */
    pw_rect(38,38,365,201,0,1);
#ifdef ASSET_FRAME_ORDER
    const u32 rows=8;
#else
    const u32 rows=7;
#endif
    for(u32 row=0;row<rows;row++)for(u32 col=0;col<3;col++){
        u32 value=pw_cell(row,col);
        for(u32 i=0;i<8;i++){
            pw_digit(48+col*100+i*8,60+row*17,value>>(28-i*4));LOOP_PAD();
        }
    }
}
unsigned long long pw_submit(PW_ARGS){pw_render();return pw_native(0x136c30)(PW_PASS);}
#undef PW_ARGS
#undef PW_PASS
