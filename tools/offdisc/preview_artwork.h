/* Only the private preview owns these heap words. One graphics-worker request
 * at a time, two records per artwork type, and one unpublished replacement. */
struct pv_art_state {
 /* Jobs: covers0/1, shared frame2, notes3/4, explosions5/6. Custom
  * records are reserved lazily, reused, and never exceed the native16 slots. */
 u32 attached,slots,native_slot[2],wanted[7],applied[7],delay[7],serial[7];
 u32 owner,cover,sequence,turn,error,finished;
 volatile u32 state; /*0 idle,1 queued/working,2 decoded,3 failed */
 u32 frame_slot,objects[6];
 u32 custom_slot[2][2];
 u32 staged[0x228/4];
};
