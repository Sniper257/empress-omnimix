/* Included by vdisc.c only for FILE_IO_LOG diagnostic builds. All mutations
 * happen under its existing file-I/O lock; no allocation, formatting or writes
 * during charts. Timings include driver RPC/transfer and EE mapping/zero fill,
 * excluding time waiting to acquire that lock. */
#include "io_probe.h"
static io_probe_snapshot io_capture;
static unsigned io_active;
#ifndef IO_PROBE_TEST
static unsigned io_clock(void){unsigned n;__asm__ volatile("mfc0 %0,$9":"=r"(n));return n;}
#endif
#ifndef io_chart_frame
#ifdef HOST_TEST
#define io_chart_frame() 0u
#else
#define io_chart_frame() (*(volatile unsigned *)0x2ad470)
#endif
#endif
static void io_add64(unsigned at,unsigned n)
{ unsigned old=io_capture.h[at];io_capture.h[at]+=n;io_capture.h[at+1]+=io_capture.h[at]<old; }
static void io_count(unsigned at){if(io_active)io_capture.h[at]++;}
static void io_begin(io_probe_event *e,unsigned lba,unsigned n,unsigned kind)
{
    e->sequence=0;
    if(!io_active)return;
    e->sequence=++io_capture.h[IO_REQUESTS];e->tick=io_clock();e->ticks=0;
    e->lba=lba;e->requested=n;e->delivered=0;e->kind=kind|(io_chart_frame()<<2);e->error=0;
    io_count(kind==1?IO_SECTOR_READS:IO_PREADS);io_add64(IO_WANT_LO,n);
}
static void io_end(io_probe_event *e,unsigned delivered,unsigned error)
{
    if(!e->sequence)return;
    e->ticks=io_clock()-e->tick;e->delivered=delivered;e->error=error;
    io_add64(IO_GOT_LO,delivered);io_add64(IO_TICKS_LO,e->ticks);
    if(e->ticks>io_capture.h[IO_MAX_TICKS])io_capture.h[IO_MAX_TICKS]=e->ticks;
    if(error)io_count(IO_ERRORS);
    unsigned bucket=0,limit=1u<<12;
    while(bucket<7 && e->ticks>=limit){bucket++;limit<<=2;LOOP_PAD();}
    io_capture.h[IO_HIST+bucket]++;
    unsigned at=io_capture.h[IO_RING_NEXT];io_capture.ring[at]=*e;
    io_capture.h[IO_RING_NEXT]=(at+1)%64;
    if(io_capture.h[IO_RING_COUNT]<64)io_capture.h[IO_RING_COUNT]++;
    at=io_capture.h[IO_SLOW_COUNT];
    if(at<16)io_capture.h[IO_SLOW_COUNT]++;
    else { at=0;for(unsigned i=1;i<16;i++){if(io_capture.slow[i].ticks<io_capture.slow[at].ticks)at=i;LOOP_PAD();}
        if(e->ticks<=io_capture.slow[at].ticks)return; }
    io_capture.slow[at]=*e;
}
void vd_io_start(void)
{
    lock();io_active=0;
    unsigned *p=(unsigned *)&io_capture;
    for(unsigned i=0;i<sizeof io_capture/4;i++){p[i]=0;LOOP_PAD();}
    io_capture.h[IO_SCHEMA]=1;io_capture.h[IO_BYTES]=sizeof io_capture;
    io_capture.h[IO_EVENT_BYTES]=32;io_capture.h[IO_RING_CAP]=64;io_capture.h[IO_SLOW_CAP]=16;
    io_capture.h[IO_BEGIN]=io_clock();io_active=1;unlock();
}
void vd_io_stop(void){lock();io_active=0;io_capture.h[IO_END]=io_clock();unlock();}
const void *vd_io_snapshot(unsigned *bytes){*bytes=sizeof io_capture;return &io_capture;}
