/* Resident USB profile editor. Only called while the song-select modal owns
 * actions. No SDK, IOP reset, new USB transfers or gameplay polls. */
#if (defined(USB_HID) && !defined(HOST_TEST)) || defined(LIVE_BIND_TEST)
#include "usb_hid/native.h"
#include "usb_hid/profile_store.h"
#include "usb_hid/capture.h"
#ifndef LIVE_BIND_TEST
extern mu_native_config usb_input_config;
extern mu_frame usb_input_frames[2];
extern s32 vd_controller_load(const char *,mu_saved *);
extern s32 vd_controller_save(const char *,const mu_saved *,const mu_saved_player *,u32);
#define lb_bind(c) (((s32 (*)(void *,u32,u32))0x112b50)(c,MU_RPC_ID,1))
#define lb_call(c,op,q,r) (((s32 (*)(void *,u32,u32,void *,s32,void *,s32,void *,void *))0x112d30)(c,op,1,q,sizeof(mu_request),r,sizeof(mu_reply),0,0))
#define lb_busy(c) (((s32 (*)(void *))0x112f30)(c))
#define lb_frames ((volatile mu_frame *)((u32)usb_input_frames|0x20000000u))
#endif
static __attribute__((noinline,minsize)) void *lb_clear(void *p,int v,unsigned n){u8 *q=p;for(unsigned i=0;i<n;i++){q[i]=v;LOOP_PAD();}return p;}
#define MK_FREESTANDING
#define MK_VALIDATE_ONLY
#define memset lb_clear
#include "usb_hid/bindings.c"
#undef memset
#undef MK_FREESTANDING
#undef MK_VALIDATE_ONLY
/* client uses the original retail10-word layout, not SDK SifRpcClientData_t.
 * Buffers are permanent until an asynchronous request has actually drained. */
enum {LB_BIND,LB_INFO,LB_MODEL,LB_TYPE,LB_EDIT,LB_CAPTURE,LB_WARN,LB_APPLY,LB_ACK,LB_SAVE_ERROR,LB_RESTORE_INFO,LB_RESTORE,LB_RELEASE,LB_ERROR,LB_SWAP_UNPLUG,LB_SWAP_PLUG,LB_SWAP_PICK,LB_TURNTABLE,LB_SWAP_RESTORE,LB_HOME};
static struct lb_state {
    u32 client[16] __attribute__((aligned(64)));
    union {mu_request data;u8 bytes[MU_RPC_BYTES(mu_request)];} request __attribute__((aligned(64)));
    union {mu_reply data;u8 bytes[MU_RPC_BYTES(mu_reply)];} reply __attribute__((aligned(64)));
    mh_model model;mh_values base,values;
    mu_saved saved;mu_live_info info;mk_profile draft;
    mu_info candidates[MU_DEVICES];u32 seen[MU_DEVICES],wizard,swap,turntable;
    char root[96];void *allocation;
    u32 state,pending,operation,age,divider,player,cursor,clear,stick,capture,wait,revision,head,warn_choice,missing,duplicates,applied,finished,edges,quit;
    const char *message;
} *lb;
static u32 lb_nav_held,lb_nav_repeat,lb_nav_state,lb_nav_armed;
static __attribute__((noinline,minsize)) void lb_nav_transition(void){
    u32 panel=lb->state|(lb->clear<<8);
    if(lb_nav_state!=panel){lb_nav_state=panel;lb_nav_armed=lb_nav_repeat=lb->edges=0;lb_nav_held=~0u;}
}
static __attribute__((noinline,minsize)) void lb_error(const char *message){lb->state=lb->applied?LB_SAVE_ERROR:LB_ERROR;lb->cursor=lb->applied?1:0;lb->message=message;}
static __attribute__((noinline,minsize)) void lb_back(void){
    if(lb){u32 blocked=lb->pending;LOOP_PAD();blocked|=lb->applied;if(blocked)return;}
    if(lb){void *p=lb->allocation;lb=0;free_(p);}
    menu.page=MENU_ROOT;menu.row=4;menu.release=1;
}
static __attribute__((noinline,minsize)) void lb_request(u32 operation){
    if(lb->pending)return;
    mu_request *q=&lb->request.data;lb_clear(q,0,sizeof *q);
    q->version=MU_VERSION;q->player=lb->player;q->slot=lb->info.slot;q->generation=lb->info.generation;
    if(operation==MU_LIVE_CONFIGURE||operation==MU_LIVE_REPLACE){q->identity=lb->info.identity;q->profile=lb->state==LB_RESTORE?lb->saved.player[lb->player].profile:lb->draft;q->ee_target=lb->revision;}
    lb_clear(&lb->reply,0,sizeof lb->reply);lb->operation=operation;lb->age=0;
    if(lb_call(lb->client,operation,q,&lb->reply.data)<0){lb_error("USB REQUEST FAILED - BACK TO RETRY");return;}
    lb->pending=1;if(operation==MU_LIVE_CONFIGURE||operation==MU_LIVE_REPLACE)lb->applied=1;
}
static __attribute__((noinline,minsize)) u32 lb_latest(void){u32 h=0;for(u32 i=0;i<2;i++){volatile mu_frame *f=lb_frames+i;u32 a=f->head;if(a&&a==f->tail&&a==f->head&&(!h||(s32)(a-h)>0))h=a;}return h;}
static __attribute__((noinline,minsize)) int lb_ack(void){
    u32 newest=0,ready=0;
    for(u32 i=0;i<2;i++){
        volatile mu_frame *f=lb_frames+i;u32 h=f->head,v=f->version,c=f->connected[lb->player],keys=f->logical[lb->player];
        u32 marker=f->reserved[0],rev=f->reserved[1+lb->player],tail=f->tail;
        if(h&&h==tail&&h==f->head&&v==MU_VERSION&&(!newest||(s32)(h-newest)>0)){
            newest=h;ready=marker==MU_LIVE_FRAME&&rev==lb->revision&&(s32)(h-lb->head)>0&&c&&!keys;
        }
    }
    return ready;
}
static __attribute__((noinline,minsize)) void lb_open(u32 player){
    if(lb){menu.page=MENU_BIND;menu.row=0;menu.release=1;return;}
    void *p=malloc_(sizeof(struct lb_state)+63);
    if(!p){menu.error=-11;return;}
    lb=(struct lb_state *)(((unsigned long)p+63)&~63ul);lb_clear(lb,0,sizeof *lb);lb->allocation=p;lb->player=player;lb_nav_held=lb_nav_repeat=lb_nav_armed=0;lb_nav_state=~0u;
    menu.page=MENU_BIND;menu.row=0;menu.release=1;
    unsigned n=mm_length(menu_control.path),end=0;for(unsigned i=0;i<n;i++)if(menu_control.path[i]=='/'||menu_control.path[i]=='\\')end=i+1;
    if(!end||end>=sizeof lb->root){lb_error("CONTROLLER SAVE PATH UNAVAILABLE");return;}mm_copy(lb->root,menu_control.path,end);lb->root[end]=0;
    if(!usb_input_config.owned[player]||!usb_input_config.gate_token){lb_error("ENABLE THIS USB PLAYER IN THE LAUNCHER");return;}
    if(vd_controller_load(lb->root,&lb->saved)){lb_error("COULD NOT READ CONTROLLER FILES - RETRY");return;}
    lb->state=LB_BIND;lb->pending=1;lb->operation=0;
    if(lb_bind(lb->client)<0){lb->pending=0;lb_error("USB SERVICE BIND FAILED - BACK TO RETRY");}
}
static __attribute__((noinline,minsize)) u32 lb_old_navigation(void){
    u32 out=0;
    for(u32 s=0;s<2;s++)if(s!=lb->player || !usb_input_config.owned[s] || (lb->wizard&&!lb->applied))out|=menu_navigation(s);
    const mk_profile *p=lb->wizard==2?&lb->draft:&lb->info.profile;
    for(u32 j=0;j<3;j++){
        u32 k=p->type==MK_DUALSHOCK?(j==0?1:j==1?0:7):j==0?0:j==1?1:p->type==MK_5KEY?(lb->wizard==2?3:4):5;const mk_binding *b=p->control+k;
        if(b->field>=lb->model.field_count||!lb->values.valid[b->field])continue;
        s32 v=lb->values.value[b->field];u32 down=b->kind==MK_BUTTON?v!=0:b->kind==MK_HAT?v==b->threshold:b->kind==MK_LOW?v<=b->threshold:b->kind==MK_HIGH?v>=b->threshold:0;
        if(down)out|=j==0?MENU_DOWN:j==1?MENU_UP:MENU_OK;
    }
    return out;
}
static __attribute__((noinline,minsize)) void lb_save(void){
    mu_saved_player next=lb->saved.player[lb->player];next.profile=lb->draft;next.identity=lb->info.identity;
    if(vd_controller_save(lb->root,&lb->saved,&next,lb->player)){lb->state=LB_SAVE_ERROR;lb->cursor=0;lb->message="MAPPING APPLIED; FILE SAVE FAILED";return;}
    lb->applied=0;lb->state=LB_RELEASE;lb->finished=1;lb->message="SAVED - RELEASE CONTROLS TO RETURN";
}
static __attribute__((noinline,minsize)) void lb_apply(void){lb->state=LB_APPLY;lb->head=lb_latest();lb_request(lb->swap?MU_LIVE_REPLACE:MU_LIVE_CONFIGURE);}
static __attribute__((noinline,minsize)) void lb_guide(void){lb->state=LB_CAPTURE;lb->capture=1;lb->wait=0;lb->edges=0;}
static __attribute__((noinline,minsize)) void lb_candidate(u32 slot){
    mu_info *d=lb->candidates+slot;lb->info.slot=slot;lb->info.generation=d->generation;lb->info.identity=d->identity;
    lb->wizard=2;lb->swap=1;lb_clear(&lb->values,0,sizeof lb->values);lb->state=LB_MODEL;lb_request(MU_MODEL);
}
static __attribute__((noinline,minsize)) void lb_checked_apply(void){
    uint16_t missing,duplicates;
    if(mk_validate(&lb->model,&lb->draft,&missing,&duplicates)){lb_error("INVALID BINDING - RETURN AND RETRY");return;}
    if(missing||duplicates){lb->missing=missing;lb->duplicates=duplicates;lb->state=LB_WARN;lb->warn_choice=0;}
    else lb_apply();
}
static __attribute__((noinline,minsize)) void lb_completed(void){
    mu_reply *r=&lb->reply.data;
    if(r->version!=MU_VERSION){lb_error("USB SERVICE VERSION MISMATCH");return;}
    if(r->status){
        if(lb->operation==MU_VALUES&&r->status==-2)return;
        if(lb->operation==MU_LIVE_REPLACE&&r->status==-4)lb->applied=lb->state==LB_RESTORE?2:0;
        if(lb->applied){lb->state=LB_SAVE_ERROR;lb->cursor=1;lb->message="RECONNECT USB THEN RESTORE OLD BINDINGS";return;}
        lb_error("RECONNECT ORIGINAL USB / PS2 PAD: BACK");return;
    }
    if(lb->operation==MU_LIST){
        mu_info *d=r->data.devices;
        if(lb->state==LB_SWAP_RESTORE){
            for(u32 i=0;i<MU_DEVICES;i++)if(d[i].connected&&mu_equal(&d[i].identity,&lb->saved.player[lb->player].identity,sizeof d[i].identity)){
                lb->info.identity=d[i].identity;lb->info.slot=i;lb->info.generation=d[i].generation;lb->state=LB_RESTORE;lb->head=lb_latest();lb_request(MU_LIVE_REPLACE);return;
            }
            return;
        }
        if(lb->state==LB_SWAP_UNPLUG){
            if(!lb->wait++){for(u32 i=0;i<MU_DEVICES;i++)lb->seen[i]=d[i].connected?d[i].generation:0;}
            u32 old=0;for(u32 i=0;i<MU_DEVICES;i++)if(d[i].connected&&d[i].generation==lb->info.generation&&mu_equal(&d[i].identity,&lb->info.identity,sizeof d[i].identity))old=1;
            if(old){for(u32 i=0;i<MU_DEVICES;i++)lb->seen[i]=d[i].connected?d[i].generation:0;return;}
            lb->state=LB_SWAP_PLUG;
        }
        u32 found=0,slot=0;
        for(u32 i=0;i<MU_DEVICES;i++){
            lb->candidates[i]=d[i];
            if(d[i].connected&&d[i].generation!=lb->seen[i] &&
               !(lb->saved.player[1-lb->player].enabled&&mu_equal(&d[i].identity,&lb->saved.player[1-lb->player].identity,sizeof d[i].identity))){found++;slot=i;}
            else lb->candidates[i].connected=0;
        }
        if(found==1)lb_candidate(slot);
        else if(found>1){lb->state=LB_SWAP_PICK;lb->cursor=0;lb->edges=0;}
    }else if(lb->operation==MU_LIVE_INFO){
        mu_live_info info=r->data.live;
        if(info.version!=MU_LIVE_VERSION || !info.enabled || info.slot>=MU_DEVICES || !info.generation || !info.revision){lb_error("USB PLAYER DISCONNECTED / NOT AVAILABLE");return;}
        if(lb->state==LB_RESTORE_INFO){
            if(!mu_equal(&info.identity,&lb->info.identity,sizeof info.identity)){lb->state=LB_SAVE_ERROR;lb->message="RECONNECT ORIGINAL USB CONTROLLER";return;}
            lb->revision=info.revision;lb->info.generation=info.generation;lb->info.slot=info.slot;lb->head=lb_latest();
            if(mu_equal(&info.profile,&lb->info.profile,sizeof info.profile)){lb->finished=2;lb->wait=0;lb->state=LB_ACK;return;}
            if(!mu_equal(&info.profile,&lb->draft,sizeof info.profile)){lb->state=LB_SAVE_ERROR;lb->message="USB PROFILE CHANGED - RESTART REQUIRED";return;}
            lb->state=LB_RESTORE;lb_request(MU_LIVE_CONFIGURE);return;
        }
        if(!lb->saved.player[lb->player].enabled || !mu_equal(&info.identity,&lb->saved.player[lb->player].identity,sizeof info.identity)||!mu_equal(&info.profile,&lb->saved.player[lb->player].profile,sizeof info.profile)){lb_error("STORED / LIVE BINDINGS DIFFER - RESTART");return;}
        lb->info=info;lb->revision=info.revision;lb->draft=info.profile;lb->state=LB_MODEL;lb_request(MU_MODEL);
    }else if(lb->operation==MU_MODEL){
        lb->model=r->data.model;
        if(lb->model.field_count>MH_FIELDS||!lb->model.field_count||lb->model.report_count>MH_REPORTS||!lb->model.report_count){lb_error("INVALID USB DESCRIPTOR MODEL");return;}
        if(lb->wizard==2){lb->cursor=1;lb_guide();lb_request(MU_VALUES);return;}
        lb->state=LB_HOME;lb->cursor=0;lb->stick=lb->draft.type==MK_DUALSHOCK;lb->message="CURRENT TYPE AND BINDINGS KEPT";lb_request(MU_VALUES);
    }else if(lb->operation==MU_VALUES){
        lb->values=r->data.values;
        if(lb->state==LB_CAPTURE){
            if(lb->capture==1){if(mu_capture_released(&lb->model,&lb->values)){lb->base=lb->values;lb->capture=2;}}
            else {mk_binding b;u32 slot=mk_control_slot(lb->draft.type,lb->cursor-1),filter=lb->wizard==2&&lb->draft.type!=MK_DUALSHOCK&&slot>=MK_CCW?(lb->turntable==2?2:1):0;if(mu_capture_filtered(&lb->model,&lb->base,&lb->values,mk_capture_target(lb->draft.type,slot),lb->stick,filter,&b) && !(filter==1&&slot==MK_CW&&(b.field!=lb->draft.control[MK_CCW].field || b.kind==lb->draft.control[MK_CCW].kind))){lb->draft.control[mk_control_slot(lb->draft.type,lb->cursor-1)]=b;lb->capture=0;lb->state=LB_RELEASE;lb->finished=0;lb->wait=0;lb->message="BOUND - RELEASE INPUT TO CONTINUE";}}
        }
    }else if(lb->operation==MU_LIVE_CONFIGURE||lb->operation==MU_LIVE_REPLACE){
        if(r->data.live.version!=MU_LIVE_VERSION||!r->data.live.revision||r->data.live.revision==lb->revision){lb_error("USB APPLY ACKNOWLEDGEMENT INVALID");return;}
        lb->revision=r->data.live.revision;lb->applied=2;lb->wait=0;
        if(lb->state==LB_RESTORE)lb->finished=2;else lb->finished=0;
        lb->state=LB_ACK;lb->message="RELEASE ALL CONTROLS - WAITING FOR USB";
    }
}
static __attribute__((noinline,minsize)) void lb_update(void){
    if(!lb){menu.page=MENU_ROOT;return;}
    u32 shortcut=menu_shortcut();
    /* START/SELECT remain ordinary bindable inputs during raw capture. */
    if(shortcut&&lb->state!=LB_CAPTURE)lb->quit=shortcut;
    lb_nav_transition();
    if(lb->state==LB_CAPTURE&&++lb->wait>1200){lb->capture=0;if(lb->wizard){lb->wait=0;lb->capture=1;}else {lb->state=LB_EDIT;lb->message="CAPTURE TIMED OUT - BINDING UNCHANGED";}}
    if(lb->state==LB_TYPE||lb->state==LB_EDIT||lb->state==LB_WARN||lb->state==LB_ERROR||lb->state==LB_SAVE_ERROR||lb->state>=LB_SWAP_UNPLUG){
    u32 held=(lb->state==LB_SAVE_ERROR||lb->wizard==2)?lb_old_navigation():menu_navigation(menu.owner);
    for(u32 s=0;s<2;s++)if(s!=menu.owner&&(menu_scheme(s)==1||menu_scheme(s)==4))held|=menu_navigation(s);
    if(!lb_nav_armed){lb->edges=lb_nav_repeat=0;if(!held)lb_nav_armed=1;lb_nav_held=held;}
    else {
    lb->edges|=held&~lb_nav_held;
    if(held&&held==lb_nav_held){if(++lb_nav_repeat>=24&&(lb_nav_repeat-24)%5==0)lb->edges|=held&(MENU_UP|MENU_DOWN);}
    else lb_nav_repeat=0;lb_nav_held=held;
    }
    }
    if(lb->wizard&&!lb->applied)for(u32 side=0;side<2;side++)
        if(!usb_input_config.owned[side]&&(menu_scheme(side)==1||menu_scheme(side)==4))lb->edges|=menu_navigation(side)&MENU_BACK;
    if(lb->pending){
        if(lb_busy(lb->client)){if(lb->age<60000)lb->age++;return;}
        lb->pending=0;
        if(!lb->operation){/* Retail1126a8 publishes server at+24(word9), buffer at+14.
         * Word4 is the command field, not the bound service handle. */
        if(!lb->client[9]){lb_error("USB SERVICE NOT AVAILABLE - RETURN");return;}lb->state=LB_INFO;lb_request(MU_LIVE_INFO);return;}
        lb_completed();
        lb_nav_transition(); /* Async replies may have opened a new panel. */
        if(lb->pending)return;
    }
    if(lb->quit==2){
        if(!lb->applied){lb_back();if(!lb)menu_close(0);return;}
        if(lb->state!=LB_RESTORE_INFO&&lb->state!=LB_RESTORE&&lb->state!=LB_SWAP_RESTORE&&!(lb->state==LB_ACK&&lb->finished==2)){
            lb->state=lb->swap?LB_SWAP_RESTORE:LB_RESTORE_INFO;lb_request(lb->swap?MU_LIST:MU_LIVE_INFO);return;
        }
    }else if(lb->quit==1&&!lb->applied&&(lb->state==LB_HOME||lb->state==LB_EDIT||lb->state==LB_TYPE||lb->state==LB_TURNTABLE||lb->state==LB_ERROR)){
        if(lb->state==LB_ERROR||mu_equal(&lb->draft,&lb->saved.player[lb->player].profile,sizeof lb->draft)){lb_back();if(!lb)menu_close(1);return;}
        lb_checked_apply();return;
    }
    if(lb->wizard&&!lb->applied&&(lb->edges&MENU_BACK)){lb->edges=0;lb_back();return;}
    if(lb->state==LB_ACK){
        if(lb_ack()){
            if(lb->finished==2){lb->applied=0;lb->finished=0;lb->draft=lb->saved.player[lb->player].profile;mm_copy(&lb->info.profile,&lb->draft,sizeof lb->draft);lb->info.revision=lb->revision;lb->state=LB_EDIT;lb->message="PREVIOUS BINDINGS RESTORED";if(lb->swap){lb->wizard=lb->swap=0;lb->state=LB_MODEL;lb_request(MU_MODEL);}}
            else lb_save();
        }else if(++lb->wait>300){lb->state=LB_SAVE_ERROR;lb->cursor=1;lb->message="RELEASE / RECONNECT USB; RESTORE TO RETRY";}
        return;
    }
    if(lb->state==LB_CAPTURE){if(!(lb->divider++&1))lb_request(MU_VALUES);return;}
    if(lb->state==LB_RELEASE){
        if(lb->finished){if(lb_ack()){u32 quit=lb->quit;lb_back();if(!lb&&quit)menu_close(quit==1);return;}return;}
        u32 released=mu_capture_released(&lb->model,&lb->values);
        if(lb->wizard==2){mk_binding *b=lb->draft.control+mk_control_slot(lb->draft.type,lb->cursor-1);
            if(b->kind==MK_LOW||b->kind==MK_HIGH){if(!lb->values.valid[b->field]||(b->kind==MK_LOW?lb->values.value[b->field]<=b->threshold:lb->values.value[b->field]>=b->threshold))released=0;}}
        if(released&&!(menu_raw(menu.owner)&0x900)){
            if(++lb->wait>=3){
                lb_nav_held=lb_old_navigation();lb->edges=0;
                if(lb->wizard==2){
                    u32 count=mk_control_count(lb->draft.type);
                    if(lb->cursor==count){lb_checked_apply();return;}
                    if(lb->draft.type!=MK_DUALSHOCK&&lb->cursor==count-2){lb->state=LB_TURNTABLE;lb->cursor=0;return;}
                    lb->cursor++;lb_guide();
                }else {lb->state=LB_EDIT;lb->message="BINDING CHANGED IN DRAFT ONLY";}
            }
        }else lb->wait=0;
        if(!(lb->divider++&1))lb_request(MU_VALUES);return;
    }
    u32 press=lb->edges;lb->edges=0;
    if(lb->state==LB_HOME){
        if(press&MENU_DOWN)lb->cursor=(lb->cursor+1)%4;
        if(press&MENU_UP)lb->cursor=lb->cursor?lb->cursor-1:3;
        if((press&MENU_BACK)||((press&MENU_OK)&&lb->cursor==3)){lb_back();return;}
        if(press&MENU_OK){if(!lb->cursor){lb->state=LB_EDIT;lb->cursor=1;}
            else {lb->wizard=lb->cursor==1;lb->state=LB_TYPE;lb->cursor=lb->draft.type;}}
    }else if(lb->state==LB_ERROR){if(press&(MENU_OK|MENU_BACK)){lb_back();return;}}
    else if(lb->state==LB_SAVE_ERROR){
        if(press&(MENU_UP|MENU_DOWN))lb->cursor^=1;
        if(press&MENU_OK){if(!lb->cursor&&lb->applied==2){lb->state=LB_ACK;lb->wait=0;lb->finished=0;}else {if(lb->swap){lb->state=LB_SWAP_RESTORE;lb_request(MU_LIST);}else {lb->state=LB_RESTORE_INFO;lb_request(MU_LIVE_INFO);}}return;}
    }else if(lb->state==LB_WARN){
        if(press&(MENU_UP|MENU_DOWN))lb->warn_choice^=1;
        if(press&MENU_BACK){if(lb->wizard)lb_back();else lb->state=LB_EDIT;return;}
        if(press&MENU_OK){if(lb->warn_choice)lb_apply();else if(lb->wizard)lb_back();else lb->state=LB_EDIT;return;}
    }else if(lb->state==LB_TYPE){
        if(press&MENU_DOWN)lb->cursor=(lb->cursor+1)%4;
        if(press&MENU_UP)lb->cursor=lb->cursor?(lb->cursor-1):3;
        if((press&MENU_BACK)||((press&MENU_OK)&&lb->cursor==3)){lb->state=LB_HOME;lb->cursor=0;lb->wizard=0;return;}
        if(press&MENU_OK){u32 type=lb->cursor;if(lb->wizard)lb_clear(&lb->draft,0,sizeof lb->draft);lb->draft.type=type;lb->stick=lb->draft.type==MK_DUALSHOCK;lb->cursor=0;lb->state=LB_EDIT;lb->edges=0;lb->message="OLD BINDINGS STAY ACTIVE WHILE EDITING";if(lb->wizard){lb->state=LB_SWAP_UNPLUG;lb_clear(lb->seen,0,sizeof lb->seen);lb->wait=0;lb_request(MU_LIST);return;}}
    }else if(lb->state>=LB_SWAP_UNPLUG){
        if((press&MENU_BACK)||((press&MENU_OK)&&lb->state!=LB_TURNTABLE&&lb->state!=LB_SWAP_PICK)){if(!lb->applied)lb_back();return;}
        if(lb->state==LB_TURNTABLE||lb->state==LB_SWAP_PICK){
            u32 rows=lb->state==LB_TURNTABLE?4:MU_DEVICES+1;
            if(press&MENU_DOWN)lb->cursor=(lb->cursor+1)%rows;
            if(press&MENU_UP)lb->cursor=lb->cursor?lb->cursor-1:rows-1;
            if(press&MENU_OK){
                if(lb->cursor==rows-1){lb_back();return;}
                if(lb->state==LB_SWAP_PICK){if(lb->candidates[lb->cursor].connected)lb_candidate(lb->cursor);}
                else {lb->turntable=lb->cursor;lb->stick=lb->cursor==0;lb->cursor=mk_control_count(lb->draft.type)-1;lb_guide();}
                return;
            }
        }else if(!(lb->divider++%15))lb_request(MU_LIST);
    }else if(lb->state==LB_EDIT){
        u32 count=mk_control_count(lb->draft.type),rows=lb->clear?count+1:count+6;
        if(press&MENU_DOWN)lb->cursor=(lb->cursor+1)%rows;
        if(press&MENU_UP)lb->cursor=lb->cursor?lb->cursor-1:rows-1;
        if(press&MENU_BACK){if(lb->clear){lb->clear=0;lb->cursor=mk_control_count(lb->draft.type)+1;}else lb_back();return;}
        if(press&MENU_OK){
            if(lb->clear){if(lb->cursor<count)lb->draft.control[mk_control_slot(lb->draft.type,lb->cursor)].kind=0;lb->clear=0;lb->cursor=mk_control_count(lb->draft.type)+1;}
            else if(!lb->cursor){u32 player=1-lb->player;if(!usb_input_config.owned[player])lb->message="OTHER PLAYER USES PHYSICAL PS2 INPUT";else {lb_back();lb_open(player);return;}}
            else if(lb->cursor<=count){lb->state=LB_CAPTURE;lb->capture=1;lb->wait=0;lb_request(MU_VALUES);return;}
            else if(lb->cursor==count+1){lb->clear=1;lb->cursor=0;}
            else if(lb->cursor==count+2)lb->stick^=1;
            else if(lb->cursor==count+3){lb_checked_apply();return;}
            else if(lb->cursor==count+4){lb->wizard=0;lb->cursor=0;lb->state=LB_HOME;lb->edges=0;}
            else {lb_back();return;}
        }
    }
    if(lb && !lb->pending && (lb->state==LB_EDIT||lb->state==LB_SAVE_ERROR||lb->state==LB_TURNTABLE||lb->state==LB_WARN)&&!(lb->divider++&1))lb_request(MU_VALUES);
}
static __attribute__((noinline,minsize)) void lb_mask_text(char *out,const char *label,u32 bits){unsigned n=0;mm_append(out,79,&n,label,mm_length(label));for(u32 j=0;j<MK_CONTROLS;j++)if(bits&(1u<<j)){if(j<7&&lb->draft.type!=MK_DUALSHOCK){mm_number(out,79,&n,j+1);mm_append(out,79,&n," ",1);}else {mm_append(out,79,&n,mk_control_name(lb->draft.type,j),mm_length(mk_control_name(lb->draft.type,j)));mm_append(out,79,&n," ",1);}}if(!bits)mm_append(out,79,&n,"NONE",4);}
static __attribute__((noinline,minsize)) void lb_binding_text(char *out,const mk_binding *b){unsigned n=0;const char *k[]={"UNBOUND","BUTTON ","AXIS LOW ","AXIS HIGH ","HAT ","MOTION CCW ","MOTION CW "};mm_append(out,79,&n,k[b->kind<=6?b->kind:0],mm_length(k[b->kind<=6?b->kind:0]));if(b->kind){mm_number(out,79,&n,b->field);mm_append(out,79,&n," (HID ",6);if(b->field<lb->model.field_count)mm_number(out,79,&n,lb->model.fields[b->field].usage);mm_append(out,79,&n,")",1);}}
static __attribute__((noinline,minsize)) void lb_draw(void){
    fs_text("BIND USB CONTROLLER",0,190,108,100);
    if(!lb){fs_text("NOT ENOUGH MEMORY - BACK",4,110,180,100);return;}
    fs_text(lb->player?"PLAYER 2":"PLAYER 1",0,245,132,100);char line[80];
    if(lb->pending&&(lb->age>180||(lb->operation!=MU_VALUES&&lb->operation!=MU_LIST&&lb->state!=LB_EDIT&&lb->state!=LB_CAPTURE&&lb->state!=LB_SAVE_ERROR))){fs_text(lb->age>180?"USB REQUEST DELAYED - WAIT":"WAITING FOR RESIDENT USB SERVICE",0,70,200,100);return;}
    if(lb->state==LB_HOME){const char *names[]={"REBIND BUTTONS","ASSIGN NEW CONTROLLER","CHANGE CONTROLLER TYPE","BACK"};
        fs_text(mk_type_name(lb->draft.type),0,190,160,100);
        for(u32 i=0;i<4;i++)fs_text(names[i],lb->cursor==i?1:0,100,205+i*30,100);
        fs_text("START SAVE AND EXIT   SELECT DISCARD",0,75,365,100);return;}
    if(lb->state==LB_TYPE){for(u32 i=0;i<4;i++){if(lb->cursor==i)fs_text(">",1,90,180+i*30,100);fs_text(i<3?mk_type_name(i):"BACK",lb->cursor==i?1:0,110,180+i*30,100);}return;}
    if(lb->state==LB_ERROR){fs_text(lb->message,4,55,195,100);fs_text("> BACK",1,120,245,100);return;}
    if(lb->state>=LB_SWAP_UNPLUG){
        if(lb->state==LB_TURNTABLE||lb->state==LB_SWAP_PICK){
            const char *names[]={"STICK AXIS","ABSOLUTE / ROTATING AXIS","BUTTONS","CANCEL"};
            fs_text(lb->state==LB_TURNTABLE?"TURNTABLE INPUT TYPE":"CHOOSE NEW USB INTERFACE",0,100,160,100);
            u32 rows=lb->state==LB_TURNTABLE?4:MU_DEVICES+1;
            for(u32 i=0;i<rows;i++){
                if(lb->state==LB_TURNTABLE)mm_copy(line,names[i],mm_length(names[i])+1);
                else {unsigned n=0;mm_append(line,79,&n,i==MU_DEVICES?"CANCEL":"USB INTERFACE ",i==MU_DEVICES?6:14);if(i<MU_DEVICES){mm_number(line,79,&n,i+1);if(!lb->candidates[i].connected)mm_append(line,79,&n," (UNAVAILABLE)",14);}}
                fs_text(line,lb->cursor==i?1:0,110,195+i*26,100);
            }
            fs_text(lb->draft.type==MK_5KEY?"1/2: MOVE  4: ACCEPT":"1/2: MOVE  6: ACCEPT",0,100,350,100);
        }else {fs_text(lb->state==LB_SWAP_RESTORE?"UNPLUG NEW USB; RECONNECT ORIGINAL":lb->state==LB_SWAP_UNPLUG?"UNPLUG THE PREVIOUS USB CONTROLLER":"PLUG IN THE NEW USB CONTROLLER",0,60,195,100);fs_text(lb->applied?"RESTORE WAITS FOR ORIGINAL CONTROLLER":"> CANCEL",1,90,255,100);}
        return;
    }
    if(lb->state==LB_CAPTURE){fs_text(mk_control_name(lb->draft.type,mk_control_slot(lb->draft.type,lb->cursor-1)),1,210,175,100);fs_text(lb->capture==1?"RELEASE BUTTONS / HAT FIRST":"PRESS THE USB INPUT TO BIND",0,100,210,100);fs_text(lb->wizard?"WAITING - PS2 SQUARE CAN CANCEL":"CAPTURE TIMES OUT AFTER 20 SECONDS",0,95,250,100);fs_text("START / SELECT ARE BINDABLE CONTROLS",0,75,285,100);return;}
    if(lb->state==LB_WARN){lb_mask_text(line,"UNBOUND: ",lb->missing);line[52]=0;fs_text(line,4,60,185,100);lb_mask_text(line,"DUPLICATE: ",lb->duplicates);line[52]=0;fs_text(line,4,60,215,100);fs_text(lb->warn_choice?"  CANCEL / RETURN":"> CANCEL / RETURN",lb->warn_choice?0:1,90,265,100);fs_text(lb->warn_choice?"> APPLY AND SAVE ANYWAY":"  APPLY AND SAVE ANYWAY",lb->warn_choice?1:0,90,295,100);return;}
    if(lb->state==LB_SAVE_ERROR){fs_text(lb->message,4,40,185,100);fs_text(lb->cursor?"  RETRY SAVE":"> RETRY SAVE",lb->cursor?0:1,110,245,100);fs_text(lb->cursor?"> RESTORE PREVIOUS BINDINGS":"  RESTORE PREVIOUS BINDINGS",lb->cursor?1:0,110,275,100);fs_text("OLD FILE GENERATION REMAINS AVAILABLE",0,65,325,100);return;}
    if(lb->state==LB_ACK||lb->state==LB_RELEASE){fs_text(lb->message,0,55,205,100);return;}
    if(lb->state!=LB_EDIT){fs_text("WAITING FOR USB SERVICE",0,120,205,100);return;}
    u32 count=mk_control_count(lb->draft.type),rows=lb->clear?count+1:count+6,top=lb->cursor>3?lb->cursor-3:0;if(top+7>rows)top=rows-7;
    for(u32 i=top;i<rows&&i<top+7;i++){
        if(lb->clear)mm_copy(line,i<count?mk_control_name(lb->draft.type,mk_control_slot(lb->draft.type,i)):"BACK",mm_length(i<count?mk_control_name(lb->draft.type,mk_control_slot(lb->draft.type,i)):"BACK")+1);
        else if(!i)mm_copy(line,"CHANGE PLAYER >",16);
        else if(i<=count){u32 slot=mk_control_slot(lb->draft.type,i-1);char bind[80];lb_binding_text(bind,lb->draft.control+slot);unsigned n=0;mm_append(line,79,&n,mk_control_name(lb->draft.type,slot),mm_length(mk_control_name(lb->draft.type,slot)));mm_append(line,79,&n,": ",2);mm_append(line,79,&n,bind,mm_length(bind));}
        else {const char *s=i==count+1?"UNBIND A CONTROL >":i==count+2?(lb->stick?"SCRATCH AXES: STICK":"SCRATCH AXES: ROTATION"):i==count+3?"APPLY, SAVE AND RETURN":i==count+4?"CONTROLLER MENU >":"DISCARD AND RETURN";mm_copy(line,s,mm_length(s)+1);}
        if(i==lb->cursor)fs_text(">",1,65,165+(i-top)*24,100);line[52]=0;fs_text(line,i==lb->cursor?1:0,85,165+(i-top)*24,100);
    }
    fs_text(menu_scheme(menu.owner)==1||menu_scheme(menu.owner)==4?"D-PAD: MOVE  CIRCLE: ACCEPT  SQUARE: BACK":menu_scheme(menu.owner)==3?"1/2: MOVE  5: ACCEPT":"1/2: MOVE  6: ACCEPT",0,50,350,100);
    fs_text(lb->message?lb->message:"",0,45,380,100);
    fs_text("CONTROLLER SAVES ARE SEPARATE FROM MENU SETTINGS",0,30,405,100);
}
#else
static __attribute__((noinline,minsize)) void lb_open(u32 player){(void)player;menu.page=MENU_BIND;menu.row=0;menu.release=1;}
static __attribute__((noinline,minsize)) void lb_update(void){if(menu_navigation(menu.owner)&(MENU_OK|MENU_BACK)){menu.page=MENU_ROOT;menu.row=4;menu.release=1;}}
static __attribute__((noinline,minsize)) void lb_draw(void){fs_text("USB INPUT SERVICE REQUIRED",4,110,185,100);fs_text("> BACK TO MOD MENU",1,120,245,100);}
#endif
