#ifndef MMIX_SELECT_FRAME_H
#define MMIX_SELECT_FRAME_H
/* No OS dependencies: exercised against a separate Python decoder. */
static inline unsigned msp_crc(const void *v,unsigned n){const unsigned char *p=v;unsigned c=~0u;while(n--){c^=*p++;for(unsigned i=0;i<8;i++)c=c&1?(c>>1)^0xedb88320u:c>>1;}return ~c;}
static inline unsigned msp_le32(const unsigned char *p){return (unsigned)p[0]|(unsigned)p[1]<<8|(unsigned)p[2]<<16|(unsigned)p[3]<<24;}
static inline void msp_be16(unsigned char *p,unsigned n){p[0]=n>>8;p[1]=n;}
static inline void msp_be32(unsigned char *p,unsigned n){p[0]=n>>24;p[1]=n>>16;p[2]=n>>8;p[3]=n;}
static inline unsigned msp_u16(const unsigned char *p){return (unsigned)p[0]<<8|p[1];}
static inline unsigned msp_u32(const unsigned char *p){return (unsigned)p[0]<<24|(unsigned)p[1]<<16|(unsigned)p[2]<<8|p[3];}
static inline unsigned msp_sum(const unsigned char *p,unsigned n){unsigned s=0;while(n>1){s+=msp_u16(p);p+=2;n-=2;}if(n)s+=(unsigned)*p<<8;while(s>>16)s=(s&65535)+(s>>16);return (~s)&65535;}
static inline unsigned msp_udp(unsigned char *out,const unsigned char *mac,const unsigned char *peer,unsigned local,unsigned ip,const unsigned char *data,unsigned bytes){
 memcpy(out,peer,6);memcpy(out+6,mac,6);msp_be16(out+12,0x800);
 unsigned char *p=out+14;memset(p,0,28);p[0]=0x45;msp_be16(p+2,28+bytes);p[8]=64;p[9]=17;
 msp_be32(p+12,local);msp_be32(p+16,ip);msp_be16(p+10,msp_sum(p,20));
 msp_be16(p+20,MSP_PORT);msp_be16(p+22,MSP_PORT);msp_be16(p+24,8+bytes);
 /* IPv4 UDP checksum zero is legal; capture envelope has two CRCs. */
 memcpy(p+28,data,bytes);return 42+bytes;
}
static inline unsigned msp_arp(unsigned char *out,const unsigned char *mac,const unsigned char *dest,unsigned local,unsigned peer,unsigned op){
 memcpy(out,dest,6);memcpy(out+6,mac,6);msp_be16(out+12,0x806);unsigned char *p=out+14;
 memset(p,0,46);msp_be16(p,1);msp_be16(p+2,0x800);p[4]=6;p[5]=4;msp_be16(p+6,op);
 memcpy(p+8,mac,6);msp_be32(p+14,local);if(op==2)memcpy(p+18,dest,6);msp_be32(p+24,peer);return 60;
}
#endif
