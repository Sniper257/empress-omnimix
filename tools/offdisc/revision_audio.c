/* Fast, bounded PS-ADPCM decoding for the offline revision audit only. */
#include <stdint.h>
void revision_adpcm(const uint8_t *src, int frames, int16_t *dst)
{
    static const int c[5][2]={{0,0},{60,0},{115,-52},{98,-55},{122,-60}};
    int s1=0,s2=0;
    for(int i=0;i<frames;i++){
        const uint8_t *p=src+i*16;
        int pred=p[0]>>4,shift=p[0]&15;
        if(pred>4)pred=0;
        for(int j=0;j<28;j++){
            if(p[1]==7){*dst++=0;continue;}
            int nib=(p[2+j/2]>>((j&1)*4))&15;
            if(nib>=8)nib-=16;
            int residual=(nib*4096)>>shift;
            int sample=(int)(residual+(s1*c[pred][0]+s2*c[pred][1])/64.0);
            if(sample>32767)sample=32767;
            if(sample< -32768)sample= -32768;
            *dst++=sample;s2=s1;s1=sample;
        }
    }
}
