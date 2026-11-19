/* R46's measured scheduling change, independent of capture/chart identity.
 * The four native PLAY request sites still call 140260 once. Batch all scene
 * producers, drawing, submission and movie service before moving ONE existing
 * sound-worker wake ahead of the frame wait. Its native coalescing semaphore,
 * worker/IOP cadence and the original frame wait/flip remain unchanged. */
#define AD_ARGS unsigned long long a,unsigned long long b,unsigned long long c,unsigned long long d,unsigned long long e,unsigned long long f,unsigned long long g,unsigned long long h
#define AD_PASS a,b,c,d,e,f,g,h
#ifndef AUDIO_DISPATCH_TEST
#define ad_native(a) ((unsigned long long (*)(AD_ARGS))(unsigned long)(a))
#define ad_phase() (*(volatile unsigned *)0x2ad534)
#endif
#ifdef LOAD_PROFILE
extern unsigned long long lp_vblank(AD_ARGS);
#define ad_wait_call(...) lp_vblank(__VA_ARGS__)
#elif defined(EXIT_TRACE)
extern unsigned long long exit_trace_vblank(AD_ARGS);
#define ad_wait_call(...) exit_trace_vblank(__VA_ARGS__)
#else
#define ad_wait_call(...) ad_native(0x10eca0)(__VA_ARGS__)
#endif
#ifdef EXIT_TRACE
extern unsigned long long exit_trace_audio_tick(AD_ARGS);
#define ad_wake_call(...) exit_trace_audio_tick(__VA_ARGS__)
#else
#define ad_wake_call(...) ad_native(0x13f5b0)(__VA_ARGS__)
#endif
static unsigned ad_requested,ad_done;
static unsigned long long ad_result;
#ifdef PREVIEW_TRACE
static void preview_trace_boundary(unsigned);
#endif
static void ad_note(unsigned long long result)
{
    unsigned p=ad_phase();
    if((unsigned)result==0 && p>=5 && p<8)ad_requested=1;
}
unsigned long long ad_request(AD_ARGS)
{
#ifdef AUDIO_PROBE
    return ap_request(AD_PASS); /* observer invokes ad_note after native call */
#else
    unsigned long long r=ad_native(0x140260)(AD_PASS);ad_note(r);return r;
#endif
}
unsigned long long ad_frame_wait(AD_ARGS)
{
#ifdef USB_BOOT_TEXT
    boot_loop_note(0x1519e0,0,(unsigned)a);
#endif
    unsigned p=ad_phase(),early=ad_requested && p>=5 && p<8;
    ad_requested=0;ad_done=early;
    if(early){
#ifdef AUDIO_PROBE
        if(ap_near())ap_emit(0,4,1,0,0,0);
#endif
        ad_result=ad_wake_call(0,0,0,0,0,0,0,0);
    }
#ifdef PREVIEW_TRACE
    preview_trace_boundary(0xa6);
    unsigned long long r=ad_wait_call(AD_PASS);
    preview_trace_boundary(0xa7);
#else
    unsigned long long r=ad_wait_call(AD_PASS);
#endif
#ifdef USB_BOOT_TEXT
    boot_loop_note(0x1519e0,1,(unsigned)r);
#endif
    return r;
}
#ifdef USB_BOOT_TEXT
static unsigned long long ad_boot_return(unsigned long long r){boot_loop_note(0x1519f0,1,(unsigned)r);return r;}
#define AD_BOOT_RETURN(r) return ad_boot_return(r)
#else
#define AD_BOOT_RETURN(r) return (r)
#endif
unsigned long long ad_wake(AD_ARGS)
{
#ifdef USB_BOOT_TEXT
    boot_loop_note(0x1519f0,0,(unsigned)a);
#endif
    if(ad_done){ad_done=0;AD_BOOT_RETURN(ad_result);}
#ifdef AUDIO_PROBE
    if(ap_near())ap_emit(0,4,0,0,0,0);
#endif
#ifdef PREVIEW_TRACE
    preview_trace_boundary(0xa8);
    unsigned long long r=ad_wake_call(AD_PASS);
    preview_trace_boundary(0xa9);AD_BOOT_RETURN(r);
#else
    AD_BOOT_RETURN(ad_wake_call(AD_PASS));
#endif
}
#undef AD_BOOT_RETURN
