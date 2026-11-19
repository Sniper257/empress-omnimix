#include "profile_format.h"
#include "profile_wire.h"
#if defined(__mips__)
#define MP_PAD() __asm__ volatile("nop\n\tnop\n\tnop")
#else
#define MP_PAD() ((void)0)
#endif
static void copy(void *a,const void *b,mp_u32 n){mp_u8 *d=a;const mp_u8 *s=b;while(n--){*d++=*s++;MP_PAD();}}
static void zero(void *a,mp_u32 n){mp_u8 *d=a;while(n--){*d++=0;MP_PAD();}}
static int equal(const void *a,const void *b,mp_u32 n){const mp_u8 *x=a,*y=b;while(n--){if(*x++!=*y++)return 0;MP_PAD();}return 1;}
static int empty(const mp_u8 *a,mp_u32 n){while(n--){if(*a++)return 0;MP_PAD();}return 1;}
mp_u32 mp_read32(const mp_u8 *p){return (mp_u32)p[0]|(mp_u32)p[1]<<8|(mp_u32)p[2]<<16|(mp_u32)p[3]<<24;}
mp_u64 mp_read64(const mp_u8 *p){return mp_read32(p)|(mp_u64)mp_read32(p+4)<<32;}
void mp_write32(mp_u8 *p,mp_u32 v){p[0]=v;p[1]=v>>8;p[2]=v>>16;p[3]=v>>24;}
void mp_write64(mp_u8 *p,mp_u64 v){mp_write32(p,(mp_u32)v);mp_write32(p+4,(mp_u32)(v>>32));}
mp_u32 mp_crc(mp_u32 c,const void *data,mp_u32 n)
{
    static const mp_u32 t[16]={0,0x1db71064,0x3b6e20c8,0x26d930ac,0x76dc4190,0x6b6b51f4,0x4db26158,0x5005713c,
        0xedb88320,0xf00f9344,0xd6d6a3e8,0xcb61b38c,0x9b64c2b0,0x86d3d2d4,0xa00ae278,0xbdbdf21c};
    const mp_u8 *p=data;while(n--){c^=*p++;c=(c>>4)^t[c&15];c=(c>>4)^t[c&15];MP_PAD();}return c;
}
mp_u32 mp_crc_full(const void *p,mp_u32 n){return ~mp_crc(~0u,p,n);}
static int extension(mp_u8 *out,mp_u32 cap,mp_u32 *at,mp_u32 n)
{
    while(n>=255){if(*at>=cap)return 0;out[(*at)++]=255;n-=255;MP_PAD();}
    if(*at>=cap)return 0;out[(*at)++]=(mp_u8)n;return 1;
}
mp_u32 mp_pack(const mp_u8 *in,mp_u32 n,mp_u8 *out,mp_u32 cap,mp_u16 *dict)
{
    mp_u32 at=0,anchor=0,i=0,k; if(n>MP_PAGE)return 0;
    for(k=0;k<4096;k++){dict[k]=65535;MP_PAD();}
    while(i+12<=n){
        mp_u32 hash=(mp_read32(in+i)*2654435761u)>>20,previous=dict[hash];dict[hash]=(mp_u16)i;
        if(previous==65535 || !equal(in+previous,in+i,4)){i++;continue;}
        mp_u32 length=4,literals=i-anchor;
        while(i+length<n-5 && in[previous+length]==in[i+length]){length++;MP_PAD();}
        if(at>=cap)return 0;out[at++]=(mp_u8)((literals<15?literals:15)<<4)|((length-4)<15?length-4:15);
        if(literals>=15 && !extension(out,cap,&at,literals-15))return 0;
        if(literals>cap-at)return 0;copy(out+at,in+anchor,literals);at+=literals;
        if(cap-at<2)return 0;out[at++]=(mp_u8)(i-previous);out[at++]=(mp_u8)((i-previous)>>8);
        if(length>=19 && !extension(out,cap,&at,length-19))return 0;
        i+=length;anchor=i;
    }
    k=n-anchor;if(at>=cap)return 0;out[at++]=(mp_u8)((k<15?k:15)<<4);
    if(k>=15 && !extension(out,cap,&at,k-15))return 0;
    if(k>cap-at)return 0;copy(out+at,in+anchor,k);return at+k;
}
static int length(const mp_u8 *in,mp_u32 n,mp_u32 *at,mp_u32 *value,mp_u32 limit)
{
    mp_u32 b;do{if(*at>=n)return 0;b=in[(*at)++];if(*value>limit || b>limit-*value)return 0;*value+=b;MP_PAD();}while(b==255);return 1;
}
int mp_unpack(const mp_u8 *in,mp_u32 n,mp_u8 *out,mp_u32 wanted)
{
    mp_u32 i=0,o=0,last_match=0,had_match=0;if(wanted>MP_PAGE)return MP_LIMIT;
    while(i<n){
        mp_u32 token=in[i++],lit=token>>4,match=(token&15)+4,off,k;
        if(lit==15 && !length(in,n,&i,&lit,wanted))return MP_CORRUPT;
        if(lit>n-i || lit>wanted-o)return MP_CORRUPT;
        copy(out+o,in+i,lit);i+=lit;o+=lit;
        if(i==n)return o==wanted && (!had_match || (lit>=5 && last_match+12<=wanted))?MP_OK:MP_CORRUPT;
        if(n-i<2)return MP_CORRUPT;off=in[i]|(mp_u32)in[i+1]<<8;i+=2;
        if(!off || off>o)return MP_CORRUPT;
        if((token&15)==15 && !length(in,n,&i,&match,wanted))return MP_CORRUPT;
        if(match>wanted-o)return MP_CORRUPT;last_match=o;had_match=1;
        for(k=0;k<match;k++){out[o]=out[o-off];o++;MP_PAD();}
    }
    return MP_CORRUPT;
}
static int within(mp_u64 at,mp_u64 n,mp_u64 limit){return at<=limit && n<=limit-at;}
static int read_at(const mp_io *io,mp_u64 at,void *p,mp_u32 n,mp_u64 end)
{
    if(!within(at,n,end) || !within(at,n,io->limit))return MP_LIMIT;
    return io->read(io->context,at,p,n)?MP_IO:MP_OK;
}
static int write_at(const mp_io *io,mp_u64 at,const void *p,mp_u32 n)
{
    if(!within(at,n,io->limit))return MP_LIMIT;
    return io->write(io->context,at,p,n)?MP_IO:MP_OK;
}
void mp_wire_encode_entry(mp_u8 *e,const mp_section *s)
{
    mp_write32(e,s->type);mp_write32(e+4,s->version);mp_write32(e+8,s->flags);mp_write32(e+12,s->stride);
    mp_write64(e+16,s->count);mp_write64(e+24,s->bytes);mp_write64(e+32,s->offset);mp_write64(e+40,s->stored);
    mp_write32(e+48,s->crc);mp_write32(e+52,s->stored_crc);mp_write64(e+56,s->pages);
}
void mp_wire_decode_entry(const mp_u8 *e,mp_section *s)
{
    s->type=mp_read32(e);s->version=mp_read32(e+4);s->flags=mp_read32(e+8);s->stride=mp_read32(e+12);
    s->count=mp_read64(e+16);s->bytes=mp_read64(e+24);s->offset=mp_read64(e+32);s->stored=mp_read64(e+40);
    s->crc=mp_read32(e+48);s->stored_crc=mp_read32(e+52);s->pages=mp_read64(e+56);
}
int mp_wire_section_shape(const mp_section *s)
{
    mp_u64 value=s->count,product=0;mp_u32 multiplier=s->stride;
    if(!s->type || !s->version)return MP_CORRUPT;
    if(s->flags&~MP_MANDATORY)return MP_UNSUPPORTED;
    /* R5900 has no 64-bit multiply/divide. Shift/add with explicit overflow
     * checks preserves full-width counts without unsupported instructions. */
    while(multiplier){
        if(multiplier&1){if(value>~(mp_u64)0-product)return MP_CORRUPT;product+=value;}
        multiplier>>=1;if(multiplier){if(value>>63)return MP_CORRUPT;value<<=1;}MP_PAD();
    }
    if(s->stride && product!=s->bytes)return MP_CORRUPT;
    return MP_OK;
}
int mp_wire_read_header(const mp_u8 *h,mp_u64 bytes,mp_u64 limit,mp_info *info)
{
    if(bytes<MP_HEADER+MP_FOOTER)return MP_CORRUPT;if(bytes>limit)return MP_LIMIT;
    if(!equal(h,"MMIXPRF2",8) || mp_crc_full(h,124)!=mp_read32(h+124))return MP_CORRUPT;
    if(mp_read32(h+8)!=2 || mp_read32(h+12)!=MP_HEADER || mp_read32(h+72)!=MP_PAGE ||
       mp_read32(h+76) || !empty(h+84,40))return MP_UNSUPPORTED;
    if(empty(h+16,16) || !mp_read64(h+32) || mp_read64(h+48)!=bytes || mp_read64(h+56)!=MP_HEADER)return MP_CORRUPT;
    if(mp_read64(h+64)>(bytes-MP_FOOTER-MP_HEADER)/MP_ENTRY)return MP_CORRUPT;
    copy(info->uuid,h+16,16);info->generation=mp_read64(h+32);info->sequence=mp_read64(h+40);
    info->bytes=bytes;info->sections=mp_read64(h+64);return MP_OK;
}
void mp_wire_make_header(mp_u8 *h,const mp_info *info,mp_u32 directory_crc)
{
    zero(h,MP_HEADER);copy(h,"MMIXPRF2",8);mp_write32(h+8,2);mp_write32(h+12,MP_HEADER);copy(h+16,info->uuid,16);
    mp_write64(h+32,info->generation);mp_write64(h+40,info->sequence);mp_write64(h+48,info->bytes);mp_write64(h+56,MP_HEADER);
    mp_write64(h+64,info->sections);mp_write32(h+72,MP_PAGE);mp_write32(h+80,directory_crc);
    mp_write32(h+124,mp_crc_full(h,124));
}
int mp_wire_check_footer(const mp_u8 *h,const mp_u8 *f)
{
    return equal(f,"MMIXEND2",8) && equal(f+8,h+16,16) && equal(f+24,h+32,16) &&
       mp_read64(f+40)==mp_read64(h+48) && mp_read32(f+48)==mp_read32(h+124) && mp_read32(f+52)==mp_read32(h+80) &&
       !mp_read32(f+56) && mp_crc_full(f,60)==mp_read32(f+60);
}
void mp_wire_make_footer(mp_u8 *f,const mp_u8 *h)
{
    zero(f,MP_FOOTER);copy(f,"MMIXEND2",8);copy(f+8,h+16,16);copy(f+24,h+32,16);mp_write64(f+40,mp_read64(h+48));
    mp_write32(f+48,mp_read32(h+124));mp_write32(f+52,mp_read32(h+80));mp_write32(f+60,mp_crc_full(f,60));
}
#ifndef MP_ASYNC_ONLY
int mp_check(const mp_io *io,mp_u64 bytes,mp_info *info,const mp_visitor *v,mp_work *w)
{
    mp_u8 h[MP_HEADER],f[MP_FOOTER],e[MP_ENTRY],p[MP_PAGE_HEADER];mp_section s;mp_info parsed;
    mp_u64 count,index,next,end,offset,raw,page;mp_u32 dc=~0u,rc,sc,previous=0;int r;
    if(!io || !io->read || !io->limit || !info || !w)return MP_CALLBACK;
    if(bytes<MP_HEADER+MP_FOOTER)return MP_CORRUPT;if(bytes>io->limit)return MP_LIMIT;
    if((r=read_at(io,0,h,MP_HEADER,bytes)))return r;
    if((r=mp_wire_read_header(h,bytes,io->limit,&parsed)))return r;
    count=mp_read64(h+64);end=bytes-MP_FOOTER;
    if(count>(end-MP_HEADER)/MP_ENTRY)return MP_CORRUPT;next=MP_HEADER+count*MP_ENTRY;
    if((r=read_at(io,end,f,MP_FOOTER,bytes)))return r;
    if(!mp_wire_check_footer(h,f))return MP_CORRUPT;
    for(index=0;index<count;index++){
        if((r=read_at(io,MP_HEADER+index*MP_ENTRY,e,MP_ENTRY,end)))return r;
        dc=mp_crc(dc,e,MP_ENTRY);mp_wire_decode_entry(e,&s);
        if((r=mp_wire_section_shape(&s)))return r;
        if(s.type<=previous || s.offset!=next || !within(s.offset,s.stored,end) ||
           s.pages!=s.bytes/MP_PAGE+(s.bytes%MP_PAGE!=0))return MP_CORRUPT;
        previous=s.type;next=s.offset+s.stored;
        if(v && v->section && (r=v->section(v->context,index,&s)))return r;
        offset=s.offset;raw=0;rc=sc=~0u;
        for(page=0;page<s.pages;page++){
            mp_u32 codec,n,k,wanted=(s.bytes-raw)>MP_PAGE?MP_PAGE:(mp_u32)(s.bytes-raw);
            if((r=read_at(io,offset,p,MP_PAGE_HEADER,next)))return r;offset+=MP_PAGE_HEADER;sc=mp_crc(sc,p,MP_PAGE_HEADER);
            codec=mp_read32(p);n=mp_read32(p+4);k=mp_read32(p+8);
            if(n!=wanted || !k || k>n || mp_read64(p+16)!=page || (codec==0 && k!=n))return MP_CORRUPT;
            if(codec>1)return MP_UNSUPPORTED;
            if((r=read_at(io,offset,w->packed,k,next)))return r;offset+=k;sc=mp_crc(sc,w->packed,k);
            if(codec){if((r=mp_unpack(w->packed,k,w->raw,n)))return r;}else copy(w->raw,w->packed,n);
            if(mp_crc_full(w->raw,n)!=mp_read32(p+12))return MP_CORRUPT;rc=mp_crc(rc,w->raw,n);
            if(v && v->page && (r=v->page(v->context,index,&s,raw,w->raw,n)))return r;raw+=n;
        }
        if(offset!=next || raw!=s.bytes || ~rc!=s.crc || ~sc!=s.stored_crc)return MP_CORRUPT;
    }
    /* An inactive MC file may retain an earlier larger allocation. Only a
     * verified zero tail may bridge section data to the final commit footer. */
    while(next<end){mp_u32 n=end-next>MP_PAGE?MP_PAGE:(mp_u32)(end-next);
        if((r=read_at(io,next,w->raw,n,end)))return r;if(!empty(w->raw,n))return MP_CORRUPT;next+=n;}
    if(~dc!=mp_read32(h+80))return MP_CORRUPT;
    copy(info->uuid,h+16,16);info->generation=mp_read64(h+32);info->sequence=mp_read64(h+40);info->bytes=bytes;info->sections=count;
    return MP_OK;
}
int mp_write(const mp_io *io,const mp_info *in,const mp_source *source,mp_info *out,mp_work *w)
{
    mp_u8 h[MP_HEADER],f[MP_FOOTER],e[MP_ENTRY],p[MP_PAGE_HEADER];mp_u64 index,at,offset;mp_u32 previous=0,dc=~0u;int r;mp_info result;
    if(!io || !io->write || !io->sync || !io->limit || !in || !source || !source->read || !source->describe || !out || !w)return MP_CALLBACK;
    if(empty(in->uuid,16) || !in->generation)return MP_CORRUPT;
    if(io->limit<MP_HEADER+MP_FOOTER || in->sections>(io->limit-MP_HEADER-MP_FOOTER)/MP_ENTRY)return MP_LIMIT;
    at=MP_HEADER+in->sections*MP_ENTRY;zero(h,sizeof h);
    if((r=write_at(io,0,h,MP_HEADER)))return r;
    for(index=0;index<in->sections;index++){
        mp_section s;if(source->describe(source->context,index,&s))return MP_CALLBACK;
        if((r=mp_wire_section_shape(&s)))return r;if(s.type<=previous)return MP_CORRUPT;previous=s.type;
        s.offset=at;s.pages=0;s.crc=s.stored_crc=~0u;
        for(offset=0;offset<s.bytes;){
            mp_u32 n=s.bytes-offset>MP_PAGE?MP_PAGE:(mp_u32)(s.bytes-offset),k,codec=1;const mp_u8 *payload;
            if(source->read(source->context,index,offset,w->raw,n))return MP_CALLBACK;
            s.crc=mp_crc(s.crc,w->raw,n);k=mp_pack(w->raw,n,w->packed,sizeof w->packed,w->dictionary);
            if(!k || k>=n){codec=0;k=n;payload=w->raw;}else payload=w->packed;
            mp_write32(p,codec);mp_write32(p+4,n);mp_write32(p+8,k);mp_write32(p+12,mp_crc_full(w->raw,n));mp_write64(p+16,s.pages);
            if((r=write_at(io,at,p,MP_PAGE_HEADER)))return r;at+=MP_PAGE_HEADER;
            if((r=write_at(io,at,payload,k)))return r;at+=k;
            s.stored_crc=mp_crc(s.stored_crc,p,MP_PAGE_HEADER);s.stored_crc=mp_crc(s.stored_crc,payload,k);
            s.pages++;offset+=n;
        }
        s.crc=~s.crc;s.stored_crc=~s.stored_crc;s.stored=at-s.offset;mp_wire_encode_entry(e,&s);dc=mp_crc(dc,e,sizeof e);
        if((r=write_at(io,MP_HEADER+index*MP_ENTRY,e,sizeof e)))return r;
    }
    if(!within(at,MP_FOOTER,io->limit))return MP_LIMIT;
    if(in->bytes>at+MP_FOOTER){
        mp_u64 stop=in->bytes-MP_FOOTER;if(in->bytes>io->limit)return MP_LIMIT;zero(w->raw,MP_PAGE);
        while(at<stop){mp_u32 n=stop-at>MP_PAGE?MP_PAGE:(mp_u32)(stop-at);
            if((r=write_at(io,at,w->raw,n)))return r;at+=n;}
    }
    copy(result.uuid,in->uuid,16);result.generation=in->generation;result.sequence=in->sequence;
    result.sections=in->sections;result.bytes=at+MP_FOOTER;
    mp_wire_make_header(h,&result,~dc);
    if((r=write_at(io,0,h,MP_HEADER)))return r;
    if(io->sync(io->context))return MP_IO; /* Never publish footer before this succeeds. */
    mp_wire_make_footer(f,h);
    if((r=write_at(io,at,f,sizeof f)))return r;if(io->sync(io->context))return MP_IO;
    copy(out->uuid,in->uuid,16);out->generation=in->generation;out->sequence=in->sequence;
    out->sections=in->sections;out->bytes=at+MP_FOOTER;return MP_OK;
}
#endif
