#include "profile_mc.h"
#include "save_probe.h"
#if defined(__mips__)
#define MC_PAD() __asm__ volatile("nop\n\tnop\n\tnop")
#else
#define MC_PAD() ((void)0)
#endif
static void zero(void *to,mp_u32 n){mp_u8 *p=to;while(n--){*p++=0;MC_PAD();}}
static int start(void *context,int op,const char *path,int fd,void *data,mp_u32 n,mp_u64 arg)
{
    mp_mc *m=context;int r,which,argument=0;void *p=data;mp_u32 bytes=n;
    if(m->poisoned || m->active || arg>0x7fffffffu)return -1;
    switch(op){
    case MP_TSTAT:
        if(!path || !data || n!=sizeof(mp_file_stat))return -1;
        zero(m->directory,sizeof m->directory);which=CP_STAT;p=m->directory;bytes=64;break;
    case MP_TSPACE:
        if(!data || n!=sizeof(mp_file_space))return -1;
        zero(&m->card,sizeof m->card);which=CP_INFO;p=&m->card;bytes=0;break;
    case MP_TOPEN:
        if(!path || (arg!=1 && arg!=2 && arg!=0x203))return -1;
        which=CP_OPEN;argument=(int)arg;break;
    case MP_TREAD:case MP_TWRITE:
        if(fd<0 || !data || !n || n>MP_PAGE)return -1;
        which=op==MP_TREAD?CP_READ:CP_WRITE;break;
    case MP_TCLOSE:if(fd<0)return -1;which=CP_CLOSE;break;
    case MP_TSEEK:if(fd<0)return -1;which=CP_SEEK;bytes=(mp_u32)arg;break; /* SEEK_SET=0 */
    default:return -1;
    }
    sp_u64 observed=sp_timer();r=m->ops->start(which,m->port,path,fd,p,bytes,argument);
    sp_io_start((unsigned)op,n,r,observed);
    if(r)return r;
    m->active=1;m->operation=op;m->output=data;return 0;
}
static int poll(void *context,int *result)
{
    mp_mc *m=context;int r,value=0;if(!m->active || !result)return -1;
    sp_u64 observed=sp_timer();r=m->ops->poll(&value);
    if(!r){sp_io_poll(0,0,observed);return 0;}m->active=0;
    if(r<0){sp_io_poll(r,value,observed);m->poisoned=1;return -1;}
    if(m->operation==MP_TSTAT){
        if(value==1){mp_file_stat *s=m->output;s->bytes=mp_read32(m->directory+16);s->exists=1;s->reserved=0;value=0;}
        else if(!value || value==-4){mp_file_stat *s=m->output;s->bytes=0;s->exists=0;s->reserved=0;value=0;}
        else if(value>0)value=-1000;
    }else if(m->operation==MP_TSPACE && !value){
        mp_file_space *s=m->output;
        if(m->card.type!=2 || m->card.formatted!=1 || m->card.free_blocks<0)value=-1000;
        else {s->free_bytes=(mp_u64)(mp_u32)m->card.free_blocks<<10;s->block_bytes=1024;s->writable=1;}
    }
    sp_io_poll(1,value,observed);*result=value;return 1;
}
int mp_mc_init(mp_mc *m,mp_storage *storage,const cp_ops *ops,int port)
{
    if(!m || !storage || !ops || !ops->start || !ops->poll || port<0 || port>1)return MP_CALLBACK;
    zero(m,sizeof *m);zero(storage,sizeof *storage);m->ops=ops;m->port=port;
    storage->context=m;storage->start=start;storage->poll=poll;
    storage->limit=0x7fffffffu;storage->reserve_bytes=8192;return MP_OK;
}
