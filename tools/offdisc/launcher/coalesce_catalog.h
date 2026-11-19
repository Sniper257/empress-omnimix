#ifndef MEGAMIX_COALESCE_CATALOG_H
#define MEGAMIX_COALESCE_CATALOG_H
/* Explicit reviewed source combinations only. Work on relocated records after
 * every published chart ID is indexed; retain hidden original IDs and files. */
static int cm_fid(unsigned fid){return fid>=FID0 && fid-FID0<(unsigned)n_fid;}
static int cm_same_file(unsigned a,unsigned b){
 return cm_fid(a)&&cm_fid(b)&&!memcmp(fids+(a-FID0)*3,fids+(b-FID0)*3,12);
}
static unsigned cm_workspace(unsigned plan,unsigned big){
 if(plan&ASSET_WORKSPACE_ABSOLUTE)return plan&ASSET_WORKSPACE_MASK;
 return (big?0xd08000u:ASSET_WORKSPACE_FLOOR)-plan;
}
static int catalog_coalesce(void){
 static const unsigned fields[10]={0xd0,0xc0,0xc8,0xd8,0xe0,0xd4,0xc4,0xcc,0xdc,0xe4};
 static const unsigned groups[4]={2,0,1,3};unsigned rows=FIRST+sdb->n_new;
 for(unsigned m=0;m<catalog_merge_n;m++){
  catalog_merge_t *merge=catalog_merges+m;int owner=-1,donor=-1;
  for(unsigned r=0;r<rows;r++){if(catalog_rows[r]==merge->owner)owner=r;if(catalog_rows[r]==merge->donor)donor=r;}
  if(owner<0||donor<0)continue; /* Installed/enabled source combinations decide availability. */
  if(owner<FIRST||donor<FIRST)goto invalid;
  catalog_source_t *a=catalog+merge->owner,*b=catalog+merge->donor;
  u8 *dst=catalog_record(owner),*src=catalog_record(donor);
  if(((merge->borrow|merge->shared)&~(a->has&b->has)) || a->rank<=b->rank ||
     memcmp(dst+0x40,src+0x40,10) || memcmp(dst+0x60,src+0x60,4) ||
     !cm_same_file(*(u32 *)(dst+0x58),*(u32 *)(src+0x58)))goto invalid;
  /* A borrowed chart can select the second movie when its sound bank differs.
   * These reviewed combinations have identical movies; materialize that fallback. */
  for(unsigned ch=0;ch<10;ch++)if((merge->borrow|merge->shared)>>ch&1){
   if(!src[0x4a+ch])goto invalid;
   if(merge->shared>>ch&1){
    const u8 *ga=a->listing_has>>ch&1?a->listing[ch]:a->chart[ch];
    const u8 *gb=b->listing_has>>ch&1?b->listing[ch]:b->chart[ch];
    if(!dst[0x4a+ch]||memcmp(ga,gb,16))goto invalid;
   }else{
    unsigned diff=ch%5,dp=ch/5;if(diff>=4||dst[0x4a+ch]||memcmp(a->chart[ch],b->chart[ch],16))goto invalid;
    unsigned audio=0xe8+8*groups[diff]+dp*2;
    /* No guessed fallback semantics for unreviewed sparse sound records. */
    if(!cm_fid(*(u32 *)(src+fields[ch])) || !cm_fid(*(u16 *)(src+audio)) || !cm_fid(*(u16 *)(src+audio+4)) || sdb->chart_index[donor*10+ch]==NOSLOT)goto invalid;
   }
  }
  for(unsigned ch=0;ch<10;ch++){
   if(merge->borrow>>ch&1){
    unsigned diff=ch%5,dp=ch/5,audio=0xe8+8*groups[diff]+dp*2;
    dst[0x4a+ch]=src[0x4a+ch];memcpy(dst+fields[ch],src+fields[ch],4);
    memcpy(dst+audio,src+audio,2);memcpy(dst+audio+4,src+audio+4,2);
    unsigned notes=dp?5+2*diff:diff;memcpy(dst+0x108+2*notes,src+0x108+2*notes,dp?4:2);
    if(diff<3)memcpy(dst+0x126+2*(dp*3+diff),src+0x126+2*(dp*3+diff),2);
    sdb->chart_index[owner*10+ch]=sdb->chart_index[donor*10+ch];
   }
   if((merge->borrow|merge->shared)>>ch&1)src[0x4a+ch]=0;
  }
  *(u32 *)(dst+0x5c)=*(u32 *)(dst+0x58);
  unsigned *plans=(unsigned *)asset_mem[1];unsigned x=cm_workspace(plans[owner],dst[0xbd]),y=cm_workspace(plans[donor],src[0xbd]);
  if(y>x)x=y;dst[0xbd]|=src[0xbd];
  plans[owner]=dst[0xbd]?(ASSET_WORKSPACE_ABSOLUTE|x):ASSET_WORKSPACE_FLOOR-x;
  logf_("catalog: %s:%u borrows mask%x from %s:%u; shared mask%x, score IDs retained\n",a->source,a->sid,merge->borrow,b->source,b->sid,merge->shared);
 }
 return 0;
 invalid:logf_("Invalid/incompatible source coalescing rule\n");return -1;
}
#endif
