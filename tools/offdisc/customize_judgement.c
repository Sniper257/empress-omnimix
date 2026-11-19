/* Only the native visual producers are interposed. Hit timing, counters,
 * clocks and native update/age decrements are left intact. */
unsigned long long cs_judge_visible_call(FS_ARGS){
 if(!cs_initialized)return CS_NATIVE(0x16a400,unsigned long long (*)(FS_ARGS))(FS_PASS);
 return skin.field[0].judge||skin.field[1].judge;
}
static unsigned long long cs_judge_call(u32 at,FS_ARGS){
 u32 hud=fs_word(0x2ad5e8),side=(u32)b;
 if(!cs_initialized||!hud||side>1)return CS_NATIVE(at,unsigned long long (*)(FS_ARGS))(FS_PASS);
 if(!skin.field[side].judge)return 0;
 short before=*(short *)(unsigned long)(hud+0x10c);
 *(short *)(unsigned long)(hud+0x10c)=cs_judge_y(hud,side);
 unsigned long long result=CS_NATIVE(at,unsigned long long (*)(FS_ARGS))(FS_PASS);
 *(short *)(unsigned long)(hud+0x10c)=before;return result;
}
unsigned long long cs_judge_bad(FS_ARGS){return cs_judge_call(0x1fe5a0,FS_PASS);}
unsigned long long cs_judge_good(FS_ARGS){return cs_judge_call(0x1fe690,FS_PASS);}
unsigned long long cs_judge_pgreat(FS_ARGS){return cs_judge_call(0x1fe8f0,FS_PASS);}

extern unsigned long long cs_score_body(FS_ARGS);
unsigned long long cs_score_call(FS_ARGS){
 if(PLAY_PREVIEW_ACTIVE())return CS_NATIVE(0x204140,u32 (*)(u32))((u32)a);
 return cs_score_body(FS_PASS);
}
