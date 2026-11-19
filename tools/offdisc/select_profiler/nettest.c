/* Standalone communication qualification. Contains no game data and does not
 * mount or write storage, switch Game IDs, or touch EMPRESS settings/saves. */
#include <kernel.h>
#include <sifrpc.h>
#include <iopcontrol.h>
#include <iopheap.h>
#include <loadfile.h>
#include <sbv_patches.h>
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "protocol.h"
#include "config.h"
#include "frame.h"
int msp_boot(msp_config *);
int msp_exchange(unsigned,const msp_request *,msp_reply *);
/* Newlib startup otherwise probes OSD timezone/ROM before main. This app
 * uses only raw EE/IOP counters, and the laptop supplies wall-clock time. */
void _libcglue_timezone_update(void){}
void _libcglue_rtc_update(void){}
static unsigned char sample[8192] __attribute__((aligned(64)));
static msp_request request __attribute__((aligned(64)));
static msp_reply reply;
static msp_config cfg={.abi=1,.enabled=2,.seconds=20,.peer=0x0a4d4d01u,.local=0x0a4d4d02u};
static unsigned count_now(void){unsigned n;__asm__ volatile("mfc0 %0,$9":"=r"(n));return n;}
void msp_boot_progress(const char *name,int id,int result){
 if(id<0&&result==0)scr_printf("Loading %s...\n",name);
 else scr_printf("  %s id=%d start=%d\n",name,id,result);
}
static void finish(const char *text,int code){
 scr_printf("\n%s (%d)\n",text,code);
 scr_printf("No files or saves were modified.\nPower off before removing the card.\n");
 for(;;)SleepThread();
}
static int packet(unsigned session,unsigned seq,unsigned whole){
 unsigned off=seq*MSP_CHUNK,bytes=sizeof sample-off;if(bytes>MSP_CHUNK)bytes=MSP_CHUNK;
 unsigned *h=(unsigned *)request.data;memcpy(h,MSP_MAGIC,8);
 h[2]=1;h[3]=session;h[4]=seq;h[5]=(sizeof sample+MSP_CHUNK-1)/MSP_CHUNK;
 h[6]=sizeof sample;h[7]=off;h[8]=bytes;memcpy(request.data+48,sample+off,bytes);
 h[9]=msp_crc(request.data+48,bytes);h[10]=whole;h[11]=msp_crc(h,44);
 request.bytes=48+bytes;
 return msp_exchange(MSP_DATA,&request,&reply);
}
static void make_sample(unsigned session,unsigned round){
 unsigned *h=(unsigned *)sample;memset(sample,0,128);memcpy(h,"MMIXNET1",8);
 h[2]=1;h[3]=128;h[4]=sizeof sample;h[5]=session;h[6]=round;h[7]=count_now();
 h[8]=reply.clock_lo;h[9]=reply.clock_hi;h[10]=reply.link;h[11]=reply.dropped;h[12]=reply.free_total;h[13]=reply.free_max;
 for(unsigned i=128;i<sizeof sample;i++)sample[i]=(unsigned char)((i*29+round*17)^(i>>8));
 h[30]=msp_crc(sample+128,sizeof sample-128);h[31]=msp_crc(h,124);
}
int main(int argc,char **argv){
 (void)argc;(void)argv;
 /* Private SDK text screen selects NTSC60 without ROM/IOP access. Put
  * visible checkpoints before SIF initialization and the IOP reset. */
 init_scr();scr_clear();
 scr_printf("MEGAMIX NETTEST R121b - standalone\n\nPS2 10.77.77.2 / laptop 10.77.77.1\nUDP 27777; direct Ethernet cable\nStart receive.py on laptop first.\n\nVideo ready; initializing SIF RPC...\n");
 SifInitRpc(0);scr_printf("SIF ready; requesting IOP reset...\n");unsigned n;
 for(n=0;n<2000&&!SifIopReset("",0);n++)usleep(1000);
 if(n==2000)finish("IOP reset request timed out",-1);
 scr_printf("IOP reset requested; waiting for sync...\n");
 for(n=0;n<5000&&!SifIopSync();n++)usleep(1000);
 if(n==5000)finish("IOP reset synchronization timed out",-2);
 scr_printf("IOP reset complete; binding services...\n");
 SifInitRpc(0);SifLoadFileInit();SifInitIopHeap();
 sbv_patch_enable_lmb();sbv_patch_disable_prefix_check();
 int r=msp_boot(&cfg);if(r)finish("Network initialization failed",r);
 memset(&request,0,sizeof request);request.abi=MSP_ABI;request.token=cfg.token;request.local=cfg.local;request.peer=cfg.peer;
 scr_printf("Network module ready; waiting for link (30s).\n");
 for(n=0;n<300;n++){
  r=msp_exchange(MSP_STATUS,&request,&reply);if(r)finish("Network RPC failed",r);
  if(reply.link)break;
  usleep(100000);
 }
 if(!reply.link){msp_exchange(MSP_STOP,&request,&reply);finish("NO LINK - check cable/interface",-3);}
 scr_clear();
 scr_printf("MEGAMIX NETTEST R121b - communication test\nPS2 10.77.77.2 / laptop 10.77.77.1\nLINK UP; IOP free=%u largest=%u\n\n",reply.free_total,reply.free_max);
 unsigned seed=(count_now()^reply.clock_lo)&0x7fffffffu;if(!seed)seed=1;
 unsigned passed=0;
 for(unsigned round=0;round<10;round++){
  unsigned session=seed+round;if(!session)session=0xffffffffu;
  request.bytes=0;r=msp_exchange(MSP_STATUS,&request,&reply);if(r)finish("Status RPC failed",r);
  make_sample(session,round);unsigned whole=msp_crc(sample,sizeof sample),accepted=0;
  scr_printf("Round %u session %08x: ",round+1,session);
  for(unsigned repeat=0;repeat<2;repeat++)for(unsigned seq=0;seq<sizeof sample/MSP_CHUNK;seq++){
   unsigned tries;
   for(tries=0;tries<150;tries++){
    r=packet(session,seq,whole);
    if(!r){accepted++;break;}
    if(r!=-2&&r!=-3&&r!=-4)break;
    usleep(100000);
   }
   if(r){request.bytes=0;msp_exchange(MSP_STOP,&request,&reply);finish("TX/ARP failed (laptop IP/cable?)",r);}
   usleep(1000);
  }
  request.bytes=0;
  unsigned polls;
  for(polls=0;polls<30;polls++){
   r=msp_exchange(MSP_STATUS,&request,&reply);if(r)finish("ACK status RPC failed",r);
   if(reply.ack_session==session&&reply.ack_crc==whole)break;
   usleep(100000);
  }
  if(polls<30){passed++;scr_printf("PC ACK OK (%u packets)\n",accepted);}
  else scr_printf("NO PC ACK (TX accepted %u)\n",accepted);
  usleep(500000);
 }
 request.bytes=0;usleep(100000);r=msp_exchange(MSP_STOP,&request,&reply);
 scr_printf("\nAcknowledged %u/10; queue retries=%u\nIOP free=%u largest=%u\n",passed,reply.dropped,reply.free_total,reply.free_max);
 if(r)finish("STOP RPC failed",r);
 if(passed==10)finish("COMMUNICATION PASS - check laptop reports too",0);
 finish("INCOMPLETE - inspect receiver/firewall",(int)passed);
 return 0;
}
