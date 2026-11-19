/* Native FREE-mode preferences, edited only under the song-select modal.
 * Do not emulate title commands or alter chart IDs/arrangement/score paths. */
struct nm_state {u32 special,regul,gambol,random[2],mirror[2],hard[2];};
#ifndef NATIVE_MODIFIERS_TEST
#define nm_preferences() (((u8 *(*)(void))0x1686d0)())
#define nm_style() (((u32 (*)(void *))0x1959a0)(((void *(*)(void))0x1686f0)()))
#define nm_options(p,m) (((u8 *(*)(u8 *,u32))0x198b30)(p,m))
#define nm_random_plus(s,v) (((void (*)(u32,u32))0x20e230)(s,v))
#define nm_mirror_plus(s,v) (((void (*)(u32,u32))0x20dfa0)(s,v))
#define nm_hazard(s,v) (((void (*)(u32,u32))0x20e510)(s,v))
#endif
static u32 nm_available(void)
{
    /* The five letter commands are FREE only. This first menu slice exposes
     * their companion native variants there too; original options elsewhere
     * remain untouched. No override for courses, STANDARD or test scenes. */
    /* 1959a0 is PLAY STYLE (SP1/SP2/DP1/DP2/battle), not game mode.
     * The original hidden commands gate FREE on select-state+38 alone. */
    return MS_STATE && I32(MS_STATE+0x38)==2 && nm_style()<6 && nm_preferences()!=0;
}
static void nm_capture(struct nm_state *s)
{
    u8 *p=nm_preferences(),*o=nm_options(p,nm_style());
    s->special=p[0x1c4]?1:p[0x1c5]?2:0;s->regul=p[0x1c6]!=0;
    s->gambol=p[0x1c8]?1:p[0x1c9]?2:0;
    for(u32 i=0;i<2;i++){
        s->random[i]=*(u32 *)(o+i*0x38+4);s->mirror[i]=*(u32 *)(o+i*0x38+8);
        s->hard[i]=*(u32 *)(o+i*0x38+0x18);LOOP_PAD();
    }
}
static void nm_store(const struct nm_state *s)
{
    u8 *p=nm_preferences(),*o=nm_options(p,nm_style());
    /* Verified native setters198b20/198b00/198ae0/198aa0/198a80 and
     * 1d80f0/1d8130/1d8070 are exactly these byte/word stores. */
    p[0x1c4]=s->special==1;p[0x1c5]=s->special==2;p[0x1c6]=s->regul;
    p[0x1c8]=s->gambol==1;p[0x1c9]=s->gambol==2;
    for(u32 i=0;i<2;i++){
        *(u32 *)(o+i*0x38+4)=s->random[i];*(u32 *)(o+i*0x38+8)=s->mirror[i];
        *(u32 *)(o+i*0x38+0x18)=s->hard[i];LOOP_PAD();
    }
}
static u32 nm_plus_allowed(u32 side)
{
    u8 *o=nm_options(nm_preferences(),nm_style())+side*0x38;
    return !*(u32 *)(o+0x1c) && !*(u32 *)(o+0x20); /* Native assist/5KEY gates. */
}
static u32 nm_command(const struct nm_state *s){return s->special|(s->regul<<2)|(s->gambol<<3);}
static u32 nm_variant(const struct nm_state *s,u32 i)
{
    return (s->hard[i]==2)|((s->random[i]==2||s->random[i]==4)<<1)|((s->mirror[i]==2)<<2);
}
static void nm_encode(struct mm_options *o,const struct nm_state *s)
{
    o->nativecmd=nm_command(s);o->variant[0]=nm_variant(s,0);o->variant[1]=nm_variant(s,1);
}
static void nm_restore_preferences(const struct mm_options *o)
{
    struct nm_state s;nm_capture(&s);
    if(o->nativecmd<32){s.special=o->nativecmd&3;s.regul=(o->nativecmd>>2)&1;s.gambol=(o->nativecmd>>3)&3;}
    nm_store(&s);
    /* P1 is the final owner when native DP/sync couples both sides. Independent
     * battle preferences remain independent. Do not let stale P2 variant bits
     * override the primary player's linked choice on startup. */
    for(u32 order=0;order<2;order++){
        u32 i=1-order;
        u32 v=o->variant[i];if(v>=MM_VARIANT_UNSET){LOOP_PAD();continue;}
        nm_capture(&s);
        if(s.hard[i]==1||s.hard[i]==2)nm_hazard(i,v&1);
        if(nm_plus_allowed(i)){
            if(s.random[i]>=1&&s.random[i]<=4)nm_random_plus(i,(v>>1)&1);
            if(s.mirror[i])nm_mirror_plus(i,(v>>2)&1);
        }
        LOOP_PAD();
    }
}
/* Ask the actual native variant functions to normalize a temporary draft.
 * Only their proven preference fields change; restore them before rendering,
 * persistence or native select update. No audio, IO or input work in these
 * routines. Their own DP/sync/assist rules remain authoritative. */
static void nm_change(struct nm_state *draft,u32 row,u32 side,int delta)
{
    if(row==0){int v=(int)draft->special+(delta<0?-1:1);draft->special=v<0?2:v>2?0:v;return;}
    if(row==1){draft->regul^=1;return;}
    if(row==2){int v=(int)draft->gambol+(delta<0?-1:1);draft->gambol=v<0?2:v>2?0:v;return;}
    if(row==3 && draft->hard[side]!=1 && draft->hard[side]!=2)return;
    if(row==4 && (!nm_plus_allowed(side)||!draft->random[side]||draft->random[side]>4))return;
    if(row==5 && (!nm_plus_allowed(side)||!draft->mirror[side]||draft->mirror[side]>2))return;
    struct nm_state old;nm_capture(&old);nm_store(draft);
    if(row==3)nm_hazard(side,draft->hard[side]!=2);
    else if(row==4){
        u32 plus=draft->random[side]!=2&&draft->random[side]!=4;
        nm_random_plus(side,plus);if(draft->mirror[side])nm_mirror_plus(side,plus);
    }else if(row==5)nm_mirror_plus(side,draft->mirror[side]!=2);
    nm_capture(draft);nm_store(&old);
}
