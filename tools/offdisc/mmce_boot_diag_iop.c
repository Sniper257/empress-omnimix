#include "megamix_diag.h"
static volatile unsigned *progress;
void megamix_diag_bind(int argc,char **argv)
{
    progress=0;
    if(argc<1 || argc>32 || !argv)return;
    for(int n=0;n<argc;n++){
        const char *p=argv[n];const char *tag="trace=";unsigned address=0;
        if(!p)continue;
        int i;for(i=0;i<6 && p[i] && p[i]==tag[i];i++){}
        if(i!=6)continue;
        p+=6;
        for(i=0;i<8;i++){
            unsigned v=(unsigned char)p[i];
            if(v>='0' && v<='9')v-='0';
            else if(v>='a' && v<='f')v=v-'a'+10;
            else break;
            address=(address<<4)|v;
        }
        if(i==8 && !p[8] && address>=0x1000 && address<=0x200000-64 && !(address&15)){
            progress=(volatile unsigned *)address;
            if(progress[0]!=0x4d545243)progress=0;
        }
    }
}
void megamix_diag(unsigned stage,unsigned a,unsigned b)
{
    if(!progress)return;
    progress[1]++; /* Odd: a reader must not accept a partial update. */
    __asm__ volatile("":::"memory");
    progress[2]=stage;progress[3]=a;progress[4]=b;
    __asm__ volatile("":::"memory");
    progress[1]++;
}
void megamix_diag_end(void){progress=0;}
