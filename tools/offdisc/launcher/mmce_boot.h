#ifndef MEGAMIX_MMCE_BOOT_H
#define MEGAMIX_MMCE_BOOT_H
/* Physical memory-card slot 1; a separate homebrew ID, never retail EMPRESS. */
#define MMCE_BENCH_ROOT "mmce0:/MEGAMIX/"
#define MMCE_GAME_ID "PIZD-00000"
#ifdef MMCE_EXIT_TRACE
#define MMCE_TRACE_REV "R15"
#elif defined(MMCE_RETAIL_BRIDGE)
#define MMCE_TRACE_REV "R7"
#elif defined(MMCE_DIAG)
#define MMCE_TRACE_REV "R2"
#else
#define MMCE_TRACE_REV "R1"
#endif
int mmce_boot_prepare(int game_handoff);
/* Set only while no MC RPC/file is active. Empty ID leaves the current VMC.
 * The launcher must restart before changing ID after its MC browser starts. */
int mmce_boot_set_game_id(const char *id);
int mmce_boot_select_game_card(void);
const char *mmce_boot_status(void);
void mmce_boot_set_reporter(void (*reporter)(const char *));
void mmce_boot_note(const char *fmt,...);
int mmce_boot_trace_active(void);
int mmce_boot_flush_log(void);
void mmce_boot_set_debug(int enabled);
/* Keep RAM control from replacing the failed Ethernet run's checkpoints. */
void mmce_boot_set_profile_log(int mode);
#endif
