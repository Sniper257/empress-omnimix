#ifndef MEGAMIX_CONTROLLER_STORE_H
#define MEGAMIX_CONTROLLER_STORE_H
#include "protocol.h"
#ifdef LOOP_PAD
#define MU_STORE_PAD() LOOP_PAD()
#else
#define MU_STORE_PAD()
#endif
/* Version2 controller files; legacy version1 is read without rewriting it. */
typedef struct {uint32_t enabled;mu_identity identity;mk_profile profile;} mu_saved_player;
typedef struct {uint32_t magic,version,generation,navigation;mu_saved_player player[2];uint32_t crc;} mu_saved;
_Static_assert(sizeof(mu_saved)==308,"launcher controller-file ABI2");
static __attribute__((noinline,minsize)) unsigned mu_crc(const void *data,unsigned n){const unsigned char *p=data;unsigned c=~0u;while(n--){MU_STORE_PAD();c^=*p++;for(unsigned i=0;i<8;i++)c=(c>>1)^(0xedb88320u&-(c&1));}return ~c;}
static __attribute__((noinline,minsize)) int mu_equal(const void *a,const void *b,unsigned n){const unsigned char *x=a,*y=b;for(unsigned i=0;i<n;i++){MU_STORE_PAD();if(x[i]!=y[i])return 0;}return 1;}
static __attribute__((noinline,minsize)) int mu_saved_valid(const mu_saved *s,int n){if(n!=sizeof *s || s->magic!=MU_RPC_ID || s->version!=MU_VERSION || s->navigation>1 || s->crc!=mu_crc(s,sizeof *s-4))return 0;for(unsigned p=0;p<2;p++){if(s->player[p].enabled>1||s->player[p].profile.type>MK_5KEY)return 0;for(unsigned j=0;j<MK_CONTROLS;j++){const mk_binding *b=s->player[p].profile.control+j;if(b->kind>MK_MOTION_POS||b->reserved||b->field>=MH_FIELDS)return 0;}}return 1;}
/* Read version1 without rewriting it: its11-control profiles mean7KEY. */
typedef struct {uint32_t enabled;mu_identity identity;mk_binding control[11];} mu_saved_player_v1;
typedef struct {uint32_t magic,version,generation,navigation;mu_saved_player_v1 player[2];uint32_t crc;} mu_saved_v1;
_Static_assert(sizeof(mu_saved_v1)==252,"controller legacy ABI1");
static __attribute__((noinline,minsize)) int mu_saved_decode(mu_saved *out,const void *data,int n){
 if(n==sizeof(mu_saved)){const mu_saved *s=data;if(!mu_saved_valid(s,n))return -1;*out=*s;return 0;}
 if(n!=sizeof(mu_saved_v1))return -1;
 mu_saved_v1 old;unsigned char *d=(unsigned char*)&old;const unsigned char *src=data;
 for(unsigned i=0;i<sizeof old;i++){MU_STORE_PAD();d[i]=src[i];}
 if(old.magic!=MU_RPC_ID||old.version!=1||old.navigation>1||old.crc!=mu_crc(&old,sizeof old-4))return -1;
 mu_saved next;unsigned char *z=(unsigned char*)&next;for(unsigned i=0;i<sizeof next;i++){MU_STORE_PAD();z[i]=0;}next.magic=MU_RPC_ID;next.version=MU_VERSION;next.generation=old.generation;next.navigation=old.navigation;
 for(unsigned p=0;p<2;p++){next.player[p].enabled=old.player[p].enabled;next.player[p].identity=old.player[p].identity;for(unsigned j=0;j<11;j++){MU_STORE_PAD();next.player[p].profile.control[j]=old.player[p].control[j];}}
 next.crc=mu_crc(&next,sizeof next-4);if(!mu_saved_valid(&next,sizeof next))return -1;*out=next;return 0;
}
struct mu_store_io {void *ctx;int (*read)(void *,const char *,void *,unsigned);int (*write)(void *,const char *,const void *,unsigned);};
static __attribute__((noinline,minsize)) int mu_store_path(const char *root,unsigned slot,char out[112]){unsigned n=0;while(root[n]){MU_STORE_PAD();if(n>=96)return -1;out[n]=root[n];n++;}const char *name="controllers.";for(unsigned i=0;name[i];i++){MU_STORE_PAD();out[n++]=name[i];}out[n++]='0'+slot;out[n]=0;return 0;}
/* read callbacks bootstrap absent slots nontruncating, then query exact extent;
 * empty/malformed generations are distinct from transport/close failures. */
static __attribute__((noinline,minsize)) int mu_store_load(const struct mu_store_io *io,const char *root,mu_saved *out){mu_saved a,b;char path[112];if(mu_store_path(root,0,path))return -1;int an=io->read(io->ctx,path,&a,sizeof a);if(an<0&&an!=-3)return -2;if(mu_store_path(root,1,path))return -1;int bn=io->read(io->ctx,path,&b,sizeof b);if(bn<0&&bn!=-3)return -2;int av=!mu_saved_decode(&a,&a,an),bv=!mu_saved_decode(&b,&b,bn);if(!av&&!bv)return -3;if(av&&bv&&a.generation==b.generation&&!mu_equal(&a,&b,sizeof a))return -4;*out=!av||(bv&&(int32_t)(b.generation-a.generation)>0)?b:a;return 0;}
static __attribute__((noinline,minsize)) int mu_store_save(const struct mu_store_io *io,const char *root,const mu_saved *old,const mu_saved_player *player,unsigned side){mu_saved current,next=*old,check;char path[112];if(side>1||!mu_saved_valid(old,sizeof *old))return -1;next.generation++;next.player[side]=*player;next.crc=mu_crc(&next,sizeof next-4);if(!mu_saved_valid(&next,sizeof next))return -1;int r=mu_store_load(io,root,&current);if(r)return r;if(mu_equal(&current,&next,sizeof next))return 0;/* uncertain readback already committed */if(!mu_equal(&current,old,sizeof current))return -5;if(mu_store_path(root,next.generation&1,path))return -1;if(io->write(io->ctx,path,&next,sizeof next))return -6;int n=io->read(io->ctx,path,&check,sizeof check);if(!mu_saved_valid(&check,n)||!mu_equal(&check,&next,sizeof next))return -7;return 0;}
#endif
