#ifndef MEGAMIX_USB_IOP_BUDGET_H
#define MEGAMIX_USB_IOP_BUDGET_H
#include "iop_budget.h"
/* SDK-private client, never reused by native game. Reset once with the IOP.
 * Retail80000003 op6=largest,op7=total: verified against actual FILEIO. */
static SifRpcClientData_t ib_client __attribute__((aligned(64)));
static unsigned ib_reply[16] __attribute__((aligned(64)));
static volatile unsigned *ib_state;
static unsigned ib_bound;
static unsigned ib_sdk_query(unsigned op){
 if(!ib_bound)return ~0u;
 ib_reply[0]=~0u;
 int r=SifCallRpc(&ib_client,op,0,0,0,ib_reply,4,0,0);
 return r<0?~0u:ib_reply[0];
}
static void ib_sdk_sample(unsigned tag){
 unsigned largest=ib_sdk_query(6),total=ib_sdk_query(7);
 if(ib_state)ib_record(ib_state,tag,total,largest,0);
 usb_boot_note("H%03x IOP total=%08x largest=%08x",tag,total,largest);
}
static void ib_sdk_bind(void){
 ib_state=0;
 memset(&ib_client,0,sizeof ib_client);ib_bound=0;
 /* SifInitIopHeap has already bound this service. One checked bind; avoid
  * another unbounded loop when recording a failed service is the objective. */
 int r=SifBindRpc(&ib_client,0x80000003,0);
 if(r>=0&&ib_client.server)ib_bound=1;
 usb_boot_note("H000 heap observer mode=%u bind=%d ready=%u",0,r,ib_bound);
}
static int ib_sdk_configure(void){
 char line[128];unsigned at=0,words=0,seen=0;int failed=0;
#ifdef USB_UNUSED_RING
 unsigned ring_seen=0;
#endif
 FILE *f=fopen(USB_BENCH_ROOT "launcher/vdisc.txt","rb");if(!f)return -1;
 while(fgets(line,sizeof line,f)){
#ifdef USB_UNUSED_RING
  if(!strncmp(line,"usb_stream_ring ",16)){
   unsigned abi,site,bytes;char extra;ring_seen++;
   if(sscanf(line,"usb_stream_ring %u %x %x %c",&abi,&site,&bytes,&extra)!=3||abi!=1||site!=0x14d300||bytes!=0x28010)failed=1;
  }
#endif
  if(!strncmp(line,"iop_budget ",11)){
  unsigned abi;char trailing;
  if(sscanf(line,"iop_budget %u %x %u %c",&abi,&at,&words,&trailing)!=3||abi!=1)failed=1;
  seen++;
 }}
#ifdef USB_UNUSED_RING
 if(ring_seen!=1)failed=1;
#endif
 if(ferror(f))failed=1;
 if(fclose(f))failed=1;
 if(failed||seen!=1||words!=IB_WORDS||(at&3)||at<0x7c4700||at>0x994700-IB_WORDS*4)return -1;
#ifdef USB_IOP_BUDGET_TEST
 ib_state=ib_test_address(at);
#else
 ib_state=(volatile unsigned *)at;
#endif
 if(ib_state[0]!=IB_MAGIC||ib_state[1]||ib_state[3]||ib_state[4]){ib_state=0;return -1;}
 for(unsigned i=1;i<IB_WORDS;i++)ib_state[i]=0;
#ifdef USB_AUDIO_RESERVE
 ib_state[1]=2;
#else
 ib_state[1]=1;
#endif
 usb_boot_note("H010 native heap mode=%u",ib_state[1]);
 ib_sdk_sample(0x10);return 0;
}
#endif
