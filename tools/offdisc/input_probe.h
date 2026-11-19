#ifndef MEGAMIX_INPUT_PROBE_H
#define MEGAMIX_INPUT_PROBE_H
/* Debug-only ABI. Raw EE Count ticks, no assumed timer frequency. */
#define INPUT_PROBE_RECORD_BYTES 48u
#define INPUT_PROBE_MIN_RECORDS 8000u
#define INPUT_PROBE_HEADER_BYTES 128u
typedef struct {
 unsigned tick,sequence[2],read_ticks[2];
 unsigned short buttons[2];
 unsigned char length[2],state[2]; /* 0 skipped,255 failed,1..128 packet bytes */
 unsigned held,pressed,chart_frame,storage_reads,sampler_ticks;
} input_probe_record;
_Static_assert(sizeof(input_probe_record)==INPUT_PROBE_RECORD_BYTES,"input log wire record");
/* First five words are filled by the launcher; remaining data is runtime-only. */
typedef struct {
 unsigned schema,mode,buffer,capacity,path;
 unsigned running,ready,count,dropped,exports,last_error,serial,blocked;
 unsigned metadata[8];
 unsigned char chart_id[16];
 unsigned autoplay; /* latched native autoplay side mask; no pad records */
} input_probe_config;
#endif
