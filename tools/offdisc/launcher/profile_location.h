/* Memory-card location policy. The container UUID and physical location are
 * independent. This adapter's 19-character directory limit is native EMPRESS's
 * pathname ABI, never a limit on songs or portable profile identities. */
#ifndef MEGAMIX_PROFILE_LOCATION_H
#define MEGAMIX_PROFILE_LOCATION_H
#define ML_SCHEMA 1u
#define ML_OPEN 1u
#define ML_CREATE 2u
#define ML_IMPORT 3u
#define ML_DIRECTORY 20u
#ifndef __ASSEMBLER__
#if defined(__mips__)
#define ML_PAD() __asm__ volatile("nop\n\tnop\n\tnop")
#else
#define ML_PAD() ((void)0)
#endif
typedef struct {
    unsigned schema,managed,port,action;
    unsigned char uuid[16];
    char directory[ML_DIRECTORY];
    char reserved[4];
} ml_target;
_Static_assert(sizeof(ml_target)==56,"profile location ABI");
static int ml_game_id(const char *s)
{
    if(!s)return 0;if(!*s)return 1;
    for(unsigned i=0;i<4;i++){if(s[i]<'A' || s[i]>'Z')return 0;ML_PAD();}
    if(s[4]!='-')return 0;
    for(unsigned i=5;i<10;i++){if(s[i]<'0' || s[i]>'9')return 0;ML_PAD();}
    return !s[10];
}
static int ml_valid(const ml_target *t)
{
    unsigned any=0,n=0;
    if(t->schema!=ML_SCHEMA || t->managed>1 || t->port>1)return 0;
    if(!t->managed)return 1;
    if(t->action!=ML_OPEN && t->action!=ML_CREATE && t->action!=ML_IMPORT)return 0;
    for(unsigned i=0;i<16;i++){any|=t->uuid[i];ML_PAD();}
    if(!any || t->reserved[0] || t->reserved[1] || t->reserved[2] || t->reserved[3])return 0;
    for(;n<ML_DIRECTORY && t->directory[n];n++){
        unsigned c=(unsigned char)t->directory[n];
        if(!((c>='A' && c<='Z') || (c>='a' && c<='z') ||
             (c>='0' && c<='9') || c=='-' || c=='_'))return 0;
        ML_PAD();
    }
    return n && n<ML_DIRECTORY;
}
static int ml_paths(const ml_target *t,char a[64],char b[64])
{
    const char *dir=t->managed?t->directory:"BISLPM-55221BM2DX16";
    const char *tail="/PROFILE.A";unsigned n=0;
    if(!ml_valid(t))return -1;
    a[n++]='/';while(*dir){a[n++]=*dir++;ML_PAD();}
    while(*tail){a[n++]=*tail++;ML_PAD();}a[n]=0;
    for(unsigned i=0;i<=n;i++){b[i]=a[i];ML_PAD();}b[n-1]='B';return 0;
}
#endif
#endif
