/* SDK launcher only. Called AFTER the final native-IOP/MMCE reset and USB
 * input setup. No network module is loaded for ordinary or RAM-only runs. */
#ifdef MSP_BOOT_TEST
#include "test_boot.h"
#else
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#endif
#include "protocol.h"
#include "config.h"
extern unsigned char msp_dev9[],msp_netman[],msp_smap[],msp_transport[];
extern unsigned int msp_dev9_size,msp_netman_size,msp_smap_size,msp_transport_size;
static SifRpcClientData_t client __attribute__((aligned(64)));
static unsigned char request[MSP_ROUND(sizeof(msp_request))] __attribute__((aligned(64)));
static unsigned char response[MSP_ROUND(sizeof(msp_reply))] __attribute__((aligned(64)));
void msp_boot_progress(const char *,int,int) __attribute__((weak));
static int drained(void){for(unsigned i=0;i<2000;i++){if(!SifCheckStatRpc(&client))return 1;usleep(1000);}return 0;}
static int load_modules(msp_config *cfg){
 cfg->gate=cfg->token=0;
 unsigned char *modules[]={msp_dev9,msp_netman,msp_smap,msp_transport};
 unsigned sizes[]={msp_dev9_size,msp_netman_size,msp_smap_size,msp_transport_size};
 const char *names[]={"DEV9","NETMAN","SMAP","MEGAPROF"};
 for(unsigned i=0;i<4;i++){
  int result=-999;if(msp_boot_progress)msp_boot_progress(names[i],-1,0);
  int id=SifExecModuleBuffer(modules[i],sizes[i],0,0,&result);
  if(msp_boot_progress)msp_boot_progress(names[i],id,result);
  if(id<0||result)return -10-(int)i;
 }
 return 0;
}
static int configure(msp_config *cfg,unsigned operation){
 if(msp_boot_progress)msp_boot_progress("BIND RPC",-1,0);
 int bind=SifBindRpc(&client,MSP_RPC,SIF_RPC_M_NOWAIT);
 int ready=bind>=0&&drained()&&client.server;
 if(msp_boot_progress)msp_boot_progress("BIND RPC",0,ready?0:-20);
 if(!ready)return -20;
 msp_request *q=(msp_request *)request;q->abi=MSP_ABI;q->peer=cfg->peer;q->local=cfg->local;
 const char *name=operation==MSP_STATUS?"STATUS RPC":"START RPC";
 if(msp_boot_progress)msp_boot_progress(name,-1,0);
 int call=SifCallRpc(&client,operation,SIF_RPC_M_NOWAIT,q,MSP_ROUND(20),response,sizeof response,0,0);
 ready=call>=0&&drained();
 if(msp_boot_progress)msp_boot_progress(name,0,ready?0:-21);
 if(!ready)return -21;
 msp_reply *r=(msp_reply *)response;
 if(r->abi!=MSP_ABI||r->result||!r->token||r->gate<0x1000||r->gate>0x1ffff0||(r->gate&15))return -22;
 cfg->gate=r->gate;cfg->token=r->token;return 0;
}
/* Native game handoff: obtain an inactive gate only. No network stack/RX/TX
 * registration may cross ExecPS2 and the game's SIF initialization. Native
 * code binds its OWN RPC client and starts networking after capture freezes. */
int msp_stage(msp_config *cfg){int r=load_modules(cfg);return r?r:configure(cfg,MSP_STATUS);}
/* Standalone qualification retains its original active-stack contract. */
int msp_boot(msp_config *cfg){int r=load_modules(cfg);return r?r:configure(cfg,MSP_START);}

int msp_exchange(unsigned operation,const msp_request *in,msp_reply *out){
 if(SifCheckStatRpc(&client))return -90;
 if(in->bytes>MSP_PACKET)return -91;
 memcpy(request,in,sizeof(*in));
 if(SifCallRpc(&client,operation,SIF_RPC_M_NOWAIT,request,MSP_ROUND(20+in->bytes),response,sizeof response,0,0)<0||!drained())return -92;
 msp_reply *r=(msp_reply *)response;if(r->abi!=MSP_ABI)return -93;
 if(out)*out=*r;
 return r->result;
}
