/* Generic HID input, separate from PADMAN/SIO2 and storage. No output reports,
 * hot-path logging, allocation or synchronous EE RPC. */
#include "protocol.h"
#include "scratch.h"
#include <string.h>
#ifndef MU_DRIVER_TEST
#include <irx.h>
#include <loadcore.h>
#include <thbase.h>
#include <sifcmd.h>
#include <sifman.h>
#include <intrman.h>
#include <vblank.h>
#include <usbd.h>
#include <usbd_macro.h>
#endif
IRX_ID("MEGAMIX_HID", 1, 0);
typedef struct {
  mu_info info;
  mh_model model;
  /* Report bytes are needed only until parsing/fingerprinting completes.
   * Live values/motion start afterwards; keep every parser/device bound. */
  union {
    unsigned char descriptor[4096] __attribute__((aligned(16)));
    struct { mh_values values; mk_motion motion; };
  };
  int dev, control, input, pending, running, configured;
  unsigned packet_bytes, descriptor_bytes;
  volatile unsigned changing;
  unsigned char buffer[64] __attribute__((aligned(16)));
} device;
static device devices[MU_DEVICES];
static mu_diagnostics diagnostics;
static struct {
  int enabled;
  mu_identity identity;
  mk_profile profile;
  int scratch_axes;
  mk_scratch scratch;
} players[2];
static uint64_t scratch_hold;
static uint64_t scratch_now(void) {
  iop_sys_clock_t clock;
  if (GetSystemTime(&clock))
    return UINT64_MAX; /* fail neutral; do not retain stale scratch on failure */
  return ((uint64_t)clock.hi << 32) | clock.lo;
}
static mu_frame frames[2] __attribute__((aligned(16)));
static unsigned destination, sequence = 1, generation;
static unsigned configuration_revision, player_revision[2];
static unsigned next_revision(void) {
  if (!++configuration_revision) configuration_revision=1;
  return configuration_revision;
}
static int dma[2];
static volatile unsigned game_gate[4] __attribute__((aligned(16)));
static unsigned game_mode, game_token;
static SifRpcDataQueue_t queue;
static SifRpcServerData_t server;
static union {
  mu_reply value;
  unsigned char bytes[MU_RPC_BYTES(mu_reply)];
} reply_storage __attribute__((aligned(16)));
#define reply reply_storage.value
static unsigned char rpc_buffer[MU_RPC_BYTES(mu_request)]
    __attribute__((aligned(16)));
#include "boot_diag.h"
static int same(const mu_identity *a, const mu_identity *b) {
  return !memcmp(a, b, sizeof *a);
}
static void scratch_clear(device *d) {
  for (unsigned p = 0; p < 2; p++)
    if (same(&players[p].identity, &d->info.identity))
      memset(&players[p].scratch, 0, sizeof players[p].scratch);
}
static int report_index(const mh_model *m, unsigned char *p) {
  for (unsigned i = 0; i < m->report_count; i++)
    if (m->reports[i].id == (m->report_ids ? p[0] : 0))
      return i;
  return -1;
}
static void submit(device *d);
static void input_done(int result, int bytes, void *arg) {
  unsigned token = (unsigned long)arg;
  device *d = devices + (token & 3);
  if (d->info.generation != (token >> 2))
    return;
  d->pending = 0;
  if (!d->running)
    return;
  ++d->changing;
  __asm__ volatile("" ::: "memory");
  int ri = bytes > 0 ? report_index(&d->model, d->buffer) : -1;
  if (result == USB_RC_OK && ri >= 0 &&
      !mh_decode(&d->model, d->buffer, bytes, &d->values)) {
    mk_observe(&d->model, &d->values, ri, &d->motion);
    unsigned scratching = 0;
    for (unsigned p = 0; p < 2; p++)
      if (players[p].enabled && players[p].scratch_axes &&
          same(&players[p].identity, &d->info.identity))
        scratching |= 1u << p;
    uint64_t now = scratching ? scratch_now() : UINT64_MAX;
    for (unsigned p = 0; p < 2; p++)
      if (scratching & (1u << p))
        mk_scratch_observe(&d->model, &d->values, ri, &players[p].profile,
                           &players[p].scratch, now,
                           now == UINT64_MAX ? 0 : scratch_hold);
    d->info.reports++;
    d->info.connected = 1;
  } else {
    d->info.errors++;
    d->info.connected = 0;
    memset(&d->values, 0, sizeof d->values);
    memset(&d->motion, 0, sizeof d->motion);
    scratch_clear(d);
  }
  __asm__ volatile("" ::: "memory");
  ++d->changing;
  /* NAK/unchanged reports remain pending in USBD. Actual errors release
   * controls immediately; requeue permits recovery without a held key. */
  submit(d);
}
static void submit(device *d) {
  if (d->running && d->configured && !d->pending) {
    d->pending = 1;
    if (sceUsbdInterruptTransfer(
            d->input, d->buffer, d->packet_bytes, input_done,
            (void *)(unsigned long)((d->info.generation << 2) |
                                    (unsigned)(d - devices)))) {
      d->pending = 0;
      d->info.connected = 0;
      d->info.errors++;
      scratch_clear(d);
    }
  }
}
static void enumeration_failed(device *d, unsigned stage, int result) {
  diagnostics.stage = stage;
  diagnostics.result = result;
  diagnostics.interface = d->info.identity.interface;
  d->running = d->configured = d->info.connected = 0;
  scratch_clear(d);
  if (d->input >= 0) sceUsbdClosePipe(d->input);
  if (d->control >= 0) sceUsbdClosePipe(d->control);
  d->input = d->control = -1;
}
static void descriptor_done(int result, int bytes, void *arg) {
  unsigned token = (unsigned long)arg;
  device *d = devices + (token & 3);
  if (!d->running || d->info.generation != (token >> 2)) return;
  if (result || bytes != (int)d->descriptor_bytes) {
    enumeration_failed(d, MU_DESCRIPTOR_ERROR, result ? result : -bytes-1);
    return;
  }
  result = mh_parse(&d->model, d->descriptor, bytes);
  if (!result) for (unsigned r = 0; r < d->model.report_count; r++)
    if ((d->model.reports[r].bits[0]+7u)/8u+d->model.report_ids > d->packet_bytes)
      result = MH_LIMIT;
  if (result) { enumeration_failed(d, MU_REJECTED, result); return; }
  d->info.identity.descriptor_size = bytes;
  d->info.identity.descriptor_hash = mh_fingerprint(d->descriptor, bytes);
  memset(&d->values, 0, sizeof d->values);
  memset(&d->motion, 0, sizeof d->motion);
  diagnostics.accepted++;
  diagnostics.stage = MU_ACCEPTED;
  diagnostics.result = 0;
  d->configured = 1;
  submit(d);
}
static void configured(int result, int bytes, void *arg) {
  (void)bytes;
  unsigned token = (unsigned long)arg;
  int dev = -1;
  for (unsigned i=0; i<MU_DEVICES; i++)
    if (devices[i].running && devices[i].info.generation==token) dev=devices[i].dev;
  if (dev < 0) return;
  for (unsigned i=0; i<MU_DEVICES; i++) {
    device *d=devices+i;
    if (d->dev!=dev || !d->running) continue;
    if (result) { enumeration_failed(d,MU_CONFIG_ERROR,result); continue; }
    diagnostics.stage=MU_DESCRIPTOR;
    diagnostics.interface=d->info.identity.interface;
    /* HID report descriptors are interface requests. Fetch AFTER configuration;
     * USBD's optional early cache does not validate control-transfer results. */
    int rc=sceUsbdControlTransfer(d->control, USB_DIR_IN|USB_RECIP_INTERFACE,
        USB_REQ_GET_DESCRIPTOR, 0x2200, d->info.identity.interface,
        d->descriptor_bytes, d->descriptor, descriptor_done,
        (void *)(unsigned long)((d->info.generation<<2)|i));
    if(rc) enumeration_failed(d,MU_DESCRIPTOR_ERROR,rc);
  }
}
/* Probe non-boot HID interfaces from descriptors only. Accept into the exposed
 * gamepad list ONLY after configured report-descriptor validation. */
static int scan(int dev, int install) {
  UsbDeviceDescriptor *dd=sceUsbdScanStaticDescriptor(dev,0,USB_DT_DEVICE);
  UsbConfigDescriptor *cfg=sceUsbdScanStaticDescriptor(dev,0,USB_DT_CONFIG);
  if(!install) { diagnostics.probes++; diagnostics.stage=MU_PROBE; }
  if(!dd || !cfg || cfg->bLength<9 || cfg->wTotalLength<9 || cfg->wTotalLength>4096) return 0;
  diagnostics.vendor=dd->idVendor; diagnostics.product=dd->idProduct;
  unsigned char *p=(unsigned char *)cfg,*end=p+cfg->wTotalLength;
  unsigned iface=0, descriptor_bytes=0, accepted=0;
  int eligible=0;
  device *first=0;
  while(p+2<=end) {
    unsigned n=p[0],type=p[1];
    if(n<2 || n>(unsigned)(end-p)) break;
    if(type==USB_DT_INTERFACE) {
      eligible=n>=9 && p[3]==0 && p[5]==USB_CLASS_HID &&
          !(p[6]==1 && (p[7]==1 || p[7]==2));
      iface=n>=9?p[2]:0; descriptor_bytes=0;
    } else if(eligible && type==0x21 && n>=6) {
      for(unsigned at=6; at+3<=n && at<6u+3u*p[5]; at+=3)
        if(p[at]==0x22) descriptor_bytes=p[at+1]|((unsigned)p[at+2]<<8);
    } else if(eligible && type==USB_DT_ENDPOINT && n>=7 &&
        (p[2]&USB_DIR_IN) && (p[3]&3)==USB_ENDPOINT_XFER_INT) {
      unsigned max=p[4]|((unsigned)p[5]<<8);
      if(max && max<=64 && descriptor_bytes && descriptor_bytes<=4096) {
        if(!install) return 1;
        device *d=0;
        for(unsigned i=0;i<MU_DEVICES;i++)
          if(!devices[i].running && !devices[i].pending) { d=devices+i;break; }
        if(!d) break;
        memset(d,0,sizeof *d);d->dev=dev;d->input=d->control=-1;
        d->packet_bytes=max;d->descriptor_bytes=descriptor_bytes;
        d->info.identity.vendor=dd->idVendor;d->info.identity.product=dd->idProduct;
        d->info.identity.interface=iface;
        if(sceUsbdGetDeviceLocation(dev,d->info.identity.path)) break;
        generation=(generation+1)&0x3fffffff;if(!generation) generation=1;
        d->info.generation=generation;
        d->input=sceUsbdOpenPipe(dev,(UsbEndpointDescriptor *)p);
        if(d->input<0) break;
        d->control=sceUsbdOpenPipe(dev,0);
        if(d->control<0) { sceUsbdClosePipe(d->input);break; }
        d->running=1;if(!first) first=d;accepted++;
      }
      eligible=0;
    }
    p+=n;
  }
  if(first) {
    diagnostics.connects++;diagnostics.stage=MU_CONFIGURING;
    int rc=sceUsbdSetConfiguration(first->control,cfg->bConfigurationValue,
        configured,(void *)(unsigned long)first->info.generation);
    if(rc) {
      for(unsigned i=0;i<MU_DEVICES;i++)
        if(devices[i].dev==dev && devices[i].running)
          enumeration_failed(devices+i,MU_CONFIG_ERROR,rc);
      return 0;
    }
  } else if(!accepted) diagnostics.stage=MU_NO_INTERFACE;
  return accepted!=0;
}
static int probe(int dev) { return scan(dev,0); }
static int connect(int dev) { return scan(dev,1)?0:-1; }
static int disconnect(int dev) {
  for (unsigned i = 0; i < MU_DEVICES; i++) {
    device *d = devices + i;
    if (d->dev != dev || !d->running)
      continue;
    d->running = 0;
    d->info.connected = 0;
    scratch_clear(d);
    if (d->input >= 0)
      sceUsbdClosePipe(d->input);
    if (d->control >= 0)
      sceUsbdClosePipe(d->control);
    /* Closing removes the endpoint's transfers. A queued completion carries
     * its attachment generation and cannot mutate a later device in this slot.
     */
    d->pending = 0;
  }
  return 0;
}
static sceUsbdLddOps driver = {.name = "MEGAMIX HID",
                               .probe = probe,
                               .connect = connect,
                               .disconnect = disconnect};
static int publish(void *unused) {
  (void)unused;
  if (!destination || (game_mode &&
      (game_gate[0]!=game_token || game_gate[1]!=1)))
    return 1;
  unsigned slot = sequence & 1;
  if (dma[slot] && sceSifDmaStat(dma[slot]) >= 0)
    return 1;
  /* USB completions run in a thread and VBlank can interrupt their decode.
   * Never publish the temporary absence of a coherent report as a release.
   * Leave both EE slots and accumulated motion alone until the next VBlank;
   * the native adapter retains its previous complete sample and its existing
   * four-missed-publication watchdog still fails neutral if decoding dies.
   * Ignore work on devices not assigned to either player. */
  for (unsigned i = 0; i < MU_DEVICES; i++)
    if (devices[i].running && (devices[i].changing & 1))
      for (unsigned p = 0; p < 2; p++)
        if (players[p].enabled &&
            same(&players[p].identity, &devices[i].info.identity))
          return 1;
  __asm__ volatile("" ::: "memory");
  mu_frame *f = frames + slot;
  memset(f, 0, sizeof *f);
  f->head = sequence;
  f->version = MU_VERSION;
  f->reserved[0] = MU_LIVE_FRAME;
  f->reserved[1] = player_revision[0];
  f->reserved[2] = player_revision[1];
  f->reserved[3] = players[0].profile.type;
  f->reserved[4] = players[1].profile.type;
  uint64_t now = ((players[0].enabled && players[0].scratch_axes) ||
                  (players[1].enabled && players[1].scratch_axes))
                     ? scratch_now() : UINT64_MAX;
  for (unsigned p = 0; p < 2; p++)
    if (players[p].enabled) {
      for (unsigned i = 0; i < MU_DEVICES; i++) {
        device *d = devices + i;
        if (!d->running || !d->info.connected || (d->changing & 1) ||
            !same(&players[p].identity, &d->info.identity))
          continue;
        f->connected[p] = 1;
        /* A device can be assigned to both players intentionally. Consume
         * relative motion only after both samples below. */
        f->logical[p] = mk_scratch_peek(
            &d->model, &d->values, &players[p].profile, &d->motion,
            &players[p].scratch, now, now == UINT64_MAX ? 0 : scratch_hold);
        break;
      }
    }
  for (unsigned i = 0; i < MU_DEVICES; i++)
    if (!(devices[i].changing & 1))
      memset(devices[i].motion.motion, 0, sizeof devices[i].motion.motion);
  f->tail = sequence;
  SifDmaTransfer_t transfer = {
      f, (void *)(unsigned long)(destination + slot * sizeof *f), sizeof *f, 0};
  dma[slot] = sceSifSetDma(&transfer, 1);
  if (dma[slot]) {
    sequence++;
    if (!sequence)
      sequence = 1;
  }
  return 1;
}
static void *rpc(int operation, void *data, int bytes) {
  mu_request *r = data;
  memset(&reply, 0, sizeof reply);
  reply.version = MU_VERSION;
  reply.status = -1;
  if (bytes < (int)sizeof *r || r->version != MU_VERSION)
    return &reply;
#ifdef MU_BOOT_DIAG_BUILD
  if (operation==MU_BOOT_DIAG) { reply.status=md_start();return &reply; }
#endif
  int replace=operation==MU_LIVE_REPLACE;
  int live_apply=operation==MU_LIVE_CONFIGURE || replace;
  if (operation == MU_LIVE_INFO && r->player<2) {
    unsigned p=r->player;
    int state;CpuSuspendIntr(&state);
    reply.data.live.version=MU_LIVE_VERSION;
    reply.data.live.enabled=players[p].enabled;
    reply.data.live.revision=player_revision[p];
    reply.data.live.slot=MU_DEVICES;
    reply.data.live.identity=players[p].identity;
    reply.data.live.profile=players[p].profile;
    for(unsigned i=0;i<MU_DEVICES;i++)if(devices[i].running && devices[i].configured &&
        devices[i].info.connected && same(&players[p].identity,&devices[i].info.identity)) {
      reply.data.live.slot=i;reply.data.live.generation=devices[i].info.generation;break;
    }
    CpuResumeIntr(state);reply.status=0;
  } else if (operation == MU_LIST) {
    for (unsigned i = 0; i < MU_DEVICES; i++)
      if (devices[i].running && devices[i].configured)
        reply.data.devices[i] = devices[i].info;
    reply.status = 0;
  } else if (operation == MU_DIAGNOSTICS) {
    reply.data.diagnostics=diagnostics;
#ifndef MU_DRIVER_TEST
    /* Read-only, only when requested by the launcher menu. No PLAY sampling. */
    volatile const unsigned *ohci=(void *)0xbf801600;
    reply.data.diagnostics.host_control=ohci[1];
    reply.data.diagnostics.host_frame=ohci[15];
    reply.data.diagnostics.root_ports[0]=ohci[21];
    reply.data.diagnostics.root_ports[1]=ohci[22];
#endif
    reply.status=0;
  } else if (operation == MU_MODEL || operation == MU_VALUES) {
    if (r->slot < MU_DEVICES) {
      device *d = devices + r->slot;
      if (d->running && d->configured && d->info.generation == r->generation) {
        unsigned changing=d->changing;
        if(operation==MU_VALUES && (changing&1)){reply.status=-2;return &reply;}
        __asm__ volatile("" ::: "memory");
        if (operation == MU_MODEL)
          reply.data.model = d->model;
        else
          reply.data.values = d->values;
        __asm__ volatile("" ::: "memory");
        if(d->running && d->configured && d->info.generation==r->generation &&
            (operation==MU_MODEL || d->changing==changing))reply.status=0;
        else reply.status=-2;
      }
    }
  } else if (operation == MU_DISABLE && r->player < 2) {
    int state;
    CpuSuspendIntr(&state);
    players[r->player].enabled = 0;
    player_revision[r->player]=next_revision();
    memset(&players[r->player].scratch, 0, sizeof players[r->player].scratch);
    CpuResumeIntr(state);
    reply.status = 0;
  } else if ((operation == MU_CONFIGURE || live_apply) && r->player < 2) {
    int valid = r->profile.type<=MK_5KEY;
    for (unsigned j = 0; j < MK_CONTROLS; j++) {
      const mk_binding *binding = r->profile.control + j;
      if (binding->reserved || binding->field >= MH_FIELDS ||
          binding->kind > MK_MOTION_POS)
        valid = 0;
    }
    for (unsigned i = 0; i < MU_DEVICES; i++)
      if (devices[i].running && same(&r->identity, &devices[i].info.identity)) {
        uint16_t missing, duplicates;
        if (mk_validate(&devices[i].model, &r->profile, &missing, &duplicates))
          valid = 0;
      }
    if (valid && r->identity.descriptor_size &&
        r->identity.descriptor_size <= 4096) {
      int state;
      CpuSuspendIntr(&state);
      if(live_apply && (r->slot>=MU_DEVICES || !r->generation ||
          !devices[r->slot].running || !devices[r->slot].configured ||
          !devices[r->slot].info.connected || devices[r->slot].info.generation!=r->generation ||
          !same(&r->identity,&devices[r->slot].info.identity) ||
          !players[r->player].enabled || (!replace && !same(&r->identity,&players[r->player].identity)) ||
          !r->ee_target || player_revision[r->player]!=r->ee_target)) {
        CpuResumeIntr(state);reply.status=-4;return &reply;
      }
      if(replace) {
        unsigned peer=1-r->player;
        int occupied=players[peer].enabled && same(&r->identity,&players[peer].identity);
        for(unsigned i=0;i<MU_DEVICES;i++)
          if(devices[i].running && devices[i].configured && devices[i].info.connected &&
             same(&players[r->player].identity,&devices[i].info.identity) &&
             !same(&r->identity,&players[r->player].identity)) occupied=1;
        if(occupied){CpuResumeIntr(state);reply.status=-4;return &reply;}
      }
      players[r->player].enabled = 1;
      players[r->player].identity = r->identity;
      players[r->player].profile = r->profile;
      players[r->player].scratch_axes = 0;
      for (unsigned i = MK_CCW; r->profile.type!=MK_DUALSHOCK && i <= MK_CW; i++)
        if (r->profile.control[i].kind == MK_MOTION_NEG ||
            r->profile.control[i].kind == MK_MOTION_POS)
          players[r->player].scratch_axes = 1;
      memset(&players[r->player].scratch, 0, sizeof players[r->player].scratch);
      for (unsigned i = 0; i < MU_DEVICES; i++)
        if (devices[i].configured &&
            same(&r->identity, &devices[i].info.identity)) {
          mk_scratch_reset(&devices[i].model, &devices[i].values, &r->profile,
                            &players[r->player].scratch);
          break;
        }
      player_revision[r->player]=next_revision();
      if(live_apply){reply.data.live.version=MU_LIVE_VERSION;reply.data.live.revision=player_revision[r->player];}
      CpuResumeIntr(state);
      reply.status = 0;
    }
  } else if (operation == MU_TARGET || operation == MU_GAME_TARGET) {
    /* Reserved EE memory supplied by the launcher; reject kernel/IOP/GS
     * aliases and misaligned DMA. Stop before changing a live target. */
    if ((operation == MU_TARGET && !r->ee_target) || (!(r->ee_target & 127) && r->ee_target >= 0x100000 &&
                          r->ee_target <= 0x1ffff00)) {
      int state;
      CpuSuspendIntr(&state);
      destination = 0;
      int busy = 0;
      for (unsigned i = 0; i < 2; i++)
        if (dma[i] && sceSifDmaStat(dma[i]) >= 0)
          busy = 1;
      if (!busy) {
        for (unsigned p = 0; p < 2; p++)
          memset(&players[p].scratch, 0, sizeof players[p].scratch);
        game_mode=operation==MU_GAME_TARGET;
        if(game_mode) {
          if(!++game_token) game_token=1;
          game_gate[0]=game_gate[1]=game_gate[2]=game_gate[3]=0;
          reply.data.game_gate.address=(unsigned long)game_gate & 0x1fffffff;
          reply.data.game_gate.token=game_token;
        }
        destination=r->ee_target;
      }
      CpuResumeIntr(state);
      reply.status = busy ? -2 : 0;
    }
  }
  return &reply;
}
static void rpc_thread(void *unused) {
  (void)unused;
  sceSifInitRpc(0);
  sceSifSetRpcQueue(&queue, GetThreadId());
  sceSifRegisterRpc(&server, MU_RPC_ID, rpc, rpc_buffer, 0, 0, &queue);
  sceSifRpcLoop(&queue);
}
static void recovery_thread(void *unused) {
  (void)unused;
  unsigned diagnostic_divider=0;
  for (;;) {
    DelayThread(16667);
#ifdef MU_BOOT_DIAG_BUILD
    if(++diagnostic_divider==6){diagnostic_divider=0;md_tick();}
#else
    (void)diagnostic_divider;
#endif
    for (unsigned i = 0; i < MU_DEVICES; i++)
      if (devices[i].running && !devices[i].pending)
        submit(devices + i);
  }
}
int _start(int argc, char **argv) {
  (void)argc;
  (void)argv;
  if (sizeof(mu_frame) != 128)
    return MODULE_NO_RESIDENT_END;
  iop_sys_clock_t hold_clock;
  USec2SysClock(MK_SCRATCH_HOLD_US, &hold_clock);
  scratch_hold = ((uint64_t)hold_clock.hi << 32) | hold_clock.lo;
  if (!scratch_hold)
    return MODULE_NO_RESIDENT_END;
  iop_thread_t t = {0};
  t.attr = TH_C;
  t.thread = rpc_thread;
  t.priority = 40;
  t.stacksize = 0x2000;
  int id = CreateThread(&t);
  if (id < 0)
    return MODULE_NO_RESIDENT_END;
  t.thread = recovery_thread;
  t.priority = 50;
#ifdef MU_BOOT_DIAG_BUILD
  /* File-system calls are diagnostic-only and need a normal service stack. */
  t.stacksize = 0x2000;
#else
  t.stacksize = 0x800;
#endif
  int recovery = CreateThread(&t);
  if (recovery < 0) {
    DeleteThread(id);
    return MODULE_NO_RESIDENT_END;
  }
  if (RegisterVblankHandler(0, 128, publish, 0)) {
    DeleteThread(id);
    DeleteThread(recovery);
    return MODULE_NO_RESIDENT_END;
  }
  if (sceUsbdRegisterLdd(&driver)) {
    ReleaseVblankHandler(0, publish);
    DeleteThread(id);
    DeleteThread(recovery);
    return MODULE_NO_RESIDENT_END;
  }
  int first = StartThread(id, 0), second = StartThread(recovery, 0);
  if (first < 0 || second < 0) {
    sceUsbdUnregisterLdd(&driver);
    ReleaseVblankHandler(0, publish);
    if (first >= 0)
      TerminateThread(id);
    if (second >= 0)
      TerminateThread(recovery);
    DeleteThread(id);
    DeleteThread(recovery);
    return MODULE_NO_RESIDENT_END;
  }
  return MODULE_RESIDENT_END;
}
