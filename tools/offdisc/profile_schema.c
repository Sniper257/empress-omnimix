#include "profile_schema.h"
#if defined(__mips__)
#define SC_PAD() __asm__ volatile("nop\n\tnop\n\tnop")
#else
#define SC_PAD() ((void)0)
#endif
static int any(const mp_u8 *p,mp_u32 n){while(n--){if(*p++)return 1;SC_PAD();}return 0;}
static int less(const mp_u8 *a,const mp_u8 *b,mp_u32 n)
{
    while(n--){if(*a!=*b)return *a<*b;a++;b++;SC_PAD();}return 0;
}
static void copy(mp_u8 *a,const mp_u8 *b,mp_u32 n){while(n--){*a++=*b++;SC_PAD();}}
int mp_schema_finish(mp_schema_state *s)
{
    if(s->used || s->utf_remaining)return MP_CORRUPT;
    if(s->type==MP_NATIVE && ~s->native_crc!=s->native_expected)return MP_CORRUPT;
    return MP_OK;
}
static int section(void *context,mp_u64 index,const mp_section *entry)
{
    mp_schema_state *s=context;int r=mp_schema_finish(s);(void)index;if(r)return r;
    s->type=0;s->used=s->have_key=0;s->received=0;
    mp_u32 stride=0;int known=1;
    switch(entry->type){
    case MP_METADATA:break;
    case MP_CHARTS:case MP_SONGS:stride=48;break;
    case MP_NATIVE:case MP_V1_ENVELOPE:stride=1;break;
    case MP_PROVENANCE:break;
    default:known=0;break;
    }
    if(!known || entry->version!=1 || entry->stride!=stride)
        return entry->flags&MP_MANDATORY?MP_UNSUPPORTED:MP_OK;
    if(entry->type==MP_METADATA){
        if(entry->count!=1 || !entry->bytes || entry->bytes>128)return MP_CORRUPT;
        s->type=MP_METADATA;s->utf_remaining=0;
    }
    if(entry->type==MP_CHARTS || entry->type==MP_SONGS)s->type=entry->type;
    if(entry->type==MP_NATIVE){
        if(entry->bytes!=833290u)return MP_CORRUPT;
        s->type=MP_NATIVE;s->native_crc=~0u;
    }
    /* Provenance/envelope are optional opaque import data for the game. The
     * host importer/exporter performs their source-specific semantic checks. */
    return MP_OK;
}
static int record(mp_schema_state *s)
{
    const mp_u8 *r=s->record;mp_u32 key=s->type==MP_CHARTS?20:36,i;
    if(!any(r,16))return MP_CORRUPT;
    if(s->type==MP_CHARTS){
        if(r[16]>2 || r[17] || r[18]!=1 || r[19] || r[31]>>4>9 || (r[23]&0x80) ||
           (mp_read32(r+40)&0x0fffc000u))return MP_CORRUPT;
    }else{
        mp_u32 kind=r[32]|(mp_u32)r[33]<<8,side=r[34]|(mp_u32)r[35]<<8,value=mp_read32(r+36);
        if(!any(r+16,16) || !((kind==1 && !side) || (kind==2 && side<2)) ||
           value<16 || value>320 || any(r+40,8))return MP_CORRUPT;
    }
    if(s->have_key && !less(s->last_key,r,key))return MP_CORRUPT;
    for(i=0;i<key;i++){s->last_key[i]=r[i];SC_PAD();}s->have_key=1;return MP_OK;
}
static int utf8(mp_schema_state *s,mp_u8 b)
{
    if(!s->utf_remaining){
        if(!b)return MP_CORRUPT;if(b<0x80)return MP_OK;
        if(b>=0xc2 && b<=0xdf){s->utf_remaining=1;s->utf_value=b&31;s->utf_minimum=0x80;}
        else if(b>=0xe0 && b<=0xef){s->utf_remaining=2;s->utf_value=b&15;s->utf_minimum=0x800;}
        else if(b>=0xf0 && b<=0xf4){s->utf_remaining=3;s->utf_value=b&7;s->utf_minimum=0x10000;}
        else return MP_CORRUPT;
    }else{
        if((b&0xc0)!=0x80)return MP_CORRUPT;s->utf_value=(s->utf_value<<6)|(b&63);s->utf_remaining--;
        if(!s->utf_remaining && (s->utf_value<s->utf_minimum || s->utf_value>0x10ffff ||
           (s->utf_value>=0xd800 && s->utf_value<=0xdfff)))return MP_CORRUPT;
    }
    return MP_OK;
}
static int page(void *context,mp_u64 index,const mp_section *entry,mp_u64 offset,const void *data,mp_u32 n)
{
    mp_schema_state *s=context;const mp_u8 *p=data;mp_u32 i,k;int r;(void)index;(void)entry;
    if(offset!=s->received)return MP_CORRUPT;s->received+=n;
    if(s->type==MP_NATIVE){
        k=0;if(!offset){if(n<4)return MP_CORRUPT;s->native_expected=mp_read32(p);k=4;}
        s->native_crc=mp_crc(s->native_crc,p+k,n-k);return MP_OK;
    }
    if(s->type==MP_METADATA){for(i=0;i<n;i++){if((r=utf8(s,p[i])))return r;SC_PAD();}return MP_OK;}
    if(s->type!=MP_CHARTS && s->type!=MP_SONGS)return MP_OK;
    while(n){
        k=48-s->used;if(k>n)k=n;copy(s->record+s->used,p,k);s->used+=k;p+=k;n-=k;
        if(s->used==48){if((r=record(s)))return r;s->used=0;}
    }
    return MP_OK;
}
void mp_schema_begin(mp_schema_state *s,mp_visitor *v)
{
    s->type=s->used=s->have_key=s->utf_remaining=0;s->received=0;
    v->context=s;v->section=section;v->page=page;
}
#ifndef MP_ASYNC_ONLY
int mp_check_profile(const mp_io *io,mp_u64 size,mp_info *info,mp_work *work,mp_schema_state *state)
{
    mp_visitor v;mp_schema_begin(state,&v);int r=mp_check(io,size,info,&v,work);return r?r:mp_schema_finish(state);
}
#endif
