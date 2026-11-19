#ifndef MEGAMIX_FILE_META_H
#define MEGAMIX_FILE_META_H
/* Launcher-only, bounded per-boot metadata reuse. No persistent stale index,
 * no game heap/reserved-block ownership; released before ExecPS2. Mutable
 * settings and generated cache products always go directly to storage. */
#define LM_LIMIT (2u*1024u*1024u)
typedef struct lm_entry {struct lm_entry *next;long size;char *text;u32 raw,packed;unsigned measured;char path[1];} lm_entry;
static lm_entry *lm_buckets[256];
static unsigned lm_bytes,lm_peak,lm_size_hits,lm_text_hits,lm_measure_hits,lm_queries;
static unsigned lm_hash(const char *s){unsigned h=2166136261u;while(*s)h=(h^(unsigned char)*s++)*16777619u;return h&255;}
static int lm_source(const char *s){return !strncmp(s,"songs/",6)||!strncmp(s,"EMPRESS/",8)||!strncmp(s,"skins/",6)||!strncmp(s,"mods/",5)||!strncmp(s,"catalog/",8);}
static lm_entry *lm_find(const char *s,int create)
{
 if(!lm_source(s))return 0;
 unsigned h=lm_hash(s);lm_entry *e=lm_buckets[h];
 while(e){if(!strcmp(e->path,s))return e;e=e->next;}
 unsigned n=sizeof(lm_entry)+strlen(s);
 if(!create || n>LM_LIMIT-lm_bytes || !(e=calloc(1,n)))return 0;
 strcpy(e->path,s);e->size=-1;e->next=lm_buckets[h];lm_buckets[h]=e;
 lm_bytes+=n;if(lm_bytes>lm_peak)lm_peak=lm_bytes;return e;
}
static void lm_text_keep(const char *s,const char *b,unsigned n)
{
 unsigned len=strlen(s);lm_entry *e;
 if(len<9 || strcmp(s+len-9,"/song.txt") || n>16384 || !(e=lm_find(s,1)) || e->text || n+1>LM_LIMIT-lm_bytes)return;
 if(!(e->text=malloc(n+1)))return;
 memcpy(e->text,b,n+1);e->size=n;
 lm_bytes+=n+1;if(lm_bytes>lm_peak)lm_peak=lm_bytes;
}
static void lm_clear(void)
{
 for(unsigned i=0;i<256;i++){lm_entry *e=lm_buckets[i];while(e){lm_entry *next=e->next;free(e->text);free(e);e=next;}lm_buckets[i]=0;}lm_bytes=0;
}
#endif
