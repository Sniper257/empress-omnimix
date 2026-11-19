#ifndef MM_DEPLOYMENT_H
#define MM_DEPLOYMENT_H
#include <stdio.h>
#include <string.h>
/* Device-independent launcher locations. Bounds match current native settings
 * path80 and compact runtime prefix64, not an assumed folder name. */
#define DL_ROOT 64
#define DL_PATH 256
extern char dl_game[DL_ROOT],dl_config[DL_ROOT],dl_boot[DL_ROOT],dl_cfg[80];
extern unsigned dl_persistent,dl_usb_active,dl_mmce_active;
static int dl_root(char out[DL_ROOT],const char *in){
 char b[DL_ROOT];unsigned n=0,k=0;
 if(!in||!*in)return -1;
 while(in[n]){if(n>=DL_ROOT-1 || (unsigned char)in[n]<32)return -1;b[n]=in[n]=='\\'?'/':in[n];n++;}
 b[n]=0;char *colon=strchr(b,':');if(!colon||colon==b)return -1;
 k=(unsigned)(colon-b)+1;
 if(b[k]&&b[k]!='/')return -1;
 /* No relative parent traversal, repeated separator, or second device. */
 for(unsigned i=k;i<n;i++)if(b[i]==':' || (b[i]=='/'&&b[i+1]=='/') ||
   (b[i]=='/'&&b[i+1]=='.'&&(b[i+2]=='/'||!b[i+2]||
     (b[i+2]=='.'&&(b[i+3]=='/'||!b[i+3])))))return -1;
 if(n==k||b[n-1]!='/'){if(n>=DL_ROOT-1)return -1;b[n++]='/';}
 b[n]=0;memcpy(out,b,n+1);return 0;
}
static int dl_device(const char *p){
 if(!strncmp(p,"mass:",5))return 1;
 if(p[0]=='m'&&!strncmp(p,"mass",4)&&p[4]>='0'&&p[4]<='9'&&p[5]==':')return 1;
 if(!strncmp(p,"mmce",4)&&(p[4]=='0'||p[4]=='1')&&p[5]==':')return 2;
 if(!strncmp(p,"mc",2)&&(p[2]=='0'||p[2]=='1')&&p[3]==':')return 3;
 return 0;
}
static int dl_exedir(const char *exe){
 char b[DL_PATH];unsigned n=exe?strlen(exe):0;if(!n||n>=sizeof b)return -1;
 memcpy(b,exe,n+1);for(unsigned i=0;i<n;i++)if(b[i]=='\\')b[i]='/';
 char *end=strrchr(b,'/');if(!end){end=strchr(b,':');if(!end)return -1;end++;*end=0;}
 else end[1]=0;
 return dl_root(dl_config,b);
}
/* Short-lived expressions only; callers retaining a path copy it into owned
 * storage. Eight slots avoid aliasing nested path arguments in one SDK call. */
static const char *dl_join(const char *root,const char *rel){
 static char paths[8][DL_PATH];static unsigned next;char *p=paths[next++&7];
 if(!root||!rel||snprintf(p,DL_PATH,"%s%s",root,rel)>=DL_PATH){p[0]=0;}return p;
}
static const char *game_path(const char *rel){return dl_join(dl_game,rel);}
static const char *dl_settings(void){return dl_cfg;}
static int dl_set_config(const char *root){
 char b[DL_ROOT];if(dl_root(b,root))return -1;
 if(snprintf(dl_cfg,sizeof dl_cfg,"%slauncher.cfg",b)>=(int)sizeof dl_cfg)return -1;
 strcpy(dl_config,b);return 0;
}
#endif
