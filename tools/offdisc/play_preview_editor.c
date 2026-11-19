#if defined(PLAY_PREVIEW_TEST) && !defined(PLAY_PREVIEW_EDITOR_TEST)
/* The lifecycle harness supplies native boundary adapters. The separate editor
 * regression exercises the full production editor below, not this adapter. */
static void pv_editor_begin(void){}
static void pv_editor_draw(void){}
static void pv_editor_adopt(void){}
static void pv_editor_update(void){u32 held=menu_navigation(menu.owner),press=held&~pv->held;pv->held=held;if(!held)pv->armed=1;if(pv->armed&&(press&(MENU_OK|MENU_BACK)))pv->exit=1;}
#else
static void pv_editor_display(void);
static u32 pv_editor_properties(void){return pv->style>=4?14:pv->style>=2?16:15;}
static void pv_editor_move(u32 down){
 u32 count=pv_editor_properties(),i=pv->row<16?pv->row:count+pv->row-16;
 i=(i+(down?1:count+1))%(count+2);pv->row=i<count?i:16+i-count;
}

static void pv_editor_begin(void){
 pv->cs_draft=menu_customize;
 if(pv->cs_draft.layout<0)pv->cs_draft.layout=*(u32 *)(cs_system()+0x84)==1;
 if(pv->cs_draft.dp_type<0)pv->cs_draft.dp_type=*(u32 *)(cs_system()+0x88)==1;
 if(pv->cs_draft.frame<0)pv->cs_draft.frame=cs_native_asset(0);
 for(u32 s=0;s<2;s++){LOOP_PAD();for(u32 k=0;k<3;k++){LOOP_PAD();if(pv->cs_draft.art[s][k]<0)pv->cs_draft.art[s][k]=cs_native_asset(k==0?2:k==1?3:1);}}cs_players(&pv->cs_draft);pv->display_draft=menu.draft;
 for(u32 s=0;s<2;s++){LOOP_PAD();if(pv->cs_draft.value[s][3]<0)pv->cs_draft.value[s][3]=cs_native_judge();if(pv->cs_draft.ghost[s]<0)pv->cs_draft.ghost[s]=cs_native_ghost();pv->nav_held[s]=pv->nav_repeat[s]=0;}
 for(u32 s=0;s<2;s++){LOOP_PAD();pv->art.applied[s]=pv->cs_draft.art[s][2];}
 pv->art.applied[2]=pv->cs_draft.frame;
 pv->row=pv->edit_mask=pv->accept=0;cs_live=&pv->cs_draft;cs_apply();pv_editor_display();
}
static void pv_editor_display(void){
 for(u32 s=0;s<2;s++){LOOP_PAD();fs_type[s]=pv->display_draft.type[s];menu_control.options.type[s]=fs_type[s];menu_control.options.bpm_notice[s]=pv->display_draft.bpm_notice[s];}
 menu_control.options.counters=pv->display_draft.counters;menu_control.options.counters2=pv->display_draft.counters2;
 fs_counters_enabled=mm_any_counters(&menu_control.options);
}
static void pv_editor_change(u32 side,s32 delta){
 if(pv->row<4){s32 *v=&pv->cs_draft.value[side][pv->row],n=*v+delta;if(n<cs_low[pv->row])n=cs_low[pv->row];if(n>cs_high[pv->row])n=cs_high[pv->row];*v=n;cs_apply();}
 else if(pv->row==4)pv->display_draft.type[side]=(pv->display_draft.type[side]+(delta<0?3:1))%4;
 else if(pv->row==5){if(side)pv->display_draft.counters2^=1;else pv->display_draft.counters^=1;}
 else if(pv->row==6)pv->display_draft.bpm_notice[side]=(pv->display_draft.bpm_notice[side]+(delta<0?4:1))%5;
 else if(pv->row==7)pv->cs_draft.ghost[side]=(pv->cs_draft.ghost[side]+(delta<0?3:1))%4;
 else if(pv->row==8||pv->row==10||pv->row==12||pv->row==13){
  u32 kind=pv->row==8?3:pv->row==10?1:pv->row==12?0:2;
  s32 *v=kind==3?&pv->cs_draft.frame:&pv->cs_draft.art[side][kind];
  u32 n=kind==3?8:cs_art_high[kind]+1;*v=kind==1?cs_art_next(1,*v,delta):(*v+n+(delta<0?-1:1))%n;
  pv_art_change(side,kind,*v);
 }
 else if(pv->row==9){pv->cs_draft.notes[side]=cs_art_next(0,pv->cs_draft.notes[side],delta);pv_art_change(side,4,pv->cs_draft.notes[side]);}
 else if(pv->row==11){int *v=&pv->cs_draft.bomb_size[side];*v+=delta*10;if(*v<70)*v=70;if(*v>130)*v=130;cs_apply();}
 else if(pv->row==14||pv->row==15){
  s32 *v=pv->row==14?&pv->cs_draft.layout:&pv->cs_draft.dp_type;*v^=1;pv->reload=1;
 }
 pv_editor_display();
}
static void pv_editor_update(void){
 u32 shortcut=menu_shortcut();if(shortcut&&!pv->exit){pv->quit_menu=pv->exit=1;pv->accept=shortcut==1;}
 if(pv->phase!=PV_PLAY||fs_phase()!=5)return;
 u32 mask=pv->style==0?1:pv->style==1?2:pv->style<4?1:3;
 u32 all=0,held[2]={0,0};for(u32 side=0;side<2;side++){LOOP_PAD();if(mask&(1u<<side)){held[side]=menu_navigation(side);if(pv->style==2||pv->style==3)held[side]|=menu_navigation(1);all|=held[side]|(menu_raw(side)&0x900);}}
 if(!pv->armed){if(!all)pv->armed=1;for(u32 s=0;s<2;s++)pv->nav_held[s]=held[s];return;}
 for(u32 s=0;s<2;s++){LOOP_PAD();if(!(mask&(1u<<s)))continue;
  if(pv->row==8&&pv->edit_mask&&!(pv->edit_mask&(1u<<s)))continue;
  u32 now=held[s],press=now&~pv->nav_held[s];
  if(now&&now==pv->nav_held[s]){if(++pv->nav_repeat[s]>=24&&(pv->nav_repeat[s]-24)%5==0)press=now&(MENU_UP|MENU_DOWN|MENU_LEFT|MENU_RIGHT);}else pv->nav_repeat[s]=0;
  pv->nav_held[s]=now;
  if(press&MENU_BACK){if(pv->edit_mask&(1u<<s))pv->edit_mask&=~(1u<<s);else{pv->exit=1;pv->accept=0;}continue;}
  if(pv->edit_mask&(1u<<s)){
   if(press&(MENU_UP|MENU_RIGHT))pv_editor_change(s,1);if(press&(MENU_DOWN|MENU_LEFT))pv_editor_change(s,-1);
   if(press&MENU_OK)pv->edit_mask&=~(1u<<s);
  }else{
   if(!pv->edit_mask){if(press&MENU_DOWN)pv_editor_move(1);if(press&MENU_UP)pv_editor_move(0);}
   if(press&MENU_OK){if(pv->row<16)pv->edit_mask|=1u<<s;else{pv->accept=pv->row==16;pv->exit=1;}}
   if(pv->row<16&&(press&(MENU_LEFT|MENU_RIGHT)))pv_editor_change(s,press&MENU_LEFT?-1:1);
  }
 }
}
static void pv_editor_value(char *text,u32 row,u32 side){
 unsigned n=0;text[0]=0;u32 v=row<4?pv->cs_draft.value[side][row]:0;
 if(row<3){mm_number(text,31,&n,v/10);mm_append(text,31,&n,".",1);mm_number(text,31,&n,v%10);}
 else if(row==3){if(!v)mm_append(text,31,&n,"OFF",3);else mm_number(text,31,&n,v);}
 else if(row==4){const char *v[]={"A","B","C","OFF"};mm_append(text,31,&n,v[pv->display_draft.type[side]],mm_length(v[pv->display_draft.type[side]]));}
 else if(row==5){const char *v=mm_counters(&pv->display_draft,side)?"ON":"OFF";mm_append(text,31,&n,v,mm_length(v));}
 else if(row==6){const char *v[]={"OFF","A","B","C","D"};const char *a=v[pv->display_draft.bpm_notice[side]];mm_append(text,31,&n,a,mm_length(a));}
 else if(row==7){const char *v[]={"OFF","A","B","C"};const char *a=v[pv->cs_draft.ghost[side]];mm_append(text,31,&n,a,mm_length(a));}
 else if(row==8||row==10||row==12||row==13){u32 kind=row==8?3:row==10?1:row==12?0:2;
  u32 id=kind==3?pv->cs_draft.frame:pv->cs_draft.art[side][kind],last=kind==3?7:cs_art_high[kind];
  if(kind==1){const char *name=cs_bomb_name(id);mm_append(text,31,&n,name,mm_length(name));}
  else if(id==last)mm_append(text,31,&n,"RANDOM",6);
  else {mm_append(text,31,&n,"TYPE ",5);mm_number(text,31,&n,id+1);}
 }
 else if(row==9){const struct custom_art_pack *p=cs_pack(0,pv->cs_draft.notes[side]);const char *v=p?p->name:"EMPRESS";mm_append(text,31,&n,v,mm_length(v));}
 else if(row==11){mm_number(text,31,&n,pv->cs_draft.bomb_size[side]);mm_append(text,31,&n,"%",1);}
 else if(row==14){const char *v=pv->cs_draft.layout?"ARCADE":"ORIGINAL";mm_append(text,31,&n,v,mm_length(v));}
 else if(row==15)mm_append(text,31,&n,pv->cs_draft.dp_type?"TYPE B":"TYPE A",6);
}
static void pv_editor_samples(u32 hud){
 u32 mask=pv->style==0?1:pv->style==1?2:3;
 for(u32 s=0;s<2;s++){LOOP_PAD();if(!(mask&(1u<<s)))continue;
  if(pv->row==8&&pv->edit_mask&&!(pv->edit_mask&(1u<<s)))continue;
  /* Native AUTO already draws PGREAT/combo through1FE450. Reusing those
   * animation handles here would enqueue a second copy. Only the disabled
   * AUTO F/S and BPM notices need isolated position samples. */
  u32 owner=cs_dp()?0:s,type=pv->display_draft.type[owner];s32 x,y;
  if(type<3){fs_word_position(hud,s,type,&x,&y);if(type==0&&cs_ghost(s)==1)x=(s32)fs_word(hud+0x104+s*4)+124;fs_text("FAST",1,x,y,100);}
  type=pv->display_draft.bpm_notice[owner];if(type&&bn_position(hud,s,type,9,&x,&y)){u32 step=10;if(type>=3)fs_outside(hud,s,9,cs_dp()&&type==4,&x,&step);fs_text_sized("120 > 150",1,x,y,100,step,0,2,0,0);}
  type=cs_ghost(s);if(type){
   static const short ax[6][2]={{75,521},{203,393},{135,135},{460,460},{82,272},{155,442}};
   u32 layout=fs_word(hud);if(layout<6)fs_ghost_text((unsigned long long)(unsigned long)"+0088",0,ax[layout][s],*(short *)(unsigned long)(hud+0x10c)-7,100,0,2,0,0,s);
  }
 }
}
/* White is idle; the owner's colour identifies an active value edit. A shared
 * frame cell uses the player who acquired its exclusive edit focus. */
static u32 pv_editor_colour(u32 row,u32 side){
 if(row!=pv->row)return 0;
 if(row==8||row==14||row==15){if(!pv->edit_mask)return 0;side=(pv->edit_mask&1)?0:1;}
 else if(!(pv->edit_mask&(1u<<side)))return 0;
 return side?1:4;
}
static int pv_editor_font(void){
 /* Only our private replacement is allowed through the ordinary queue guard.
  * FONT452 is unchanged by this request; every other pending loader keeps the
  * native stale-slot protection, including in-flight/unreturned callbacks. */
 u32 count=fs_word(0x78af08),head=fs_word(0x78af00);
 if(!count)return fs_font_ready();
 if(count!=1||head>=72)return 0;
 u32 q=0x78af0c+head*28;
 if(fs_word(q)!=0x1359d0||fs_word(q+4)!=(u32)(unsigned long)pv->art.staged)return 0;
 u32 n=fs_word(0x2ad030);if(n>16)return 0;
 for(u32 i=0;i<n;i++){LOOP_PAD();u32 p=0x34aa80+i*0x228;if(fs_word(p+0x210)==452)return fs_word(p)&&fs_word(p+12)==414;}
 return 0;
}
static void pv_editor_draw(void){
 u32 hud=fs_word(0x2ad5e8);if(!hud||fs_phase()!=5||!pv_editor_font())return;
 pv_editor_samples(hud);
 /* Native BGA clipping: clear the usable BGA pane, never dim the playfield.
  * Original DP has no BGA pane; its lower-priority fallback is central70%.
  * CS battle has small outer movies: keep the compact panel outside lanes. */
 static const short panes[6][4]={{200,60,240,280},{4,64,140,284},{330,60,270,280},{40,60,270,280},{400,60,235,280},{200,90,240,280}};
 u32 layout=fs_word(hud);if(layout>5)return;const short *r=panes[layout];s32 x=r[0],y=r[1],w=r[2];
 const char *labels[]={"LANE LIGHT","KEYBEAM","NOTE HEIGHT","JUDGE HEIGHT","FAST/SLOW","F/S COUNTERS","BPM NOTICE","GHOST","FRAME GLOBAL","NOTES","EXPLOSION ART","EXPLOSION SIZE","TURNTABLE","LANE COVER","PLAY LAYOUT","DP TYPE","SAVE / EXIT","CANCEL"};
 fs_text("CUSTOMIZE",3,x+8,y+8,100);
 if(pv->style>=4){fs_text_sized("P1",4,x+8,y+23,100,8,0,2,0,0);fs_text_sized("P2",1,x+w-24,y+23,100,8,0,2,0,0);}
 u32 props=pv_editor_properties(),pages=(props+2)/3+1,start=pv->row>=16?16:pv->row/3*3,count=start==16?2:props-start;if(count>3)count=3;
 for(u32 i=0;i<count;i++){LOOP_PAD();u32 row=start+i;s32 yy=y+38+i*55;u32 hover=row==pv->row;char value[32];
  s32 labelstep=(w-16)/(s32)mm_length(labels[row]);if(labelstep>9)labelstep=9;
  fs_text_sized(labels[row],0,x+(w-(s32)mm_length(labels[row])*labelstep)/2,yy,100,labelstep,0,2,0,0);
  if(row<16){
   if(pv->style>=4&&row!=8){
    for(u32 side=0;side<2;side++){LOOP_PAD();pv_editor_value(value,row,side);s32 cell=w/2-10,step=cell/(s32)mm_length(value);if(step>10)step=10;
     s32 xx=x+5+side*(w/2);fs_text_sized(value,pv_editor_colour(row,side),xx+(cell-(s32)mm_length(value)*step)/2,yy+20,100,step,0,2,0,0);
     if(hover&&(pv->edit_mask&(1u<<side))){short c[4]={xx,(short)(yy+16),cell,20};menu_native_rect(c,0x383030,80,3);}
    }
    short separator[4]={(short)(x+w/2),(short)(yy+19),1,14};menu_native_rect(separator,0x705050,90,3);
   }else{u32 owner=pv->style==1?1:0;pv_editor_value(value,row,owner);s32 step=10;if((s32)mm_length(value)*step>w-12)step=(w-12)/(s32)mm_length(value);
    fs_text_sized(value,pv_editor_colour(row,owner),x+(w-(s32)mm_length(value)*step)/2,yy+20,100,step,0,2,0,0);
   }
  }
  if(hover){short band[4]={x+4,(short)(yy-4),w-8,row<16?43:24};menu_native_rect(band,0x302828,80,3);}
  short rule[4]={x+4,(short)(yy+43),w-8,1};menu_native_rect(rule,0x503040,80,3);
 }
 u32 scheme=menu_scheme(menu.owner),pad=scheme==1||scheme==4;
 const char *hint=pv->art.error?"LOAD FAILED":pv->art.state?"LOADING ART":pv->edit_mask?(pad?"D-PAD VALUE":"1/2 VALUE"):(pad?"D-PAD MOVE":"1/2 MOVE");
 fs_text_sized(hint,pv->art.error?3:0,x+8,y+r[3]-58,100,8,0,2,0,0);
 fs_text_sized(pad?"O EDIT / DONE":scheme==3?"5 EDIT / DONE":"6 EDIT / DONE",0,x+8,y+r[3]-40,100,9,0,2,0,0);
 char page[16];unsigned n=0;mm_number(page,15,&n,pv->row>=16?pages:pv->row/3+1);mm_append(page,15,&n," / ",3);mm_number(page,15,&n,pages);fs_text(page,0,x+w-58,y+r[3]-20,100);
 short accent[4]={x,y,w,2};menu_native_rect(accent,0x603060,100,3);
 menu_native_rect(r,0,layout==5?70:95,3);
}
static void pv_editor_adopt(void){
 if(!pv->accept){if(pv->quit_menu)menu_close(0);return;}
 menu_customize=pv->cs_draft;
 menu.draft.type[0]=pv->display_draft.type[0];menu.draft.type[1]=pv->display_draft.type[1];
 menu.draft.counters=pv->display_draft.counters;menu.draft.counters2=pv->display_draft.counters2;
 menu.draft.bpm_notice[0]=pv->display_draft.bpm_notice[0];menu.draft.bpm_notice[1]=pv->display_draft.bpm_notice[1];
 menu_close(1); /* outside PLAY, using existing recoverable settings writer */
}
#endif
