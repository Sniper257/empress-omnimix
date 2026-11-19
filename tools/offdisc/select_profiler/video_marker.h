#ifndef MMIX_VIDEO_MARKER_H
#define MMIX_VIDEO_MARKER_H
/* Shared, renderer-independent marker. No allocation, SDK, timers or I/O.
 * Call once per submitted render, using the SAME raw clock as its trace.
 * Poll at least once per 32-bit counter period; a complete unseen wrap cannot
 * be recovered from a 32-bit source. Calibration is supplied by the caller. */
typedef struct {
 unsigned session,frame,last,high,start,clock_hz;
} msp_video_marker;
#define MSP_MARKER_TEXT 28u
static void msp_marker_begin(msp_video_marker *v,unsigned session,unsigned tick,unsigned hz){
 *v=(msp_video_marker){session,0,tick,0,tick,hz};
}
static void msp_marker_sample(msp_video_marker *v,unsigned tick){
 if(tick<v->last)v->high++;
 v->last=tick;v->frame++;
}
static unsigned msp_marker_ms(const msp_video_marker *v){
 if(!v->clock_hz)return ~0u;
 unsigned long long ticks=((unsigned long long)v->high<<32)+v->last-v->start;
#ifdef MSP_MARKER_DIV
 return MSP_MARKER_DIV(ticks*1000,v->clock_hz);
#else
 return (unsigned)(ticks*1000/v->clock_hz);
#endif
}
static void msp_marker_decimal(char *out,unsigned value,unsigned digits){
 while(digits){out[--digits]=(char)('0'+value%10);value/=10;}
}
static void msp_marker_text(const msp_video_marker *v,char out[MSP_MARKER_TEXT]){
 static const char hex[]="0123456789ABCDEF";
 out[0]='S';for(unsigned i=0;i<8;i++)out[i+1]=hex[(v->session>>(28-i*4))&15];
 out[9]=' ';unsigned ms=msp_marker_ms(v);
 if(ms==~0u){out[10]=out[11]=out[12]=out[14]=out[15]=out[16]='-';}
 else {msp_marker_decimal(out+10,ms/1000,3);msp_marker_decimal(out+14,ms%1000,3);}
 out[13]='.';out[17]=' ';out[18]='F';msp_marker_decimal(out+19,v->frame,8);out[27]=0;
}
#endif
