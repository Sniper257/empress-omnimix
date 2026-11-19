/* Native artwork only. Ordinary PLAY resolves choices during construction;
 * the preview may explicitly change resident effect families without I/O. */
static struct {s32 turn[2],bomb[2];u32 cover[2],filter;const struct custom_art_pack *custom_bomb[2];} ca;
u32 ca_particle_scale=100;
#ifdef PLAY_LIFT
u32 lift_particle_y;
#endif
static const struct cs_preferences *ca_prefs(void){return cs_live?cs_live:&customize;}
static u32 ca_random(u32 n){return CS_NATIVE(0x1d7aa0,u32 (*)(u32))(n);}
/* Keep this tiny resolver at its call sites. Outlining it solely to share with
 * the preview would spend the full-library resident tail needed by MPEG. */
static __attribute__((always_inline)) inline u32 ca_cover_resolve(u32 v){
 if(v!=36)return v;
 for(u32 retry=0;retry<11;retry++){u32 r=ca_random(36);if(r!=2&&r!=10&&r!=15)return r;LOOP_PAD();}
 return 0;
}
static void ca_frame(u8 *play){const struct cs_preferences *p=ca_prefs();if(p->frame>=0){u32 v=p->frame;if(v==7){v=ca_random(7);if(v==6)v=0;}play[9]=v;play[4]=v==6;}}
static void ca_prepare(u8 *play){
 const struct cs_preferences *p=ca_prefs();u32 dp=cs_dp();ca.filter=2;
 for(u32 side=0;side<2;side++){LOOP_PAD();if(dp&&side){ca.turn[1]=ca.turn[0];ca.bomb[1]=ca.bomb[0];ca.cover[1]=ca.cover[0];continue;}u32 owner=dp?0:side;
  s32 t=p->art[owner][0],b=p->art[owner][1],c=p->art[owner][2];
  ca.turn[side]=t==9?(s32)ca_random(9):t;ca.bomb[side]=b==9?(s32)ca_random(9):b;
  /* Native constructor already resolved its fallback RANDOM cover. Preserve it
   * exactly when no explicit owner override exists. DP shares one resolution. */
  ca.cover[side]=dp&&side?ca.cover[0]:c<0?play[12]:ca_cover_resolve(c);
 }
 u32 primary=cs_style()==1?1:0;play[12]=ca.cover[primary];play[13]=play[12]==10||play[12]==12;
}
static void ca_preview_effect(u32 side,u32 kind,u32 value){
 if(side>1||kind>1)return;
 s32 resolved=value==9?(s32)ca_random(9):(s32)value;
 if(kind)ca.bomb[side]=resolved;else ca.turn[side]=resolved;
 if(cs_dp()){if(kind)ca.bomb[1]=ca.bomb[0];else ca.turn[1]=ca.turn[0];}
}
/* Main-thread preview commit, after prior-frame DMA. No scene reconstruction,
 * chart reset or hit-time I/O. End only the old artwork's bursts: their timer
 * belongs to its own timeline and cannot be interpreted by a different pack. */
static __attribute__((noinline)) void ca_preview_pack(u32 side,u32 kind,u32 value){
 const struct custom_art_pack *p=cs_pack(kind,kind?value-CUSTOM_BOMB_BASE:value);
 if(kind)ca_preview_effect(side,1,value);
 for(u32 s=0;s<2;s++){LOOP_PAD();if(!cs_dp()&&s!=side)continue;
  if(!kind)skin.field[s].pad[0]=p?(p->fid<<16)+((*(u8 **)0x2ad5e8)[5]?3:0):0;
  else if(ca.custom_bomb[s]!=p){
   for(u32 lane=0;lane<8;lane++){LOOP_PAD();*(s32 *)(*(u8 **)0x2ad5e8+0x10+s*32+lane*4)=-1;}
   ca.custom_bomb[s]=p;
  }
 }
}
extern unsigned long long ca_load_body(FS_ARGS),ca_bomb_init_body(FS_ARGS),ca_bomb_hit_body(FS_ARGS),ca_cover_top_body(FS_ARGS),ca_cover_bottom_body(FS_ARGS);
static int ca_custom_queue(u32 id){
 u32 n=*(u32 *)0x2ad030;
 for(u32 i=0;i<n&&i<16;i++){LOOP_PAD();if(*(u32 *)(unsigned long)(0x34ac90+i*0x228)==id)return 1;}
 if(n>=16)return 0;CS_NATIVE(0x136190,void (*)(u32))(id);return 1;
}
static void ca_custom_load(u8 *hud){
 const struct cs_preferences *p=ca_prefs();u32 style=cs_style(),dp=cs_dp();
 for(u32 s=0;s<2;s++){LOOP_PAD();skin.field[s].pad[0]=0;ca.custom_bomb[s]=0;
  if(style<2&&s!=style)continue;u32 owner=dp?0:s;
  const struct custom_art_pack *note=cs_pack(0,p->notes[owner]),*bomb=cs_pack(1,p->art[owner][1]-CUSTOM_BOMB_BASE);
  if(note&&ca_custom_queue(note->fid))skin.field[s].pad[0]=(note->fid<<16)+(hud[5]?3:0);
  if(bomb&&ca_custom_queue(bomb->fid))ca.custom_bomb[s]=bomb;
 }
}
static u32 ca_cover_id(u8 *hud,u32 side){return *(u32 *)(unsigned long)(0x281e00+ca.cover[side]*8+(hud[5]?0:4));}
unsigned long long ca_load_call(FS_ARGS){
 u8 *hud=*(u8 **)0x2ad5e8;
 /*1FA680 writes its native cover AFTER1FA3E0. Resolve owner overrides at
  * this loader boundary, never from the earlier geometry callback. */
 ca_prepare(hud);unsigned long long r=ca_load_body(FS_PASS);
 /* One extra cover at most. Native retirement frees the complete resource
  * batch. Never append models or queue duplicates during a draw callback. */
 if(!cs_dp()&&cs_style()>=4&&ca.cover[0]!=ca.cover[1])CS_NATIVE(0x136190,void (*)(u32))(ca_cover_id(hud,1));
 ca_custom_load(hud);
 return r;
}
unsigned long long ca_bomb_init_call(FS_ARGS){
 unsigned long long r=ca_bomb_init_body(FS_PASS);u8 *hud=*(u8 **)0x2ad5e8;
 for(u32 s=0;s<2;s++){LOOP_PAD();if(ca.bomb[s]>=0)for(u32 lane=0;lane<8;lane++){LOOP_PAD();*(u16 *)(hud+0x50+s*16+lane*2)=ca.bomb[s]>=10?0:ca.bomb[s];}}
 return r;
}
unsigned long long ca_bomb_hit_call(FS_ARGS){
 if(a>1)return ca_bomb_hit_body(FS_PASS);
 u8 *cfg=CS_NATIVE(0x1686c0,u8 *(*)(void))();u8 before=cfg[0x50+3*0x4d];
 s32 v=ca.bomb[(u32)a];if(v>=0){
  cfg[0x50+3*0x4d]=v==10?10:0;
  /* Apply a fixed family at the next hit, preserving existing bursts/timers.
   * Native PER HIT chooses its own new type inside the original producer. */
  if(v!=10&&b<8)*(u16 *)(*(u8 **)0x2ad5e8+0x50+(u32)a*16+(u32)b*2)=v>10?0:v;
 }
 u32 scale=ca_particle_scale;ca_particle_scale=skin.field[(u32)a].pad[1];
#ifdef PLAY_LIFT
 u32 old_lift=lift_particle_y;lift_particle_y=lift_pixels((u32)a);
#endif
 unsigned long long r=ca_bomb_hit_body(FS_PASS);
#ifdef PLAY_LIFT
 lift_particle_y=old_lift;
#endif
 ca_particle_scale=scale;cfg[0x50+3*0x4d]=before;
 if(b<8&&ca.custom_bomb[(u32)a])*(u32 *)(*(u8 **)0x2ad5e8+0x10+(u32)a*32+(u32)b*4)=ca.custom_bomb[(u32)a]->frames;
 return r;
}
/* Called only from1FF298, with its actual outer/inner field/lane registers.
 * Return handle+native tick in v0; the full128 adapter preserves all fifteen
 * native draw arguments, including clip rectangle, alpha, blend and layer. */
unsigned long long ca_bomb_map(u32 side,u32 lane,u32 handle,u32 frame){
 const struct custom_art_pack *p=ca.custom_bomb[side];if(p){
  handle=p->fid<<16;frame=p->frames-*(u32 *)(*(u8 **)0x2ad5e8+0x10+side*32+lane*4);
 }return ((unsigned long long)frame<<32)|handle;
}
/* Native TT rotation/alpha and eligibility remain native; only the48-frame
 * sprite family changes at the native1FB650 per-field131410 call. */
unsigned long long ca_turn_call(FS_ARGS){
 u32 side=(u32)c;if(cs_style()==1)side=1;u8 *hud=*(u8 **)0x2ad5e8;
 if(side<2&&ca.turn[cs_dp()?0:side]>=0){u32 t=ca.turn[cs_dp()?0:side];u32 base=*(u32 *)(unsigned long)(0x281d30+t*8+hud[5]*4);
  if(hud[8]&&hud[5])base=*(u32 *)(unsigned long)(0x281d80+t*4);
  b=base+((u32)b-*(u32 *)(hud+0x234));
 }
 return CS_NATIVE(0x131410,unsigned long long (*)(unsigned long long,unsigned long long))(a,b);
}
/* Both cover producers are pure draw passes. Scope the native cover handle,
 * asymmetric-art mirroring flag and side gate; retain all cover/SUD+/LIFT
 * gates and the native twelve-argument glyph/sprite forwarding internally. */
unsigned long long ca_cover_gate(FS_ARGS){if(ca.filter<2&&a!=ca.filter)return 0;return CS_NATIVE(0x1f6430,unsigned long long (*)(FS_ARGS))(FS_PASS);}
static unsigned long long ca_cover_draw(u32 top,FS_ARGS){
 u8 *hud=*(u8 **)0x2ad5e8;
 /* Common/single-player case remains one original native draw pass. */
 if(cs_style()<2||ca.cover[0]==ca.cover[1])return top?ca_cover_top_body(FS_PASS):ca_cover_bottom_body(FS_PASS);
 u32 saved=*(u32 *)(hud+0x228);u8 flag=hud[13];unsigned long long r=0;
 for(u32 side=0;side<2;side++){LOOP_PAD();u32 style=cs_style();if(style<2&&side!=style)continue;ca.filter=side;*(u32 *)(hud+0x228)=ca_cover_id(hud,side)<<16;hud[13]=ca.cover[side]==10||ca.cover[side]==12;
  r=top?ca_cover_top_body(FS_PASS):ca_cover_bottom_body(FS_PASS);
 }
 *(u32 *)(hud+0x228)=saved;hud[13]=flag;ca.filter=2;return r;
}
unsigned long long ca_cover_top_call(FS_ARGS){return ca_cover_draw(1,FS_PASS);}
unsigned long long ca_cover_bottom_call(FS_ARGS){return ca_cover_draw(0,FS_PASS);}

/* Apply native system options BEFORE1FA3E0, so the whole geometry/BGA and
 * dependent native option readers agree. Battle retains its forced layout.
 * Preview snapshots/restores these globals separately from per-style options. */
void frame_geometry(u8 *play){
 const struct cs_preferences *p=ca_prefs();if(cs_style()<4){u8 *sys=cs_system();if(p->layout>=0)*(u32 *)(sys+0x84)=p->layout;if(p->dp_type>=0)*(u32 *)(sys+0x88)=p->dp_type;}
 CS_NATIVE(0x1fa3e0,void (*)(u8 *))(play);
}
