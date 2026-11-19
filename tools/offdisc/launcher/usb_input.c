#ifndef MUI_CLIENT_TEST
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#endif
#include "usb_input.h"
#ifdef PORTABLE_LAUNCHER
#include "deployment.h"
#endif
#include <stdio.h>
#include <string.h>
#include <unistd.h>
extern unsigned char mui_usbd[], mui_hid[];
extern unsigned mui_usbd_size, mui_hid_size;
static SifRpcClientData_t client __attribute__((aligned(64)));
static union {
  mu_request value;
  unsigned char bytes[MU_RPC_BYTES(mu_request)];
} request_storage __attribute__((aligned(64)));
static union {
  mu_reply value;
  unsigned char bytes[MU_RPC_BYTES(mu_reply)];
} reply_storage __attribute__((aligned(64)));
#define request_buffer request_storage.value
#define reply_buffer reply_storage.value
static mu_frame frame_buffer[2] __attribute__((aligned(128)));
static unsigned ready, save_generation, last_head, stale_frames;
static char status[128];
static unsigned boot_trace_enabled,boot_trace_started;
static char boot_trace_path[192];
void mui_trace_enable(int enabled,const char *root){
  boot_trace_enabled=!!enabled;boot_trace_started=0;
  snprintf(boot_trace_path,sizeof boot_trace_path,"%susb-launch.txt",root);
}
void mui_trace_phase(const char *phase,int result){
  if(!boot_trace_enabled)return;
  FILE *f=fopen(boot_trace_path,boot_trace_started?"a":"w");
  if(!f){boot_trace_enabled=0;return;}
  boot_trace_started=1;
  int failed=fprintf(f,"R32 %s result=%d\n",phase,result)<0;
  if(fclose(f))failed=1;
  if(failed)boot_trace_enabled=0;
}
int mui_start_boot_trace(void){
  mu_request r={0};return mui_call(MU_BOOT_DIAG,&r,0);
}
mui_player mui_players[2];
unsigned mui_navigation;
const char *mui_status(void) { return status; }
int mui_call(int operation, mu_request *request, mu_reply *reply) {
  if (!ready)
    return -1;
  request->version = MU_VERSION;
  request_buffer = *request;
  if (SifCallRpc(&client, operation, 0, &request_buffer, sizeof request_buffer,
                 &reply_buffer, sizeof reply_buffer, 0, 0) < 0)
    return -1;
  if (reply_buffer.version != MU_VERSION)
    return -1;
  if (reply)
    *reply = reply_buffer;
  return reply_buffer.status;
}
int mui_target(unsigned address) {
  mu_request r = {0};
  r.ee_target = address;
  for (unsigned i = 0; i < 200; i++) {
    int result = mui_call(MU_TARGET, &r, 0);
    if (result != -2)
      return result;
    usleep(1000);
  }
  return -1;
}

int mui_list(mu_info out[MU_DEVICES]) {
  mu_request r = {0};
  mu_reply reply;
  if (mui_call(MU_LIST, &r, &reply))
    return -1;
  memcpy(out, reply.data.devices, sizeof reply.data.devices);
  unsigned found=0;
  for(unsigned i=0;i<MU_DEVICES;i++) found|=out[i].generation;
  if(found) snprintf(status,sizeof status,"USB HID gamepad detected");
  else if(!mui_call(MU_DIAGNOSTICS,&r,&reply)) {
    const mu_diagnostics *d=&reply.data.diagnostics;
    const char *reason=d->stage==MU_CONFIGURING?"configuring":
        d->stage==MU_DESCRIPTOR?"reading HID descriptor":
        d->stage==MU_CONFIG_ERROR?"configuration failed":
        d->stage==MU_DESCRIPTOR_ERROR?"HID descriptor read failed":
        d->stage==MU_REJECTED?"unsupported HID report":
        d->stage==MU_NO_INTERFACE?"no eligible HID interface":"awaiting USB device";
    if(d->probes) snprintf(status,sizeof status,"%04X:%04X %s (%d)",
        (unsigned)d->vendor,(unsigned)d->product,reason,(int)d->result);
    else snprintf(status,sizeof status,"%s",(d->root_ports[0]|d->root_ports[1])&1?
        "USB device present; awaiting enumeration":"USB service ready; no USB device detected");
  }
  return 0;
}
int mui_configure(unsigned player) {
  mu_request r = {0};
  r.player = player;
  if (!mui_players[player].enabled)
    return mui_call(MU_DISABLE, &r, 0);
  r.identity = mui_players[player].identity;
  r.profile = mui_players[player].profile;
  return mui_call(MU_CONFIGURE, &r, 0);
}
static int mui_boot_common(int launcher) {
  ready = last_head = stale_frames = 0;
  memset(&client, 0, sizeof client);
  int result = -1;
  /* 4096-byte configuration + device header. SDK numeric option parsing
   * rejects digit 9, so use the equivalent bounded allocation 4128. */
  /* Four HID interfaces need neither 32 USB device nodes nor 256 I/O
   * requests. Retain full configuration/report limits and native priorities. */
  const char host_args[] = "reportd=0\0conf=4128\0dev=8\0ed=16\0gtd=32\0itd=2\0ioreq=24\0hub=1\0port=4";
  int id;
#if defined(USB_BENCH) || defined(PORTABLE_LAUNCHER)
#ifdef PORTABLE_LAUNCHER
  if(!dl_usb_active){
    id=SifExecModuleBuffer(mui_usbd,mui_usbd_size,sizeof host_args,host_args,&result);
    if(id<0||(result!=0&&result!=2)){snprintf(status,sizeof status,"USB host failed: %d / %d",id,result);return -1;}
    dl_usb_active=1;
  }
#endif
  /* usb_boot_prepare already restored the mass-storage host. A second USBD
   * would reset the same OHCI controller underneath mounted storage. The HID
   * driver registers on that existing host, in launcher and game phases. */
  (void)host_args;
  if(!launcher)mui_trace_phase("reuse storage USB host",0);
#else
  if(!launcher)mui_trace_phase("load USB host: enter",0);
  id = SifExecModuleBuffer(mui_usbd, mui_usbd_size, sizeof(host_args),
                               host_args, &result);
  if(!launcher)mui_trace_phase("load USB host: return",id<0?id:result);
  if (id < 0 || (result != 0 && result != 2)) {
    snprintf(status, sizeof status, "USB host failed: %d / %d", id, result);
    return -1;
  }
#endif
  if(!launcher)mui_trace_phase("load HID module: enter",0);
  id = SifExecModuleBuffer(mui_hid, mui_hid_size, 0, 0, &result);
  if (id < 0 || (result != 0 && result != 2)) {
    if(!launcher)mui_trace_phase("load HID module: failed",id<0?id:result);
    snprintf(status, sizeof status, "USB input failed: %d / %d", id, result);
    return -1;
  }
  if(!launcher)mui_trace_phase("load HID module: returned; bind RPC",result);
  for (unsigned i = 0; i < 200; i++) {
    if (SifBindRpc(&client, MU_RPC_ID, 0) < 0)
      break;
    if (client.server) {
      ready = 1;
      break;
    }
    usleep(10000);
  }
  if (!ready) {
    snprintf(status, sizeof status, "USB input service unavailable");
    return -1;
  }
  memset(frame_buffer, 0, sizeof frame_buffer);
  SyncDCache(frame_buffer, frame_buffer + 2);
  if (launcher && mui_target((unsigned)frame_buffer))
    return -1;
  for (unsigned p = 0; p < 2; p++)
    mui_configure(p);
  if(!launcher)mui_trace_phase("USB profiles applied; game boot ready",0);
  snprintf(status, sizeof status, "USB HID ready");
  return 0;
}
int mui_boot(void) { return mui_boot_common(1); }
int mui_boot_game(void) { return mui_boot_common(0); }
int mui_quiesce(void) { return ready ? mui_target(0) : 0; }
int mui_game_target(unsigned address, unsigned *gate_address, unsigned *token) {
  mu_request r={0};mu_reply reply;r.ee_target=address;
  int result=mui_call(MU_GAME_TARGET,&r,&reply);
  if(result)return result;
  *gate_address=reply.data.game_gate.address;*token=reply.data.game_gate.token;
  return (*gate_address>=0x1000 && *gate_address<=0x1ffff0 && !(*gate_address&15) && *token)?0:-1;
}
unsigned mui_buttons(void) {
  if (!ready)
    return 0;
  volatile const mu_frame *f = (void *)((unsigned)frame_buffer | 0x20000000);
  mu_frame selected = {0}, copy;
  for (unsigned i = 0; i < 2; i++) {
    if (mu_snapshot(&copy, f + i) &&
        (!selected.head || (int)(copy.head - selected.head) > 0))
      selected = copy;
  }
  if (selected.head != last_head) {
    last_head = selected.head;
    stale_frames = 0;
  } else if (stale_frames < 4)
    stale_frames++;
  if (stale_frames >= 4)
    return 0;
  unsigned b = 0;
  for (unsigned p = 0; p < 2; p++)
    if (selected.connected[p] && mui_players[p].enabled)
      {unsigned keys=selected.logical[p];
       if(selected.reserved[3+p]==MK_DUALSHOCK){unsigned nav=0;if(keys&1)nav|=2;if(keys&2)nav|=1;if(keys&(1u<<5))nav|=1u<<5;if(keys&(1u<<12))nav|=1u<<MK_START;if(keys&(1u<<13))nav|=1u<<MK_SELECT;b|=nav;}
       else {if(selected.reserved[3+p]==MK_5KEY&&keys&(1u<<4))keys|=1u<<5;b|=keys;}}

  return b;
}
/* Two small CRC-protected generations: interrupted saves retain the preceding
 * complete profile. Independent of native game saves and debug.cfg. */
typedef mu_saved saved;
static unsigned crc32(const void *data, unsigned n) {
  const unsigned char *p = data;
  unsigned c = ~0u;
  while (n--) {
    c ^= *p++;
    for (unsigned i = 0; i < 8; i++)
      c = (c >> 1) ^ (0xedb88320u & -(c & 1));
  }
  return ~c;
}
static int read_save(const char *root, unsigned slot, saved *out) {
  char path[192];
  snprintf(path, sizeof path, "%scontrollers.%u", root, slot);
  FILE *f = fopen(path, "rb");
  if (!f)
    return -1;
  saved raw;int ok=!fseek(f,0,SEEK_END);long n=ok?ftell(f):-1;
  ok=ok&&n>=0&&n<=sizeof raw&&!fseek(f,0,SEEK_SET);
  if(ok&&n)ok=fread(&raw,1,n,f)==(unsigned)n;
  if(fclose(f))ok=0;
  if(!ok||mu_saved_decode(out,&raw,n))return -1;
  return 0;
}
void mui_load(const char *root) {
  saved a, b, *s = 0;
  if (!read_save(root, 0, &a))
    s = &a;
  if (!read_save(root, 1, &b) &&
      (!s || (int)(b.generation - s->generation) > 0))
    s = &b;
  if (s) {
    mui_navigation = s->navigation;
    save_generation = s->generation;
    memcpy(mui_players, s->player, sizeof mui_players);
  }
  for (unsigned p = 0; p < 2; p++)
    mui_configure(p);
}
int mui_save(const char *root) {
  saved s = {0};
  s.magic = MU_RPC_ID;
  s.version = MU_VERSION;
  s.generation = save_generation + 1;
  s.navigation = mui_navigation;
  memcpy(s.player, mui_players, sizeof mui_players);
  s.crc = crc32(&s, sizeof s - 4);
  char path[192];
  snprintf(path, sizeof path, "%scontrollers.%u", root, s.generation & 1);
  FILE *f = fopen(path, "wb");
  if (!f)
    return -1;
  int failed = fwrite(&s, 1, sizeof s, f) != sizeof s;
  if (fclose(f))
    failed = 1;
  saved check;
  if (failed || read_save(root, s.generation & 1, &check) ||
      memcmp(&s, &check, sizeof s))
    return -1;
  save_generation = s.generation;
  return 0;
}

int mui_ensure_boot(void) { return ready ? 0 : mui_boot(); }

void mui_diagnostic_save(const char *root) {
  mu_request r={0};mu_reply reply;
  if(mui_call(MU_DIAGNOSTICS,&r,&reply))return;
  char path[192];snprintf(path,sizeof path,"%susb-input.txt",root);
  FILE *f=fopen(path,"w");if(!f)return;
  const mu_diagnostics *d=&reply.data.diagnostics;
  fprintf(f,"USB enumeration R30\nprobes %u\nconnects %u\naccepted %u\nstage %u\nvid %04X\npid %04X\ninterface %u\nresult %d\nhost_control %08X\nhost_frame %08X\nports %08X %08X\nstatus %s\n",
      (unsigned)d->probes,(unsigned)d->connects,(unsigned)d->accepted,
      (unsigned)d->stage,(unsigned)d->vendor,(unsigned)d->product,
      (unsigned)d->interface,(int)d->result,(unsigned)d->host_control,
      (unsigned)d->host_frame,(unsigned)d->root_ports[0],(unsigned)d->root_ports[1],status);
  fclose(f);
}
