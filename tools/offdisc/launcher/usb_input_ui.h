/* Included after renderer helpers; all input is event-driven at launcher vsync. */
#include "usb_hid/capture.h"
static unsigned mui_kind,mui_capture_target;
static int mui_device=-1,mui_player_slot=0,mui_cursor=0,mui_screen=0,mui_capture=0,mui_warn=0,mui_warn_choice=0;
static unsigned mui_generation,mui_refresh;
static mu_info mui_devices[MU_DEVICES];
static mh_model mui_model;
static mh_values mui_base;
static mui_player mui_backup;
static char mui_notice[128];
static int mui_cancel,mui_axis_mode;
static int mui_fetch(int operation,mu_reply *out){mu_request r={0};r.slot=mui_device;r.generation=mui_generation;return mui_call(operation,&r,out);}
static void mui_enter(void){mui_screen=0;mui_cursor=0;mui_notice[0]=0;mui_refresh=0;mui_kind=mui_players[mui_player_slot].profile.type;mui_list(mui_devices);}
static int mui_commit(void){
    mui_player draft=mui_players[mui_player_slot];
    if(mui_configure(mui_player_slot))return -1;
    if(!mui_save(SETTINGS_ROOT))return 0;
    mui_players[mui_player_slot]=mui_backup;
    int rollback=mui_configure(mui_player_slot);
    mui_players[mui_player_slot]=draft;
    snprintf(mui_notice,sizeof mui_notice,rollback?"Save/apply failed; restart launcher":"Save failed; previous bindings restored");
    return -1;
}
static int mui_arm(int key){
    mu_reply reply;if(mui_fetch(MU_VALUES,&reply)){snprintf(mui_notice,sizeof mui_notice,"Device disconnected; choose it again");return -1;}
    mui_base=reply.data.values;mui_cancel=0;mui_capture=1;mui_capture_target=key;return 0;
}
static void mui_capture_poll(unsigned physical){
    static unsigned old_physical;unsigned fresh=physical&~old_physical;old_physical=physical;
    if(fresh&PAD_DOWN)mui_cancel=1;
    if(mui_cancel&&(fresh&PAD_CROSS)){mui_capture=0;mui_cancel=0;return;}
    mu_reply r;if(mui_fetch(MU_VALUES,&r)){mui_capture=0;snprintf(mui_notice,sizeof mui_notice,"Device disconnected; binding cancelled");return;}
    mh_values *v=&r.data.values;
    if(mui_capture==1){
        if(physical||!mu_capture_released(&mui_model,v))return;
        mui_base=*v;mui_capture=2;return;
    }
    mk_binding binding;
    if(mu_capture_control(&mui_model,&mui_base,v,mk_capture_target(mui_players[mui_player_slot].profile.type,mui_capture_target),mui_axis_mode,&binding)){
        mui_players[mui_player_slot].profile.control[mui_capture_target]=binding;mui_capture=0;
        snprintf(mui_notice,sizeof mui_notice,"%s bound. Release input to continue.",mk_control_name(mui_players[mui_player_slot].profile.type,mui_capture_target));
    }
}
static void mui_binding_text(char *out,unsigned n,const mk_binding *b){
    if(!b->kind){snprintf(out,n,"unbound");return;}
    if(b->field>=mui_model.field_count){snprintf(out,n,"invalid - rebind");return;}
    const mh_field *f=mui_model.fields+b->field;
    const char *kind=b->kind==MK_BUTTON?"Button":b->kind==MK_HAT?"Hat":b->kind==MK_LOW?"Axis -":b->kind==MK_HIGH?"Axis +":b->kind==MK_MOTION_NEG?"Motion -":"Motion +";
    snprintf(out,n,"%s %u (report %u)",kind,f->usage,mui_model.reports[f->report].id);
}
static void mui_draw_art(int selected); /* native EMPRESS art below */
static void mui_draw(void){
    char line[160];frame_begin();int y=28;
    text_c(y,"USB CONTROLLER",C_HEAD);y+=ROW+8;
    if(!mui_screen){
        snprintf(line,sizeof line,"Assign to: PLAYER %d  (X changes)",mui_player_slot+1);row(y,mui_cursor==0,line,C_TEXT);y+=ROW;
        snprintf(line,sizeof line,"Controller type: %s",mk_type_name(mui_kind));row(y,mui_cursor==1,line,C_TEXT);y+=ROW;
        for(unsigned i=0;i<MU_DEVICES;i++){
            const mu_info *d=mui_devices+i;
            if(d->generation)snprintf(line,sizeof line,"USB %u: %04X:%04X interface %u%s",i+1,d->identity.vendor,d->identity.product,d->identity.interface,d->connected?"":" (waiting)");
            else snprintf(line,sizeof line,"USB %u: no gamepad",i+1);
            row(y,mui_cursor==(int)i+2,line,C_TEXT);y+=ROW;
        }
        row(y,mui_cursor==6,"Use physical PS2 controller for this player",C_TEXT);y+=ROW;
        row(y,mui_cursor==7,"Exit",C_TEXT);y+=ROW;
        text_c(y+8,mui_status(),C_DIM);
    }else if(mui_warn){
        uint16_t missing,duplicates;mk_validate(&mui_model,&mui_players[mui_player_slot].profile,&missing,&duplicates);
        text_c(y,"Bindings need attention",C_HEAD);y+=ROW;
        text_c(y,"Some controls are unbound or share an input.",C_TEXT);y+=ROW+8;
        for(unsigned kind=0;kind<2;kind++){
            unsigned mask=kind?duplicates:missing;
            snprintf(line,sizeof line,"%s: ",kind?"Duplicate":"Unbound");
            if(!mask)strcat(line,"none");
            for(unsigned i=0;i<MK_CONTROLS;i++)if(mask>>i&1){strncat(line,mk_control_name(mui_players[mui_player_slot].profile.type,i),sizeof line-strlen(line)-1);strncat(line,"  ",sizeof line-strlen(line)-1);}
            text_fit(24,y,line,C_TEXT,gs->Width-48);y+=ROW+12;
        }
        row(y+8,!mui_warn_choice,"Return to editing",C_TEXT);row(y+8+ROW,mui_warn_choice,"Save and exit anyway",C_TEXT);
    }else{
        snprintf(line,sizeof line,"PLAYER %d  -  %s",mui_player_slot+1,mui_capture?"Press the USB input to bind":"Choose a control, then X to bind");text_c(y,line,C_TEXT);y+=ROW;
        unsigned type=mui_players[mui_player_slot].profile.type,count=mk_control_count(type);
        mui_draw_art(mui_capture?mui_capture_target:mui_cursor<(int)count?mk_control_slot(type,mui_cursor):-1);
        int top=mui_cursor>5?mui_cursor-5:0;if(top>(int)count-2)top=count-2;
        for(int i=top;i<(mui_screen==2?(int)count+1:(int)count+4)&&i<top+7;i++){
            if(i<(int)count){unsigned slot=mk_control_slot(type,i);char b[80];mui_binding_text(b,sizeof b,&mui_players[mui_player_slot].profile.control[slot]);snprintf(line,sizeof line,"%s: %s",mk_control_name(type,slot),b);}
            else if(mui_screen==2)snprintf(line,sizeof line,"Return to editor");
            else snprintf(line,sizeof line,"%s",i==(int)count?"Unbind a control >":i==(int)count+1?"Save and exit":i==(int)count+2?"Discard and exit":mui_axis_mode?"Scratch axes: stick":"Scratch axes: rotation");
            int x=gs->Width/2+12;
            if(mui_cursor==i)gsKit_prim_sprite(gs,x-6,y+3,gs->Width-20,y+ROW+1,1,RGBA(0x70,0x10,0x48,0x50));
            text_fit(x,y,line,mui_cursor==i?C_SEL:C_TEXT,gs->Width/2-32);y+=ROW;
        }
        if(mui_capture)text(24,gs->Height-96,mui_cancel?"> Cancel binding: press gamepad X":"Gamepad DOWN: select Cancel binding",C_DIM);
        text(24,gs->Height-68,mui_capture==1?"Release inputs first":mui_capture?"Waiting for USB input...":mui_notice,C_DIM);
        text(24,gs->Height-40,"START saves   SQUARE unbinds   SELECT changes navigation",C_DIM);
    }
    frame_end();
}
/* Returns 1 to leave this submenu. Captured inputs are consumed by caller. */
static int mui_ui(unsigned nb,unsigned physical){
    unsigned count=mk_control_count(mui_players[mui_player_slot].profile.type);
    if(!mui_screen){
        if(!(mui_refresh++%60))mui_list(mui_devices);
        if(nb&PAD_UP)mui_cursor=(mui_cursor+7)%8;
        if(nb&PAD_DOWN)mui_cursor=(mui_cursor+1)%8;
        if(nb&PAD_CROSS){
            if(mui_cursor==0){mui_player_slot^=1;mui_kind=mui_players[mui_player_slot].profile.type;}
            else if(mui_cursor==1)mui_kind=(mui_kind+1)%3;
            else if(mui_cursor==7)return 1;
            else if(mui_cursor==6){mui_backup=mui_players[mui_player_slot];mui_players[mui_player_slot].enabled=0;if(mui_commit()){mui_players[mui_player_slot]=mui_backup;snprintf(mui_notice,sizeof mui_notice,"Could not save controller settings");}else return 1;}
            else{
                mui_device=mui_cursor-2;mui_generation=mui_devices[mui_device].generation;mu_reply r;
                if(mui_generation&&!mui_fetch(MU_MODEL,&r)){
                    mui_model=r.data.model;mui_backup=mui_players[mui_player_slot];
                    if(memcmp(&mui_backup.identity,&mui_devices[mui_device].identity,sizeof mui_backup.identity)||mui_backup.profile.type!=mui_kind)memset(&mui_players[mui_player_slot],0,sizeof mui_players[0]);
                    mui_players[mui_player_slot].profile.type=mui_kind;mui_axis_mode=mui_kind==MK_DUALSHOCK;
                    mui_players[mui_player_slot].identity=mui_devices[mui_device].identity;mui_players[mui_player_slot].enabled=1;
                    mui_screen=1;mui_cursor=0;mui_notice[0]=0;
                }
            }
        }
    }else if(mui_capture){mui_capture_poll(physical);}
    else if(mui_screen==2){
        if(nb&PAD_UP)mui_cursor=(mui_cursor+count)%(count+1);
        if(nb&PAD_DOWN)mui_cursor=(mui_cursor+1)%(count+1);
        if(nb&PAD_CROSS){if(mui_cursor<(int)count)mui_players[mui_player_slot].profile.control[mk_control_slot(mui_players[mui_player_slot].profile.type,mui_cursor)].kind=0;mui_screen=1;mui_cursor=0;}
    }
    else if(mui_warn){
        if(nb&(PAD_UP|PAD_DOWN))mui_warn_choice^=1;
        if(nb&PAD_CROSS){if(mui_warn_choice){if(!mui_commit()){mui_warn=0;return 1;}snprintf(mui_notice,sizeof mui_notice,"Save failed; return to editing");}else mui_warn=0;}
    }else{
        if(nb&PAD_UP)mui_cursor=(mui_cursor+count+3)%(count+4);
        if(nb&PAD_DOWN)mui_cursor=(mui_cursor+1)%(count+4);
        if((nb&PAD_SQUARE)&&mui_cursor<(int)count)mui_players[mui_player_slot].profile.control[mk_control_slot(mui_players[mui_player_slot].profile.type,mui_cursor)].kind=0;
        if(nb&PAD_START || ((nb&PAD_CROSS)&&mui_cursor==(int)count+1)){
            uint16_t missing,duplicates;
            if(mk_validate(&mui_model,&mui_players[mui_player_slot].profile,&missing,&duplicates))snprintf(mui_notice,sizeof mui_notice,"Invalid binding; please edit again");
            else if(missing||duplicates){mui_warn=1;mui_warn_choice=0;}
            else if(!mui_commit()){return 1;}
            else snprintf(mui_notice,sizeof mui_notice,"Could not save controller settings");
        }else if(nb&PAD_CROSS){
            if(mui_cursor<(int)count)mui_arm(mk_control_slot(mui_players[mui_player_slot].profile.type,mui_cursor));
            else if(mui_cursor==(int)count){/* No back button needed: choose a control,
                * then use the separate unbind action to clear that control. */mui_screen=2;mui_cursor=0;}
            else if(mui_cursor==(int)count+3){mui_axis_mode^=1;}
            else if(mui_cursor==(int)count+2){mui_players[mui_player_slot]=mui_backup;mui_configure(mui_player_slot);return 1;}
        }
    }
    mui_draw();return 0;
}
static void mui_draw_art(int selected){
    static GSTEXTURE art[3];static int loaded[3];unsigned type=mui_players[mui_player_slot].profile.type;GSTEXTURE *pic=art+type;
    if(!loaded[type]){int len;u8 *data=d1_load(309,&len);loaded[type]=sprite_tex(pic,data,len,type==MK_DUALSHOCK?24:type==MK_5KEY?22:23,GS_FILTER_LINEAR)?-1:1;free(data);}
    if(loaded[type]<0){text(24,140,"EMPRESS controller art missing",C_DIM);return;}
    float x=24,y=138,w=gs->Width/2-48,scale=w/pic->Width,h=pic->Height*scale;
    gsKit_TexManager_bind(gs,pic);
    gsKit_prim_sprite_texture(gs,pic,x,y,0,0,x+w,y+h,pic->Width,pic->Height,2,WHITE);
    /* Regions measured on the unmodified native sprite23 (D309 sheet7).
     * The selected target, including START/SELECT and either scratch direction,
     * is shown on the diagram even before any USB control is assigned. */
    static const int region[11][4]={{173,74,12,22},{183,46,12,22},{192,74,12,22},{202,46,12,22},{211,74,12,22},{221,46,12,22},{230,74,12,22},{162,43,10,7},{162,34,10,7},{47,21,98,98},{47,21,98,98}};
    static const int five[11][4]={{25,88,10,18},{31,70,10,16},{37,88,10,18},{43,70,10,16},{49,88,10,18},{0,0,0,0},{0,0,0,0},{17,53,12,7},{6,53,10,7},{67,55,64,64},{67,55,64,64}};
    static const int ds[14][4]={{78,44,14,14},{78,73,14,14},{64,58,14,14},{93,58,14,14},{177,40,14,14},{177,74,14,14},{160,57,14,14},{194,57,14,14},{68,11,33,9},{68,3,33,8},{169,11,33,9},{169,3,33,8},{138,58,12,10},{118,58,12,10}};
    if(selected>=0&&selected<(int)(type==MK_DUALSHOCK?14:11)){const int *r=type==MK_DUALSHOCK?ds[selected]:type==MK_5KEY?five[selected]:region[selected];float l=x+r[0]*scale,t=y+r[1]*scale,rr=l+r[2]*scale,b=t+r[3]*scale;
        gsKit_prim_sprite(gs,l,t,rr,b,3,RGBA(0x30,0x10,0x30,0x45));
        gsKit_prim_line(gs,l,t,rr,t,4,C_SEL);gsKit_prim_line(gs,l,b,rr,b,4,C_SEL);gsKit_prim_line(gs,l,t,l,b,4,C_SEL);gsKit_prim_line(gs,rr,t,rr,b,4,C_SEL);
        text_fit(24,(int)(y+h+12),mk_control_name(type,selected),C_SEL,(int)w);
    }
}
