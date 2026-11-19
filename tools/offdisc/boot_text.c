/* Resident USB startup text. Port of the hardware-qualified/no-ROM
 * PS2SDK R121b screen's packet format, with no SDK/IOP/heap dependency.
 * Match patched game video mode early; after native graphics, only transfer
 * pixels into its two framebuffer surfaces. No late display/GS-state reset.
 * All DMA sources are aligned persistent runtime objects. Never overwrite
 * an in-flight source or spin indefinitely. Native graphics takes ownership.
 * PS2SDK screen copyright/licence: PS2DEV, Academic Free License2.0. */
#include "boot_text_font.h"
#ifndef BT_TEXT_TEST
#define btx_word(a) (*(volatile u32 *)(a))
#define btx_env(a) (*(volatile unsigned long long *)(a))
#define btx_write(a,v) (*(volatile unsigned long long *)(a)=(unsigned long long)(v))
#define btx_uncached(p) ((void *)((u32)(p)|0x20000000u))
static void btx_crt(u32 progressive){((void (*)(u32,u32,u32))0x10e860)(!progressive,progressive?0x50:2,progressive);}
static u32 btx_clock(void){u32 n;__asm__ volatile("mfc0 %0,$9":"=r"(n));return n;}
static void btx_start(const void *p,u32 n){__asm__ volatile("sync":::"memory");btx_word(0x1000a020)=n;btx_word(0x1000a010)=(u32)p;btx_word(0x1000a000)=0x101;}
#endif
static u32 btx_initialized,btx_owned,btx_failed,btx_native,btx_retired;
static u32 btx_progressive,btx_height,btx_psm,btx_base,btx_dx,btx_dy,btx_width=10;
static int btx_wait(void){for(u32 i=0;i<65536;i++){if(!(btx_word(0x1000a000)&0x100))return 1;LOOP_PAD();}btx_failed=1;return 0;}
static void btx_pause(u32 ticks){u32 start=btx_clock();while((u32)(btx_clock()-start)<ticks){LOOP_PAD();LOOP_PAD();}}
static void btx_copy(void *out,const void *in,u32 bytes){volatile u32 *d=btx_uncached(out);const u32 *s=in;for(u32 i=0;i<bytes/4;i++){d[i]=s[i];LOOP_PAD();}}
static int btx_send(const void *p,u32 n){if(!btx_wait())return 0;btx_start(p,n);return btx_wait();}
static const unsigned long long btx_setup_template[30] __attribute__((aligned(16)))={
 0x100000000000800eull,0xe,0xa0000,0x4c,0x8c,0x4e,
 27648ull|(30976ull<<32),0x18,0x00df0000027f0000ull,
 0x40,1,0x1a,1,0x46,0,0x45,0x70000,0x47,
 0x30000,0x47,6,0,0x3f80000000000000ull,1,0x79006c00,5,
 0x87009400,5,0x70000,0x47
};
/* Note SCISSOR is four uint16 (0,639,0,223), not two reversed words. */
static unsigned long long btx_setup[30] __attribute__((aligned(16)));
static unsigned long long btx_char[12] __attribute__((aligned(16)));
static u32 btx_pixels[64] __attribute__((aligned(16)));
/* One compact48-cell row, persistent until its DMA completes. */
static u32 btx_strip[48*7*8] __attribute__((aligned(16)));
static int btx_init(void){
 if(btx_initialized)return !btx_failed;
 if(!btx_wait())return 0;
 /* Read checked, launcher-patched native instructions, never GS readback.
  * This also works after a warm prepared-image restore or a menu mode change. */
 u32 mode=btx_word(0x139e00),lines=btx_word(0x139f30);
 if(mode!=0x24060002&&mode!=0x24060050){btx_failed=1;return 0;}
 btx_progressive=mode==0x24060050;btx_height=btx_progressive?(lines&65535):224;
 if((!btx_progressive&&lines!=0x240700e0)||(btx_progressive&&lines!=0x240701c0&&lines!=0x240701e0)){btx_failed=1;return 0;}
 btx_write(0x12001000,0x200);btx_write(0x12001010,0xff00);btx_crt(btx_progressive);
 btx_write(0x12000000,0xff62);btx_write(0x12000090,0x1400);
 /* SDK graph_mode's 480p timing: DX232,DY35,1440 dots; 640*2 dots,
  * centered80 dots later. NTSC retains the hardware-qualified R136 setup. */
 btx_write(0x120000a0,btx_progressive?312ull|35ull<<12|1ull<<23|1279ull<<32|(unsigned long long)(btx_height-1)<<44:0x001bf9ff0983227cull);
 btx_word(0x10003000)=1;btx_word(0x10003010)=0;btx_word(0x1000e000)|=1;
 btx_copy(btx_setup,btx_setup_template,sizeof btx_setup);
 ((volatile unsigned long long *)btx_uncached(btx_setup))[8]=639ull|(unsigned long long)(btx_height-1)<<32;
 if(!btx_send(btx_setup,15))return 0;
 btx_initialized=btx_owned=1;return 1;
}
static int btx_glyph(u32 x,u32 y,u32 color,unsigned ch){
 if(!btx_wait())return 0;
 static const unsigned long long q[12] __attribute__((aligned(16)))={0x1000000000000004ull,0xe,0xa000000000000ull,0x50,0,0x51,8ull|(8ull<<32),0x52,0,0x53,0x0800000000008010ull,0};
 btx_copy(btx_char,q,sizeof q);
 volatile unsigned long long *cmd=btx_uncached(btx_char);cmd[4]=(unsigned long long)(x+btx_dx)<<32|(unsigned long long)(y+btx_dy)<<48;
 cmd[2]=(unsigned long long)btx_base<<32|(unsigned long long)btx_width<<48|(unsigned long long)btx_psm<<56;
 u32 bytes=btx_psm?3:4;cmd[10]=0x0800000000008000ull|bytes*4;
 if(!btx_send(btx_char,6))return 0;
 if(ch<32||ch>127)ch=32;const u8 *font=bt_text_font+(ch-32)*8;volatile u8 *pixels=btx_uncached(btx_pixels);
 for(u32 i=0;i<8;i++)for(u32 j=0;j<8;j++){u32 c=(font[i]&(128u>>j))?color:0;for(u32 k=0;k<bytes;k++){pixels[(i*8+j)*bytes+k]=(u8)(c>>(8*k));LOOP_PAD();}}
 return btx_send(btx_pixels,bytes*4);
}
static int btx_line(u32 row,const char *s,u32 color){
 u32 end=0;for(u32 i=0;i<72;i++){u32 c=end?32:(u8)*s;if(!c){c=32;end=1;}else if(!end)s++;if(!btx_glyph(16+i*7,16+row*12,color,c))return 0;LOOP_PAD();}return 1;
}
static int btx_strip_line(u32 row,const char *s,u32 color){
 if(!btx_wait())return 0;
 volatile u8 *pixels=btx_uncached(btx_strip);u32 bytes=btx_psm?3:4,end=0;
 for(u32 cell=0;cell<48;cell++){
  u32 ch=end?32:(u8)*s;if(!ch){ch=32;end=1;}else if(!end)s++;if(ch<32||ch>127)ch=32;
  const u8 *font=bt_text_font+(ch-32)*8;
  for(u32 y=0;y<8;y++)for(u32 x=0;x<7;x++){
   u32 c=(font[y]&(128u>>x))?color:0,at=(y*336+cell*7+x)*bytes;
   for(u32 k=0;k<bytes;k++){pixels[at+k]=(u8)(c>>(k*8));LOOP_PAD();}
  }LOOP_PAD();
 }
 static const unsigned long long q[12] __attribute__((aligned(16)))={0x1000000000000004ull,0xe,0,0x50,0,0x51,336ull|(8ull<<32),0x52,0,0x53,0,0};
 btx_copy(btx_char,q,sizeof q);volatile unsigned long long *cmd=btx_uncached(btx_char);
 cmd[2]=(unsigned long long)btx_base<<32|(unsigned long long)btx_width<<48|(unsigned long long)btx_psm<<56;
 cmd[4]=(unsigned long long)(16+btx_dx)<<32|(unsigned long long)(16+row*12+btx_dy)<<48;
 cmd[10]=0x0800000000008000ull|bytes*168;
 return btx_send(btx_char,6)&&btx_send(btx_strip,bytes*168);
}
static const char *btx_name(u32 site){switch(site){
 case 0x151894:return "NATIVE ARENA";case 0x1518ac:return "NATIVE GLOBALS";
 case 0x12f798:return "AUDIO RESET";case 0x12f7a0:return "SIF RPC FIRST";
 case 0x12f7a8:return "CD INIT FIRST";case 0x12f7b0:return "CD MODE FIRST";
 case 0x12f7f8:return "SIF RPC SECOND";case 0x12f800:return "LOADFILE CLIENT";
 case 0x12f808:return "FILEIO CLIENT";case 0x12f810:return "CD INIT SECOND";
 case 0x12f818:return "CD MODE SECOND";case 0x12f820:return "IOP HEAP CLIENT";
 case 0x12f904:return "FILE QUEUE";case 0x12f90c:return "NATIVE AUDIO";
 case 0x12f914:return "NATIVE GRAPHICS";case 0x12f91c:return "NATIVE PAD";
 case 0x151988:return "GAME INITIALIZATION";case 0x151990:return "LOOP INPUT STATE";
 case 0x1519a0:return "INPUT WAIT";case 0x1519c0:return "SCENE UPDATE";
 case 0x1519d0:return "GRAPHICS SUBMIT";case 0x1519e8:return "FRAME FLIP";
 case 0x1519a8:return "RENDER RESET";case 0x1519b0:return "SYSTEM UPDATE";
 case 0x1519b8:return "SERVICE 130730";case 0x1519c8:return "SCENE DRAW";
 case 0x1519d8:return "MOVIE SERVICE";case 0x1519e0:return "FRAME WAIT";
 case 0x1519f0:return "AUDIO WAKE";case 0x1519f8:return "TIMER UPDATE";
 case 0x151a00:return "SCENE CLEANUP";case 0x1380000:return "TRACE LIMIT REACHED";
 default:return "UNKNOWN STAGE";
}}
static void btx_hex(char *s,u32 v){static const char h[]="0123456789ABCDEF";for(u32 i=0;i<8;i++){s[i]=h[(v>>(28-i*4))&15];LOOP_PAD();}}
static const char *btx_sound_name(u32 site){switch(site){
 case 0x13e8a8:return "BANK SUBMIT";case 0x13e8b0:return "BANK FIRST POLL";
 case 0x13e8d0:return "BANK RECHECK";case 0x13f630:return "FADE BANK";
 case 0x13f670:return "FADE STREAM";case 0x13f678:return "STOP STREAM";
 case 0x13f878:return "TRANSFER STATUS";case 0x13f880:return "VOICE RESIDENCY";
 case 0x13f888:return "VOICE TICK";case 0x13f890:return "STREAM TICK";
 case 0x13f898:return "COMMAND BUILD";case 0x13f8a0:return "COMMAND SEND";
 case 0x13f8d8:return "TRANSFER PUBLISH";case 0x13e490:return "AUDIO INIT";case 0x13fa70:return "ALLOC 32K";case 0x13fa80:return "ALLOC 128K";case 0x13fa9c:return "ALLOC 760K";case 0x145030:return "STATUS BANK";case 0x145064:return "STATUS STREAM";case 0x13e8e0:return "BANK FINISH";case 0x14713c:return "RPC BEFORE";case 0x1471f8:return "RPC AFTER";case 0x13f328:return "QUEUE FIRST";case 0x13f340:return "QUEUE REPEAT";case 0x13f360:return "UPLOAD FIRST";case 0x13f378:return "UPLOAD REPEAT";case 0x13f398:return "STREAM FIRST";case 0x13f3b8:return "STREAM REPEAT";case 0x13f3d0:return "STREAM LAST FIRST";case 0x13f3fc:return "STREAM LAST REPEAT";default:return "NO CALL YET";
}}
static int btx_sound_line(u32 row,const char *prefix,volatile u32 *cur){
 char s[49];u32 j=0;while(*prefix&&j<48){s[j++]=*prefix++;LOOP_PAD();}
 btx_hex(s+j,cur[0]);j+=8;s[j++]=' ';s[j++]=cur[1]?'R':'E';s[j++]=' ';
 const char *name=btx_sound_name(cur[0]);while(*name&&j<48){s[j++]=*name++;LOOP_PAD();}s[j]=0;
 return btx_strip_line(row,s,cur[1]?0x50ff50:0x50c0ff);
}
static int btx_panel(u32 site,u32 after,u32 value){
 if(boot_alloc[5]){
  char a[]="RESULT 00000000";btx_hex(a+7,boot_alloc[0]);
  char b[]="32K 00000000 128K 00000000";btx_hex(b+4,boot_alloc[1]);btx_hex(b+18,boot_alloc[2]);
  char c[]="760K 00000000";btx_hex(c+5,boot_alloc[3]);
  char mode[]="MODE 0 OWN 0 RESERVED 00000000";mode[5]='0'+boot_iop_budget[1];mode[11]='0'+boot_iop_budget[3];btx_hex(mode+22,boot_iop_budget[2]);
  if(!btx_strip_line(0,"R143 AUDIO IOP ALLOCATION FAILED",0x50c0ff)||
   !btx_strip_line(1,mode,0xffffff)||
   !btx_strip_line(2,boot_alloc[6]?"EARLY RESERVE FAILED - INIT NOT RUN":a,0xffffff)||
   !btx_strip_line(4,b,0xffffff)||!btx_strip_line(6,c,0xffffff)||
   !btx_strip_line(8,"STOPPED BEFORE BANK TRANSFER",0xffffff))return 0;
  u32 n=boot_iop_budget[4],first=n>8?n-8:0;
  for(u32 i=0;i<8;i++){
   char line[]="H00000000 T00000000 M00000000 R00000000";
   if(first+i<n&&first+i<IB_RECORDS){volatile u32 *r=boot_iop_budget+8+(first+i)*4;
    btx_hex(line+1,r[0]);btx_hex(line+11,r[1]);btx_hex(line+21,r[2]);btx_hex(line+31,r[3]);
   }else line[0]=0;
   if(!btx_strip_line(9+i,line,0xffffff))return 0;
  }return 1;
 }
 if(boot_loop_state[0]){
  char a[]="R143 F00000000 C00000000 A00000000";
  btx_hex(a+6,boot_loop_state[0]);btx_hex(a+16,boot_loop_state[12]);btx_hex(a+26,boot_detail[16]);
  char c[]="STAT E00000000 R00000000 B00000000 P00000000";
  btx_hex(c+6,boot_workers[4]);btx_hex(c+16,boot_workers[5]);btx_hex(c+26,boot_detail[13]);btx_hex(c+36,boot_detail[14]);
  char d[]="V00000000 U00000000 R00000000";
  btx_hex(d+1,boot_sound_cursor[6]);btx_hex(d+11,boot_workers[0]);btx_hex(d+21,boot_workers[1]);
  u32 rpc=(boot_loop_state[0]/120)%2,k=rpc*8;
  char e[]="RPC0 P00000000 I00000000 S00000000";e[3]='0'+rpc;
  btx_hex(e+6,boot_rpc[k]);btx_hex(e+16,boot_rpc[k+1]);btx_hex(e+26,boot_rpc[k+2]);
  char f[]="F00000000 I00000000 B00000000 V00000000";
  btx_hex(f+1,boot_rpc[k+3]);btx_hex(f+11,boot_rpc[k+4]);btx_hex(f+21,boot_rpc[k+5]);btx_hex(f+31,boot_rpc[k+7]);
  u32 rec=((boot_loop_state[0]/30)%30)*8;
  char g[]="T00000000 E00000000 R00000000";
  btx_hex(g+1,boot_sound[rec]);btx_hex(g+11,boot_sound[rec+4]);btx_hex(g+21,boot_sound[rec+5]);
  char h[]="INIT 00000000 M00000000 00000000 00000000";
  btx_hex(h+5,boot_alloc[0]);btx_hex(h+15,boot_alloc[1]);btx_hex(h+24,boot_alloc[2]);btx_hex(h+33,boot_alloc[3]);
  return btx_strip_line(0,a,0xffffff)&&
   (site==0x1380000?btx_strip_line(2,"TRACE LIMIT REACHED",0x50ff50):btx_sound_line(2,"SND ",boot_sound_cursor))&&btx_strip_line(4,c,0xffffff)&&
   btx_sound_line(6,"BNK ",boot_sound_cursor+4)&&btx_strip_line(8,d,0xffffff)&&
   btx_strip_line(10,e,0xffffff)&&btx_strip_line(12,f,0xffffff)&&
   btx_strip_line(14,g,0xffffff)&&btx_strip_line(16,h,0xffffff);
 }
 char line[]="SITE 00000000   ARG/RET 00000000";btx_hex(line+5,site);btx_hex(line+24,value);
 return btx_line(0,"MEGAMIX USB R143 - SOUND AND IOP MEMORY",0xffffff)&&
    btx_line(2,btx_name(site),0xffffff)&&btx_line(4,after?"RETURNED":"ENTER",after?0x50ff50:0x50c0ff)&&btx_line(6,line,0xffffff);
}
static int btx_native_panel(u32 site,u32 after,u32 value){
 /* Native sceGsDBuff DISPFB2 software mirrors (10 and38), in EE RAM.
  * FRAME_1 is the shared off-screen render target, NOT the displayed surface.
  * Host-to-local IMAGE writes touch only transfer registers, not native
  * FRAME/TEST/XYOFFSET/SCISSOR/texture state, PMODE or display registers.
  * Write both framebuffers; native parity may flip before the next marker. */
 /* Don't submit over an active native VIF/VU/GIF producer. Busy at this
  * boundary skips the panel, not the native call or all future checkpoints. */
 u32 ready=0;for(u32 n=0;n<65536;n++){
  if(!(btx_word(0x10009000)&0x100)&&!(btx_word(0x10003c00)&7)&&!(btx_word(0x10003020)&0x1f001feb)){ready=1;break;}LOOP_PAD();
 }if(!ready)return 0;
 for(u32 i=0;i<2;i++){
  unsigned long long f=btx_env(i?0x34cd38:0x34cd10);
  u32 psm=(u32)(f>>15)&31,width=(u32)(f>>9)&63,base=((u32)f&511)*32,dx=(u32)(f>>32)&2047,dy=(u32)(f>>43)&2047;
  if(psm>1||width!=10||dx+528>640||dy+((boot_loop_state[0]||boot_alloc[5])?216:96)>btx_height||base+width*64*(dy+btx_height)*4/256>16384)return 0;
  btx_base=base;btx_width=width;btx_psm=psm;btx_dx=dx;btx_dy=dy;
  if(!btx_panel(site,after,value))return 0;LOOP_PAD();
 }return 1;
}
static void boot_trace_mark(u32 site,u32 after,u32 value){
 boot_trace_state[0]=site;boot_trace_state[1]=after;boot_trace_state[2]=value;
 if(site==0x151988&&!after)return; /* native constructors still precede arena */
 if(site==0x12f914&&after){btx_owned=0;btx_native=1;}
 if(btx_failed||btx_retired)return;
 u32 first=!btx_initialized;if(!btx_init()){boot_trace_state[3]++;return;}
 if(!(btx_native?btx_native_panel(site,after,value):btx_panel(site,after,value))){boot_trace_state[3]++;return;}
 /* First2 loops have recording holds; subsequent panels have no deliberate
  * pause. The already-observed first flip is no longer the retirement point. */
 u32 frame=boot_loop_state[0];
 if(!frame)btx_pause(first?0x60000000u:btx_native?0x10000000u:0x01000000u);
 else if(frame<=2||site==0x1380000)btx_pause(frame==1&&site>=0x1519f0?0x10000000u:0x02000000u);
}
