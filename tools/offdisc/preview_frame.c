/* The frame-only prefix of retail201B10. Never call its global pool resets,
 * common HUD constructor, explosion initialization or chart/key-mode setup. */
static u32 pf_id(u32 theme){u32 layout=**(u32 **)0x2ad5e8;return *(u32 *)(unsigned long)(0x281610+theme*24+layout*4);}
static int pf_reserve(void){
 struct pv_art_state *a=&pv->art;if(a->objects[0])return 1;
 u32 *h=*(u32 **)0x2ad5e8,added[6],n=0;u32 old[6]={h[0x5b]?h[0x5b]:h[0x5d],h[0x5c],h[0x5e],h[0x5f],h[0x60],h[0x61]};
 for(u32 k=0;k<6;k++){LOOP_PAD();u32 lo=k?0x2b9c50:0x2bcc50,stride=k?0x30:0x38,count=k?256:128,flag=k?0x2c:0x30,p=old[k];
  if(p){if(p<lo||p>=lo+count*stride||(p-lo)%stride||!*(u32 *)(unsigned long)(p+flag))goto fail;}
  else {for(u32 i=0;i<count;i++){LOOP_PAD();u32 q=lo+i*stride;if(!*(u32 *)(unsigned long)(q+flag)){p=q;*(u32 *)(unsigned long)(q+flag)=1;added[n++]=q+flag;break;}}if(!p)goto fail;}
  for(u32 i=0;i<k;i++){LOOP_PAD();if(a->objects[i]==p)goto fail;}a->objects[k]=p;
 }
 return 1;
fail:
 for(u32 i=0;i<n;i++){LOOP_PAD();*(u32 *)(unsigned long)added[i]=0;}pa_zero(a->objects,6);return 0;
}
static u32 pf_static(u32 slot,u32 id,s32 x,s32 y){
 if(!id)return 0;
 return PV_CALL(0x131460,u32 (*)(u32,unsigned long long,s32,s32,u32,u32,u32))(pv->art.objects[slot],id,x,y,0,100,0);
}
static void pf_show(void){
 struct pv_art_state *a=&pv->art;u32 *h=*(u32 **)0x2ad5e8,layout=h[0],theme=a->wanted[2],off=theme*24+layout*4;
 for(u32 i=0x5b;i<0x62;i++){h[i]=0;LOOP_PAD();}
 u32 anim=theme?*(u32 *)(unsigned long)(0x2816c0+off):pf_id(0)<<16;
 if(anim)h[theme?0x5d:0x5b]=PV_CALL(0x1310f0,u32 (*)(u32,unsigned long long,u32,s32,s32,u32,u32))(a->objects[0],anim,1,0,0,16,0);
 if(theme){
  h[0x5c]=pf_static(1,*(u32 *)(unsigned long)(0x281770+off),0,0);
  const short *pos=(const short *)(unsigned long)(0x281f60+theme*36+layout*6);
  for(u32 side=0;side<2;side++){LOOP_PAD();u32 at=theme*48+layout*8+side*4,id=*(u32 *)(unsigned long)(0x281820+at-side*4);
   if((s32)id<=0)continue;
   static const short xs[6][2]={{25,471},{153,343},{0,0},{0,0},{25,215},{60,347}};
   u32 p=pf_static(2+side,id,xs[layout][side],0);h[0x5e + side]=p;
   if(!((u8 *)h)[6+side]){*(u32 *)(unsigned long)(p+0x18)=0xc2c80000;*(short *)(unsigned long)(p+8)+=*(short *)(unsigned long)(p+0x26);}
   if(side?((u8 *)h)[7]:!((u8 *)h)[6])h[0x60+side]=pf_static(4+side,*(u32 *)(unsigned long)(0x281970+at),pos[side],pos[2]);
  }
 }
 ((u8 *)h)[9]=theme;((u8 *)h)[4]=theme==6;a->applied[2]=pv->cs_draft.frame;
}
