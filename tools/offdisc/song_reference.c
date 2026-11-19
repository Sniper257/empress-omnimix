/* One alias-to-catalog boundary for scores, metadata, graphics and scenes.
 * Special rows and the -1 sentinel never name playable/resource-owning songs. */
s32 song_catalog_row(s32 song)
{
    u32 id;
    if(song < -128 || song > 254 || song==-1)return -1;
    id=(u8)song;
    if(id<E16_RESOURCE_SONGS)return id;
    LOOP_PAD();
    if(id<FIRST || (id>=124 && id<=127))return -1;
    if(sdb.slot_of[id]==NOSLOT || sdb.slot_of[id]>=sdb.n_new)return -1;
    return FIRST+sdb.slot_of[id];
}
