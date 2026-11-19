/* LIFT: visual hit-line displacement only. No clocks, hit windows, audio,
 * input sampling or note-event mutation. The original input owner supplies
 * cover motion; LIFT and SUD keep independent lengths and white numbers. */
u32 lift_y[2];
static u32 lift_route_mask;
static u32 lift_on(u32 side){return side<2&&fs_word(0x2ad5e8)&&(fh_active_mask()&(1u<<side))&&menu_control.options.lift[fs_dp()?0:side];}
static u32 lift_pixels(u32 side){if(!lift_on(side))return 0;u32 n=menu_control.options.lift_height[fs_dp()?0:side];return n>265?265:n;}
static void lift_sync(void){for(u32 s=0;s<2;s++){LOOP_PAD();lift_y[s]=lift_pixels(s);}}
static void lift_prepare(void){lift_route_mask=0;lift_sync();}
extern unsigned long long lift_hidden_body(FS_ARGS);
unsigned long long lift_hidden_call(FS_ARGS){return lift_on((u32)a)?0:lift_hidden_body(FS_PASS);}
static u32 lift_cover_ready(u32 id){
 u32 n=fs_word(0x2ad030);if(n>64)return 0;
 for(u32 i=0;i<n;i++){LOOP_PAD();u32 *p=(u32 *)(unsigned long)(0x34aa80+i*0x228);if(p[0x84]==id&&p[0]&&p[3])return 1;}return 0;
}
static void lift_draw(void){
 u32 hud=fs_word(0x2ad5e8),phase=fs_phase();if(!hud||phase<2||phase>=8||!fs_font_ready())return;
 u32 layout=fs_word(hud);if(layout>5)return;u32 wide=*(u8 *)(unsigned long)(hud+5),width=wide?144:233;
 for(u32 s=0;s<2;s++){LOOP_PAD();if(!lift_on(s))continue;u32 dy=lift_pixels(s),id=ca_cover_id((u8 *)(unsigned long)hud,s);
  s32 x=*(short *)(unsigned long)(0x2815a0+layout*4+s*2),line=321-(s32)dy;
  short clip[4]={(short)x,(short)line,(short)width,(short)dy};
  if(dy&&lift_cover_ready(id)){
   /* Reuse this player's already resident SUD texture. Match native
    * asymmetric-cover orientation; keep clipping in physical lane space.
    *12FDD0 arg10 is blend mode, arg12 is priority (not the reverse).
    * Priority15: behind READY9/judgement10/bombs11, above notes16/beams25. */
   u32 mirror=(ca.cover[s]==10||ca.cover[s]==12)&&!*(u8 *)(unsigned long)(hud+6+s);
   fs_native_glyph(id<<16,x+(mirror?(s32)width:0),line,mirror?-100:100,100,0,0,100,0,0,clip,15);
   short red[4]={(short)x,(short)(line-2),(short)width,2};
   /*137EC0 accepts RRGGBB, then packs GS low-byte red.*/
   fh_native_rect(red,0xc02020,100,15);
  }
  if(!FH_HOLD(s))continue;
  char text[12];unsigned n=0;u32 wn=dy*1000/320;
  mm_number(text,11,&n,wn);fs_text(text,0,x+width/2-43,line+7,100);
  n=0;u32 units=fh_floating(s)?fhs.units[s]:fh_step(s),gn=fh_green(s,fh_bpm_for(s),units);
  mm_number(text,11,&n,gn);fs_text(text,2,x+width/2+7,line+7,100);
  fs_text("L",0,x+width/2-61,line+7,100);
 }
}
