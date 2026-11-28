/* Catalog metadata is independent of the native optional-resource tables.
 * Called from entry hooks, so every caller gets the same ownership checks. */
static u8 *resource_record(s32 song)
{
    s32 row=song_catalog_row(song);
    if(row<0)return 0;
    if(row>=FIRST)return rec_of(song_ctx(),song);      /* the alias's cached record (rec_added) */
    return rec_of(song_ctx(),row);
}

/* Native mode5 is a view of the primary SP record; the assembly routes it
 * there. Mode4 is derived from a native-only auxiliary lamp array, so an
 * imported chart gets a fresh empty view. It must never overwrite battle's
 * earned record merely because it is outside modes0..3. */
u8 *resource_derived_save(void)
{
    u8 *record=sdb.saves+(NSAVE-1)*0x230;
    memcpy_(record,UNPLAYED,0x1c);
    return record;
}

/* Original1e3650: reference graph target from the song metadata. */
u32 resource_graph_target(s32 song,s32 dp,s32 diff)
{
    u8 *rec;
    if((u32)dp>=2 || (u32)diff>=3 || !(rec=resource_record(song)))return 0;
    return *(u16 *)(rec+0x126+(dp*3+diff)*2);
}

/* Original1e3700: SP notes or the selected/summed DP side. */
u32 resource_note_count(s32 song,s32 dp,s32 diff,s32 side)
{
    u8 *rec;
    if((u32)dp>=2 || (u32)diff>=5 || !(rec=resource_record(song)))return 0;
    if(!dp)return *(u16 *)(rec+0x108+diff*2);
    rec+=0x112+diff*4;
    if(side==0)return *(u16 *)rec;
    if(side==1)return *(u16 *)(rec+2);
    if(side==3)return (u32)*(u16 *)rec+*(u16 *)(rec+2);
    return 0;
}

#ifndef SONG_RESOURCE_TEST
extern void resource_profile_rebuild_body(u8 *);
extern void resource_profile_baseline_body(u8 *);
#define resource_lamp_weights ((s32 *)0x25ef20)
#define resource_grade_weights ((s32 *)0x25ef40)
#endif

/* The same DJ POINT formula as original1da410/1da240. Only its metadata and
 * earned record come from the catalog; no native99-song cache is enlarged. */
static s32 resource_points(const u8 *sv)
{
    u32 score=*(const u16 *)(sv+8)&0x3fff, lamp=sv[15]>>5, grade=sv[11]>>4;
    if(!score || lamp>=8 || grade>=8)return 0;
    return score*(resource_lamp_weights[lamp]+100+resource_grade_weights[grade]);
}

static void resource_add_points(u8 *p,s32 value)
{
    s32 old=I32(p);
    I32(p)=old>0x7fffffff-value?0x7fffffff:old+value;
}

/* Mark chart identities, not aliases. One native/imported equivalent counts
 * once; distinct older charts contribute in their actual difficulty slots.
 * The scratch bitmap is bounded by the catalog's u16 identity indices and
 * freed before the original caller resumes. No persistent or gameplay buffer. */
static void resource_profile_imports(u8 *ctx,s32 baseline)
{
    u8 *seen;
    s32 bytes,row,dp,d;u32 index;
    if(sdb.identity_version!=1 || !sdb.chart_index || !sdb.chart_ids ||
       !sdb.summary || !sdb.identity_count || sdb.identity_count>NOSLOT)return;
    bytes=(sdb.identity_count+7)/8;
    seen=malloc_(bytes);if(!seen)return;
    for(d=0;d<bytes;d++){seen[d]=0;LOOP_PAD();}
    for(row=0;row<E16_RESOURCE_SONGS;row++) {
        u8 *rec=rec_of(song_ctx(),row);
        if(sdb.present[row] && rec)for(d=0;d<10;d++) {
            if(d%5==3 || !rec[0x4a+d])continue;
            index=sdb.chart_index[row*10+d];
            if(index<sdb.identity_count)seen[index>>3]|=1u<<(index&7);
            LOOP_PAD();
        }
        LOOP_PAD();
    }
    for(row=0;row<(s32)sdb.n_new;row++) {
        const u8 *rec=slot_summary(row)+SUM_LEVELS-0x4a;   /* rec[0x4a + i] = level i */
        for(dp=0;dp<2;dp++) {
            s32 best=0;
            for(d=0;d<5;d++) {
                LOOP_PAD();
                s32 points;
                if(d==3 || !rec[0x4a+dp*5+d])continue;
                index=sdb.chart_index[(FIRST+row)*10+dp*5+d];
                if(index>=sdb.identity_count || (seen[index>>3]>>(index&7)&1))continue;
                seen[index>>3]|=1u<<(index&7);
                points=resource_points(slot_save(row,dp?2:0,d));
                if(!baseline)resource_add_points(ctx+0x18+(dp*5+d)*4,points);
                if(points>best)best=points;
                LOOP_PAD();
            }
            resource_add_points(ctx+0x70+dp*4,best);
            if(baseline)resource_add_points(ctx+0x68+dp*4,best);
            LOOP_PAD();
        }
        LOOP_PAD();
    }
    free_(seen);
}

void resource_profile_rebuild(u8 *ctx)
{
    resource_profile_rebuild_body(ctx);
    resource_profile_imports(ctx,0);
}

void resource_profile_baseline(u8 *ctx)
{
    resource_profile_baseline_body(ctx);
    resource_profile_imports(ctx,1);
}
