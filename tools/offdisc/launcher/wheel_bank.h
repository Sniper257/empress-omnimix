#ifndef MEGAMIX_WHEEL_BANK_H
#define MEGAMIX_WHEEL_BANK_H
#include "wheel_cache.h"
/* Launcher-only construction. The index lives in launcher heap, never the
 * game's reserved tables. No song/style limit beyond the caller's count. */
static FILE *wheel_file;
static u32 *wheel_offsets,wheel_count,wheel_pos;
static int wheel_pad(u32 n){
 static const u8 zero[512]={0};
 while(n){u32 k=n>sizeof zero?sizeof zero:n;if(fwrite(zero,1,k,wheel_file)!=k)return -1;n-=k;}
 return 0;
}
static int wheel_begin(u32 count){
 wheel_file=0;wheel_offsets=0;wheel_count=count;wheel_pos=0;
 if(!count)return 0;
 if(count>(0x7fffffffu-16-WC_PAGE)/4)return -1;
 wheel_offsets=malloc(count*4);if(!wheel_offsets)return -1;
 memset(wheel_offsets,255,count*4);
 wheel_file=fopen(game_path("cache/wheel_build.tmp"),"wb");
 if(!wheel_file){free(wheel_offsets);wheel_offsets=0;return -1;}
 wheel_pos=(16+count*4+WC_PAGE-1)&~(WC_PAGE-1);
 if(wheel_pad(wheel_pos)){fclose(wheel_file);wheel_file=0;free(wheel_offsets);wheel_offsets=0;return -1;}
 return 0;
}
static int wheel_add(u32 k,const u8 *data,u32 bytes){
 if(!wheel_file||k>=wheel_count||bytes>WC_PAGE||bytes<12||memcmp(data,"SPR1",4))return -1;
 u32 left=WC_PAGE-(wheel_pos&(WC_PAGE-1));
 if(bytes>left){if(wheel_pad(left))return -1;wheel_pos+=left;}
 if(wheel_pos>0x7fffffffu-bytes)return -1;
 wheel_offsets[k]=wheel_pos;
 if(fwrite(data,1,bytes,wheel_file)!=bytes)return -1;
 wheel_pos+=bytes;return 0;
}
static int wheel_finish(void){
 if(!wheel_file)return wheel_count?-1:0;
 int bad=0;u32 pad=(-wheel_pos)&(WC_PAGE-1);
 if(wheel_pad(pad))bad=1;wheel_pos+=pad;
 u32 h[4]={WC_MAGIC,WC_VERSION,wheel_count,WC_PAGE};
 if(fseek(wheel_file,0,SEEK_SET)||fwrite(h,1,16,wheel_file)!=16||fwrite(wheel_offsets,4,wheel_count,wheel_file)!=wheel_count)bad=1;
 if(fclose(wheel_file))bad=1;wheel_file=0;free(wheel_offsets);wheel_offsets=0;
 return bad?-1:0;
}
#endif
