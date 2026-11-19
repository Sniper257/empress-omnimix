#ifndef MEGAMIX_USB_RUNTIME_LAYOUT_H
#define MEGAMIX_USB_RUNTIME_LAYOUT_H
/* Native USB DMA storage belongs to the selected runtime, never to a compiler
 * address header. Used after BOTH cold builds and prepared-image restores. */
typedef struct {unsigned config,frames;} mui_runtime_layout;
static int mui_layout_number(char **p,unsigned base,unsigned *out)
{
    char *s=*p;unsigned value=0,n=0;
    while(*s==' '||*s=='\t')s++;
    if(base==16 && s[0]=='0' && (s[1]=='x'||s[1]=='X'))s+=2;
    for(;;s++){
        unsigned d=*s>='0'&&*s<='9'?(unsigned)(*s-'0'):*s>='a'&&*s<='f'?(unsigned)(*s-'a'+10):*s>='A'&&*s<='F'?(unsigned)(*s-'A'+10):99;
        if(d>=base)break;
        if(value>(0xffffffffu-d)/base)return 0;
        value=value*base+d;n++;
    }
    if(!n || (*s && *s!=' '&&*s!='\t'&&*s!='\r'))return 0;
    *p=s;*out=value;return 1;
}
static int mui_layout_end(char *s)
{while(*s==' '||*s=='\t'||*s=='\r')s++;return !*s;}
static int mui_layout_parse(char *text,unsigned length,unsigned lo,unsigned bytes,unsigned version,mui_runtime_layout *out)
{
    mui_runtime_layout found={0,0};unsigned seen=0,base_seen=0,base=0;
    out->config=out->frames=0;
    if(!text || !length || memchr(text,0,length) || bytes<256 || lo>0xffffffffu-bytes)return -1;
    char *p=text,*l;
    while((l=line(&p))){
        while(*l==' '||*l=='\t')l++;
        char *q;
        if(!strncmp(l,"base",4) && (l[4]==' '||l[4]=='\t')){
            q=l+4;if(base_seen++ || !mui_layout_number(&q,16,&base)||!mui_layout_end(q))return -1;
        }
        if(!strncmp(l,"usb_hid",7) && (l[7]==' '||l[7]=='\t')){
            unsigned abi;q=l+7;
            if(seen++ || !mui_layout_number(&q,10,&abi)||abi!=version ||
               !mui_layout_number(&q,16,&found.config)||!mui_layout_number(&q,16,&found.frames)||!mui_layout_end(q))return -1;
        }
    }
    if(seen!=1 || base_seen!=1 || base!=lo || (found.config&3) || (found.frames&127) ||
       found.config<lo || found.frames<lo || found.config>lo+bytes-20 || found.frames>lo+bytes-256 ||
       (found.config<found.frames+256 && found.frames<found.config+20))return -1;
    *out=found;return 0;
}
#endif
