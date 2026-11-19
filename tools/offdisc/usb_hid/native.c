/* This adapter is called only at the retail PADMAN setup/read call sites.
 * It doesn't change FUN_0013b710 history, detection, delay or judgement. */
#include "native.h"
mu_native_config usb_input_config = {MU_NATIVE_VERSION, {0, 0}, 0, 0};
mu_frame usb_input_frames[2] __attribute__((aligned(128)));
static mu_frame usb_input_snapshot;
static unsigned usb_input_seen, usb_input_stale, usb_input_armed;
static unsigned usb_input_type_seen[2];
/* Dedicated cache lines: cached neighbour writes cannot overwrite uncached arm data. */
static unsigned usb_input_arm_words[32] __attribute__((aligned(128)));
typedef struct { void *source, *destination; unsigned bytes, attributes; } mu_arm_transfer;
#ifndef MU_NATIVE_TEST
#define MU_SETUP(p) ((void (*)(void *))0x13ba60)(p)
#define MU_ARM_WORDS ((volatile unsigned *)((unsigned)usb_input_arm_words | 0x20000000))
/* Exact retail sceSifSetDma syscall wrapper, confirmed at 0x10f020. */
#define MU_ARM_DMA(d) ((int (*)(mu_arm_transfer *, int))0x10f020)(d, 1)
#define MU_ARM_BARRIER() __asm__ volatile("sync" ::: "memory")
#define MU_READ(p, s, b) ((int (*)(int, int, void *))0x11b9d8)(p, s, b)
#define MU_TYPE(p) ((void (*)(void *))0x13b620)(p)
#define MU_DMA_FRAMES                                                          \
  ((volatile mu_frame *)((unsigned)usb_input_frames | 0x20000000))
#endif
#ifdef MU_NATIVE_TEST
#ifndef MU_TYPE
#define MU_TYPE(p) ((void)(p))
#endif
#endif
/* One boot-time 16-byte EE->IOP arm, after native SIF/PAD initialization.
 * No RPC, waits or recurring transfers in the input path. Source is written
 * uncached and left immutable once queued; the native sampler stays unchanged. */
static void usb_input_arm(void) {
  if (usb_input_armed) return;
  unsigned address=usb_input_config.gate_address;
  if (!usb_input_config.gate_token || address<0x1000 || address>0x1ffff0 || (address&15)) return;
  volatile unsigned *words=MU_ARM_WORDS;
  words[0]=usb_input_config.gate_token;words[1]=1;words[2]=0;words[3]=0;
  MU_ARM_BARRIER();
  mu_arm_transfer transfer={usb_input_arm_words,(void *)(unsigned long)address,16,0};
  if (MU_ARM_DMA(&transfer)>0) usb_input_armed=1;
}
static int usb_input_get(void) {
  volatile mu_frame *frames = MU_DMA_FRAMES;
  mu_frame candidate;
  unsigned newest = 0;
  for (unsigned i = 0; i < 2; i++) {
    if (!mu_snapshot(&candidate, frames + i))
      continue;
    if (newest && (int)(candidate.head - newest) <= 0)
      continue;
    if (usb_input_seen && (int)(candidate.head - usb_input_seen) < 0)
      continue;
    usb_input_snapshot = candidate;
    newest = candidate.head;
  }
  if (newest && newest != usb_input_seen) {
    usb_input_seen = newest;
    usb_input_stale = 0;
  } else if (usb_input_stale < 120)
    usb_input_stale++;
  /* Fail neutral if the publisher dies. Unchanged HID reports don't time
   * out: the IOP heartbeat still advances without new USB transfers. */
  return usb_input_seen && usb_input_stale < 4;
}
void usb_input_setup(void *pad) {
  unsigned char *p = pad;
  unsigned port = *(unsigned *)(p + 0x100);
  if (port >= 2 || !usb_input_config.owned[port]) {
    MU_SETUP(pad);
    return;
  }
  usb_input_arm();
  if (!port || !usb_input_config.owned[0])
    usb_input_get(); /* one snapshot for both ports */
  unsigned connected = usb_input_seen && usb_input_stale < 4 &&
                       usb_input_snapshot.connected[port];
  unsigned type=usb_input_snapshot.reserved[3+port];
  if(connected && type<=MK_5KEY && usb_input_type_seen[port]!=type){
    /* A controller-family change is a port setup transition. Only this port's
     * repeat/history/mapping state is reset; no new gameplay debounce. */
    for(unsigned i=0x10a;i<0x11c;i++){p[i]=0;
#ifndef MU_NATIVE_TEST
      LOOP_PAD();
#endif
    }
    for(unsigned i=0x120;i<0x178;i++){p[i]=0;
#ifndef MU_NATIVE_TEST
      LOOP_PAD();
#endif
    }
    *(unsigned *)(p+0x11c)=0;usb_input_type_seen[port]=type;
  }
  *(unsigned *)(p + 0x104) = connected ? 5 : 0;
  p[0x108] = 0;
}
void usb_input_type(void *pad){
  MU_TYPE(pad);unsigned char *p=pad;unsigned port=*(unsigned *)(p+0x100);
  if(port>=2||!usb_input_config.owned[port]||*(unsigned *)(p+0x104)!=5)return;
  unsigned type=usb_input_snapshot.reserved[3+port];
  *(unsigned *)(p+0x11c)=type==MK_DUALSHOCK?4:type==MK_5KEY?3:2;
  if(type==MK_7KEY)*(unsigned short *)(p+0x10a)&=0x1fff;
}
int usb_input_read(int port, int slot, void *packet) {
  if ((unsigned)port >= 2 || !usb_input_config.owned[port])
    return MU_READ(port, slot, packet);
  if (slot || usb_input_stale >= 4 || !usb_input_seen ||
      !usb_input_snapshot.connected[port])
    return 0;
  unsigned type=usb_input_snapshot.reserved[3+port];
  unsigned logical = usb_input_snapshot.logical[port] & mk_control_mask(type);
  /* Inverse native KOC tables; kept here allocation/libc free. */
  static const unsigned short map[2][11] = {
      {8, 0x40, 4, 0x80, 2, 0x1000, 1, 0x800, 0x100, 0x10, 0x20},
      {0x10, 0x20, 8, 0x40, 4, 0x80, 2, 0x800, 0x100, 0x1000, 1}};
  unsigned buttons = 0xe000;
  for (unsigned i = 0; i < 11; i++)
    if (logical >> i & 1)
      buttons |= map[port][i];
  if(type==MK_DUALSHOCK){
    static const unsigned short ds[14]={0x1000,0x4000,0x8000,0x2000,0x10,0x40,0x80,0x20,4,1,8,2,0x800,0x100};
    buttons=0;for(unsigned i=0;i<14;i++){if(logical>>i&1)buttons|=ds[i];
#ifndef MU_NATIVE_TEST
      LOOP_PAD();
#endif
    }
  }else if(type==MK_5KEY){
    static const unsigned short five[11]={0x80,4,0x40,8,0x20,0,0,0x800,0x100,0x4000,0x1000};
    buttons=0;for(unsigned i=0;i<11;i++){if(logical>>i&1)buttons|=five[i];
#ifndef MU_NATIVE_TEST
      LOOP_PAD();
#endif
    }
  }
  unsigned char *b = packet;
  for (unsigned i = 0; i < 32; i++) {
    b[i] = 0;
#ifndef MU_NATIVE_TEST
    LOOP_PAD();
#endif
  }
  b[1] = 0x41;
  b[2] = ~buttons >> 8;
  b[3] = ~buttons;
  return 4;
}
