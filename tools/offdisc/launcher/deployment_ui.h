/* First-start storage selection. All discovery is before catalog generation.
 * Use physical pad until USB profiles have been found; every page has Back. */
static char dl_binding_import[DL_ROOT],dl_origin[DL_ROOT];
static unsigned dl_origin_writable;
static int dl_valid_game(const char *root){
 if(dl_device(root)!=1&&dl_device(root)!=2)return 0;
 FILE *f=fopen(dl_join(root,"EMPRESS/base.txt"),"rb");char b[2];int ok=f&&fread(b,1,2,f)==2&&b[0]=='E'&&b[1]==' ';if(f&&fclose(f))ok=0;
 const char *required[]={"EMPRESS/SLPM_552.21","songs/styles.txt","launcher/vdisc-portable.bin","launcher/vdisc-portable.txt"};
 for(unsigned i=0;ok&&i<4;i++){f=fopen(dl_join(root,required[i]),"rb");if(!f)ok=0;else if(fclose(f))ok=0;}return ok;
}
static int dl_writable(const char *root){
 char path[256];unsigned count;
#ifdef DL_PROBE_TICK
 count=DL_PROBE_TICK();
#else
 __asm__ volatile("mfc0 %0,$9":"=r"(count));
#endif
 snprintf(path,sizeof path,"%s.mmx-%08x.tmp",root,count);
 /* Exclusive create: never truncate an existing user's file. Known-length
  * roundtrip tests real writes, not just a filesystem attribute. */
 int fd=fileXioOpen(path,0xa02,0);if(fd<0)return 0;
 const char bytes[]="MEGAMIX write probe";int good=fileXioWrite(fd,(void*)bytes,sizeof bytes)==sizeof bytes;
 if(fileXioClose(fd)<0)good=0;
 fd=fileXioOpen(path,1,0);char check[sizeof bytes];
 if(fd<0)good=0;else{if(fileXioRead(fd,check,sizeof check)!=sizeof check||memcmp(check,bytes,sizeof check))good=0;if(fileXioClose(fd)<0)good=0;}
 if(fileXioRemove(path)<0)good=0;return good;
}
static int dl_open_config(void){
 /* Nontruncating canonical bootstrap, then normal checked undo recovery. */
 FILE *f=fopen(dl_settings(),"ab");if(!f)return -1;if(fclose(f))return -1;
 char *text=cfg_read_text();if(!text)return -1;
 char *p=text,*l;unsigned seen=0;char found[DL_ROOT]={0};
 while((l=line(&p)))if(!strncmp(l,"gamepath ",9)){seen++;if(dl_root(found,l+9))seen=2;}
 free(text);if(seen>1)return -1;
 if(seen==1&&dl_valid_game(found)){strcpy(dl_game,found);return 1;}return 0;
}
#define DL_DIRS 4096
static char (*dl_dirs)[64];static unsigned dl_dirs_cap;
static int dl_dir_compare(const void *a,const void *b){return strcmp(a,b);}
static int dl_list(const char *path){
 int fd=fileXioDopen(path);if(fd<0)return -1;iox_dirent_t e;unsigned n=0;int got;
 while((got=fileXioDread(fd,&e))>0){
  e.name[sizeof e.name-1]=0;
  if(FIO_S_ISDIR(e.stat.mode)&&strcmp(e.name,".")&&strcmp(e.name,"..")&&strlen(e.name)<64){
   if(n==DL_DIRS){fileXioDclose(fd);return -2;}
   if(n==dl_dirs_cap){unsigned cap=dl_dirs_cap?dl_dirs_cap*2:64;void *grown=realloc(dl_dirs,cap*64);if(!grown){fileXioDclose(fd);return -2;}dl_dirs=grown;dl_dirs_cap=cap;}
   strcpy(dl_dirs[n++],e.name);
  }
 }
 if(fileXioDclose(fd)<0)return -1;
 /* Dread -1 can mean EOF on MMCE. Never use listing completion to establish
  * file absence or a valid installation; dl_valid_game opens known files. */
 qsort(dl_dirs,n,64,dl_dir_compare);
 return n;
}
static int dl_browse_menu(unsigned cancellable,u32 (*input)(void)){
 const char *devices[]={"mass:/","mass0:/","mass1:/","mmce0:/","mmce1:/"};
 char path[DL_ROOT]={0},notice[100]="Select USB storage or an MMCE slot";
 int page=0,cur=0,n=5;u32 old=0xffffffff;
 for(;;){
  unsigned count=page?n+2:5+cancellable;unsigned shown=(gs->Height-150)/ROW;if(shown<2)shown=2;if(shown>8)shown=8;
  unsigned top=cur>=shown?cur-shown+1:0;
  frame_begin();text_c(18,"GAME FILE LOCATION",C_HEAD);text_fit(24,50,page?path:"Choose storage",C_TEXT,gs->Width-48);
  for(unsigned r=top;r<count&&r<top+shown;r++){
   const char *name=!page?(r==5?"Exit":devices[r]):!r?"Use this folder":r==1?"Back / parent":dl_dirs[r-2];
   row(82+(r-top)*ROW,r==(unsigned)cur,name,C_TEXT);
  }
  text_fit(24,gs->Height-58,notice,C_DIM,gs->Width-48);text_c(gs->Height-30,"D-PAD choose   X open   O back",C_DIM);frame_end();
  u32 b=input?input():pad_physical(),press=b&~old;old=b;
  if(!page&&cancellable&&((press&PAD_CIRCLE)||(cur==5&&(press&PAD_CROSS)))){free(dl_dirs);dl_dirs=0;dl_dirs_cap=0;return 1;}
  if(press&PAD_DOWN)cur=(cur+1)%count;if(press&PAD_UP)cur=(cur+count-1)%count;
  if((press&PAD_CIRCLE)||(page&&cur==1&&(press&PAD_CROSS))){
   if(page){unsigned len=strlen(path);if(len&&path[len-1]=='/')path[--len]=0;char *last=strrchr(path,'/');
    if(!last){page=0;cur=0;n=5;path[0]=0;continue;}last[1]=0;n=dl_list(path);if(n<0)n=0;cur=0;
   }continue;
  }
  if(!(press&PAD_CROSS))continue;
  if(!page){strcpy(path,devices[cur]);page=1;cur=0;n=dl_list(path);if(n<0){snprintf(notice,sizeof notice,"Storage unavailable; go back to retry");n=0;}else notice[0]=0;continue;}
  if(cur==0){if(dl_valid_game(path)){strcpy(dl_game,path);free(dl_dirs);dl_dirs=0;dl_dirs_cap=0;return 0;}snprintf(notice,sizeof notice,"No compatible MEGAMIX game files in this folder");continue;}
  if(cur>=2){char candidate[DL_ROOT];if(snprintf(candidate,sizeof candidate,"%s%s/",path,dl_dirs[cur-2])>=(int)sizeof candidate){snprintf(notice,sizeof notice,"Path exceeds the current 63-character limit");continue;}
   strcpy(path,candidate);n=dl_list(path);if(n<0){snprintf(notice,sizeof notice,"Directory unavailable or too many folders");n=0;}else notice[0]=0;cur=0;
  }
 }
}
static int dl_browse(void){return dl_browse_menu(0,0);}
static int dl_migrate(void){
 /* First relocation only: copy existing checked preferences from the game
  * install. Never overwrite a nonempty config beside the executable. */
 char *current=cfg_read_text();if(!current)return -1;unsigned empty=!current[0];free(current);
 if(!empty||!strcmp(dl_config,dl_game))return 0;
 char source[80];snprintf(source,sizeof source,"%slauncher.cfg",dl_game);
 FILE *f=fopen(source,"rb");if(!f)return 0;if(fclose(f))return -1;
 char *text=malloc(MM_CFG_MAX+1);u8 *scratch=malloc(MM_CFG_MAX+MM_PENDING_HEADER+1);int result=-1;
 if(text&&scratch){int n=mm_cfg_load(&cfg_io,source,text,scratch);if(n>=0){result=mm_cfg_commit(&cfg_io,dl_settings(),text,n,scratch);if(!result)strcpy(dl_binding_import,dl_game);}}
 free(text);free(scratch);return result;
}

static int dl_config_present(const char *root){
 char text[MM_CFG_MAX+1];return cfg_read_io(0,dl_join(root,"launcher.cfg"),text,MM_CFG_MAX)>0;
}
static unsigned dl_choose_config(const char *first,const char *second,unsigned readonly){
 unsigned cur=0;u32 old=0xffffffff;
 for(;;){
  frame_begin();text_c(24,"CHOOSE SETTINGS",C_HEAD);
  text_c(62,readonly?"Boot settings are read-only":"Two settings locations are available",C_DIM);
  row(112,cur==0,readonly?"Import boot settings":"Use settings beside executable",C_TEXT);
  text_fit(30,145,first,C_DIM,gs->Width-60);
  row(190,cur==1,"Use settings with game files",C_TEXT);
  text_fit(30,223,second,C_DIM,gs->Width-60);
  text_c(gs->Height-66,"The other configuration will be kept",C_DIM);
  text_c(gs->Height-32,"UP / DOWN choose   X confirm",C_DIM);frame_end();
  u32 b=pad_physical(),p=b&~old;old=b;
  if(p&(PAD_UP|PAD_DOWN))cur^=1;if(p&PAD_CROSS)return cur;
 }
}
static int dl_copy_config(const char *source){
 char *text=malloc(MM_CFG_MAX+1);u8 *scratch=malloc(MM_CFG_MAX+MM_PENDING_HEADER+1);int result=-1;
 if(text&&scratch){int n=mm_cfg_load(&cfg_io,source,text,scratch);if(n>=0){result=mm_cfg_commit(&cfg_io,dl_settings(),text,n,scratch);if(!result){char src[256];snprintf(src,sizeof src,"%s",source);char *end=strrchr(src,'/');if(end){end[1]=0;dl_root(dl_binding_import,src);}}}}
 free(text);free(scratch);return result;
}
static int dl_locator(int save){
 if(!dl_origin_writable)return 0;
 char path[80];snprintf(path,sizeof path,"%smegamix.path",dl_origin);
 FILE *f=fopen(path,"ab");if(!f||fclose(f))return -1;
 char text[160];u8 *scratch=malloc(MM_CFG_MAX+MM_PENDING_HEADER+1);char *old=malloc(MM_CFG_MAX+1);int result=-1;
 if(scratch&&old){
  int n=mm_cfg_load(&cfg_io,path,old,scratch);
  if(n>=0){
   if(save){int bytes=snprintf(text,sizeof text,"gamepath %s\n",dl_game);result=bytes>0&&bytes<(int)sizeof text?mm_cfg_commit(&cfg_io,path,text,bytes,scratch):-1;}
   else {char *p=old,*l;unsigned seen=0;char root[DL_ROOT]={0};result=0;
    while((l=line(&p)))if(!strncmp(l,"gamepath ",9)){if(++seen>1||dl_root(root,l+9)){result=-1;break;}}
    if(!result&&seen&&dl_valid_game(root))strcpy(dl_game,root);
   }
  }
 }
 free(scratch);free(old);return result;
}
static int dl_setup(int argc,char **argv){
 int writable=0,saved=0;char origin[DL_ROOT]={0};
 if(argc&&argv&&argv[0]&&!dl_exedir(argv[0])){
  strcpy(origin,dl_config);strcpy(dl_origin,origin);writable=dl_writable(origin);dl_origin_writable=writable;
  if(writable){if(dl_set_config(origin))return -1;dl_persistent=1;saved=dl_open_config();if(saved<0)return -1;}
 }
 if(!saved&&origin[0]&&dl_valid_game(origin))strcpy(dl_game,origin);
 /* A later explicit location change overrides a boot config's older gamepath.
  * The unselected nonempty config remains untouched. */
 if(dl_locator(0))return -1;
 if(!dl_game[0]&&dl_browse())return -1;
 char selected[DL_ROOT];strcpy(selected,dl_game);
 if(!writable){
  if(!dl_writable(selected)||dl_set_config(selected))return -1;dl_persistent=0;
  if(dl_open_config()<0)return -1;strcpy(dl_game,selected);
  if(origin[0]&&strcmp(origin,selected)&&dl_config_present(origin)){
   if(!dl_choose_config(dl_join(origin,"launcher.cfg"),dl_settings(),1)){
    if(dl_copy_config(dl_join(origin,"launcher.cfg")))return -1;
   }
  }else {
   for(unsigned i=0;i<90;i++)message("Settings will be saved with the game files","Boot location is unavailable or read-only");
  }
 }else if(strcmp(origin,selected)&&dl_config_present(selected)){
  unsigned use_game=dl_choose_config(dl_join(origin,"launcher.cfg"),dl_join(selected,"launcher.cfg"),0);
  if(use_game){if(dl_set_config(selected)||dl_open_config()<0)return -1;strcpy(dl_game,selected);}
  else if(dl_migrate())return -1; /* only import into an empty boot config */
 }
 return 0;
}

static int dl_change_game(u32 (*input)(void),int (*save)(void)){
 char previous[DL_ROOT];strcpy(previous,dl_game);
 int r=dl_browse_menu(1,input);if(r)return r;
 char selected[DL_ROOT];strcpy(selected,dl_game);
 strcpy(dl_game,previous);
 if(save&&save()){snprintf(dl_location_notice,sizeof dl_location_notice,"Could not save current settings; location unchanged");return -1;}
 strcpy(dl_game,selected);
 if(dl_origin_writable)r=dl_locator(1);
 else {
  /* Read-only/unidentified boot sources still require the explicit browser
   * next boot. Preserve the choice in the shown writable settings location. */
  char *old=cfg_read_text(),*out=malloc(MM_CFG_MAX+1);u8 *scratch=malloc(MM_CFG_MAX+MM_PENDING_HEADER+1);
  const char *owned[]={"gamepath"};char field[80];int n=snprintf(field,sizeof field,"gamepath %s\n",selected);r=-1;
  if(old&&out&&scratch&&n>0&&n<(int)sizeof field){int len=mm_merge(old,strlen(old),field,n,owned,1,out,MM_CFG_MAX);if(len>=0)r=mm_cfg_commit(&cfg_io,dl_settings(),out,len,scratch);}
  free(old);free(out);free(scratch);
 }
 strcpy(dl_game,previous); /* Current catalogs/assets belong to this root until restart. */
 if(r){snprintf(dl_location_notice,sizeof dl_location_notice,"Could not save game location; select again to retry");return -1;}
 strcpy(dl_next_game,selected);dl_location_changed=strcmp(selected,previous)!=0;
 snprintf(dl_location_notice,sizeof dl_location_notice,!dl_location_changed?"Game file location unchanged":dl_origin_writable?"Location saved; restart launcher to apply":"Restart launcher and select this folder again");
 return 0;
}
