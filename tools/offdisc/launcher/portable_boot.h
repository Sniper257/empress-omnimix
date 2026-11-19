#ifndef MM_PORTABLE_BOOT_H
#define MM_PORTABLE_BOOT_H
int portable_boot_prepare(int game);
const char *portable_boot_status(void);
void portable_boot_set_reporter(void (*fn)(const char *));
void portable_boot_note(const char *fmt,...);
int portable_boot_flush_log(void);
int portable_game_id(const char *id);
#endif
