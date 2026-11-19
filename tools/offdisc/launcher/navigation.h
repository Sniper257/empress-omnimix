#ifndef MEGAMIX_LAUNCHER_NAVIGATION_H
#define MEGAMIX_LAUNCHER_NAVIGATION_H
/* Launcher-only input mixing. USB player ownership belongs to game handoff;
 * it must never gate physical menu input. DualShock pads retain their own
 * D-pad/X controls even when a saved digital KOC layout is selected. */
static unsigned launcher_physical_navigation(unsigned raw,unsigned digital_koc,unsigned dualshock)
{
    if(!digital_koc || dualshock)return raw;
    unsigned b=raw&(PAD_START|PAD_SELECT|PAD_CIRCLE|PAD_TRIANGLE|PAD_SQUARE);
    if(raw&PAD_LEFT)b|=PAD_DOWN;
    if(raw&PAD_L1)b|=PAD_UP;
    if(raw&PAD_L2)b|=PAD_CROSS;
    return b;
}
static unsigned launcher_usb_navigation(unsigned logical)
{
    unsigned b=0;
    if(logical&1)b|=PAD_DOWN;
    if(logical&2)b|=PAD_UP;
    if(logical&(1u<<5))b|=PAD_CROSS;
    if(logical&(1u<<MK_START))b|=PAD_START;
    if(logical&(1u<<MK_SELECT))b|=PAD_SELECT;
    return b;
}
#endif
