#ifndef MM_MENU_SETTINGS_H
#define MM_MENU_SETTINGS_H
/* Shared launcher/game settings. Pending OLD full-text record is verified BEFORE
 * canonical replacement; boot rolls back after an interrupted write.
 * This is process-interruption recovery, not a storage power-loss guarantee. */
#ifdef __mips__
#define MM_PAD() __asm__ volatile("nop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop")
#else
#define MM_PAD() ((void)0)
#endif
#define MM_CFG_MAX 8192u
#define MM_PENDING_HEADER 20u
#define MM_TIMING_UNSET 51
#define MM_NATIVE_UNSET 32u
#define MM_VARIANT_UNSET 8u
#define MM_KEY_COUNT 44u
#define MM_LANE_UNSET 9u
#define MM_LANE_PATTERN_DEFAULT (42u<<13)
#define MM_FHS_COVER_UNSET 321u
#define MM_FHS_VISIBLE_UNSET 2u
#define MM_OPTIONS_INITIALIZER {1,1,{0,0},1,MM_TIMING_UNSET,MM_NATIVE_UNSET,{MM_VARIANT_UNSET,MM_VARIANT_UNSET},{0,0},{321,321,321,321},{2,2,2,2},{0,0,0,0},{9,9},{344064,344064,344064,344064},2,2,51,{0,0},{1,1},{0,0},{0,0},{321,321,321,321}}
struct mm_options {unsigned version,gas,type[2],counters;int timing;unsigned nativecmd,variant[2],fhs[2],cover[4],visible[4],gn[4],lane[2],lanep[4],gas2,counters2;int timing2;unsigned bpm_notice[2],quick_retry[2],lift[2],lift_height[2],lift_span[4];};
struct mm_control {unsigned schema;struct mm_options options;char path[80];};
struct mm_cfg_io {void *ctx;int (*read)(void *,const char *,void *,unsigned);int (*write)(void *,const char *,const void *,unsigned);};
static unsigned mm_length(const char *s){unsigned n=0;while(s[n]){MM_PAD();n++;}return n;}
static void mm_copy(void *d,const void *s,unsigned n){unsigned char *a=d;const unsigned char *b=s;for(unsigned i=0;i<n;i++){MM_PAD();a[i]=b[i];}}
static int mm_equal(const void *a,const void *b,unsigned n){const unsigned char *x=a,*y=b;for(unsigned i=0;i<n;i++){MM_PAD();if(x[i]!=y[i])return 0;}return 1;}
static unsigned mm_crc(const void *p,unsigned n){const unsigned char *b=p;unsigned c=~0u;for(unsigned i=0;i<n;i++){c^=b[i];for(unsigned k=0;k<8;k++){MM_PAD();c=(c>>1)^((0u-(c&1))&0xedb88320u);}}return ~c;}
static unsigned mm_get(const void *p){const unsigned char *b=p;return b[0]|(unsigned)b[1]<<8|(unsigned)b[2]<<16|(unsigned)b[3]<<24;}
static void mm_put(void *p,unsigned v){unsigned char *b=p;for(unsigned i=0;i<4;i++){MM_PAD();b[i]=(unsigned char)(v>>(i*8));}}
static void mm_defaults(struct mm_options *p){p->version=1;p->gas=p->counters=1;p->type[0]=p->type[1]=0;p->timing=p->timing2=MM_TIMING_UNSET;p->gas2=p->counters2=2;p->bpm_notice[0]=p->bpm_notice[1]=0;p->quick_retry[0]=p->quick_retry[1]=1;p->nativecmd=MM_NATIVE_UNSET;p->variant[0]=p->variant[1]=MM_VARIANT_UNSET;p->fhs[0]=p->fhs[1]=0;p->lane[0]=p->lane[1]=MM_LANE_UNSET;p->lift[0]=p->lift[1]=p->lift_height[0]=p->lift_height[1]=0;for(unsigned i=0;i<4;i++){MM_PAD();p->cover[i]=MM_FHS_COVER_UNSET;p->visible[i]=MM_FHS_VISIBLE_UNSET;p->gn[i]=0;p->lanep[i]=MM_LANE_PATTERN_DEFAULT;p->lift_span[i]=321;}}
static int mm_key(const char *line,unsigned n,const char *key){unsigned k=mm_length(key);return n>=k && mm_equal(line,key,k) && (n==k || line[k]==' ' || line[k]=='\t' || line[k]=='\r');}
static int mm_integer(const char *s,unsigned n,unsigned skip,int *v){unsigned i=skip,minus=0,digits=0;int value=0;while(i<n && (s[i]==' '||s[i]=='\t')){MM_PAD();i++;}if(i<n && s[i]=='-'){minus=1;i++;}for(;i<n && s[i]>='0'&&s[i]<='9';i++){MM_PAD();if(value>100000)return -1;value=value*10+s[i]-'0';digits++;}while(i<n && (s[i]==' '||s[i]=='\t'||s[i]=='\r')){MM_PAD();i++;}if(!digits || i!=n)return -1;*v=minus?-value:value;return 0;}
static const char *const mm_keys[]={"settings","gas","fstype1","fstype2","fscounters","timing","nativecmd","native1","native2","fhs1","fhs2","fhsc1","fhsc2","fhsc3","fhsc4","fhsv1","fhsv2","fhsv3","fhsv4","fhsg1","fhsg2","fhsg3","fhsg4","lane1","lane2","lanep1","lanep2","lanep3","lanep4","gas2","fscounters2","timing2","bpmnotice1","bpmnotice2","quickretry1","quickretry2","lift1","lift2","lifth1","lifth2","liftspan1","liftspan2","liftspan3","liftspan4"};
static unsigned mm_lane_pattern_valid(unsigned v){unsigned mask=v>>13,n=0;if((v&8191)>5039 || mask>127)return 0;for(unsigned i=0;i<7;i++){MM_PAD();n+=(mask>>i)&1;}return n==3;}
static int mm_parse(struct mm_options *p,const char *text,unsigned n){
 unsigned seen[(MM_KEY_COUNT+31)/32]={0};for(unsigned at=0;at<n;){unsigned end=at;while(end<n && text[end]!='\n'){MM_PAD();end++;}unsigned len=end-at;
  for(unsigned k=0;k<MM_KEY_COUNT;k++){MM_PAD();if(mm_key(text+at,len,mm_keys[k])){int v;if((seen[k>>5]&(1u<<(k&31))) || mm_integer(text+at,len,mm_length(mm_keys[k]),&v))return -1;seen[k>>5]|=1u<<(k&31);
   if(k==0){if(v!=1)return -1;p->version=1;}else if(k==1 || k==4){if(v<0||v>1)return -1;if(k==1)p->gas=v;else p->counters=v;}
   else if(k==2 || k==3){if(v<0||v>3)return -1;p->type[k-2]=v;}
   else if(k==5){if(v< -50 || v>50)return -1;p->timing=v;}
   else if(k==6){if(v<0||v>=32||(v&3)==3||((v>>3)&3)==3)return -1;p->nativecmd=v;}
   else if(k<9){if(v<0||v>7)return -1;p->variant[k-7]=v;}
   else if(k<11){if(v<0||v>7)return -1;p->fhs[k-9]=v;}
   else if(k<15){if(v<0||v>320)return -1;p->cover[k-11]=v;}
   else if(k<19){if(v<0||v>1)return -1;p->visible[k-15]=v;}
   else if(k<23){if(v<0||v>9999)return -1;p->gn[k-19]=v;}
   else if(k<25){if(v<0||v>=9)return -1;p->lane[k-23]=v;}
   else if(k<29){if(v<0||!mm_lane_pattern_valid(v))return -1;p->lanep[k-25]=v;}
   else if(k==29||k==30){if(v<0||v>1)return -1;if(k==29)p->gas2=v;else p->counters2=v;}
   else if(k==31){if(v< -50||v>50)return -1;p->timing2=v;}
   else if(k<34){if(v<0||v>4)return -1;p->bpm_notice[k-32]=v;}
   else if(k<36){if(v<0||v>1)return -1;p->quick_retry[k-34]=v;}
   else if(k<38){if(v<0||v>1)return -1;p->lift[k-36]=v;}
   else if(k<40){if(v<0||v>265)return -1;p->lift_height[k-38]=v;}
   else {if(v<0||v>320)return -1;p->lift_span[k-40]=v;}
  }}at=end+(end<n);
 }return 0;
}
static int mm_append(char *out,unsigned cap,unsigned *used,const char *s,unsigned n){if(n>cap-*used)return -1;mm_copy(out+*used,s,n);*used+=n;out[*used]=0;return 0;}
static int mm_number(char *out,unsigned cap,unsigned *used,int v){char b[12],r[12];unsigned n=0,k=0;if(v<0){b[n++]='-';v=-v;}do{MM_PAD();r[k++]=(char)('0'+v%10);v/=10;}while(v);while(k){MM_PAD();b[n++]=r[--k];}return mm_append(out,cap,used,b,n);}
static int mm_emit(const struct mm_options *p,char *out,unsigned cap){
 unsigned n=0;int v[MM_KEY_COUNT];v[0]=1;v[1]=p->gas;v[2]=p->type[0];v[3]=p->type[1];v[4]=p->counters;v[5]=p->timing;v[6]=p->nativecmd;v[7]=p->variant[0];v[8]=p->variant[1];v[9]=p->fhs[0];v[10]=p->fhs[1];
 for(unsigned i=0;i<4;i++){MM_PAD();v[11+i]=p->cover[i];v[15+i]=p->visible[i];v[19+i]=p->gn[i];v[25+i]=p->lanep[i];}
 v[23]=p->lane[0];v[24]=p->lane[1];v[29]=p->gas2;v[30]=p->counters2;v[31]=p->timing2;v[32]=p->bpm_notice[0];v[33]=p->bpm_notice[1];v[34]=p->quick_retry[0];v[35]=p->quick_retry[1];for(unsigned i=0;i<2;i++){MM_PAD();v[36+i]=p->lift[i];v[38+i]=p->lift_height[i];}for(unsigned i=0;i<4;i++){MM_PAD();v[40+i]=p->lift_span[i];}
 for(unsigned i=0;i<MM_KEY_COUNT;i++){MM_PAD();
  if((i>=40&&p->lift_span[i-40]==321)||(i==29&&p->gas2==2)||(i==30&&p->counters2==2)||(i==31&&p->timing2==MM_TIMING_UNSET)||(i>=23 && i<25 && p->lane[i-23]==MM_LANE_UNSET)||(i==5 && p->timing==MM_TIMING_UNSET)||(i==6 && p->nativecmd==MM_NATIVE_UNSET)||(i>=7 && i<9 && p->variant[i-7]==MM_VARIANT_UNSET)||(i>=11 && i<15 && p->cover[i-11]==MM_FHS_COVER_UNSET)||(i>=15 && i<19 && p->visible[i-15]==MM_FHS_VISIBLE_UNSET))continue;
  if(mm_append(out,cap,&n,mm_keys[i],mm_length(mm_keys[i])) || mm_append(out,cap,&n," ",1) || mm_number(out,cap,&n,v[i]) || mm_append(out,cap,&n,"\n",1))return -1;
 }return (int)n;
}
/* Legacy scalar fields remain P1. Absent P2 fields inherit once when the
 * launcher/menu adopts a config; later P1 edits never overwrite explicit P2. */
static unsigned mm_gas(const struct mm_options *p,unsigned side){return side&&p->gas2<2?p->gas2:p->gas;}
static unsigned mm_counters(const struct mm_options *p,unsigned side){return side&&p->counters2<2?p->counters2:p->counters;}
static int mm_timing(const struct mm_options *p,unsigned side){return side&&p->timing2!=MM_TIMING_UNSET?p->timing2:p->timing;}
static void mm_players(struct mm_options *p){if(p->gas2>1)p->gas2=p->gas;if(p->counters2>1)p->counters2=p->counters;if(p->timing2==MM_TIMING_UNSET)p->timing2=p->timing;}
static unsigned mm_any_gas(const struct mm_options *p){return mm_gas(p,0)||mm_gas(p,1);}
static unsigned mm_any_counters(const struct mm_options *p){return mm_counters(p,0)||mm_counters(p,1);}
/* Remove only caller-owned keys, preserving comments/unknown/launcher-owned
 * fields literally. The in-game caller owns only the six keys above. */
static int mm_merge(const char *old,unsigned n,const char *replacement,unsigned rn,const char *const *keys,unsigned nk,char *out,unsigned cap){unsigned used=0;for(unsigned at=0;at<n;){unsigned end=at;while(end<n&&old[end]!='\n'){MM_PAD();end++;}unsigned skip=0;for(unsigned k=0;k<nk;k++){MM_PAD();if(mm_key(old+at,end-at,keys[k]))skip=1;}if(!skip && mm_append(out,cap,&used,old+at,end-at+(end<n)))return -1;at=end+(end<n);}if(used && out[used-1]!='\n' && mm_append(out,cap,&used,"\n",1))return -1;if(mm_append(out,cap,&used,replacement,rn))return -1;return (int)used;}
static int mm_pending_path(const char *path,char *out){unsigned n=mm_length(path);if(n>100)return -1;mm_copy(out,path,n);mm_copy(out+n,".pending",9);return 0;}
static int mm_pending_valid(const unsigned char *p,int n){return n>=20 && mm_equal(p,"MMIXCFG1",8) && mm_get(p+8)<MM_CFG_MAX && (unsigned)n==20+mm_get(p+8) && mm_crc(p,16)==mm_get(p+16) && mm_crc(p+20,mm_get(p+8))==mm_get(p+12);}
/* out needs MM_CFG_MAX+1; scratch MM_CFG_MAX+MM_PENDING_HEADER+1. -2 means
 * absent ONLY in transport callbacks. All other read failures stop editing. */
static int mm_cfg_load(const struct mm_cfg_io *io,const char *path,char *out,unsigned char *scratch){char pending[112];if(mm_pending_path(path,pending))return -1;int n=io->read(io->ctx,pending,scratch,MM_CFG_MAX+20);
 if(n<0 && n!=-2)return n;if(mm_pending_valid(scratch,n)){n=(int)mm_get(scratch+8);mm_copy(out,scratch+20,n);out[n]=0;return n;}
 n=io->read(io->ctx,path,out,MM_CFG_MAX);if(n==-2)n=0;if(n<0)return n;if((unsigned)n>=MM_CFG_MAX)return -3;out[n]=0;return n;
}
static int mm_cfg_commit(const struct mm_cfg_io *io,const char *path,const char *text,unsigned n,unsigned char *scratch){
 char pending[112];if(n>=MM_CFG_MAX || mm_pending_path(path,pending))return -3;
 /* Complete any previous rollback BEFORE reusing its sole verified backup. */
 int got=io->read(io->ctx,pending,scratch,MM_CFG_MAX+20);
 if(got<0 && got!=-2)return got;
 if(mm_pending_valid(scratch,got)){
  unsigned old_n=mm_get(scratch+8),crc=mm_get(scratch+12);
  if(io->write(io->ctx,path,scratch+20,old_n))return -4;
  got=io->read(io->ctx,path,scratch,MM_CFG_MAX+20);if(got!=(int)old_n || mm_crc(scratch,old_n)!=crc)return -5;
  if(io->write(io->ctx,pending,"",0))return -8;
  if(io->read(io->ctx,pending,scratch,MM_CFG_MAX+20)!=0)return -9;
 }
 got=io->read(io->ctx,path,scratch+20,MM_CFG_MAX);if(got==-2)got=0;if(got<0 || (unsigned)got>=MM_CFG_MAX)return -3;
 unsigned old_n=(unsigned)got,crc=mm_crc(scratch+20,old_n);
 mm_copy(scratch,"MMIXCFG1",8);mm_put(scratch+8,old_n);mm_put(scratch+12,crc);mm_put(scratch+16,mm_crc(scratch,16));
 if(io->write(io->ctx,pending,scratch,old_n+20))return -4;
 got=io->read(io->ctx,pending,scratch,MM_CFG_MAX+20);if(!mm_pending_valid(scratch,got) || mm_get(scratch+8)!=old_n || mm_get(scratch+12)!=crc)return -5;
 if(io->write(io->ctx,path,text,n))return -6;
 got=io->read(io->ctx,path,scratch,MM_CFG_MAX+20);if(got!=(int)n || !mm_equal(scratch,text,n))return -7;
 if(io->write(io->ctx,pending,"",0))return -8;
 got=io->read(io->ctx,pending,scratch,MM_CFG_MAX+20);return got==0?0:-9;
}
#endif
