/* Explicit standalone qualification, NOT the game profiler. Reuses the exact
 * R120 embedded MMCE/USB images and native modules. Never writes storage or
 * switches Game ID. RAM-only video trace is sent after its capture freezes. */
#ifdef MSP_COEX_TEST
#include "test_coexist.h"
#else
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <iopheap.h>
#define NEWLIB_PORT_AWARE /* Only the supported fileXio RW-buffer setter. */
#include <fileXio_rpc.h>
#include <timer.h>
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ee_regs.h>
#endif
#include "protocol.h"
#include "config.h"
#include "frame.h"
#include "video_marker.h"
#ifndef MSP_COEX_TEST
#include "launcher/mmce_boot.h"
#include "launcher/usb_input.h"
#endif
int msp_boot(msp_config *);
int msp_exchange(unsigned,const msp_request *,msp_reply *);
void _libcglue_timezone_update(void){}
void _libcglue_rtc_update(void){}
typedef struct {const char *name;void *data;unsigned bytes;} coex_module;
extern coex_module coex_native[];
extern unsigned coex_native_count,coex_read_bytes,coex_read_crc;
static msp_config cfg={.abi=1,.enabled=2,.peer=0x0a4d4d01u,.local=0x0a4d4d02u};
static msp_request request __attribute__((aligned(64)));
static msp_reply reply;
static mu_frame usb_frames[2] __attribute__((aligned(128)));
static unsigned arm_words[32] __attribute__((aligned(128)));
static unsigned usb_gate,usb_token,usb_connected,usb_head,usb_new_frames;
static unsigned read_ok,heap_before_native,largest_before_native;
static unsigned char read_buffer[32768] __attribute__((aligned(64)));
static unsigned char pattern[8192] __attribute__((aligned(64)));
/* Header32words + bounded1024 records (32B each): separate from the24B
 * marker state and28B marker text, neither of which owns a frame history. */
static unsigned video[32+1024*8] __attribute__((aligned(64)));
static msp_video_marker marker;
static char marker_text[MSP_MARKER_TEXT],marker_drawn[MSP_MARKER_TEXT];
static unsigned count_now(void){
#ifdef MSP_COEX_TEST
 return test_count();
#else
 unsigned n;__asm__ volatile("mfc0 %0,$9":"=r"(n));return n;
#endif
}
static void finish(const char *text,int code){
 scr_printf("\n%s (%d)\n",text,code);
 scr_printf("No game files, settings or saves written.\nPower off before removing the card.\n");
 for(;;)SleepThread();
}
void msp_boot_progress(const char *name,int id,int result){
 if(id<0&&result==0)scr_printf("Loading %s...\n",name);
 else scr_printf("  %s id=%d start=%d\n",name,id,result);
}
static int usb_arm(void){
 memset(usb_frames,0,sizeof usb_frames);SyncDCache(usb_frames,usb_frames+2);
 if(mui_game_target((unsigned)usb_frames,&usb_gate,&usb_token))return -1;
 volatile unsigned *p=
#ifdef MSP_COEX_TEST
 arm_words;
#else
 (void *)((unsigned)arm_words|0x20000000u);
#endif
 p[0]=usb_token;p[1]=1;p[2]=p[3]=0;
#ifndef MSP_COEX_TEST
 __asm__ volatile("sync":::"memory");
#endif
 SifDmaTransfer_t d={.src=arm_words,.dest=(void *)usb_gate,.size=16,.attr=0};
 int id=SifSetDma(&d,1);if(id<=0)return -2;
 for(unsigned i=0;i<2000;i++){if(SifDmaStat(id)<0)return 0;usleep(1000);}
 return -3;
}
static void usb_sample(void){
 volatile const mu_frame *p=
#ifdef MSP_COEX_TEST
 usb_frames;
#else
 (void *)((unsigned)usb_frames|0x20000000u);
#endif
 mu_frame candidate,newest={0};
 for(unsigned i=0;i<2;i++)if(mu_snapshot(&candidate,p+i)&&(!newest.head||(int)(candidate.head-newest.head)>0))newest=candidate;
 if(newest.head&&newest.head!=usb_head){usb_new_frames++;usb_head=newest.head;}
 usb_connected|=(newest.connected[0]?1u:0u)|(newest.connected[1]?2u:0u);
}
static int read_asset(void){
 FILE *f=fopen("mmce0:/MEGAMIX/EMPRESS/F/0000.bin","rb");if(!f)return -1;
 /* Explicit extent, not a read-until-EOF loop. CRC uses the same bytes as
  * the source build receipt. No game/save file is ever opened for writing. */
 unsigned done=0,crc=~0u;int failed=0;
 while(done<coex_read_bytes){
  unsigned n=coex_read_bytes-done;if(n>sizeof read_buffer)n=sizeof read_buffer;
  size_t got=fread(read_buffer,1,n,f);if(!got||got>n){failed=1;break;}
  for(unsigned j=0;j<got;j++){crc^=read_buffer[j];for(unsigned k=0;k<8;k++)crc=(crc>>1)^(0xedb88320u&-(crc&1));}
  done+=(unsigned)got;usb_sample();
 }
 if(fclose(f))failed=1;
 return !failed&&done==coex_read_bytes&&~crc==coex_read_crc?0:-2;
}
static void rpc_status(void){request.bytes=0;if(msp_exchange(MSP_STATUS,&request,&reply))finish("Network status failed",-30);}
static int send_blob(const void *data,unsigned bytes,unsigned session){
 unsigned whole=msp_crc(data,bytes),parts=(bytes+MSP_CHUNK-1)/MSP_CHUNK;
 for(unsigned pass=0;pass<2;pass++)for(unsigned seq=0;seq<parts;seq++){
  unsigned offset=seq*MSP_CHUNK,n=bytes-offset;if(n>MSP_CHUNK)n=MSP_CHUNK;
  unsigned *h=(unsigned *)request.data;memcpy(h,MSP_MAGIC,8);
  h[2]=1;h[3]=session;h[4]=seq;h[5]=parts;h[6]=bytes;h[7]=offset;h[8]=n;
  memcpy(request.data+48,(const unsigned char *)data+offset,n);
  h[9]=msp_crc(request.data+48,n);h[10]=whole;h[11]=msp_crc(h,44);request.bytes=48+n;
  int r=-1;
  for(unsigned tries=0;tries<150;tries++){
   r=msp_exchange(MSP_DATA,&request,&reply);if(!r)break;
   if(r!=-2&&r!=-3&&r!=-4)return r;
   usb_sample();usleep(100000);
  }
  if(r)return r;
  usleep(1000);
 }
 for(unsigned i=0;i<30;i++){
  rpc_status();if(reply.ack_session==session&&reply.ack_crc==whole)return 0;
  usb_sample();usleep(100000);
 }
 return -40;
}
static void make_pattern(unsigned session,unsigned round){
 unsigned *h=(unsigned *)pattern;memset(pattern,0,128);memcpy(h,"MMIXNET1",8);
 h[2]=1;h[3]=128;h[4]=sizeof pattern;h[5]=session;h[6]=round;h[7]=count_now();
 h[8]=reply.clock_lo;h[9]=reply.clock_hi;h[10]=reply.link;h[11]=reply.dropped;h[12]=reply.free_total;h[13]=reply.free_max;
 h[14]=1;h[15]=heap_before_native;h[16]=largest_before_native;h[17]=read_ok;h[18]=usb_connected;h[19]=usb_new_frames;
 for(unsigned i=128;i<sizeof pattern;i++)pattern[i]=(unsigned char)((i*29+round*17)^(i>>8));
 h[30]=msp_crc(pattern+128,sizeof pattern-128);h[31]=msp_crc(h,124);
}
static int wait_vblank(void){
#ifdef MSP_COEX_TEST
 return test_vblank();
#else
 /* This standalone owns GS. Polling CSR is confined to this test and is NOT
  * an implementation suggestion for the native game's existing render path. */
 unsigned long long start=GetTimerSystemTime();*R_EE_GS_CSR=8;
 while(!(*R_EE_GS_CSR&8))if(GetTimerSystemTime()-start>294912000u)return -1;
 return 0;
#endif
}
static unsigned capture_video(unsigned session){
 scr_clear();scr_printf("MEGAMIX COEXTEST R122 - video/log sync\n\n10 seconds: marker off for2s, on for8s.\nRecord the marker and terminal together.\nUSB input + MMCE + native IOP resident.\nNo network sends during frame capture.\n\n");
 unsigned c0=count_now();unsigned long long b0=GetTimerSystemTime();usleep(100000);
 unsigned c1=count_now(),cal_count=c1-c0;unsigned long long cal_bus=GetTimerSystemTime()-b0;
 if(!cal_bus||!cal_count)finish("Clock calibration failed",-41);
 unsigned hz=(unsigned)((unsigned long long)cal_count*147456000u/cal_bus);
 if(hz<1000000u||hz>600000000u)finish("Clock calibration invalid",-42);
 unsigned count=0,dropped=0;unsigned long long start_bus=GetTimerSystemTime();
 msp_marker_begin(&marker,session,count_now(),hz);
 while(GetTimerSystemTime()-start_bus<1474560000u){
  if(count==1024){dropped++;break;}
  if(wait_vblank())finish("VBLANK timeout",-43);
  unsigned tick=count_now();msp_marker_sample(&marker,tick);
  unsigned ms=msp_marker_ms(&marker),visible=ms>=2000;
  unsigned *r=video+32+count*8;r[0]=marker.frame;r[1]=marker.last;r[2]=marker.high;r[3]=ms;r[4]=visible;
  unsigned before=count_now();
  if(visible){
   msp_marker_text(&marker,marker_text);
   /* SDK debug font uploads pixels per character. Avoid re-uploading the
    * unchanged session/labels/digits. Native game-font integration will use
    * its existing atlas instead; this test owns its standalone framebuffer. */
   for(unsigned j=0;j<MSP_MARKER_TEXT-1;j++)if(marker_text[j]!=marker_drawn[j]){
    scr_putchar(24+j*8,192,0xffffff,marker_text[j]);marker_drawn[j]=marker_text[j];
   }
  }
  r[5]=count_now()-before;usb_sample();r[6]=usb_head;r[7]=usb_connected;count++;
 }
 memcpy(video,"MMIXVID1",8);video[2]=1;video[3]=128;video[4]=128+count*32;video[5]=session;video[6]=count;video[7]=32;
 video[8]=cal_count;video[9]=(unsigned)cal_bus;video[10]=(unsigned)(cal_bus>>32);video[11]=147456000u;
 video[12]=marker.start;video[13]=hz;video[14]=dropped;video[15]=sizeof marker+sizeof marker_text+sizeof marker_drawn;
 video[16]=heap_before_native;video[17]=largest_before_native;video[18]=reply.free_total;video[19]=reply.free_max;
 video[20]=read_ok;video[21]=usb_new_frames;video[22]=usb_connected;video[23]=1; /* fixed NTSC480i field mode */
 video[30]=msp_crc(video+32,count*32);video[31]=msp_crc(video,124);
 scr_setXY(0,10);scr_printf("Captured %u renders; sending frozen trace...\n",count);
 return video[4];
}
int main(int argc,char **argv){
 (void)argc;(void)argv;init_scr();scr_setCursor(0);scr_clear();
 scr_printf("MEGAMIX COEXTEST R122\n\nPS2 10.77.77.2 / laptop 10.77.77.1\nStart updated receive.py first.\nNo files written; no Game ID switch.\n\nRestoring retail IOP + MMCE slot1...\n");
 SifInitRpc(0);mmce_boot_set_debug(0);if(mmce_boot_set_game_id(""))finish("Empty Game ID rejected",-1);
 if(mmce_boot_prepare(1))finish(mmce_boot_status(),-2);
 scr_printf("MMCE/base read OK; loading saved USB bindings...\n");
 fileXioSetRWBufferSize(2048);mui_load("mmce0:/MEGAMIX/");
 if(mui_boot_game())finish(mui_status(),-3);
 if(usb_arm())finish("USB game DMA arm failed",-4);
 scr_printf("USB game gate armed; loading network...\n");
 if(msp_boot(&cfg))finish("Network module initialization failed",-5);
 request.abi=MSP_ABI;request.token=cfg.token;request.local=cfg.local;request.peer=cfg.peer;
 rpc_status();heap_before_native=reply.free_total;largest_before_native=reply.free_max;
 scr_clear();scr_printf("R122 native modules (SDK SIO2 already loaded)\n");
 for(unsigned i=0;i<coex_native_count;i++){
  coex_module *m=coex_native+i;int result=-999;scr_printf("Loading %s...\n",m->name);
  int id=SifExecModuleBuffer(m->data,m->bytes,0,0,&result);scr_printf("  id=%d start=%d\n",id,result);
  if(id<0||(result!=0&&result!=2))finish("Native module load failed",-10-(int)i);
 }
 rpc_status();scr_printf("IOP free=%u largest=%u\nChecking two exact MMCE asset reads...\n",reply.free_total,reply.free_max);
 for(unsigned i=0;i<2;i++){if(read_asset())finish("MMCE exact read/CRC failed",-20);read_ok++;}
 scr_printf("Both read CRCs OK; waiting for link (30s)...\n");
 for(unsigned i=0;i<300;i++){rpc_status();usb_sample();if(reply.link)break;usleep(100000);}
 if(!reply.link)finish("NO LINK - restart with cable/receiver ready",-21);
 unsigned seed=(count_now()^reply.clock_lo)&0x7fffffffu;if(!seed)seed=1;
 unsigned video_bytes=capture_video(seed+10);int r=send_blob(video,video_bytes,seed+10);
 if(r)finish("Video trace not acknowledged; update receiver",r);
 scr_clear();scr_printf("MEGAMIX COEXTEST R122\nVideo trace ACK OK; last visible marker:\n%s\n\n",marker_text);
 unsigned passed=0;
 for(unsigned round=0;round<10;round++){
  rpc_status();usb_sample();make_pattern(seed+round,round);r=send_blob(pattern,sizeof pattern,seed+round);
  scr_printf("Round%u session%08x: %s (%d)\n",round+1,seed+round,r?"NO ACK":"PC ACK OK",r);
  if(!r)passed++;
  usleep(500000);
 }
 rpc_status();unsigned free_final=reply.free_total,largest_final=reply.free_max;
 request.bytes=0;r=msp_exchange(MSP_STOP,&request,&reply);
 int usb_stop=mui_quiesce();
 scr_printf("\nACK %u/10; reads %u/2; retries %u\nIOP free=%u largest=%u; USB frames=%u\nUSB connected slots mask=%u\n",passed,read_ok,reply.dropped,free_final,largest_final,usb_new_frames,usb_connected);
 if(r||usb_stop)finish("Stop/drain failed",-50);
 if(!usb_new_frames)finish("USB game DMA heartbeat not observed",-51);
 if(passed!=10)finish("Network incomplete - inspect laptop",-52);
 if(!usb_connected)finish("STACK PASS; USB device not observed - plug/retest",1);
 finish("COEXISTENCE PASS - game profiling still pending",0);return 0;
}
