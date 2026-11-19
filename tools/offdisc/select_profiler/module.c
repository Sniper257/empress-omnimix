/* One-shot select-only UDP transport. No sockets, filesystem, periodic thread,
 * controller hooks, reliable-delivery wait or unbounded packet queue. */
#include "protocol.h"
#include <string.h>
#ifdef MSP_MODULE_TEST
#include "test_iop.h"
#define _start msp_module_start
#else
#include <irx.h>
#include <loadcore.h>
#include <thbase.h>
#include <sifcmd.h>
#include <intrman.h>
#include <sysmem.h>
#include <netman.h>
#endif
#include "frame.h"
IRX_ID("MEGAMIX_PROFILE",1,0);
static unsigned char mac[6],peer_mac[6];
static unsigned ack_session,ack_crc;
static unsigned local_ip,peer_ip,known,started,stopped,token=0x53503101;
static volatile unsigned gate[4] __attribute__((aligned(16)));
static unsigned char tx[MSP_SLOTS][1536] __attribute__((aligned(16)));
static unsigned lengths[MSP_SLOTS],head,tail,dropped;
static struct {unsigned used,bytes;unsigned char data[1536];} rx[2];
static SifRpcDataQueue_t queue;
static SifRpcServerData_t server;
static unsigned char request[MSP_ROUND(sizeof(msp_request))] __attribute__((aligned(16)));
static union {msp_reply value;unsigned char bytes[MSP_ROUND(sizeof(msp_reply))];} response __attribute__((aligned(16)));
static int live(void){return started&&!stopped&&gate[0]==token;}
static unsigned char *reserve(void){return head-tail<MSP_SLOTS?tx[head%MSP_SLOTS]:0;}
static void commit(unsigned bytes){lengths[head%MSP_SLOTS]=bytes;__asm__ volatile("":::"memory");head++;NetManNetIFXmit();}
static void link_up(void){}
static void link_down(void){known=0;}
static void *alloc_rx(unsigned bytes,void **payload){
 if(!live()||bytes>1536)return 0;
 for(unsigned i=0;i<2;i++)if(!rx[i].used){rx[i].used=1;rx[i].bytes=bytes;*payload=rx[i].data;return rx+i;}
 return 0;
}
static void free_rx(void *packet){for(unsigned i=0;i<2;i++)if(packet==rx+i)rx[i].used=0;}
static void receive_rx(void *packet){
 for(unsigned i=0;i<2;i++)if(packet==rx+i){
  unsigned char *p=rx[i].data;
  if(live()&&rx[i].bytes>=42&&msp_u16(p+12)==0x806&&msp_u16(p+14)==1&&msp_u16(p+16)==0x800&&p[18]==6&&p[19]==4&&msp_u32(p+28)==peer_ip&&msp_u32(p+38)==local_ip){
   unsigned op=msp_u16(p+20);
   if(op==1||op==2){memcpy(peer_mac,p+22,6);known=1;
    if(op==1){unsigned char *out=reserve();if(out)commit(msp_arp(out,mac,peer_mac,local_ip,peer_ip,2));}
   }
  }
  if(live()&&rx[i].bytes>=66&&msp_u16(p+12)==0x800){
   unsigned char *ip=p+14;unsigned ihl=(ip[0]&15)*4,total=msp_u16(ip+2);
   if(ip[0]>>4==4&&ihl>=20&&ihl<=60&&total<=rx[i].bytes-14&&total>=ihl+8+24&&ip[9]==17&&!(msp_u16(ip+6)&0x3fff)&&!msp_sum(ip,ihl)&&msp_u32(ip+12)==peer_ip&&msp_u32(ip+16)==local_ip){
    unsigned char *udp=ip+ihl,*a=udp+8;
    if(msp_u16(udp)==MSP_PORT&&msp_u16(udp+2)==MSP_PORT&&msp_u16(udp+4)==32&&!memcmp(a,"MMIXACK1",8)&&msp_le32(a+8)==1&&msp_crc(a,20)==msp_le32(a+20)){
     ack_crc=msp_le32(a+16);__asm__ volatile("":::"memory");ack_session=msp_le32(a+12);
    }
   }
  }
  rx[i].used=0;return;
 }
}
static int next_tx(void **payload){if(!live()||head==tail)return 0;*payload=tx[tail%MSP_SLOTS];return lengths[tail%MSP_SLOTS];}
static void dequeue_tx(void){if(tail!=head)tail++;}
static struct NetManNetProtStack stack={link_up,link_down,alloc_rx,free_rx,receive_rx,next_tx,dequeue_tx,0,0};
static void *rpc(int op,void *data,int bytes){
 msp_request *q=data;msp_reply *r=&response.value;memset(&response,0,sizeof response);r->abi=MSP_ABI;r->result=-1;
 if(bytes>=(int)__builtin_offsetof(msp_request,data)&&q->abi==MSP_ABI){
  if(op==MSP_STATUS)r->result=0;
  else if(op==MSP_START&&!started&&!stopped&&q->local&&q->peer&&q->local!=q->peer){
   local_ip=q->local;peer_ip=q->peer;
   if(!NetManIoctl(NETMAN_NETIF_IOCTL_ETH_GET_MAC,0,0,mac,6)&&!NetManRegisterNetworkStack(&stack)){started=1;gate[0]=token;r->result=0;}
  }else if(op==MSP_STOP){
   gate[0]=0;if(started&&!stopped)NetManUnregisterNetworkStack();stopped=1;r->result=0;
  }else if(op==MSP_DATA&&live()&&q->token==token&&q->bytes<=MSP_PACKET&&q->bytes>=48&&bytes>=(int)(__builtin_offsetof(msp_request,data)+q->bytes)){
   int intr;CpuSuspendIntr(&intr);
   unsigned char *out=reserve();
   if(!out){dropped++;r->result=-2;}
   else if(!NetManGetGlobalNetIFLinkState())r->result=-3;
   else if(!known){static const unsigned char broadcast[6]={255,255,255,255,255,255};
    /* At most one outstanding ARP request; caller retries at a bounded rate. */
    if(head==tail){commit(msp_arp(out,mac,broadcast,local_ip,peer_ip,1));}
    r->result=-4;
   }else{commit(msp_udp(out,mac,peer_mac,local_ip,peer_ip,q->data,q->bytes));r->result=0;}
   CpuResumeIntr(intr);
  }
 }
 iop_sys_clock_t now;GetSystemTime(&now);r->clock_lo=now.lo;r->clock_hi=now.hi;
 r->link=NetManGetGlobalNetIFLinkState();r->dropped=dropped;r->gate=(unsigned)gate;r->token=token;r->queued=head-tail;
 r->ack_session=ack_session;r->ack_crc=ack_crc;
 r->free_total=QueryTotalFreeMemSize();r->free_max=QueryMaxFreeMemSize();return &response;
}
static void rpc_thread(void *arg){(void)arg;sceSifInitRpc(0);sceSifSetRpcQueue(&queue,GetThreadId());sceSifRegisterRpc(&server,MSP_RPC,rpc,request,0,0,&queue);sceSifRpcLoop(&queue);}
int _start(int argc,char **argv){(void)argc;(void)argv;iop_thread_t t={0};t.attr=TH_C;t.thread=rpc_thread;t.priority=48;t.stacksize=0x1000;int id=CreateThread(&t);if(id<0)return MODULE_NO_RESIDENT_END;if(StartThread(id,0)<0){DeleteThread(id);return MODULE_NO_RESIDENT_END;}return MODULE_RESIDENT_END;}
