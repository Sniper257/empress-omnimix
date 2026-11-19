#ifndef MEGAMIX_USB_BOOT_H
#define MEGAMIX_USB_BOOT_H
/* Fixed deployment path for the first physical USB bench. */
#define USB_BENCH_ROOT "mass:/MEGAMIX/"
#ifdef USB_RESIDENT_TRACE
#ifdef USB_IOP_BUDGET
#ifdef USB_UNUSED_RING
#define USB_TRACE_REV "R143 NO DISC RING"
#elif defined(USB_AUDIO_RESERVE)
#define USB_TRACE_REV "R142 RESERVE"
#else
#define USB_TRACE_REV "R142 CONTROL"
#endif
#elif defined(USB_MEMORY_REDUCED)
#define USB_TRACE_REV "R141 TRIM"
#else
#define USB_TRACE_REV "R141 CTRL"
#endif
int usb_boot_preload_native(void);
#elif defined(USB_ENTRY_PROBE)
#define USB_TRACE_REV "R135"
int usb_boot_preload_native(void);
int usb_entry_probe_install(unsigned *entry);
#elif defined(USB_NATIVE_PRELOAD)
#define USB_TRACE_REV "R134"
int usb_boot_preload_native(void);
#else
#define USB_TRACE_REV "R133"
#endif
int usb_boot_prepare(int game_handoff);
const char *usb_boot_status(void);
void usb_boot_set_reporter(void (*reporter)(const char *));
void usb_boot_note(const char *fmt,...);
int usb_boot_trace_active(void);
int usb_boot_flush_log(void);
void usb_journal_configure(unsigned,unsigned,unsigned,unsigned);
int usb_journal_start(void);
#endif
