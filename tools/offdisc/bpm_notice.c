/* First-upcoming BPM notice, empirically compared with LDJ. Bounded chart
 * scan once at native chart setup; fixed storage, no PLAY I/O/allocation, no new clock or event mutation. */
#include "menu_settings.h"
extern struct mm_control menu_control;
#ifndef BPM_NOTICE_TEST
#define bn_chart() ((const u32 *)(unsigned long)fs_word(0x2ad0c4))
#define bn_timing_a(s) (fast_slow_enabled && fs_type[fs_dp()?0:(s)]==0)
#define bn_ghost_a(s) (fs_player_ghost(s)==1)
#define bn_now() fs_word(0x2ad468) /* native1e5e70 target;2ad470 is target+1 after dispatch */
#define bn_kind() (((u32 (*)(void))0x1f5830)())
#endif
#ifndef fs_judge_y
#define fs_judge_y(h,s) (*(short *)(unsigned long)((h)+0x10c))
#endif
#define BN_POINTS 128u
#define BN_LEAD 120u
#if !defined(BPM_NOTICE_TEST) || defined(DISPLAY_COORDINATION_TEST)
static s32 bn_avoid(u32 hud,u32 side,s32 x,u32 width,s32 y){
 /* Reserve configured display boxes even between hits: only the notice moves,
  * never F/S when the BPM TYPE changes. Native PS2 glyphs are10x12; account
  * for that size when adapting the empirically compared LDJ layout. */
 for(u32 pass=0;pass<2;pass++)for(u32 item=0;item<2;item++){LOOP_PAD();
  u32 owner=fs_dp()?0:side,type=item?fs_player_ghost(side):fs_type[owner]+1;
  if(type<2||type>3)continue;s32 xx,yy;u32 step;
  if(item){s32 unused;fs_outside(hud,side,6,0,&xx,&step);fs_word_position(hud,side,type-1,&unused,&yy);yy+=21;}
  else{fs_word_position(hud,side,type-1,&xx,&yy);step=10;}
  u32 w=(item?6:4)*step;
  if(x<xx+(s32)w&&x+(s32)width>xx&&y<yy+16&&y+16>yy)y=yy-16;
 }return y;
}
#else
#ifndef bn_avoid
#define bn_avoid(h,s,x,w,y) (y)
#endif
#endif
static struct {u32 valid,n,index,last,end;struct {u32 frame,bpm;} point[BN_POINTS];} bn;
static void bn_reset(void){bn.valid=bn.n=bn.index=bn.last=bn.end=0;}
static void bn_start(void)
{
 bn_reset();if(!menu_control.options.bpm_notice[0]&&!menu_control.options.bpm_notice[1])return;
 if(bn_kind()==5||bn_kind()==8)return;
 const u32 *p=bn_chart();u32 address=(u32)(unsigned long)p;
 if((address&3)||address<0x100000||address>0x2000000-8)return;
 u32 limit=(0x2000000-address)/8;if(limit>65536)limit=65536;
 u32 previous=0,ended=0;
 for(u32 i=0;i<limit;i++){LOOP_PAD();u32 frame=p[i*2],event=p[i*2+1],kind=event&15;
  if(frame<previous||frame==0x7fff)return;previous=frame;
  if(kind==6){bn.end=frame;ended=1;break;}
  if(kind!=4)continue;u32 bpm=(event>>8&255)+(event&240)*16;if(!bpm)return;
  if(bn.n&&bn.point[bn.n-1].frame==frame)bn.point[bn.n-1].bpm=bpm;
  else{if(bn.n==BN_POINTS)return;bn.point[bn.n].frame=frame;bn.point[bn.n++].bpm=bpm;}
 }
 if(!ended||!bn.n||bn.point[0].frame)return;
 u32 kept=1;for(u32 i=1;i<bn.n;i++){LOOP_PAD();if(bn.point[i].bpm!=bn.point[kept-1].bpm)bn.point[kept++]=bn.point[i];}
 bn.n=kept;bn.valid=1;
}
static u32 bn_next(u32 now,u32 *current,u32 *next)
{
 if(!bn.valid||bn.n<2||now>=bn.end||bn.end-now<BN_LEAD)return 0;if(now<bn.last)bn.index=0;bn.last=now;
 while(bn.index+1<bn.n&&bn.point[bn.index+1].frame<=now){LOOP_PAD();bn.index++;}
 if(bn.index+1>=bn.n||bn.point[bn.index+1].frame-now>BN_LEAD)return 0;
 *current=bn.point[bn.index].bpm;*next=bn.point[bn.index+1].bpm;return *current!=*next;
}
/* Relative placement was empirically compared with LDJ, then adapted to
 * 640x480/native field anchors. At1920x1080: A/B same x; B+139y, C+61y, D+191y.
 * C/D align away from lane edges in SP. DP C uses outside panes; D is
 * one central notice. Narrow native DP margins use horizontal font fitting.
 * Native FONT452 is10px advance. Keep the accepted A placement when F/S A
 * (or Ghost A) is present; empirical LDJ comparison shows every type
 * raised by18px in that case.
 * TYPE B is below the complete native judgement sprite. Its35px artwork is
 * taller than the HD->PS2 projection assumed: use the loaded sprite height
 * plus12px, then the usual8px A-avoidance leaves at least4px clearance. */
static u32 bn_judge_height(u32 hud)
{
 u32 object=fs_word(hud+0x1e4);
 if(!(object&1)&&object>=0x100000&&object<=0x2000000-0x2a){
  u32 height=*(unsigned short *)(unsigned long)(object+0x28);
  if(height&&height<=128)return height;
 }
 return 35; /* Native DATA1#183 judgement/number sprites0..46. */
}
static u32 bn_position(u32 hud,u32 side,u32 type,u32 length,s32 *x,s32 *y)
{
 if(type<1||type>4||side>1)return 0;u32 double_play=fs_dp();if(double_play&&type==4&&side)return 0;
 s32 left=(s32)fs_word(hud+0x104)+97,right=(s32)fs_word(hud+0x108)+97;
 s32 center=side?right:left;static const s32 dy[4]={0,62,27,85};
 *y=fs_judge_y(hud,side)+(type==2?(s32)bn_judge_height(hud)+12:-32+dy[type-1]);
 if(bn_timing_a(side)||bn_ghost_a(side))*y-=8;
 *x=center-(s32)length*5;
 if(type>=3){u32 step;if(!fs_outside(hud,side,length,double_play&&type==4,x,&step))return 0;if(double_play&&type==4)*y-=6;*y=bn_avoid(hud,side,*x,length*step,*y);}
 return *x>=0&&*x+(s32)length*10<=640&&*y>=0&&*y<=450;
}
static void bn_draw(void)
{
 if(!bn.valid)return;u32 current,next,phase=fs_phase(),hud=fs_word(0x2ad5e8);
 if(phase<5||phase>=7||!hud||!fs_font_ready()||!bn_next(bn_now(),&current,&next))return;
 char text[24];unsigned n=0;mm_number(text,23,&n,current);mm_append(text,23,&n," > ",3);mm_number(text,23,&n,next);
 for(u32 side=0;side<2;side++){LOOP_PAD();
  u32 type=menu_control.options.bpm_notice[fs_dp()?0:side];if(!(gsm_mask()&(1u<<side))||!type)continue;
  s32 x,y;if(!bn_position(hud,side,type,n,&x,&y))continue;
  u32 step=10;if(type>=3)fs_outside(hud,side,n,fs_dp()&&type==4,&x,&step);
  fs_text_sized(text,next>current?1:4,x,y,100,step,0,2,0,0);
 }
}
