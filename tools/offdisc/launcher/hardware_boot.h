#ifndef MEGAMIX_HARDWARE_BOOT_H
#define MEGAMIX_HARDWARE_BOOT_H
/* Shared launcher diagnostics; backend bootstrap implementations stay separate. */
#if defined(PORTABLE_LAUNCHER)
#define HARDWARE_BENCH
#include "deployment.h"
#include "portable_boot.h"
#define DEV dl_game
#define SETTINGS_ROOT dl_config
#define BENCH_LABEL "storage"
#define bench_boot_prepare portable_boot_prepare
#define bench_boot_status portable_boot_status
#define bench_boot_set_reporter portable_boot_set_reporter
#define bench_boot_note portable_boot_note
#define bench_boot_flush_log portable_boot_flush_log
#elif defined(USB_BENCH) && defined(MMCE_BENCH)
#error Select one hardware backend
#elif defined(MMCE_BENCH)
#define HARDWARE_BENCH
#include "mmce_boot.h"
#define DEV MMCE_BENCH_ROOT
#define BENCH_LABEL "MMCE slot 1"
#define bench_boot_prepare mmce_boot_prepare
#define bench_boot_status mmce_boot_status
#define bench_boot_set_reporter mmce_boot_set_reporter
#define bench_boot_note mmce_boot_note
#define bench_boot_flush_log mmce_boot_flush_log
#elif defined(USB_BENCH)
#define HARDWARE_BENCH
#include "usb_boot.h"
#define DEV USB_BENCH_ROOT
#define BENCH_LABEL "USB"
#define bench_boot_prepare usb_boot_prepare
#define bench_boot_status usb_boot_status
#define bench_boot_set_reporter usb_boot_set_reporter
#define bench_boot_note usb_boot_note
#define bench_boot_flush_log usb_boot_flush_log
#else
#define DEV "host:"
#endif
#endif

#ifndef PORTABLE_LAUNCHER
#define game_path(rel) DEV rel
#define dl_settings() DEV "launcher.cfg"
#define SETTINGS_ROOT DEV
#endif
