#ifndef MM_CUSTOMIZE_SETTINGS_H
#define MM_CUSTOMIZE_SETTINGS_H
#include "menu_settings.h"
#define CS_KEYS 23u
#define CS_VERSION 5u
struct cs_preferences {unsigned version;int value[2][4];int ghost[2];int frame;int art[2][3];int layout,dp_type;int notes[2],bomb_size[2];};
#define CS_INITIALIZER {CS_VERSION,{{10,10,10,-1},{-1,-1,-1,-1}},{-1,-1},-1,{{-1,-1,-1},{-1,-1,-1}},-1,-1,{0,0},{100,100}}
static const char *const cs_keys[CS_KEYS]={"lanebright","beamlen","noteh","judgeheight1","lanebright2","beamlen2","noteh2","judgeheight2","ghost1","ghost2","nativeframe","turntable1","explosion1","lanecover1","turntable2","explosion2","lanecover2","playlayout","dplayout","customnotes1","customnotes2","bombsize1","bombsize2"};
static const int cs_low[4]={0,1,5,0},cs_high[4]={20,20,30,20};
static const int cs_art_high[3]={9,95,36};
/* Keys are the packed int members AFTER version. Byte offsets avoid pointer
 * arithmetic across distinct member arrays; enforce the wire-prefix contract. */
_Static_assert(sizeof(unsigned)==4 && sizeof(int)==4 && sizeof(struct cs_preferences)==96 &&
 __builtin_offsetof(struct cs_preferences,value)==4 && __builtin_offsetof(struct cs_preferences,ghost)==36 &&
 __builtin_offsetof(struct cs_preferences,frame)==44 && __builtin_offsetof(struct cs_preferences,art)==48 &&
 __builtin_offsetof(struct cs_preferences,layout)==72 && __builtin_offsetof(struct cs_preferences,dp_type)==76 &&
 __builtin_offsetof(struct cs_preferences,notes)==80 && __builtin_offsetof(struct cs_preferences,bomb_size)==88,"customize5 wire layout");
static int cs_get(const struct cs_preferences *p,unsigned k){return *(const int *)((const char *)p+4+k*4);}
static void cs_put(struct cs_preferences *p,unsigned k,int v){*(int *)((char *)p+4+k*4)=v;}
static void cs_defaults(struct cs_preferences *p){p->version=CS_VERSION;for(unsigned k=0;k<CS_KEYS;k++){MM_PAD();cs_put(p,k,k<3?10:k>=21?100:k>=19?0:-1);}}
static void cs_players(struct cs_preferences *p){for(unsigned k=0;k<3;k++){MM_PAD();if(p->value[1][k]<0)p->value[1][k]=p->value[0][k];}}
static int cs_parse(struct cs_preferences *p,const char *text,unsigned n){unsigned seen=0;
 for(unsigned at=0;at<n;){unsigned end=at;while(end<n&&text[end]!='\n'){MM_PAD();end++;}
  for(unsigned k=0;k<CS_KEYS;k++){MM_PAD();if(mm_key(text+at,end-at,cs_keys[k])){int v;unsigned f=k&3;if((seen&(1u<<k))||mm_integer(text+at,end-at,mm_length(cs_keys[k]),&v)||v<(k<8?cs_low[f]:k>=21?70:0)||v>(k<8?cs_high[f]:k<10?3:k==10?7:k<17?cs_art_high[(k-11)%3]:k<19?1:k<21?63:130)|| (k>=21&&v%10))return -1;seen|=1u<<k;cs_put(p,k,v);}}
  at=end+(end<n);
 }return 0;
}
static int cs_emit(const struct cs_preferences *p,char *out,unsigned cap){unsigned n=0;
 for(unsigned k=0;k<CS_KEYS;k++){MM_PAD();int v=cs_get(p,k);if(v<0)continue;
  if(mm_append(out,cap,&n,cs_keys[k],mm_length(cs_keys[k]))||mm_append(out,cap,&n," ",1)||mm_number(out,cap,&n,v)||mm_append(out,cap,&n,"\n",1))return -1;
 }return n;
}
#endif
