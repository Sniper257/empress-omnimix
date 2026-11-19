/* Temporary hardware exit diagnostic. Native routines retain their order and
 * return values. No allocations, extra pad polls or timeout
 * bypasses. Display uses native text/rectangles, with a font-free unload fallback.
 * R14 uses catalog/resource boundaries; closed exit-journal writes stay out of active play.
 * Enabled only by EXIT_TRACE=1; these captures are not timing baselines. */
#ifndef EXIT_TRACE_TEST
#define et_word(a) (*(volatile u32 *)(a))
#define et_font ((void (*)(const char *,u32,s32,s32,s32,s32,s32,const void *,s32))0x1e1850)
#define et_rect ((void (*)(s32,s32,s32,s32,u32,s32))0x12ff00)
#define et_entries ((u8 *)0x34aa80)
#define et_count et_word(0x2ad030)
extern unsigned long long exit_play_body(void);
extern void exit_render_body(void);
#define et_audio_stop() (((void (*)(void))0x13db90)())
#define et_unload() (((void (*)(void))0x1357e0)())
#endif
volatile u32 exit_trace_armed,exit_trace_stage;
static u32 exit_trace_frames;
static u32 exit_trace_intro;
extern volatile u32 exit_io[6];
static u32 et_serial,et_records,et_error,et_session,et_argument;
static u32 et_loop_phase,et_loop_seen,et_result_seen;
static u32 et_result_draw_pending,et_result_drawing;
/* Main-thread-only bounded backlog. The CD reader owns the same FILEIO lock;
 * contention must defer a checkpoint, not permanently disable the journal. */
#define ET_PENDING 32u
static u32 et_pending[ET_PENDING][32],et_head,et_queued,et_written,et_busy;
#ifndef EXIT_TRACE_TEST
extern unsigned long long exit_result_create_body(void),exit_result_draw_body(void),exit_result_side_body(void);
#endif
void exit_trace_arg(u32 a){et_argument=a;}
#ifndef EXIT_TRACE_TEST
extern s32 vd_exit_append(const char *,u32 *,const void *,u32);
#define et_path() ((const char *)(unsigned long)input_probe.path)
#define et_id input_probe.chart_id
#define et_build_id input_probe_build_id
#define et_crc(p,n) cp_crc_full(p,n)
static u32 et_clock(void){u32 r;__asm__ volatile("mfc0 %0,$9":"=r"(r));return r;}
#define et_append vd_exit_append
#define et_native(a) ((unsigned long long (*)(unsigned long long,unsigned long long,unsigned long long,unsigned long long,unsigned long long,unsigned long long,unsigned long long,unsigned long long))(a))
#endif
static s32 et_prefix(char *prefix)
{
    const char *in=et_path();u32 len=0;
    if(!in){et_error=1;return 0;}
    while(len<140 && in[len]){prefix[len]=in[len];len++;LOOP_PAD();}
    if(in[len] || len<6 || prefix[len-6]!='i' || prefix[len-5]!='n' || prefix[len-4]!='p' || prefix[len-3]!='u' || prefix[len-2]!='t' || prefix[len-1]!='-'){et_error=2;return 0;}
    prefix[len-6]='e';prefix[len-5]='x';prefix[len-4]='i';prefix[len-3]='t';prefix[len-2]='-';prefix[len-1]=0;
    return 1;
}
static void et_flush(void)
{
    u32 phase=et_word(0x2ad534);
    if(et_error || !et_queued || (phase>=5 && phase<8))return;
    char prefix[160];if(!et_prefix(prefix))return;
    /* Each flush attempts one closed record; never wait for the CD reader. */
    s32 r=et_append(prefix,&et_serial,et_pending[et_head],128);
    if(r==-8){et_busy++;return;}
    if(r<0){et_error=(u32)-r;return;}
    et_head=(et_head+1)%ET_PENDING;et_queued--;et_written++;
}
static void et_note(u32 stage)
{
    u32 phase=et_word(0x2ad534);
    if(!stage){
        et_loop_phase=et_loop_seen=et_result_seen=0;
        et_head=et_queued=et_written=et_busy=0;
        et_result_draw_pending=1;et_result_drawing=0;
    }
    /* Retry deferred records even on a deduplicated stage or after the first
     * visible RESULT draw. No retry/write in active play or its fade. */
    et_flush();
    if(et_error || et_records>=256 || (!exit_trace_armed && stage))return;
    if(stage && phase<8)return;
    u32 loop_bit=0,result_bit=0;
    if(stage==1 || stage==2 || (stage>=0x60 && stage<=0x69)) {
        if(et_loop_phase!=phase){et_loop_phase=phase;et_loop_seen=0;}
        loop_bit=1u<<(stage<=2?stage-1:stage-0x60+2);
        if(et_loop_seen&loop_bit)return;
    } else if((stage>=0x30 && stage<=0x35) || (stage>=0x40 && stage<=0x45)) {
        result_bit=1u<<(stage>=0x40?stage-0x40+6:stage-0x30);
        if(et_result_seen&result_bit)return;
    }
    if(et_queued==ET_PENDING){et_error=9;return;}
    char prefix[160];if(!et_prefix(prefix))return;
    u32 record[32];for(u32 i=0;i<32;i++){record[i]=0;LOOP_PAD();}u8 *b=(u8 *)record;
    const char *magic="MMIXEXT1";for(u32 i=0;i<8;i++){b[i]=magic[i];LOOP_PAD();}
    record[2]=1;record[3]=et_records;record[4]=stage;record[5]=et_clock();
    record[6]=phase;record[7]=et_word(0x2ad024);record[8]=et_word(0x2ad470);
    record[9]=et_word(0x2ad5b8);record[10]=exit_trace_stage;
    for(u32 i=0;i<6;i++){record[11+i]=exit_io[i];LOOP_PAD();}
    for(u32 i=0;i<16;i++){b[68+i]=stage?et_id[i]:0;b[84+i]=et_build_id[i];LOOP_PAD();}
    record[16]=et_argument; /* Former reserved I/O word; native arg0/record pointer. */
    record[25]=et_word(0x2ad120);record[26]=et_word(0x78af08);
    record[27]=et_word(0x346a28);record[28]=et_word(0x346a3c);record[29]=et_word(0x78b70c);
    record[30]=et_word(0x2ad06c);record[31]=et_crc(record,124);
    u32 *queued=et_pending[(et_head+et_queued)%ET_PENDING];
    for(u32 i=0;i<32;i++){queued[i]=record[i];LOOP_PAD();}
    et_queued++;et_records++;et_loop_seen|=loop_bit;et_result_seen|=result_bit;
    /* The prefix check above keeps malformed paths out of the queue. */
    et_flush();
}

#define ET_CALL(name,address,stage) \
unsigned long long exit_trace_##name(unsigned long long a,unsigned long long b,unsigned long long c,unsigned long long d,unsigned long long e,unsigned long long f,unsigned long long g,unsigned long long h) \
{ \
    exit_trace_arg((u32)a);exit_trace_mark(stage); \
    unsigned long long r=et_native(address)(a,b,c,d,e,f,g,h); \
    exit_trace_arg((u32)a);exit_trace_mark(stage+1);return r; \
}
void exit_trace_mark(u32);
ET_CALL(movie_tick,0x131aa0,0x60)
ET_CALL(vblank,0x10eca0,0x62)
ET_CALL(flip,0x139be0,0x64)
ET_CALL(audio_tick,0x13f5b0,0x66)
ET_CALL(frame_tail,0x14e250,0x68)
ET_CALL(cleanup,0x13a510,0x50)
ET_CALL(score_finish,0x1e48f0,0x52)
ET_CALL(preferences,0x1f3930,0x54)
ET_CALL(play_free,0x1f9db0,0x56)
ET_CALL(first_lamp,0x1a06e0,0x80)
ET_CALL(play_count,0x19ce10,0x82)
ET_CALL(lamp_commit,0x1a0420,0x84)
ET_CALL(score_commit,0x19fea0,0x86)
ET_CALL(unlock_commit,0x19fc00,0x88)
ET_CALL(score_stats,0x1e1a60,0x90)
ET_CALL(score_rank,0x22ccc0,0x92)
ET_CALL(score_history,0x199910,0x94)
/* Re-arm one main-loop pass after construction. Phase9 spans both the final
 * PLAY wait and RESULT loading, so a per-phase mask alone hides this boundary. */
unsigned long long exit_trace_result_create(unsigned long long a,unsigned long long b,unsigned long long c,unsigned long long d,unsigned long long e,unsigned long long f,unsigned long long g,unsigned long long h)
{
    exit_trace_arg((u32)a);exit_trace_mark(0xa0);
    unsigned long long r=et_native((u32)(unsigned long)exit_result_create_body)(a,b,c,d,e,f,g,h);
    et_loop_seen=0;
    exit_trace_arg((u32)a);exit_trace_mark(0xa1);return r;
}
#undef ET_CALL

/* Trace only the first visible RESULT draw, including each participating
 * player's side. Later animation frames have no journal writes. Constructor
 * and draw entry trampolines retain their original native instructions. */
unsigned long long exit_trace_result_draw(unsigned long long a,unsigned long long b,unsigned long long c,unsigned long long d,unsigned long long e,unsigned long long f,unsigned long long g,unsigned long long h)
{
    u32 state=*(u32 *)((unsigned long)a+0x2c);
    u32 trace=et_result_draw_pending && state>=3 && state<=4;
    if(trace){exit_trace_armed=1;et_result_drawing=1;exit_trace_arg((u32)a);exit_trace_mark(0xa2);}
    unsigned long long r=et_native((u32)(unsigned long)exit_result_draw_body)(a,b,c,d,e,f,g,h);
    if(trace){exit_trace_arg((u32)a);exit_trace_mark(0xa3);et_result_drawing=0;et_result_draw_pending=0;exit_trace_armed=0;}
    return r;
}
unsigned long long exit_trace_result_side(unsigned long long a,unsigned long long b,unsigned long long c,unsigned long long d,unsigned long long e,unsigned long long f,unsigned long long g,unsigned long long h)
{
    u32 stage=0xb0+(b==1?2:0);
    if(et_result_drawing){exit_trace_arg((u32)a);exit_trace_mark(stage);}
    unsigned long long r=et_native((u32)(unsigned long)exit_result_side_body)(a,b,c,d,e,f,g,h);
    if(et_result_drawing){exit_trace_arg((u32)a);exit_trace_mark(stage+1);}
    return r;
}


void exit_trace_mark(u32 n)
{
    if(exit_trace_armed)exit_trace_stage=n;
    et_note(n);
    /* Keep tracing through score writing, container unload and RESULT load.
     * Stop only once the actual RESULT scene is visible (active/fading out). */
    if(n>=0x33 && n<=0x35)exit_trace_armed=0;
}

unsigned long long exit_trace_play(void)
{
    u32 phase=et_word(0x2ad534);
    if(!phase)et_session=0;
    if(phase>=1 && phase<=4 && (!et_session || (!et_records && et_error==8))) {
        et_session=1;et_serial=et_records=et_error=0;et_note(0);
    }
    /* Normal scene1a16c0 calls1f2170, not1f0b60. Its active phase is5,
     * exit fade7, MPEG wait8 and final cleanup9; fade tick is2ad5b8. */
    exit_trace_intro=phase>=1 && phase<=4;
    if(phase>=7 && phase<=9)exit_trace_armed=1;
    else {exit_trace_armed=0;exit_trace_stage=exit_trace_frames=0;}
    exit_trace_mark(1);
    unsigned long long r=exit_play_body();
    phase=et_word(0x2ad534);
    if(phase>=7 && phase<=9)exit_trace_armed=1;
    exit_trace_mark(2);
    return r;
}

void exit_trace_audio_stop(void)
{
    exit_trace_mark(0x10);et_audio_stop();exit_trace_mark(0x11);
}
void exit_trace_unload(void)
{
    exit_trace_mark(0x12);et_unload();exit_trace_mark(0x13);
}

static char *et_hex(char *p,u32 v)
{
    const char *h="0123456789ABCDEF";
    for(s32 i=28;i>=0;i-=4){*p++=h[(v>>i)&15];LOOP_PAD();}
    return p;
}
static void et_status(const char *label)
{
    char title[36],*p=title;const char *t=label;
    while(*t){*p++=*t++;LOOP_PAD();}
    *p++=' ';*p++='J';*p++='=';p=et_hex(p,et_written);
    *p++=' ';*p++='E';*p++='=';p=et_hex(p,et_error);*p=0;
    et_font(title,0,48,50,100,0,0,0,0);
}
static void et_line(s32 y,const char *labels,u32 a,u32 b,u32 c)
{
    char line[36],*p=line;
    *p++=labels[0];*p++='=';p=et_hex(p,a);*p++=' ';
    *p++=labels[1];*p++='=';p=et_hex(p,b);*p++=' ';
    *p++=labels[2];*p++='=';p=et_hex(p,c);*p=0;
    et_font(line,0,48,y,100,0,0,0,0);
}
static s32 et_font_ready(void)
{
    /* Native unload frees entries without clearing them;136190 reuses a slot
     * by replacing only its id. A queued FONT may still hold the previous
     * PLAY HUD's base/count/rects. The native CD queue is retired only after
     * its unpack callback completes. Never draw textured diagnostics until
     * all pending native assets have finished. Rectangles remain available. */
    if(et_word(0x78af08))return 0;
    for(u32 i=0;i<et_count;i++) {
        u32 *e=(u32 *)(et_entries+i*0x228);
        if(e[0x84]==0x1c4 && e[0] && e[3]>0x18c)return 1;
        LOOP_PAD();
    }
    return 0;
}

/* Font-independent fallback after native all-container unload. These bounded
 * seven-segment hex digits use only the game's rectangle primitive, with no
 * textures, file reads or retained pointers into the freed font container. */
static void et_digit(s32 x,s32 y,u32 digit)
{
    static const u8 masks[16]={0x3f,6,0x5b,0x4f,0x66,0x6d,0x7d,7,0x7f,0x6f,0x77,0x7c,0x39,0x5e,0x79,0x71};
    static const u8 rects[7][4]={{1,0,5,1},{5,1,6,6},{5,7,6,12},{1,12,5,13},{0,7,1,12},{0,1,1,6},{1,6,5,7}};
    for(u32 i=0;i<7;i++) {
        if(masks[digit&15]&(1u<<i))et_rect(x+rects[i][0],y+rects[i][1],x+rects[i][2],y+rects[i][3],0xffffff,0);
        LOOP_PAD();
    }
}
static void et_hex_row(s32 y,u32 a,u32 b,u32 c)
{
    u32 values[3]={a,b,c};
    for(u32 j=0;j<3;j++)for(u32 i=0;i<8;i++) {
        et_digit(48+j*100+i*8,y,values[j]>>(28-i*4));LOOP_PAD();
    }
}

void exit_trace_render(void)
{
    /* An intro-only label proves this path runs before a long hardware test.
     * No drawing in active phase5. Exit display starts as soon as fade7 does,
     * and remains during MPEG wait8. Guard the native font after unload. */
    /* Native packet arena is256KiB. Leave at least48KiB after the largest
     * fallback (at most1030 rectangles x80B). Never add packets out of bounds. */
    s32 intro=exit_trace_intro && !exit_trace_armed && et_word(0x2ad534)<5;
    u32 packet=et_word(0x2ad040);
    u32 spare=(packet>=0x34d100 && packet<=0x38d100)?0x38d100-packet:0;
    if(spare<0x20000) {
        if(spare>=0x2000 && (intro || exit_trace_armed)) {
            et_digit(48,50,14);et_digit(56,50,1);et_digit(64,50,1);
        }
        exit_render_body();return;
    }
    s32 font=0;
    if(exit_trace_intro || exit_trace_armed)font=et_font_ready();
    if(intro) {
        if(font)et_status("R17 INTRO");
        else {et_digit(48,50,14);et_digit(56,50,1);et_digit(64,50,1);}
    }
    if(exit_trace_armed) {
        exit_trace_frames++;
        /* Native DMA chains run31 down to0: lower layers are in front.
         * R8/R9 incorrectly put text31 behind backing30 and native fades2. */
        et_rect(38,42,585,184,0,1);
        if(font) {
            et_status("R17 EXIT");
            et_line(68,"PSH",et_word(0x2ad534),exit_trace_stage,exit_trace_frames);
            et_line(86,"AQM",et_word(0x2ad120),et_word(0x78af08),et_word(0x2ad024));
            et_line(104,"DOR",et_word(0x346a28),et_word(0x346a3c),et_word(0x78b70c));
            et_line(122,"ILN",exit_io[0],exit_io[1],exit_io[2]);
            et_line(140,"VEC",exit_io[3],exit_io[4],et_word(0x2ad470));
            et_line(158,"JQB",et_written,et_queued,et_busy);
        } else {
            et_digit(48,50,14);et_digit(56,50,1);et_digit(64,50,1);
            et_hex_row(68,et_word(0x2ad534),exit_trace_stage,exit_trace_frames);
            et_hex_row(86,et_word(0x2ad120),et_word(0x78af08),et_word(0x2ad024));
            et_hex_row(104,et_word(0x346a28),et_word(0x346a3c),et_word(0x78b70c));
            et_hex_row(122,exit_io[0],exit_io[1],exit_io[2]);
            et_hex_row(140,exit_io[3],exit_io[4],et_word(0x2ad470));
            et_hex_row(158,et_written,et_queued,et_busy);
        }
    }
    exit_render_body();
}
