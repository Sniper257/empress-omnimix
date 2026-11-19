/* Private PREVIEW_TRACE fatal/stall capture. Normal execution only samples
 * RAM in the existing VBlank handler. NO file/RPC/heap/input/GS work until a
 * fatal exception, or180 consecutive VBlanks without a submitted preview frame.
 * Terminal display deliberately stops the failed run; it never retries PLAY.
 * Kernel exception entry follows ps2sdk ee/eedebug/src/ee_exceptions.S's private
 * stack pattern. Only fault causes1..7/10..13; interrupts/syscalls stay native.
 */
#ifndef PREVIEW_FAULT_TEST
#define pf_word(a) (*(volatile u32 *)(unsigned long)(a))
#define pf_read_reg(a) (*(volatile unsigned long long *)(unsigned long)(a))
#define pf_write_reg(a,v) (*(volatile unsigned long long *)(unsigned long)(a)=(v))
#define pf_env64(a) (*(volatile unsigned long long *)(unsigned long)(a))
extern void pvt_exception_vector(void),pvt_gif_qword(const u32 *);
extern void pvt_set_handler(u32,u32),pvt_set_tlb_handler(u32,u32);
#define pf_install(n,tlb) ((tlb)?pvt_set_tlb_handler(n,(u32)(unsigned long)pvt_exception_vector):pvt_set_handler(n,(u32)(unsigned long)pvt_exception_vector))
#define pf_vblank_native(...) (((unsigned long long (*)(FS_ARGS))0x10ece0)(__VA_ARGS__))
static u32 pf_cp0_epc(void){u32 n;__asm__ volatile("mfc0 %0,$14":"=r"(n));return n;}
#define pf_fifo(p) pvt_gif_qword(p)
#define pf_halt() do{for(;;){LOOP_PAD();__asm__ volatile("nop");}}while(0)
#endif
/* Terminal-only aliases into existing UI/title scratch, supplied by ASM.
 * At a fatal exception we abandon the overlay permanently: first576B save
 * registers, last aligned4KiB are a fresh stack. Never the interrupted stack,
 * no new resident buffers and no mutation during ordinary execution. */
#ifdef PREVIEW_FAULT_TEST
u8 pvt_fault_stack[4096] __attribute__((aligned(16)));
volatile u32 pvt_fault_frame[144] __attribute__((aligned(16)));
#else
#define pvt_fault_frame ((volatile u32 *)spr_buf)
_Static_assert(sizeof spr_buf>=18432,"terminal scratch extent");
#endif
static struct {u32 installed,last_frame,stalled,vblanks,epc,terminal,failed,dbx,dby;u32 q[4] __attribute__((aligned(16)));} pf;
static int pf_send(unsigned long long low,unsigned long long high){
 /* Bounded path3 FIFO wait, including broken DMA/GIF cases. No SDK/native GS
  * linked lists, allocation, FlushCache, syscalls or storage from IRQ/fault. */
 u32 n=0;while(((pf_word(0x10003020)>>24)&31)>=15 && n++<100000)LOOP_PAD();
 if(n>=100000){pf.failed=1;return 0;}
 pf.q[0]=(u32)low;pf.q[1]=(u32)(low>>32);pf.q[2]=(u32)high;pf.q[3]=(u32)(high>>32);pf_fifo(pf.q);return 1;
}
static void pf_tag(u32 n){pf_send((1ULL<<60)|0x8000|n,0xe);}
static void pf_ad(unsigned long long data,u32 reg){if(!pf.failed)pf_send(data,reg);}
static void pf_rect(u32 x,u32 y,u32 w,u32 h,u32 rgb){
 pf_tag(4);pf_ad(6,0);pf_ad(rgb|0x80000000u,1);
 pf_ad(((unsigned long long)(pf.dbx+x)<<4)|((unsigned long long)(pf.dby+y)<<20),5);
 pf_ad(((unsigned long long)(pf.dbx+x+w)<<4)|((unsigned long long)(pf.dby+y+h)<<20),5);
}
static void pf_digit(u32 x,u32 y,u32 digit){
 static const u8 bits[16]={63,6,91,79,102,109,125,7,127,111,119,124,57,94,121,113};
 static const u8 rect[7][4]={{1,0,5,1},{5,1,6,6},{5,7,6,12},{1,12,5,13},{0,7,1,12},{0,1,1,6},{1,6,5,7}};
 for(u32 i=0;i<7;i++){if(bits[digit&15]&(1u<<i))pf_rect(x+rect[i][0],y+rect[i][1],rect[i][2]-rect[i][0],rect[i][3]-rect[i][1],0xffffff);LOOP_PAD();}
}
static void pf_screen(u32 kind,const u32 *r){
 /* Terminal recovery only. Stop GIF DMA2/reset GIF FIFO, retaining the existing
  * CRT mode and DISPLAY geometry. Render into its currently scanned framebuffer.
  * Never initialize another video mode or issue a DMA referencing suspect RAM. */
 pf_word(0x1000a000)=0;pf_word(0x10003000)=1;pf_word(0x10003000)=0;
 pf_write_reg(0x12001000,0x103); /* FLUSH + acknowledge SIGNAL/FINISH, not RESET */
 /* PMODE/DISPFB are WRITE-ONLY on GS hardware. Native139be0->101cd8
  * always selects display environment0 at34cd00;100b20 reads its PMODE+0
  * and DISPFB+10 before writing the GS. Use that same RAM shadow, never
  * emulator-style privileged-register readback. The existing CRT/DISPLAY
  * geometry stays untouched; both field scanouts use this terminal image. */
 unsigned long long pm=pf_env64(0x34cd00),fb=pf_env64(0x34cd10);
 u32 width=(u32)(fb>>9)&63,psm=(u32)(fb>>15)&31;
 if(!(pm&3) || width<10 || (psm!=0&&psm!=1&&psm!=2&&psm!=10)){pf.failed=2;goto failed;}
 pf.dbx=(u32)(fb>>32)&2047;pf.dby=(u32)(fb>>43)&2047;
 if(pf.dbx+640>2048||pf.dby+448>2048){pf.failed=3;goto failed;}
 pf_write_reg(0x12000070,fb);pf_write_reg(0x12000090,fb);pf_write_reg(0x12000000,pm);
 pf_tag(8);
 pf_ad((fb&511)|((unsigned long long)width<<16)|((unsigned long long)psm<<24),0x4c);
 pf_ad(1ULL<<32,0x4e);pf_ad(0,0x18);
 pf_ad(pf.dbx|((unsigned long long)(pf.dbx+639)<<16)|((unsigned long long)pf.dby<<32)|((unsigned long long)(pf.dby+447)<<48),0x40);
 pf_ad(0x30000,0x47); /* GS requires ZTE1; ZTST ALWAYS, Z writes masked. */pf_ad(1,0x1a);pf_ad(1,0x46);pf_ad(0,0x45);
 pf_rect(0,0,640,448,0x100010);pf_digit(38,28,kind==1?14:10);pf_digit(46,28,kind==1?1:4);
 for(u32 row=0;row<11;row++)for(u32 col=0;col<3;col++)for(u32 i=0;i<8;i++){
  pf_digit(38+col*130+i*9,52+row*21,r[row*3+col]>>(28-i*4));LOOP_PAD();}
failed:
 if(pf.failed){pf_write_reg(0x120000e0,0x0000a0);pf_write_reg(0x12000000,0);} /* red fallback */
}
static void pf_terminal(u32 kind,u32 epc,u32 cause,u32 bad){
 if(pf.terminal)pf_halt();pf.terminal=kind;
 u32 r[33]={epc,cause,bad,pt_main_boundary,pt.stage,ptl.stage,pt.frames,pf.vblanks,ptl.owner,
  pt_word(0x78af08),pt_word(0x78af00),pt_word(0x2ad078),
  pt_word(0x2ad534),pt_word(0x2ad538),pt_word(0x2ad120),
  (u32)pf_read_reg(0x12001000),pf_word(0x10003020),pf_word(0x1000a000),
  kind==1?pvt_fault_frame[29*4]:0,kind==1?pvt_fault_frame[31*4]:0,kind==1?pvt_fault_frame[28*4]:0,
  ptl.written,ptl.error,ptl.dropped,
  /* Keep the actual absent-container observation, even when the main journal
   * could not flush before the fatal native lookup. RAM only, no fault I/O. */
  pt.missing_cont,pt.missing_idx,pt.missing_site,
  kind==1?pvt_fault_frame[4*4]:0,kind==1?pvt_fault_frame[5*4]:0,kind==1?pvt_fault_frame[6*4]:0,
  kind==1?pvt_fault_frame[7*4]:0,kind==1?pvt_fault_frame[8*4]:0,kind==1?pvt_fault_frame[9*4]:0};
 pf_screen(kind,r);pf_halt();
}
void pvt_exception_capture(void){
 pf_terminal(1,pvt_fault_frame[130],pvt_fault_frame[129],pvt_fault_frame[132]);
}
static void preview_fault_start(void){
 pf.last_frame=pt.frames;pf.stalled=pf.vblanks=pf.terminal=pf.failed=0;
 if(!pt.mode || pf.installed)return;
 /* A private diagnostic image only. Fatal captures remain installed until
  * reboot; only already-fatal exceptions change behavior, never resume them. */
 for(u32 n=1;n<=13;n++){if(n!=8&&n!=9)pf_install(n,n<=3);LOOP_PAD();}
 pf.installed=1;
}
unsigned long long pvt_vblank_call(FS_ARGS){
 u32 epc=pf_cp0_epc();
 if(pt.mode && ui_active==2 && play_preview_record && !pt_playing()){
  pf.epc=epc;pf.vblanks++;
  if(pf.last_frame==pt.frames)pf.stalled++;else{pf.last_frame=pt.frames;pf.stalled=0;}
  if(pf.stalled>=180)pf_terminal(2,epc,0,0);
 }else pf.stalled=0;
 return pf_vblank_native(FS_PASS);
}
