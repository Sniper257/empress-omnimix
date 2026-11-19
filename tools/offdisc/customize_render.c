/* Per-owner scalar rendering. Physical field routing is captured at the same
 * native geometry setup that selects SP/DP/battle; DP always uses owner0. */
#include "customize_settings.h"
#ifndef CS_NATIVE
#define CS_NATIVE(at,type) ((type)(at))
#endif
struct cs_preferences customize=CS_INITIALIZER;
static struct cs_preferences *cs_live;
static u32 cs_initialized;
static u32 cs_dp(void){return CS_NATIVE(0x205d50,u32 (*)(void))()!=0;}
static u32 cs_style(void){u8 *c=CS_NATIVE(0x1686f0,u8 *(*)(void))();return CS_NATIVE(0x1959a0,u32 (*)(u8 *))(c);}
static s32 cs_native_judge(void){return (signed char)CS_NATIVE(0x1686e0,u8 *(*)(void))()[0x94];}
static u32 cs_native_ghost(void){u8 *p=CS_NATIVE(0x168630,u8 *(*)(void))();return p[6]&&p[5]<3?p[5]+1:0;}
static u32 cs_ghost(u32 side){const struct cs_preferences *p=cs_live?cs_live:&customize;s32 v=p->ghost[cs_dp()?0:side];return v<0?cs_native_ghost():(u32)v;}
static void cs_apply(void){
 const struct cs_preferences *p=cs_live?cs_live:&customize;u32 dp=cs_dp(),rows=skin.note_rows?skin.note_rows:6;
 for(u32 s=0;s<2;s++){LOOP_PAD();u32 owner=dp?0:s;
  int lane=p->value[owner][0],beam=p->value[owner][1],note=p->value[owner][2],judge=p->value[owner][3];
  if(lane<0)lane=p->value[0][0];if(beam<0)beam=p->value[0][1];if(note<0)note=p->value[0][2];if(judge<0)judge=cs_native_judge();
  u32 v=lane*128/10;if(v>255)v=255;
  skin.field[s].tint=v*0x10101;skin.field[s].zoom=note*10;skin.field[s].zoom_skin=note*60/rows;
  skin.field[s].beam_dy=256*(10-beam)/10;skin.field[s].beam_zoom=beam*10;skin.field[s].judge=judge;
  skin.field[s].pad[1]=p->bomb_size[owner];
 }cs_initialized=1;
}
static u32 cs_lane_side(const u8 *params){
 u32 style=cs_style();if(style<4)return style==1?1:0;
 /* Native two-player fields lie on opposite sides of x320. Use the final
  * sprite anchor, not the shared container ID; DP is handled above. */
 return I32(params)>=320;
}
#ifdef PLAY_LIFT
static u32 lift_pixels(u32);
#else
#define lift_pixels(s) 0u
#endif
static s32 cs_judge_y(u32 hud,u32 side){
 if(!cs_initialized||side>1)return *(short *)(unsigned long)(hud+0x10c);
 u32 n=skin.field[side].judge;return 245-(s32)n*10-(s32)lift_pixels(side);
}

static u32 cs_native_asset(u32 category){u8 *p=CS_NATIVE(0x1686c0,u8 *(*)(void))();return CS_NATIVE(0x196cb0,u32 (*)(u8 *,u32))(p,category);}
static void ca_frame(u8 *play);

static u8 *cs_system(void){return CS_NATIVE(0x1686e0,u8 *(*)(void))();}

static const struct custom_art_pack *cs_pack(u32 kind,u32 id){
 const struct custom_art_catalog *c=skin.custom_art;if(!c||c->version!=CUSTOM_ART_VERSION)return 0;
 u32 at=kind?c->count[0]:0,n=c->count[kind];
 for(u32 i=0;i<n;i++){LOOP_PAD();if(c->packs[at+i].id==id)return c->packs+at+i;}return 0;
}
/* Stable IDs survive catalog reordering and missing packs. Native entries
 * precede custom explosions; RANDOM/PER HIT retain their original families. */
static u32 cs_art_next(u32 kind,u32 value,s32 delta){
 for(u32 i=0;i<96;i++){LOOP_PAD();value=(value+(delta<0?95:1))%96;
  if(kind?value<=10|| (value>CUSTOM_BOMB_BASE&&cs_pack(1,value-CUSTOM_BOMB_BASE)):!value||cs_pack(0,value))return value;
 }return 0;
}
/* Keep verified native labels beside the catalog lookup, outside UI scratch. */
static __attribute__((noinline)) const char *cs_bomb_name(u32 value){
 static const char *const names[]={"DEFAULT","1ST STYLE","9TH STYLE","IIDX RED","HAPPY SKY","DISTORTED","GOLD","DJ TROOPERS","EMPRESS","RANDOM","PER HIT"};
 if(value<11)return names[value];
 const struct custom_art_pack *p=cs_pack(1,value-CUSTOM_BOMB_BASE);return p?p->name:"UNAVAILABLE";
}
