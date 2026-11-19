#ifndef MMIX_SELECT_CONFIG_H
#define MMIX_SELECT_CONFIG_H
#define MSP_SELECT_ABI 2u /* dormant handoff; native START only after freeze */
typedef struct {unsigned abi,enabled,seconds,osd,peer,local,gate,token;
 unsigned count_lo,count_hi,bus_lo,bus_hi,samples,bus_hz,video,reserved;} msp_config;
#endif
