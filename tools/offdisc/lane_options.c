/* Per-player arrangements; reuse native note AND keysound source->dest maps.
 * No input/clock/score patches, allocations or PLAY I/O. */
#define LQ_NORMAL 0u
#define LQ_MIRROR 1u
#define LQ_RANDOM 2u
#define LQ_SRANDOM 3u
#define LQ_HRAN 4u
#define LQ_ALLSCR 5u
#define LQ_RRAN 6u
#define LQ_EXACT 7u
#define LQ_COLOUR 8u
#if !defined(LANE_OPTIONS_TEST) || defined(QUICK_RETRY_TEST)
static void qr_map_ready(void);
#endif
#ifndef LANE_OPTIONS_TEST
#define lq_kind() (((u32 (*)(void))0x1f5830)())
#define lq_random(n) (((u32 (*)(u32))0x1d7aa0)(n))
#define lq_native(s) ((u32 *)(unsigned long)(0x7a32a0+(s)*0x2c))
#define lq_maps(s) ((u8 *)(unsigned long)(0x79dad0+(s)*8))
#define lq_linked() (((u32 (*)(void))0x205d40)())
#define lq_flags(s) (*(u8 *)(unsigned long)(0x2ad444+(s)))
extern unsigned long long lq_setup_body(FS_ARGS),lq_hran_body(FS_ARGS),lq_allscr_body(FS_ARGS);
#endif
static struct {u32 live,mode[2],profile[2],pattern[2];u8 realised[2][8];} lq;
static const char *const lq_names[9]={"NORMAL","MIRROR","RANDOM","S-RANDOM","H-RAN","ALL-SCR","R-RAN","TRAINER EXACT","TRAINER COLOUR"};
static const u32 lq_factorial[7]={720,120,24,6,2,1,1};
static void lq_unrank(u32 rank,u8 out[7]){u8 pool[7]={0,1,2,3,4,5,6};u32 left=7;for(u32 i=0;i<7;i++){LOOP_PAD();u32 k=rank/lq_factorial[i];rank%=lq_factorial[i];out[i]=pool[k];for(u32 j=k;j+1<left;j++){LOOP_PAD();pool[j]=pool[j+1];}left--;}}
static u32 lq_rank(const u8 a[7]){u32 r=0;for(u32 i=0;i<7;i++){LOOP_PAD();u32 n=0;for(u32 j=i+1;j<7;j++){LOOP_PAD();n+=a[j]<a[i];}r+=n*lq_factorial[i];}return r;}
static u32 lq_valid(const u8 a[7],u32 colour){u32 mask=0,blue=0;for(u32 i=0;i<7;i++){LOOP_PAD();if(colour){if(a[i]>1)return 0;blue+=a[i];}else{if(a[i]>6||(mask&(1u<<a[i])))return 0;mask|=1u<<a[i];}}return colour?blue==3:mask==127;}
static u32 lq_profile(u32 owner,u32 field){return fs_dp()?2+field:owner;}
static u32 lq_derive(const struct nm_state *n,u32 s,u32 special){if(n->random[s]>=3)return special==1?LQ_HRAN:special==2?LQ_ALLSCR:LQ_SRANDOM;return n->random[s]?LQ_RANDOM:n->mirror[s]?LQ_MIRROR:LQ_NORMAL;}
static u32 lq_compatible(const struct nm_state *n,u32 s,u32 m){return m==LQ_NORMAL?!n->random[s]&&!n->mirror[s]:m==LQ_MIRROR?!n->random[s]&&n->mirror[s]:m==LQ_RANDOM?n->random[s]==1||n->random[s]==2:m>=3&&m<=5?n->random[s]>=3:m>=6&&m<=8?n->random[s]==1&&!n->mirror[s]:0;}
static u32 lq_selected(const struct mm_options *o,const struct nm_state *n,u32 s){u32 owner=fs_dp()?0:s,m=o->lane[owner];if(m>=9)return lq_derive(n,s,n->special);if(o->nativecmd<32 && n->special!=(o->nativecmd&3))return lq_derive(n,s,n->special);return lq_compatible(n,s,m)?m:lq_derive(n,s,0);}
static void lq_import(struct mm_options *o,const struct nm_state *n){for(u32 s=0;s<(fs_dp()?1u:2u);s++){LOOP_PAD();o->lane[s]=lq_selected(o,n,s);}}
static void lq_set_base(struct nm_state *n,u32 side,u32 mode){for(u32 s=0;s<2;s++){LOOP_PAD();if(!fs_dp()&&s!=side)continue;n->random[s]=mode>=6?1:mode>=3?3:mode==2?1:0;n->mirror[s]=mode==1;}}
static void lq_restore(const struct mm_options *o){struct nm_state n;nm_capture(&n);for(u32 s=0;s<(fs_dp()?1u:2u);s++){LOOP_PAD();if(o->lane[s]<9)lq_set_base(&n,s,o->lane[s]);}nm_store(&n);}
static void lq_pattern_get(u32 packed,u32 colour,u8 out[7]){if(colour){for(u32 i=0;i<7;i++){LOOP_PAD();out[i]=(packed>>(13+i))&1;}}else lq_unrank(packed&8191,out);}
static u32 lq_pattern_put(u32 old,u32 colour,const u8 a[7]){if(!colour)return (old&~8191u)|lq_rank(a);u32 mask=0;for(u32 i=0;i<7;i++){LOOP_PAD();mask|=(u32)a[i]<<i;}return (old&8191)|(mask<<13);}
static void lq_shuffle(u8 *a,u32 n){for(u32 i=n;i>1;i--){LOOP_PAD();u32 k=lq_random(i);u8 t=a[i-1];a[i-1]=a[k];a[k]=t;}}
static void lq_generate(u32 mode,u32 pattern,u8 out[8]){
 if(mode==LQ_RRAN){u32 n=lq_random(12),shift=(n<6?n:n-6)+1;for(u32 i=0;i<7;i++){LOOP_PAD();u32 v=n<6?i+shift:6-i+shift;out[i]=v>=7?v-7:v;}}
 else if(mode==LQ_EXACT)lq_unrank(pattern&8191,out);
 else{u8 white[4]={0,2,4,6},blue[3]={1,3,5};u32 w=0,b=0;lq_shuffle(white,4);lq_shuffle(blue,3);for(u32 i=0;i<7;i++){LOOP_PAD();out[i]=(pattern>>(13+i))&1?blue[b++]:white[w++];}}
 out[7]=7;
}
/* Retail linked S-RANDOM assumes its H-RAN flag is global. When battle
 * players choose H-RAN vs ordinary S-RANDOM, do not reuse the peer's note.
 * Only this note-construction call is changed; native battle/sync stays intact. */
unsigned long long lq_note_link(FS_ARGS){if(lq.live&&lq.mode[0]!=lq.mode[1]&&(lq.mode[0]==LQ_HRAN||lq.mode[1]==LQ_HRAN))return 0;return lq_linked();}
unsigned long long lq_hran_call(FS_ARGS){u32 s=(u32)a;return lq.live&&s<2&&lq.mode[s]<9?lq.mode[s]==LQ_HRAN:lq_hran_body(FS_PASS);}
unsigned long long lq_allscr_call(FS_ARGS){u32 s=(u32)a;return lq.live&&s<2&&lq.mode[s]<9?lq.mode[s]==LQ_ALLSCR&&!lq_native(s)[5]&&(lq_native(s)[1]==3||lq_native(s)[1]==4):lq_allscr_body(FS_PASS);}
static u32 lq_same(u32 s,u32 p,u32 m){return lq.mode[p]==m&&(m==LQ_RRAN||(m==LQ_EXACT?(lq.pattern[p]&8191)==(lq.pattern[s]&8191):(lq.pattern[p]>>13)==(lq.pattern[s]>>13)));}
unsigned long long lq_setup_call(FS_ARGS){
 lq.live=lq_kind()==4;lq.mode[0]=lq.mode[1]=MM_LANE_UNSET;
 if(lq.live){struct nm_state n;nm_capture(&n);for(u32 s=0;s<2;s++){LOOP_PAD();u32 m=lq_selected(&menu_control.options,&n,s);lq.mode[s]=m;lq.profile[s]=fs_dp()?2+s:s;lq.pattern[s]=menu_control.options.lanep[lq.profile[s]];
  if((m>=6&&lq_native(s)[8])||!mm_lane_pattern_valid(lq.pattern[s]))lq.mode[s]=MM_LANE_UNSET;}}
 unsigned long long r=lq_setup_body(FS_PASS);
 for(u32 s=0;s<2;s++){LOOP_PAD();u32 m=lq.mode[s];if(m<6||m>=9)continue;
  /* Native linked-map bit8 is honored only for identical explicit choices.
   * Different battle templates stay independent; DP field templates are explicit. */
  u32 peer=1-s;
  if((lq_flags(s)&8)&&lq_same(s,peer,m)&&!(lq_flags(peer)&8))continue;
  lq_generate(m,lq.pattern[s],lq.realised[s]);u8 *map=lq_maps(s);for(u32 d=0;d<7;d++){LOOP_PAD();map[lq.realised[s][d]]=d;}map[7]=7;
 }
 for(u32 s=0;s<2;s++){LOOP_PAD();u32 p=1-s,m=lq.mode[s];if(m>=6&&m<9&&(lq_flags(s)&8)&&!(lq_flags(p)&8)&&lq_same(s,p,m)){for(u32 i=0;i<8;i++){LOOP_PAD();lq_maps(s)[i]=lq_maps(p)[i];lq.realised[s][i]=lq.realised[p][i];}}}
#if !defined(LANE_OPTIONS_TEST) || defined(QUICK_RETRY_TEST)
 qr_map_ready();
#endif
 return r;
}
/* Native shared eligibility rejects records when either participating side has
 * effective H-RAN/ALL-SCR, scratch-inclusive variants or REGUL. Trainer alone
 * deliberately remains ordinary RANDOM eligibility, as requested by the user. */
static u32 lq_no_score(const struct mm_options *o,const struct nm_state *n,u32 s){u32 m=lq_selected(o,n,s);return n->regul||m==LQ_HRAN||(m==LQ_ALLSCR&&!*(u32 *)(nm_options(nm_preferences(),nm_style())+s*0x38+0x1c))||n->random[s]==2||n->random[s]==4||n->mirror[s]==2;}
static void lq_draw_pattern(u32 packed,u32 colour,s32 x,s32 y){u8 a[7];char glyph[2]={0,0};lq_pattern_get(packed,colour,a);for(u32 i=0;i<7;i++){LOOP_PAD();glyph[0]=colour?(a[i]?'B':'W'):'1'+a[i];fs_text(glyph,(colour?a[i]:(a[i]&1))?1:0,x+i*10,y,100);}}
static void lq_commands(void){
 if(!nm_available()||!fs_font_ready())return;struct nm_state n;nm_capture(&n);u32 style=nm_style(),both=style>=4,dp=fs_dp(),first=style==1?1:0,last=both?2:first+1;
 u32 blocked=0;for(u32 s=first;s<last;s++){LOOP_PAD();blocked|=lq_no_score(&menu_control.options,&n,s);}
 for(u32 s=first;s<last;s++){LOOP_PAD();s32 x=both?365+s*140:378,y=8;u32 mode=lq_selected(&menu_control.options,&n,s);
  if(both){fs_text(s?"PLAYER 2":"PLAYER 1",0,x,y,100);y+=12;}
  if(mode>=4&&mode<9){fs_text("CMD: ",0,x,y,100);if(mode>=7){lq_draw_pattern(menu_control.options.lanep[dp?2:s],mode==8,x+50,y);if(dp){y+=12;fs_text("CMD: ",0,x,y,100);lq_draw_pattern(menu_control.options.lanep[3],mode==8,x+50,y);}}else fs_text(mode==4?"H-RAN":mode==5?"ALL-SCR":"R-RAN",0,x+50,y,100);y+=12;}
  if(n.regul){fs_text("CMD: REGUL",3,x,y,100);y+=12;}
  if(n.gambol){fs_text(n.gambol==1?"CMD: GJH":"CMD: GJA",0,x,y,100);y+=12;}
  if(blocked){fs_text("NO SCORE",3,x,y,100);y+=12;}
  u8 *opt=nm_options(nm_preferences(),nm_style())+s*0x38;if(*(u32 *)(opt+0x1c)||*(u32 *)(opt+0x20)){fs_text("ASSIST ON",3,x,y,100);}
 }
}
