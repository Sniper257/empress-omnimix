/* Launcher-only reviewed identities and chart coverage. No source files changed.
 * Included after tab_alloc/grow/put and sdb definitions in main.c. */
typedef struct {
    char source[80];
    u32 sid, rank;
    u8 source_id[16], family_id[16], display_family_id[16], chart[10][16], listing[10][16], preferred[10];
    u16 has, listing_has;
} catalog_source_t;

static catalog_source_t *catalog;
static int catalog_n, catalog_cap;
static int *catalog_rows;
typedef struct {int owner,donor;unsigned borrow,shared;} catalog_merge_t;
static catalog_merge_t catalog_merges[16];static unsigned catalog_merge_n;

static int catalog_find(const char *source, u32 sid)
{
    int i;
    for (i=0;i<catalog_n;i++)
        if (catalog[i].sid==sid && !strcmp(catalog[i].source,source))return i;
    return -1;
}

static int catalog_hex(u8 out[16], const char *s)
{
    int i, any=0;
    if (strlen(s)!=32)return -1;
    for (i=0;i<16;i++) {
        int a=s[i*2],b=s[i*2+1];
        a=a>='0'&&a<='9'?a-'0':a>='a'&&a<='f'?a-'a'+10:-1;
        b=b>='0'&&b<='9'?b-'0':b>='a'&&b<='f'?b-'a'+10:-1;
        if (a<0 || b<0)return -1;
        any|=out[i]=(a<<4)|b;
    }
    return any?0:-1;
}

/* one identity line (S, C, G, E, M) of catalog/charts.txt or of a song folder's `catalog` lines; 0 = ok.
 * own: from a song folder, where an S line already in the catalog (the same song converted again, or
 * lines also merged into charts.txt by hand) is skipped together with its C/G lines instead of failing. */
static int catalog_skip;
static int catalog_line(const char *l, int own)
{
    char source[80],id[33],family[33],extra;
    unsigned sid,rank,ch,pref;
    int errors=0,at;
    do {                                   /* break = one error */
        if (l[0]=='S' && l[1]==' ' && own && sscanf(l+2,"%79s %u %u %32s %32s %c",source,&sid,&rank,id,family,&extra)==5 &&
            catalog_find(source,sid)>=0) {catalog_skip=1;return 0;}
        if (l[0]=='S' && l[1]==' ') catalog_skip=0;
        else if (own && catalog_skip) return 0;
        if (l[0]=='S' && l[1]==' ' &&
            sscanf(l+2,"%79s %u %u %32s %32s %c",source,&sid,&rank,id,family,&extra)==5) {
            if (catalog_find(source,sid)>=0 || catalog_n>=MAXSONG ||
                grow((void **)&catalog,&catalog_cap,catalog_n,sizeof(*catalog))) {errors++;break;}
            at=catalog_n++;memset(catalog+at,0,sizeof(*catalog));
            strcpy(catalog[at].source,source);catalog[at].sid=sid;catalog[at].rank=rank;
            if (catalog_hex(catalog[at].source_id,id) || catalog_hex(catalog[at].family_id,family)) {errors++;break;}
            memcpy(catalog[at].display_family_id,catalog[at].family_id,16);
        } else if (l[0]=='C' && l[1]==' ' &&
                   sscanf(l+2,"%79s %u %u %u %32s %c",source,&sid,&ch,&pref,id,&extra)==5) {
            at=catalog_find(source,sid);
            if (at<0 || ch>=10 || pref>255 || (catalog[at].has>>ch&1) ||
                catalog_hex(catalog[at].chart[ch],id)) {errors++;break;}
            catalog[at].has|=1<<ch;catalog[at].preferred[ch]=pref;
        } else if (l[0]=='G' && l[1]==' ' &&
                   sscanf(l+2,"%79s %u %u %32s %c",source,&sid,&ch,id,&extra)==4) {
            at=catalog_find(source,sid);
            if (at<0 || ch>=10 || !(catalog[at].has>>ch&1) ||
                (catalog[at].listing_has>>ch&1) || catalog_hex(catalog[at].listing[ch],id)) {errors++;break;}
            catalog[at].listing_has|=1<<ch;
        } else if(l[0]=='E' && l[1]==' '){
            if(sscanf(l+2,"%79s %u %32s %c",source,&sid,id,&extra)!=3 ||
               (at=catalog_find(source,sid))<0 || catalog_hex(catalog[at].display_family_id,id)){errors++;break;}
        } else if(l[0]=='M' && l[1]==' '){
            char other[80];unsigned other_sid,borrow,shared;
            if(catalog_merge_n>=16 || sscanf(l+2,"%79s %u %79s %u %x %x %c",source,&sid,other,&other_sid,&borrow,&shared,&extra)!=6 ||
               !borrow || (borrow&shared) || ((borrow|shared)&~0x3ffu)){errors++;break;}
            int a=catalog_find(source,sid),b=catalog_find(other,other_sid);
            if(a<0||b<0||a==b||memcmp(catalog[a].family_id,catalog[b].family_id,16)){errors++;break;}
            for(unsigned k=0;k<catalog_merge_n;k++)if(catalog_merges[k].owner==a||catalog_merges[k].donor==a||catalog_merges[k].owner==b||catalog_merges[k].donor==b)errors++;
            if(errors)break;
            catalog_merges[catalog_merge_n++]=(catalog_merge_t){a,b,borrow,shared};
        } else errors++;
    } while (0);
    return errors;
}

static int catalog_load(int n_add)
{
    char *b=slurp("catalog/charts.txt",0),*p=b,*l;
    int version=0,errors=0;
    free(catalog);catalog=0;catalog_n=catalog_cap=0;catalog_merge_n=0;catalog_skip=0;
    free(catalog_rows);catalog_rows=malloc((FIRST+n_add)*sizeof(int));
    if (!b || !catalog_rows) {free(b);logf_("missing catalog/charts.txt or out of memory\n");return -1;}
    memset(catalog_rows,0xff,(FIRST+n_add)*sizeof(int));
    while ((l=line(&p))) {
        if (!*l || *l=='#')continue;
        if (!strcmp(l,"V 1") && !version) {version=1;continue;}
        if (!version || catalog_line(l,0)) {errors++;break;}
    }
    free(b);
    if (errors || !version || !catalog_n) {logf_("invalid identity catalog\n");return -1;}
    return 0;
}

static int catalog_from_line(const char *l)
{
    char source[80],extra;
    unsigned sid;
    if (sscanf(l,"%79s %u %c",source,&sid,&extra)!=2)return -1;
    return catalog_find(source,sid);
}

static u8 *catalog_record(int row)
{
    return row<FIRST?(u8 *)(0x278b70u+row*0x134u):sdb->recs+(row-FIRST)*0x134;
}

static int catalog_better(int row,int ch,int previous,int old_ch)
{
    const catalog_source_t *a=catalog+catalog_rows[row],*b=catalog+catalog_rows[previous];
    int cmp;
    if ((a->preferred[ch]>>1)!=(b->preferred[old_ch]>>1))return (a->preferred[ch]>>1)>(b->preferred[old_ch]>>1);
    if (a->rank!=b->rank)return a->rank>b->rank;
    if ((a->preferred[ch]&1)!=(b->preferred[old_ch]&1))return (a->preferred[ch]&1)>(b->preferred[old_ch]&1);
    cmp=memcmp(a->source_id,b->source_id,16);
    if (cmp)return cmp<0;
    return ch>old_ch;
}

/* Full keys live only during launcher construction. */
#include "alphabet_catalog.h"
#include "coalesce_catalog.h"

static int catalog_finish(void)
{
    int rows=FIRST+sdb->n_new,row,ch,i,raw=0,count=0,hidden=0,cap=1,errors=0;
    int *hash=0;
    u16 *best=0;
    u8 *best_ch=0,*groups=0;
    for (row=0;row<rows;row++) {
        if (catalog_rows[row]<0)continue;
        for (i=0;i<row;i++)if(catalog_rows[i]==catalog_rows[row]) {
            logf_("duplicate installed source identity: %s:%u\n",catalog[catalog_rows[row]].source,
                  (unsigned)catalog[catalog_rows[row]].sid);return -1;
        }
        for(ch=0;ch<10;ch++)if(catalog_record(row)[0x4a+ch])raw++;
    }
    if (raw>=NOSLOT) {logf_("too many chart identities\n");return -1;}
    while (cap<raw*2+1)cap*=2;
    hash=malloc(cap*sizeof(int));best=malloc((raw+1)*sizeof(u16));best_ch=malloc(raw+1);groups=malloc((raw+1)*16);
    sdb->chart_index=tab_alloc(rows*10*2);
    sdb->chart_ids=tab_alloc((raw+1)*16);
    if (!hash || !best || !best_ch || !groups || !sdb->chart_index || !sdb->chart_ids) {
        logf_("block/heap full (chart identities)\n");errors++;goto done;
    }
    memset(hash,0xff,cap*sizeof(int));memset(sdb->chart_index,0xff,rows*10*2);
    for (row=0;row<rows;row++) {
        catalog_source_t *source;
        if (catalog_rows[row]<0)continue;
        source=catalog+catalog_rows[row];
        for (ch=0;ch<10;ch++) {
            u32 h=2166136261u;
            int n,bucket;
            const u8 *id=source->chart[ch];
            if (!catalog_record(row)[0x4a+ch])continue;
            if (!(source->has>>ch&1)) {
                logf_("catalog has no chart: %s:%u column%d\n",source->source,(unsigned)source->sid,ch);
                errors++;continue;
            }
            for (i=0;i<16;i++)h=(h^id[i])*16777619u;
            bucket=h&(cap-1);
            while ((n=hash[bucket])>=0 && memcmp(sdb->chart_ids+n*16,id,16))bucket=(bucket+1)&(cap-1);
            if (n<0) {
                n=count++;memcpy(sdb->chart_ids+n*16,id,16);hash[bucket]=n;
                best[n]=row;best_ch[n]=ch;
            } else {
                catalog_source_t *owner=catalog+catalog_rows[best[n]];
                if (memcmp(owner->family_id,source->family_id,16)) {
                    logf_("chart identity crosses unrelated songs\n");errors++;continue;
                }
                if (catalog_better(row,ch,best[n],best_ch[n])) {best[n]=row;best_ch[n]=ch;}
            }
            sdb->chart_index[row*10+ch]=n;
        }
    }
    if (errors)goto done;
    if(catalog_coalesce()){errors++;goto done;}
    /* Scores keep their published chart IDs. A separate, launcher-only hash
     * chooses which available source represents each display coverage group. */
    memset(hash,0xff,cap*sizeof(int));
    int group_count=0;
    for (row=0;row<rows;row++) {
        if (catalog_rows[row]<0)continue;
        catalog_source_t *source=catalog+catalog_rows[row];
        for (ch=0;ch<10;ch++) {
            if (!catalog_record(row)[0x4a+ch])continue;
            const u8 *id=source->listing_has>>ch&1?source->listing[ch]:source->chart[ch];
            u32 h=2166136261u;int n,bucket;
            for (i=0;i<16;i++)h=(h^id[i])*16777619u;
            bucket=h&(cap-1);
            while ((n=hash[bucket])>=0 && memcmp(groups+n*16,id,16))bucket=(bucket+1)&(cap-1);
            if (n<0) {
                n=group_count++;memcpy(groups+n*16,id,16);hash[bucket]=n;
                best[n]=row;best_ch[n]=ch;
            } else {
                catalog_source_t *owner=catalog+catalog_rows[best[n]];
                if (memcmp(owner->display_family_id,source->display_family_id,16) || ch/5!=best_ch[n]/5) {
                    logf_("listing group crosses unrelated songs or play modes\n");errors++;goto done;
                }
                if (catalog_better(row,ch,best[n],best_ch[n])) {best[n]=row;best_ch[n]=ch;}
            }
        }
    }
    for (row=0;row<rows;row++) {
        for (ch=0;ch<10;ch++) {
            if (catalog_rows[row]<0 || sdb->chart_index[row*10+ch]==NOSLOT)continue;
            catalog_source_t *source=catalog+catalog_rows[row];
            const u8 *id=source->listing_has>>ch&1?source->listing[ch]:source->chart[ch];
            u32 h=2166136261u;for(i=0;i<16;i++)h=(h^id[i])*16777619u;
            int bucket=h&(cap-1),n;
            while ((n=hash[bucket])>=0 && memcmp(groups+n*16,id,16))bucket=(bucket+1)&(cap-1);
            if (n>=0 && (best[n]!=row || best_ch[n]!=ch)) {
                catalog_record(row)[0x4a+ch]=0;hidden++;
            }
        }
    }
    /* The records, resources and hidden chart IDs survive; only derived listing
     * levels change. Source files and native serialized scores are untouched. */
    memset(sdb->fold_n,0,sizeof sdb->fold_n);
    for (row=0;row<rows;row++) {
        int fold=row<FIRST?sdb->emp_fold[row]:sdb->slot_fold[row-FIRST];
        if (row<FIRST && !sdb->present[row])continue;
        if ((unsigned)fold>=NFOLD)continue;
        for (ch=0;ch<10;ch++)if(catalog_record(row)[0x4a+ch]) {if(sdb->fold_n[fold]<255)sdb->fold_n[fold]++;break;}
    }
    if(alphabet_finish(1)){errors++;goto done;}
    sdb->identity_version=1;sdb->identity_count=count;
    sdb->custom_n=sdb->fold_n[0];
    logf_("catalog: %d source charts, %d identities, %d equivalent difficulties hidden\n",raw,count,hidden);
done:
    free(hash);free(best);free(best_ch);free(groups);
    free(catalog);catalog=0;catalog_n=catalog_cap=0;
    free(catalog_rows);catalog_rows=0;
    return errors?-1:0;
}
