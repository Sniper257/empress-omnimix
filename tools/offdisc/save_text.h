#ifndef MEGAMIX_SAVE_TEXT_H
#define MEGAMIX_SAVE_TEXT_H
/* Included after fast_slow.c in the native bridge. Additional SAVE warnings
 * use the same verified nine-argument text adapter and loaded-FONT guard.
 * Startup autoload can invoke the MC controller before FONT452 is available.
 * Native MC messages remain authoritative when the optional font is absent. */
static void ps_safe_text(const char *text)
{
    if(fs_font_ready())(void)fs_text(text,0,110,362,100);
}
#define ps_text(text) ps_safe_text(text)
#endif
