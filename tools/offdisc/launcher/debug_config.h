#ifndef MEGAMIX_DEBUG_CONFIG_H
#define MEGAMIX_DEBUG_CONFIG_H
/* External-only settings. Never rewritten by the launcher or exposed in UI.
 * A missing file, malformed switch or any value other than 1 keeps debug off. */
typedef struct { int enabled, input_mode, launcher_log,save_profile,select_profile,select_seconds,select_osd,prep_watch; } debug_options;
static int debug_range(const char *line,const char *key,int *value,int low,int high)
{
    size_t n=strlen(key);char *end;long v;
    while(*line==' ' || *line=='\t')line++;
    if(strncmp(line,key,n) || (line[n]!=' ' && line[n]!='\t'))return 0;
    line+=n;while(*line==' ' || *line=='\t')line++;
    v=strtol(line,&end,10);
    if(end==line)return -1;
    while(*end==' ' || *end=='\t' || *end=='\r' || *end=='\n')end++;
    if(*end && *end!='#')return -1;
    if(v<low || v>high)return -1;
    *value=(int)v;return 1;
}
static int debug_value(const char *line,const char *key,int *value){return debug_range(line,key,value,0,3);}
static debug_options debug_read(const char *path)
{
    debug_options opt={0,3,0,0,0,20,0,0};int select_error=0;char line[128];FILE *f=fopen(path,"rb");
    if(!f)return opt;
    while(fgets(line,sizeof line,f)) {
        int v=0,r;
        /* Do not interpret the tail of an overlong line as another option. */
        if(!strchr(line,'\n') && !feof(f)) {
            int c;while((c=fgetc(f))!=EOF && c!='\n');
            opt.enabled=0;continue;
        }
        r=debug_value(line,"debug",&v);
        if(r)opt.enabled=r==1 && v==1;
        r=debug_value(line,"inputlog",&v);
        if(r)opt.input_mode=r==1?v:0;
        r=debug_value(line,"launcherlog",&v);
        if(r)opt.launcher_log=r==1 && v==1;
        r=debug_value(line,"saveprofile",&v);
        if(r)opt.save_profile=r==1 && v==1;
        r=debug_range(line,"selectprofile",&v,0,2);
        if(r){if(r<0)select_error=1;else opt.select_profile=v;}
        r=debug_range(line,"selectseconds",&v,5,60);
        if(r){if(r<0)select_error=1;else opt.select_seconds=v;}
        r=debug_range(line,"selectosd",&v,0,1);
        if(r){if(r<0)select_error=1;else opt.select_osd=v;}
        r=debug_range(line,"prepprobe",&v,0,1);
        if(r)opt.prep_watch=r==1 && v==1;
    }
    if(ferror(f))opt.enabled=0;
    if(!opt.enabled)opt.launcher_log=opt.save_profile=0;
    if(!opt.enabled || select_error)opt.select_profile=0;
    if(!opt.enabled)opt.prep_watch=0;
    fclose(f);return opt;
}
#endif
