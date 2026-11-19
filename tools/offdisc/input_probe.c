/* Optional diagnostic build only. No pad requests, allocations or file I/O in
 * the sampler. Native libpad snapshot hook writes the exact returned sequence.
 * OFF leaves all three sampling sites unpatched in the launcher. */
#include "input_probe.h"
input_probe_config input_probe={1};
unsigned input_probe_selected;
#ifndef INPUT_LOG_ID
#define INPUT_LOG_ID {0}
#endif
const unsigned char input_probe_build_id[16]=INPUT_LOG_ID;
static struct {unsigned sequence,length,ticks,buttons;} ip_port[2];
#ifndef INPUT_PROBE_TEST
static unsigned ip_clock(void){unsigned r;__asm__ volatile("mfc0 %0,$9":"=r"(r));return r;}
extern unsigned long long input_frame_body(void);
extern int input_read_body(int,int,void *);
extern unsigned vd_probe_reads(void);
extern int vd_probe_export(const char *,unsigned *,const void *,unsigned,const void *,unsigned);
#define ip_native_frame() input_frame_body()
#define ip_native_read(p,s,b) input_read_body(p,s,b)
#define ip_native_reset() (((void (*)(void))0x1ed660)())
#define ip_pad(p) ((u8 *)(0x724b80u+(p)*0x1c0u))
#define ip_held() (*(unsigned *)0x2ad004)
#define ip_pressed() (*(unsigned *)0x2ad008)
#define ip_chart_frame() (*(unsigned *)0x2ad470)
#define ip_source() chart_identity(song_now(),ps_mode()==2,ps_diff())
#define ip_mode() ps_mode()
#define ip_diff() ps_diff()
#define ip_delay(p) ip_pad(p)[0x182]
#define ip_storage() vd_probe_reads()
#define ip_autoplay() (*(volatile u8 *)0x2ad440 & 3u)
#define ip_export(p,s,h,hn,d,dn) vd_probe_export(p,s,h,hn,d,dn)
#define ip_crc(p,n) cp_crc_full(p,n)
#define ip_write32(p,v) cp_write32(p,v)
#endif
#ifdef FILE_IO_LOG
void vd_io_start(void),vd_io_stop(void);
const void *vd_io_snapshot(unsigned *);
#endif
/* Compatibility for older host fixtures; actual native builds always use the
 * verified 2ad440 side mask, never the save-mode number (battle is not AUTO). */
#ifndef ip_autoplay
#define ip_autoplay() 0u
#endif
#ifdef AUDIO_PROBE
#include "audio_probe.c"
#endif
static unsigned ip_auto(void)
{
    unsigned mask=ip_autoplay();
    if(mask) {input_probe.autoplay|=mask;input_probe.count=input_probe.dropped=0;}
    return input_probe.autoplay;
}
static void ip_clear_ports(void)
{
    ip_port[0].sequence=ip_port[0].length=ip_port[0].ticks=ip_port[0].buttons=0;
    ip_port[1].sequence=ip_port[1].length=ip_port[1].ticks=ip_port[1].buttons=0;
}
void input_probe_start(void)
{
#ifdef USB_BOOT_JOURNAL
    extern void boot_trace_stop(void);boot_trace_stop();
#endif
    ip_native_reset(); /* Displaced native note/queue reset ALWAYS executes. */
#ifdef PLAY_PREVIEW_ACTIVE
    if(PLAY_PREVIEW_ACTIVE())return;
#endif
    if(!input_probe.mode || (input_probe.mode!=3 && (!input_probe.buffer || !input_probe.capacity)))return;
    if(input_probe.ready && (input_probe.mode==2 || input_probe.mode==3)){input_probe.blocked++;return;}
    input_probe.running=0;input_probe.ready=0;input_probe.count=input_probe.dropped=0;input_probe_selected=0;
    input_probe.last_error=0;ip_clear_ports();
    input_probe.autoplay=0;ip_auto();
#ifdef FILE_IO_LOG
    vd_io_start();
#endif
    input_probe.metadata[0]=ip_mode();input_probe.metadata[1]=ip_diff();
    input_probe.metadata[2]=ip_delay(0);input_probe.metadata[3]=ip_delay(1);
    input_probe.metadata[4]=ip_clock();input_probe.metadata[5]=ip_storage();
    const u8 *id=ip_source();for(unsigned i=0;i<16;i++){input_probe.chart_id[i]=id?id[i]:0;LOOP_PAD();}
    __asm__ volatile("":::"memory");input_probe.running=1;
#ifdef AUDIO_PROBE
    ap_start();
#endif
}
int input_probe_read(int port,int slot,u8 *data)
{
    if(!input_probe.running || input_probe.mode==3 || ip_auto() || (unsigned)port>1 || slot)return ip_native_read(port,slot,data);
    unsigned begin=ip_clock();
    input_probe_selected=(unsigned)(unsigned long)&ip_port[port];
    int n=ip_native_read(port,slot,data); /* Original read and payload unchanged. */
    unsigned end=ip_clock();input_probe_selected=0;
    ip_port[port].ticks=end-begin;
    ip_port[port].length=n>0 && n<=128?(unsigned)n:255;
    ip_port[port].buttons=n>=4 && n<=128?((unsigned)data[2]<<8|data[3])^65535u:0;
    return n;
}
unsigned long long input_probe_frame(void)
{
#ifdef AUDIO_PROBE
    return ap_input_frame();
#else
    if(!input_probe.running || input_probe.mode==3 || ip_auto())return ip_native_frame();
    unsigned begin=ip_clock();ip_clear_ports();unsigned native_begin=ip_clock();
    unsigned long long result=ip_native_frame();unsigned native_end=ip_clock();
    if(ip_auto())return result;
    if(input_probe.count<input_probe.capacity){
        input_probe_record *r=(input_probe_record *)(unsigned long)input_probe.buffer+input_probe.count;
        r->tick=begin;r->sequence[0]=ip_port[0].sequence;r->sequence[1]=ip_port[1].sequence;
        r->read_ticks[0]=ip_port[0].ticks;r->read_ticks[1]=ip_port[1].ticks;
        r->buttons[0]=ip_port[0].buttons;r->buttons[1]=ip_port[1].buttons;
        r->length[0]=ip_port[0].length;r->length[1]=ip_port[1].length;
        r->state[0]=*(unsigned *)(ip_pad(0)+0x104);r->state[1]=*(unsigned *)(ip_pad(1)+0x104);
        r->held=ip_held();r->pressed=ip_pressed();r->chart_frame=ip_chart_frame();r->storage_reads=ip_storage();
        /* C sampler body only; excludes outer assembly save/restore and the
         * snapshot tap, which perturbs the instrumented read duration above. */
        r->sampler_ticks=(native_begin-begin)+(ip_clock()-native_end);
        /* Publish only complete records, including a state saved mid-update. */
        __asm__ volatile("":::"memory");input_probe.count++;
    }else input_probe.dropped++; /* Never overwrite the start or hide overflow. */
    return result;
#endif
}
void input_probe_stop(void)
{
    if(!input_probe.running)return;
    ip_auto();
#ifdef AUDIO_PROBE
    ap_stop();
#endif
#ifdef FILE_IO_LOG
    vd_io_stop();
#endif
    input_probe.metadata[6]=ip_clock();input_probe.metadata[7]=ip_storage();
    __asm__ volatile("":::"memory");input_probe.running=0;input_probe.ready=1;
}
void input_probe_flush(void)
{
    if((input_probe.mode!=2 && input_probe.mode!=3) || !input_probe.ready || !input_probe.path)return;
#ifdef AUDIO_PROBE
    ap_flush();
#endif
    u8 header[INPUT_PROBE_HEADER_BYTES];
    for(unsigned i=0;i<sizeof header;i++){header[i]=0;LOOP_PAD();}
    const char *magic="MMIXINP1";for(unsigned i=0;i<8;i++){header[i]=magic[i];LOOP_PAD();}
    ip_write32(header+8,1);ip_write32(header+12,sizeof header);ip_write32(header+16,48);
    ip_write32(header+20,input_probe.count);ip_write32(header+24,input_probe.capacity);ip_write32(header+28,input_probe.dropped);
    for(unsigned i=0;i<8;i++){ip_write32(header+32+4*i,input_probe.metadata[i]);LOOP_PAD();}
    for(unsigned i=0;i<16;i++){header[64+i]=input_probe.chart_id[i];LOOP_PAD();}
    ip_write32(header+80,1); /* EE-visible publication, not physical/SIO2 latency */
    ip_write32(header+84,input_probe.blocked);
    for(unsigned i=0;i<16;i++){header[88+i]=input_probe_build_id[i];LOOP_PAD();}
    ip_write32(header+104,input_probe.autoplay);
#ifdef FILE_IO_LOG
    unsigned io_bytes;const void *io=vd_io_snapshot(&io_bytes);
    header[7]='2';ip_write32(header+8,2);
    ip_write32(header+116,io_bytes);ip_write32(header+120,ip_crc(io,io_bytes));
#endif
    unsigned bytes=input_probe.count*48;
    ip_write32(header+112,ip_crc((void *)(unsigned long)input_probe.buffer,bytes));
    ip_write32(header+124,ip_crc(header,124));
    int r=ip_export((const char *)(unsigned long)input_probe.path,&input_probe.serial,header,sizeof header,(void *)(unsigned long)input_probe.buffer,bytes);
    input_probe.last_error=r<0?(unsigned)-r:0;
    if(r>=0){input_probe.exports++;input_probe.ready=0;}
    /* Failure keeps bytes frozen; next result exit retries, next chart is
     * deliberately not captured rather than silently destroying this log. */
}
