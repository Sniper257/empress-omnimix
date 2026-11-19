/* Summary-only SAVE/LOAD observation. Count is extended by frequent samples;
 * an individual blocked call spanning a complete Count wrap remains ambiguous.
 * Inside-call metrics include preemption; nested metrics are inclusive.
 * Only the completed native UI boundary may export. Never called from PLAY. */
#include "save_probe.h"
#include "save_checkpoint.h"
sp_control save_probe={1};
static sp_report sp_data;
static unsigned sp_active,sp_ready,sp_depth,sp_current,sp_ui_state,sp_last;
static unsigned sp_io_active,sp_io_op,sp_io_n,sp_backend,sp_retry;
static sp_u64 sp_elapsed,sp_io_since;
static unsigned sp_chart_blocked,sp_idle_divider,sp_entries,sp_terminals,sp_last_kind,sp_last_state;
static unsigned sp_status_serial,sp_status_done,sp_status_entries,sp_status_terminals,sp_status_error;
#ifndef SAVE_PROBE_TEST
static unsigned sp_clock(void){unsigned n;__asm__ volatile("mfc0 %0,$9":"=r"(n));return n;}
extern int vd_probe_export(const char *,unsigned *,const void *,unsigned,const void *,unsigned);
#define sp_export(p,s,h,hn,d,dn) vd_probe_export(p,s,h,hn,d,dn)
#endif
static void sp_zero(void *p,unsigned n){unsigned char *s=p;while(n--){*s++=0;
#if defined(__mips__)
    __asm__ volatile("nop\n\tnop\n\tnop");
#endif
}}
static void sp_tick(void)
{
    unsigned now=sp_clock(),delta=now-sp_last;sp_last=now;sp_elapsed+=delta;
    sp_data.phase[sp_current].wall+=delta;
    if(sp_ui_state<32)sp_data.ui[sp_ui_state].wall+=delta;
}
sp_u64 sp_timer(void){return sp_active?(1ull<<63)|((sp_u64)sp_current<<32)|sp_clock():0;}
void sp_phase(unsigned n)
{if(sp_active && n<SP_ENCODE){sp_tick();sp_current=n;}}
void sp_cost(unsigned n,sp_u64 token)
{
    if(!token || !sp_active || n>=SP_PHASES)return;
    unsigned ticks=sp_clock()-(unsigned)token;sp_metric *m=sp_data.phase+n;
    m->inside+=ticks;m->calls++;if(ticks>m->max_inside)m->max_inside=ticks;
}
void sp_restore(sp_u64 token){if(token)sp_phase((unsigned)(token>>32)&255u);}
void sp_flush(void)
{
    if(!save_probe.enabled || !sp_ready || sp_active || sp_depth || sp_io_active || !save_probe.path[0])return;
    /* Failed exports retain the frozen report. Retry at idle SAVE/LOAD entry,
     * completed RESULT boundaries, or guarded menu tails; never during PLAY. */
    int r=sp_export(save_probe.path,&save_probe.serial,sp_data.h,sizeof sp_data.h,
                    (unsigned char *)&sp_data+sizeof sp_data.h,sizeof sp_data-sizeof sp_data.h);
    save_probe.last_error=(unsigned)r;
    if(r>=0){save_probe.exports++;sp_ready=0;}else sp_retry++;
}
sp_u64 sp_enter(unsigned kind,unsigned state,unsigned port,unsigned format,unsigned policy,unsigned rows)
{
    if(!save_probe.enabled || (port && !sp_active))return 0;
    if(port)sp_data.h[6]|=0x80000000u;
    if(!sp_active){
        if(!state)sp_flush();if(sp_ready){save_probe.blocked++;return 0;}
        sp_zero(&sp_data,sizeof sp_data);sp_active=1;sp_depth=sp_io_active=sp_backend=0;
        sp_elapsed=0;sp_current=SP_UI;sp_ui_state=32;sp_last=sp_clock();
        sp_data.h[0]=0x58494d4d;sp_data.h[1]=0x31505653; /* MMIXSVP1 */
        sp_data.h[2]=1;sp_data.h[3]=sizeof sp_data;sp_data.h[4]=kind;
        sp_data.h[6]=port|(format<<8);sp_data.h[7]=policy;sp_data.h[8]=rows;
        sp_data.h[11]=sp_last;
    }
    sp_entries++;sp_last_kind=kind;sp_last_state=state;
    sp_tick();sp_depth++;
    if(kind<=SP_LOAD_AUTO){
        sp_ui_state=state<32?state:31;sp_data.ui[sp_ui_state].calls++;sp_data.h[15]++;
        if(!sp_backend)sp_phase((kind<=SP_AUTO && (state==1 || state==2))?SP_PACK:SP_UI);
    }else {sp_backend=1;sp_data.h[6]|=0x20000000u;}
    return (1ull<<63)|((sp_u64)kind<<32)|sp_clock();
}
void sp_diag(const unsigned *d)
{
    if(!sp_active || (!(sp_data.h[6]&0x20000000u) && !sp_data.h[17]))return;
    sp_data.h[19]=d[4];sp_data.h[20]=d[5];sp_data.h[21]=d[6];
    sp_data.h[22]=d[7];sp_data.h[23]=d[8];
}
void sp_leave(sp_u64 token,int result,int error)
{
    if(!token || !sp_active)return;
    unsigned kind=(unsigned)(token>>32)&255,ticks=sp_clock()-(unsigned)token;
    if(kind<=SP_LOAD_AUTO){sp_metric *m=sp_data.ui+sp_ui_state;m->inside+=ticks;if(ticks>m->max_inside)m->max_inside=ticks;}
    else if(result)sp_backend=0;
    sp_tick();
    if(kind>SP_LOAD_AUTO && result){sp_data.h[5]=(unsigned)sp_elapsed;sp_data.h[16]=(unsigned)(sp_elapsed>>32);sp_data.h[6]|=0x40000000u;}
    if(sp_depth)sp_depth--;
    if(!result || sp_depth)return;
    sp_data.h[9]=(unsigned)result;sp_data.h[10]=(unsigned)error;sp_data.h[12]=sp_last;
    sp_data.h[13]=(unsigned)sp_elapsed;sp_data.h[14]=(unsigned)(sp_elapsed>>32);
    sp_data.h[24]=save_probe.count_lo;sp_data.h[25]=save_probe.count_hi;
    sp_data.h[26]=save_probe.bus_lo;sp_data.h[27]=save_probe.bus_hi;
    sp_data.h[28]=save_probe.samples;sp_data.h[29]=save_probe.bus_hz;
    sp_data.h[30]=cp_crc_full((unsigned char *)&sp_data+sizeof sp_data.h,sizeof sp_data-sizeof sp_data.h);
    sp_data.h[31]=cp_crc_full(sp_data.h,124);
    sp_terminals++;sp_active=0;sp_ready=1;sp_flush(); /* Export is outside measured UI time. */
}
void sp_tx(unsigned flags){if(sp_active){sp_data.h[17]++;sp_data.h[18]|=flags;}}
void sp_io_start(unsigned op,unsigned n,int result,sp_u64 token)
{
    if(!sp_active || !token || op>=7)return;
    unsigned ticks=sp_clock()-(unsigned)token;sp_rpc *m=sp_data.io+op;m->inside+=ticks;
    if(result==1){m->busy++;return;}if(result<0){m->errors++;return;}
    sp_tick();sp_io_active=1;sp_io_op=op;sp_io_n=n;sp_io_since=sp_elapsed;m->calls++;
}
void sp_io_poll(int status,int result,sp_u64 token)
{
    if(!sp_active || !sp_io_active || !token)return;
    sp_rpc *m=sp_data.io+sp_io_op;m->inside+=sp_clock()-(unsigned)token;m->polls++;
    if(!status){m->pending++;return;}
    sp_tick();sp_u64 elapsed=sp_elapsed-sp_io_since;m->elapsed+=elapsed;
    if(elapsed>m->max_elapsed)m->max_elapsed=elapsed>0xffffffffu?0xffffffffu:(unsigned)elapsed;
    if(status<0 || result<0)m->errors++;
    else if(sp_io_op==2 || sp_io_op==3)m->bytes+=(unsigned)result; /* READ/WRITE */
    sp_io_active=0;
}

/* A completed native MC dialog is not an idle-storage guarantee. R56 had no
 * retry between dialog completions: one temporarily busy export could suppress
 * all subsequent captures. Retry ONLY on idle menu tails, at most once per30
 * updates. Entry to chart preparation blocks this path until SELECT returns.
 * The native shared SAVE UI must also be fully idle (state0).
 * A small independent health receipt makes capture/export failures observable;
 * no screen overlay or PLAY I/O, and existing captures are never overwritten. */
void sp_set_chart(unsigned on){sp_chart_blocked=!!on;}
void sp_idle(unsigned native_state)
{
    if(!save_probe.enabled || sp_chart_blocked || native_state)return;
    if(++sp_idle_divider<30)return;sp_idle_divider=0;
    if(!sp_active && !sp_depth && !sp_io_active)sp_flush();
    if(!save_probe.path[0])return;
    if(sp_status_done && sp_status_entries==sp_entries && sp_status_terminals==sp_terminals)return;
    /* Native UI0 establishes an idle scene. A stale observer MC flag must be
     * visible in the health receipt instead of suppressing every status file.
     * A timing report still cannot flush while its observed request is pending. */
    unsigned h[32];sp_zero(h,sizeof h);
    h[0]=0x58494d4d;h[1]=0x31445653; /* MMIXSVD1 */
    h[2]=1;h[3]=sizeof h;h[4]=save_probe.enabled;
    h[5]=sp_entries;h[6]=sp_terminals;h[7]=sp_active;h[8]=sp_ready;h[9]=sp_depth;
    h[10]=sp_io_active;h[11]=save_probe.exports;h[12]=save_probe.serial;
    h[13]=save_probe.last_error;h[14]=save_probe.blocked;h[15]=sp_retry;
    h[16]=sp_last_kind;h[17]=sp_last_state;h[18]=sp_backend;h[19]=native_state;
    h[20]=sp_clock();h[21]=sp_status_error;
    h[31]=cp_crc_full(h,124);
    char path[80];unsigned n=0;
    while(n<sizeof save_probe.path && save_probe.path[n]){path[n]=save_probe.path[n];n++;}
    if(n>=sizeof save_probe.path)return;
    const char *suffix="status-";while(*suffix){path[n++]=*suffix++;
#if defined(__mips__)
        __asm__ volatile("nop\n\tnop\n\tnop");
#endif
    }path[n]=0;
    int r=sp_export(path,&sp_status_serial,h,sizeof h,0,0);
    sp_status_error=(unsigned)r;
    if(r>=0){sp_status_done=1;sp_status_entries=sp_entries;sp_status_terminals=sp_terminals;}
}
#ifndef SAVE_PROBE_TEST
#define SP_ARGS unsigned long long a,unsigned long long b,unsigned long long c,unsigned long long d,unsigned long long e,unsigned long long f,unsigned long long g,unsigned long long h
#define SP_PASS a,b,c,d,e,f,g,h
extern unsigned long long save_probe_chart_body(SP_ARGS),save_probe_select_body(SP_ARGS);
unsigned long long save_probe_chart(SP_ARGS)
{sp_set_chart(1);return save_probe_chart_body(SP_PASS);}
unsigned long long save_probe_select(SP_ARGS)
{unsigned long long r=save_probe_select_body(SP_PASS);sp_set_chart(0);return r;}
unsigned long long save_probe_tail(SP_ARGS)
{
    unsigned long long r=((unsigned long long (*)(SP_ARGS))0x14e250)(SP_PASS);
    sp_idle(*(volatile unsigned *)0x796ddc);return r;
}
#undef SP_ARGS
#undef SP_PASS
#endif
