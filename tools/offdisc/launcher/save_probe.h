#ifndef MEGAMIX_SAVE_PROBE_H
#define MEGAMIX_SAVE_PROBE_H
/* Debug-only, fixed-size aggregate. No score bytes or per-event history. */
enum {SP_UI,SP_PACK,SP_COLLECT,SP_RETAIN,SP_APPLY,SP_SETUP,SP_SCAN,
      SP_GUARD,SP_MEASURE,SP_WRITE,SP_VERIFY,SP_READ,SP_PUBLISH,SP_CLEANUP,
      SP_ENCODE,SP_DECODE,SP_PHASES};
enum {SP_MANUAL=1,SP_AUTO,SP_LOAD_UI,SP_LOAD_AUTO,SP_WRITE_ONLY,SP_READ_ONLY};
typedef unsigned long long sp_u64;
typedef struct {sp_u64 wall,inside;unsigned calls,max_inside;} sp_metric;
typedef struct {
    sp_u64 elapsed,inside,bytes;
    unsigned calls,polls,pending,busy,errors,max_elapsed;
} sp_rpc;
typedef struct {
    unsigned schema,enabled,serial,exports,last_error,blocked;
    unsigned count_lo,count_hi,bus_lo,bus_hi,samples,bus_hz;
    char path[64];
} sp_control;
typedef struct {unsigned h[32];sp_metric phase[SP_PHASES],ui[32];sp_rpc io[7];} sp_report;
_Static_assert(sizeof(sp_control)==112,"save profiler control ABI");
_Static_assert(sizeof(sp_metric)==24 && sizeof(sp_rpc)==48,"save profiler metrics ABI");
_Static_assert(sizeof(sp_report)==1616,"save profiler report ABI");
#ifdef SAVE_PROFILE
extern sp_control save_probe;
sp_u64 sp_enter(unsigned,unsigned,unsigned,unsigned,unsigned,unsigned);
void sp_leave(sp_u64,int,int);
void sp_diag(const unsigned *);
void sp_phase(unsigned);
sp_u64 sp_timer(void);
void sp_cost(unsigned,sp_u64);
void sp_restore(sp_u64);
void sp_tx(unsigned);
void sp_io_start(unsigned,unsigned,int,sp_u64);
void sp_io_poll(int,int,sp_u64);
void sp_flush(void);
void sp_idle(unsigned);
void sp_set_chart(unsigned);
#else
#define sp_phase(n) ((void)0)
#define sp_timer() 0ull
#define sp_cost(n,t) ((void)(t))
#define sp_restore(t) ((void)(t))
#define sp_tx(n) ((void)0)
#define sp_io_start(o,n,r,t) ((void)(t))
#define sp_io_poll(r,v,t) ((void)(t))
#endif
#endif
