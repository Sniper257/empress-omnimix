#ifndef MEGAMIX_USB_MEMORY_H
#define MEGAMIX_USB_MEMORY_H
/* Combined storage + up to4HID interfaces: root/hub/mass/4devices fit8nodes;
 * mass3 + HID12 + hub2 endpoints fit24. Preserve4KiB descriptor capacity,
 * original30/36priorities, USB intervals and actual audio/stream buffers. */
#define USB_MEMORY_HOST_ARGS "reportd=0\0conf=4128\0dev=8\0ed=24\0gtd=48\0itd=2\0ioreq=32\0hub=1\0port=4"
#define USB_MEMORY_STAGING 2048
#endif
