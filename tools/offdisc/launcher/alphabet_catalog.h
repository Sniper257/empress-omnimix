#ifndef MEGAMIX_ALPHABET_CATALOG_H
#define MEGAMIX_ALPHABET_CATALOG_H
/* Launcher-only full keys become compact runtime ordinals. Never repurpose a
 * score ID, source preference rank or an undocumented native record field. */
static int alphabet_compare(const void *a,const void *b){
 const char *const *x=a,*const *y=b;return strcmp(*x,*y);
}
static int alphabet_retail(const char *source){
 const char *known[]={"id","pb","djt","gold","distorted","happysky","red","tenth","ninth","eighth","seventh","sixth","fifth","fourth","third","usa"};
 for(unsigned i=0;i<sizeof known/sizeof *known;i++)if(!strcmp(known[i],source))return 1;return 0;
}
static int alphabet_finish(int required){
 unsigned rows=FIRST+sdb->n_new;char **keys=calloc(rows,sizeof *keys),**order=malloc(rows*sizeof *order);
 char *text=slurp("catalog/sort.txt",0),*p=text,*l;int version=0,error=0,count=0;
 unsigned char *seen=calloc(catalog_n,1);
 if(!keys||!order||!seen||(!text&&required)){error=1;goto done;}
 for(unsigned r=0;r<rows;r++){
  const char *raw=catalog_rows[r]>=0?(const char *)catalog_record(r):"";unsigned n=0;while(n<64&&raw[n])n++;
  if(n==64){error=1;goto done;}keys[r]=malloc(n+1);if(!keys[r]){error=1;goto done;}memcpy(keys[r],raw,n+1);
 }
 while(text&&(l=line(&p))){
  char source[80];unsigned sid;int used=0,ci,found=-1;
  if(!*l||*l=='#')continue;
  if(!strcmp(l,"V 1")&&!version){version=1;continue;}
  if(!version||sscanf(l,"S %79s %u %n",source,&sid,&used)!=2||!used||!l[used]||strlen(l+used)>255){error=1;break;}
  for(const unsigned char *q=(const unsigned char *)l+used;*q;q++)if(*q<32||*q>126){error=1;break;}
  if(error)break;
  ci=catalog_find(source,sid);if(ci<0||seen[ci]){error=1;break;}seen[ci]=1;
  for(unsigned r=0;r<rows;r++)if(catalog_rows[r]==ci){found=(int)r;break;}
  if(found<0)continue; /* Known catalog source, but not installed/enabled. */
  char *key=malloc(strlen(l+used)+1);if(!key){error=1;break;}strcpy(key,l+used);free(keys[found]);keys[found]=key;
 }
 if(text&&!version)error=1;
 if(required)for(unsigned r=0;r<rows;r++)if(catalog_rows[r]>=0&&alphabet_retail(catalog[catalog_rows[r]].source)&&!seen[catalog_rows[r]]){error=1;break;}
 if(error)goto done;
 for(unsigned r=0;r<rows;r++)order[r]=keys[r];
 qsort(order,rows,sizeof *order,alphabet_compare);
 sdb->sort_rank=tab_alloc(rows*sizeof(u16));if(!sdb->sort_rank){error=1;goto done;}
 for(unsigned r=0;r<rows;r++){
  unsigned lo=0,hi=rows;
  while(lo<hi){unsigned mid=lo+(hi-lo)/2;if(strcmp(order[mid],keys[r])<0)lo=mid+1;else hi=mid;}
  if(lo>=NOSLOT){error=1;goto done;}sdb->sort_rank[r]=(u16)lo;
 }
 sdb->sort_version=1;count=rows;
 done:
 if(keys)for(unsigned r=0;r<rows;r++)free(keys[r]);
 free(keys);free(order);free(text);free(seen);
 if(error){sdb->sort_version=0;logf_("Invalid/missing alphabetical catalog or block full\n");return -1;}
 logf_("alphabet: %d row ranks, original record strings retained\n",count);return 0;
}
#endif
