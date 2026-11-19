#ifndef MEGAMIX_WHEEL_CACHE_H
#define MEGAMIX_WHEEL_CACHE_H
/* WHL1: LE header magic/version/count/page_bytes, then count absolute u32
 * offsets (~0 for absent). Payload starts on a page boundary; SPR1 records
 * never cross pages. Cache identity covers the complete padded file. */
#define WC_MAGIC 0x314c4857u
#define WC_VERSION 1u
#define WC_PAGE 32768u
#define WC_PAGES 4u
typedef struct {unsigned version,bank;} wheel_cache_config;
#endif
