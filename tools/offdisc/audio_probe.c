/* Private EE scheduling observer. No new polling, heap, RPC, file I/O, screen
 * drawing or audio commands in PLAY. The production dispatcher owns wake scheduling; this observer only logs. Each writer owns its
 * own bounded array; only the main thread resets/freezes/exports between songs.
 * Native priority-10 sound worker preempts the main thread, never vice versa;
 * recording itself contains no yield, syscall or interrupt masking. */
#define AP_MAIN 512u
#define AP_WORKER 1024u
typedef struct {unsigned tick,frame,kind,a,b,c,d,commit;} ap_event;
static struct {ap_event main[AP_MAIN],worker[AP_WORKER];} ap_data;
static volatile unsigned ap_running,ap_frame,ap_window,ap_auto;
static unsigned char ap_chart_id[16];
static unsigned ap_count[2],ap_drop[2],ap_ready,ap_serial,ap_blocked,ap_error,ap_generation;
static unsigned ap_native_error;
static unsigned ap_handles,ap_path_hash,ap_read_error;
static unsigned ap_fileio_attempted,ap_fileio_serial,ap_fileio_error;
static void ad_note(unsigned long long result);
#ifndef AUDIO_PROBE_TEST
extern int vd_probe_native_error;
extern unsigned vd_probe_handles,vd_probe_path_hash;
extern int vd_probe_read_error;
extern const void *vd_probe_fd_snapshot(unsigned *,unsigned *,unsigned *,int *);
#endif
#ifndef AUDIO_PROBE_TEST
#define ap_word(a) (*(volatile unsigned *)(unsigned long)(a))
#define ap_native(a) ((unsigned long long (*)(unsigned long long,unsigned long long,unsigned long long,unsigned long long,unsigned long long,unsigned long long,unsigned long long,unsigned long long))(unsigned long)(a))
#endif
static unsigned ap_suppress(void)
{
    unsigned mask=ip_autoplay();
    if(mask)ap_auto|=mask;
    return ap_auto;
}
static void ap_emit(unsigned owner,unsigned kind,unsigned a,unsigned b,unsigned c,unsigned d)
{
    if(!ap_running || ap_suppress())return;
    unsigned n=ap_count[owner],cap=owner?AP_WORKER:AP_MAIN;
    if(n>=cap){ap_drop[owner]++;return;}
    ap_event *r=owner?ap_data.worker+n:ap_data.main+n;
    r->commit=0;r->tick=ip_clock();r->frame=ap_frame;r->kind=kind;
    r->a=a;r->b=b;r->c=c;r->d=d;
    __asm__ volatile("":::"memory");r->commit=ap_generation;
    __asm__ volatile("":::"memory");ap_count[owner]=n+1;
}
static unsigned ap_near(void)
{return ap_running && ap_window && (int)(ap_window-ap_frame)>=0 && !ap_suppress();}
static void ap_start(void)
{
    if(ap_ready){ap_blocked++;return;}
    ap_running=0;ap_frame=ap_window=ap_auto=0;ap_count[0]=ap_count[1]=ap_drop[0]=ap_drop[1]=0;
    ap_generation++;if(!ap_generation)ap_generation=1;
    for(unsigned i=0;i<16;i++){ap_chart_id[i]=input_probe.chart_id[i];LOOP_PAD();}
    __asm__ volatile("":::"memory");ap_running=1;
}
static unsigned long long ap_input_frame(void)
{
    unsigned begin=ip_clock();
    unsigned long long result=ip_native_frame();
    if(!ap_running)return result;
    ap_frame++;
    if(ap_suppress())return result;
    unsigned pressed=ip_pressed(),held=ip_held();
    if(pressed&0x3fffu){ap_window=ap_frame+1;ap_emit(0,2,begin,pressed,held,ip_chart_frame());}
    if(ap_frame%60==1)ap_emit(0,1,begin,ip_chart_frame(),ip_delay(0),ip_delay(1));
    return result;
}
#define AP_ARGS unsigned long long a,unsigned long long b,unsigned long long c,unsigned long long d,unsigned long long e,unsigned long long f,unsigned long long g,unsigned long long h
#define AP_PASS a,b,c,d,e,f,g,h
unsigned long long ap_request(AP_ARGS)
{
    unsigned begin=ip_clock();
    unsigned long long result=ap_native(0x140260)(AP_PASS);
    if(ap_running && !ap_suppress()){ap_window=ap_frame+1;ap_emit(0,3,(unsigned)a,(unsigned)result,begin,ip_chart_frame());}
    ad_note(result);
    return result;
}
unsigned long long ap_wake(AP_ARGS)
{
    if(ap_near())ap_emit(0,4,0,0,0,0);
    return ap_native(0x13f5b0)(AP_PASS);
}
unsigned long long ap_wait(AP_ARGS)
{
    unsigned long long result=ap_native(0x10eca0)(AP_PASS);
    if(ap_near())ap_emit(1,10,(unsigned)result,0,0,0);
    return result;
}
unsigned long long ap_service(AP_ARGS)
{
    if(ap_near())ap_emit(1,11,0,0,0,0);
    unsigned long long result=ap_native(0x13f870)(AP_PASS);
    if(ap_near())ap_emit(1,16,(unsigned)result,0,0,0);
    return result;
}
unsigned long long ap_sample(AP_ARGS)
{
    unsigned long long result=ap_native(0x1411b0)(AP_PASS);
    if(ap_near())ap_emit(1,12,(unsigned)a,(unsigned)b,(unsigned)c,(unsigned)result);
    return result;
}
unsigned long long ap_batch(AP_ARGS)
{
    if(ap_near())ap_emit(1,13,ap_word(0x2ad160),ap_word(0x2ad164),ap_word(0x2ad1d0),0);
    return ap_native(0x1414b0)(AP_PASS);
}
unsigned long long ap_rpc(AP_ARGS)
{
    unsigned begin=ip_clock(),bytes=ap_word(0x2ad1d0);
    if(ap_near())ap_emit(1,14,(unsigned)a,bytes,0,0);
    unsigned long long result=ap_native(0x146310)(AP_PASS);
    if(ap_near())ap_emit(1,15,(unsigned)result,bytes,ap_word(0x2ad1d0),ip_clock()-begin);
    return result;
}
static void ap_stop(void)
{
    if(!ap_running)return;
    ap_suppress();ap_running=0;
    __asm__ volatile("":::"memory");
    if(ap_auto)ap_count[0]=ap_count[1]=ap_drop[0]=ap_drop[1]=0;
    ap_ready=1;
}
static void ap_fileio_dump(char *path,unsigned slash)
{
    if(ap_fileio_attempted)return;
    ap_fileio_attempted=1;
    unsigned bytes,mask,phases[4];int first;
    const void *data=vd_probe_fd_snapshot(&bytes,&mask,phases,&first);
    if(!data || bytes!=6432){ap_fileio_error=2;return;}
    unsigned header[32];for(unsigned i=0;i<32;i++){header[i]=0;LOOP_PAD();}
    unsigned char *wire=(unsigned char *)header;const char *magic="MMIXFIO1";
    for(unsigned i=0;i<8;i++){wire[i]=magic[i];LOOP_PAD();}
    header[2]=2;header[3]=128;header[4]=16;header[5]=32;header[6]=4;header[7]=mask;
    header[8]=(unsigned)first;header[9]=ap_handles;header[10]=ap_path_hash;
    header[11]=ap_serial;header[12]=ap_generation;header[13]=ap_auto;
    for(unsigned i=0;i<4;i++){header[14+i]=phases[i];LOOP_PAD();}
    header[18]=bytes;header[19]=32;header[30]=64;
    for(unsigned i=0;i<16;i++){wire[80+i]=ap_chart_id[i];wire[96+i]=input_probe_build_id[i];LOOP_PAD();}
    header[28]=ip_crc(data,bytes);header[29]=ap_read_error;header[31]=ip_crc(header,124);
    const char *tail="fileio-";for(unsigned i=0;i<8;i++){path[slash+i]=tail[i];LOOP_PAD();}
    int r=ip_export(path,&ap_fileio_serial,header,128,data,bytes);
    ap_fileio_error=r<0?(unsigned)-r:0;
}
static void ap_flush(void)
{
    if(!ap_ready || !input_probe.path)return;
    char path[160];const char *prefix=(const char *)(unsigned long)input_probe.path;
    unsigned len=0,slash=0;
    while(prefix[len] && len<140){path[len]=prefix[len];if(prefix[len]=='/')slash=len+1;len++;LOOP_PAD();}
    if(!slash || prefix[len]){ap_error=2;return;}
    const char *tail="audio-";for(unsigned i=0;i<7;i++){path[slash+i]=tail[i];LOOP_PAD();}
    unsigned header[32];for(unsigned i=0;i<32;i++){header[i]=0;LOOP_PAD();}
    unsigned char *wire=(unsigned char *)header;const char *magic="MMIXAUD1";
    for(unsigned i=0;i<8;i++){wire[i]=magic[i];LOOP_PAD();}
    header[2]=1;header[3]=128;header[4]=32;header[5]=AP_MAIN;header[6]=AP_WORKER;
    header[7]=ap_count[0];header[8]=ap_count[1];header[9]=ap_drop[0];header[10]=ap_drop[1];
    header[11]=ap_auto;header[12]=ap_frame;header[13]=ap_generation;header[14]=ap_blocked;
    /* Keep fixed partitions, but clear unused bytes OUTSIDE PLAY. */
    for(unsigned k=0;k<2;k++){
        unsigned count=ap_count[k],cap=k?AP_WORKER:AP_MAIN;
        ap_event *p=k?ap_data.worker:ap_data.main;
        for(unsigned i=count;i<cap;i++){volatile unsigned *v=(volatile unsigned *)(p+i);for(unsigned j=0;j<8;j++){v[j]=0;LOOP_PAD();}LOOP_PAD();}
    }
    for(unsigned i=0;i<16;i++){wire[64+i]=ap_chart_id[i];wire[80+i]=input_probe_build_id[i];LOOP_PAD();}
    header[28]=ip_crc(&ap_data,sizeof ap_data);header[31]=ip_crc(header,124);
    int r=ip_export(path,&ap_serial,header,128,&ap_data,sizeof ap_data);
    ap_native_error=(unsigned)vd_probe_native_error;
    ap_handles=vd_probe_handles;ap_path_hash=vd_probe_path_hash;ap_read_error=(unsigned)vd_probe_read_error;
    ap_error=r<0?(unsigned)-r:0;if(r>=0)ap_ready=0;
    /* First successful capture gets one small descriptor sidecar per boot.
     * Frozen first-read/idle snapshots only, no per-input/PLAY logging added. */
    if(r>=0)ap_fileio_dump(path,slash);
}

/* Status only at the existing final packet-submission boundary. Never run
 * FILEIO, stop capture or use a texture/font from a draw callback. In particular
 * RESULT entry is the unmodified native function. Export stays at RESULT exit.
 * Seven-segment rectangles reuse the proven font-free exit diagnostic primitive;
 * bounded packets plus headroom protect the native 256KiB GS packet arena. */
#ifndef AUDIO_PROBE_TEST
extern unsigned long long ap_render_body(AP_ARGS);
#define ap_render_native(AP_PASS) ap_render_body(AP_PASS)
#define ap_rect(x1,y1,x2,y2,c,z) ((void (*)(int,int,int,int,unsigned,int))0x12ff00)(x1,y1,x2,y2,c,z)
#endif
static void ap_digit(int x,int y,unsigned digit)
{
    static const unsigned char masks[16]={0x3f,6,0x5b,0x4f,0x66,0x6d,0x7d,7,0x7f,0x6f,0x77,0x7c,0x39,0x5e,0x79,0x71};
    static const unsigned char rects[7][4]={{1,0,5,1},{5,1,6,6},{5,7,6,12},{1,12,5,13},{0,7,1,12},{0,1,1,6},{1,6,5,7}};
    for(unsigned i=0;i<7;i++){
        if(masks[digit&15]&(1u<<i))ap_rect(x+rects[i][0],y+rects[i][1],x+rects[i][2],y+rects[i][3],0xffffff,0);
        LOOP_PAD();
    }
}
static void ap_row(int y,unsigned a,unsigned b,unsigned c)
{
    unsigned values[3]={a,b,c};
    for(unsigned j=0;j<3;j++)for(unsigned i=0;i<8;i++){
        ap_digit(24+j*110+i*8,y,values[j]>>(28-i*4));LOOP_PAD();
    }
}
unsigned long long ap_render(AP_ARGS)
{
    unsigned phase=ap_word(0x2ad534),packet=ap_word(0x2ad040);
    /* No draw/FS/clock/stop/export during PLAY or its fade/MPEG shutdown. */
    if(input_probe.mode && phase!=5 && phase!=6 && phase!=7 && phase!=8 &&
       (phase<5 || phase==9 || ap_ready || input_probe.ready) &&
       packet>=0x34d100 && packet<=0x38d100 && 0x38d100-packet>=0x18000){
        ap_rect(16,42,322,122,0,1);
        ap_row(48,0x47,ap_generation,ap_frame);
        ap_row(66,ap_count[0],ap_count[1],input_probe.running|(input_probe.ready<<1)|(ap_running<<2)|(ap_ready<<3)|(ap_auto<<16));
        ap_row(84,ap_serial,ap_error,ap_native_error);
        ap_row(102,ap_handles,ap_path_hash,ap_fileio_error);
    }
    return ap_render_native(AP_PASS);
}
