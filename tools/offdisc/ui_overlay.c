/* Mutually exclusive UI text shares the existing title read scratch.
 * Entry/exit occur only while native SELECT actions are held. No relocation,
 * heap carveout, IOP reset or background loader. Exact build CRC is resident. */
extern s32 vd_ui_read(const char *,void *,u32);
#ifndef UI_OVERLAY_TEST
#define ui_flush(kind) (((void (*)(u32))0x10eee0)(kind))
#endif
volatile struct {u32 bytes,crc;} ui_specs[2]; /* Build fills the matching images. */
static __attribute__((noinline,minsize)) void ui_release(void){
    ui_active=0;spr_read_bytes=0;spr_forget_result();
}
static __attribute__((noinline,minsize)) int ui_load(u32 kind){
    if(ui_active)return ui_active==kind;
    if(kind<1||kind>2)return 0;
#ifdef SPR_ASYNC
    sa_retire();
#endif
    char path[128];unsigned n=0,end=0;
#ifdef PORTABLE_RUNTIME
    extern char vd_path_prefix[64];
    const char *root=vd_path_prefix;end=mm_length(root);
    if(!end||end>=64)return 0;
#else
    const char *root=menu_control.path;unsigned length=mm_length(root);
    for(unsigned i=0;i<length;i++){if(root[i]=='/'||root[i]=='\\'||root[i]==':')end=i+1;LOOP_PAD();}
    if(!end||end>90)return 0;
#endif
#ifdef PORTABLE_RUNTIME
    const char *name=kind==1?"launcher/bind-ui-portable":"launcher/preview-ui-portable";
#else
    const char *name=kind==1?"launcher/bind-ui":"launcher/preview-ui";
#endif
    mm_copy(path,root,end);n=end;path[n]=0;
    if(mm_append(path,127,&n,name,mm_length(name)))return 0;
#ifdef PREP_WATCH
    if(mm_append(path,127,&n,"-prep",5))return 0;
#elif defined(SELECT_PROFILE)
    if(mm_append(path,127,&n,"-select",7))return 0;
#elif defined(INPUT_LOG)
    if(mm_append(path,127,&n,"-debug",6))return 0;
#endif
    if(mm_append(path,127,&n,".bin",4))return 0;
    u32 bytes=ui_specs[kind-1].bytes,crc=ui_specs[kind-1].crc;
    if(!bytes||bytes>sizeof spr_buf)return 0;
    spr_read_bytes=0;spr_forget_result();ui_active=kind;
    s32 got=vd_ui_read(path,spr_buf,sizeof spr_buf);
    if(got!=(s32)bytes||mm_crc(spr_buf,bytes)!=crc){ui_release();return 0;}
    /* Native syscall FlushCache: data writeback, then instruction invalidate.
     * Neither operation can occur while another overlay call is active. */
    ui_flush(0);ui_flush(2);
    return 1;
}
