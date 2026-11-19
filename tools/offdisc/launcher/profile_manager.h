/* Launcher-only, read-only MC browsing. Creation is reserved here and performed
 * by native SAVE, including its directory/icons and recoverable A/B commit.
 * No MC files are overwritten or initialized by this menu. */
#ifndef PROFILE_MANAGER_TEST
#include <libmc.h>
#include <delaythread.h>
#endif
#include "profile_schema.h"
static int pm_ready,pm_poison,pm_slot,pm_page,pm_cursor,pm_count,pm_more,pm_edit=-1;
static char pm_dirs[16][ML_DIRECTORY],pm_status[160]="Choose a card, then Open or New profile";
static char pm_game_id[11];
static int pm_game_changed,pm_browsing,pm_changing;
static sceMcTblGetDir pm_table[16] __attribute__((aligned(64)));
static int pm_wait(int accepted,int *value)
{
    if(accepted<0 || pm_poison)return -1;
    for(unsigned tick=0;;){
        int command,result;int r=mcSync(1,&command,&result);
        if(r==1){*value=result;return 0;}
        if(r<0){
            /* Completion ownership is uncertain. Never return to a caller
             * that would free/reuse its DMA buffer or issue another request. */
            pm_poison=1;
            for(;;)message("Memory-card completion was lost","Restart launcher; profile was not changed");
        }
        if(tick>=600)message("Still waiting for the memory card...","Keep waiting, or restart launcher");
        else if(!(tick%6))message("Reading memory-card profiles...",0);
        DelayThread(16667);
        if(tick<600)tick++;
    }
}
static int pm_start(void)
{
    if(pm_poison)return -1;
    if(!pm_ready){
#ifdef MMCE_BENCH
        if(mmce_boot_set_game_id(selected_game_id) || mmce_boot_select_game_card()){
            snprintf(pm_status,sizeof pm_status,"Game ID switch failed; profile not changed");return -1;
        }
#endif
        /* SDK libmc has its own documented RPC client. Never substitute SDK
         * fio for the game's FILEIO. The entire IOP is restored at handoff. */
        if(SifSearchModuleByName("mcman")<0 && SifSearchModuleByName("mcman_cex")<0 &&
           SifSearchModuleByName("xfromman")<0 && SifLoadModule("rom0:MCMAN",0,0)<0){
            snprintf(pm_status,sizeof pm_status,"Could not start memory-card manager");return -1;
        }
        if(SifSearchModuleByName("mcserv")<0 && SifSearchModuleByName("xfrmserv")<0 && SifLoadModule("rom0:MCSERV",0,0)<0){
            snprintf(pm_status,sizeof pm_status,"Could not start memory-card service");return -1;
        }
        if(mcInit(MC_TYPE_MC)<0){snprintf(pm_status,sizeof pm_status,"Memory-card service unavailable");return -1;}
        pm_ready=1;
    }
    int type=0,free_blocks=0,formatted=0,result;
    if(pm_wait(mcGetInfo(pm_slot,0,&type,&free_blocks,&formatted),&result) || result<-2 || type!=2 || formatted!=1){
        snprintf(pm_status,sizeof pm_status,"Insert a formatted PS2 card in slot %d",pm_slot+1);return -1;
    }
    return 0;
}
static int pm_stat(const char *path,unsigned *size)
{
    int result;
    memset(pm_table,0,sizeof pm_table);
    if(pm_wait(mcGetDir(pm_slot,0,path,0,1,pm_table),&result))return -1;
    if(result==0 || result==-4)return 0;
    if(result!=1)return -1;
    *size=pm_table[0].FileSizeByte;return 1;
}
static int pm_close(int fd)
{
    int result;return pm_wait(mcClose(fd),&result) || result?-1:0;
}
typedef struct {int fd;unsigned length,header_crc;} pm_reader;
static int pm_read(void *context,mp_u64 offset,void *data,mp_u32 n)
{
    pm_reader *r=context;int result;
    if(offset>r->length || n>r->length-offset || n>MP_PAGE || !n)return -1;
    if(pm_wait(mcSeek(r->fd,(int)offset,0),&result) || result!=(int)offset)return -1;
    if(pm_wait(mcRead(r->fd,data,n),&result) || result!=(int)n)return -1;
    if(!offset && n==MP_HEADER)r->header_crc=mp_read32((const unsigned char *)data+124);
    return 0;
}
typedef struct {mp_schema_state schema;mp_visitor consumer;unsigned native,charts,opaque;} pm_schema;
static int pm_section(void *context,mp_u64 index,const mp_section *s)
{
    pm_schema *state=context;
    if(s->type==MP_NATIVE){
        if(s->version!=1 || s->stride!=1 || s->bytes!=833290)return MP_UNSUPPORTED;
        state->native=1;
    }
    if(s->type==MP_CHARTS){
        if(s->version!=1 || s->stride!=48)return MP_UNSUPPORTED;
        if(s->bytes>4u*1024u*1024u)return MP_LIMIT;state->charts=1;
    }
    else if(s->type!=MP_NATIVE){
        if(state->opaque>512u*1024u-80u || s->bytes>512u*1024u-80u-state->opaque)return MP_LIMIT;
        state->opaque+=80u+(unsigned)s->bytes;
    }
    return state->consumer.section(state->consumer.context,index,s);
}
static int pm_data(void *context,mp_u64 index,const mp_section *s,mp_u64 at,const void *p,mp_u32 n)
{
    pm_schema *state=context;return state->consumer.page(state->consumer.context,index,s,at,p,n);
}
/*0 absent,1 validated compact,2 validated native import,<0 blocked. Full
 * readback/schema/CRC and successful close precede selection; no header-only
 * acceptance. Opposing valid UUIDs and unsupported schemas block selection. */
static int pm_check(const char *directory,mp_info *best)
{
    mp_info pair[2];unsigned signature[2]={0};int good[2]={0},present=0,fd,result,error=0;char path[80];
    mp_work *work=memalign(64,sizeof *work);if(!work)return -1;
    for(unsigned side=0;side<2;side++){
        snprintf(path,sizeof path,"/%s/PROFILE.%c",directory,'A'+side);
        unsigned size;int found=pm_stat(path,&size);
        if(found<0){error=MP_IO;break;}if(!found)continue;present=1;
        if(size>0x7fffffff){error=MP_LIMIT;break;}
        if(size<MP_HEADER+MP_FOOTER)continue;
        if(pm_wait(mcOpen(pm_slot,0,path,1),&fd) || fd<0){error=MP_IO;break;}
        pm_reader reader={fd,size,0};mp_io io={&reader,pm_read,0,0,0x7fffffff};
        pm_schema state;memset(&state,0,sizeof state);mp_schema_begin(&state.schema,&state.consumer);
        mp_visitor visitor={&state,pm_section,pm_data};
        result=mp_check(&io,size,pair+side,&visitor,work);
        if(!result)result=mp_schema_finish(&state.schema);
        if(!result && (!state.native || !state.charts))result=MP_UNSUPPORTED;
        signature[side]=reader.header_crc;
        if(pm_close(fd)){error=MP_IO;break;}
        if(result==MP_UNSUPPORTED || result==MP_LIMIT || result==MP_IO){error=result;break;}
        good[side]=result==MP_OK;
    }
    free(work);if(error)return error;
    if(good[0] && good[1] && memcmp(pair[0].uuid,pair[1].uuid,16))return -6;
    if(good[0] && good[1] && pair[0].generation==pair[1].generation && signature[0]!=signature[1])return -6;
    if(good[0] || good[1]){*best=pair[good[1] && (!good[0] || pair[1].generation>pair[0].generation)];return 1;}
    if(present)return MP_CORRUPT;
    if(strcmp(directory,"BISLPM-55221BM2DX16"))return 0;
    snprintf(path,sizeof path,"/%s/%s",directory,directory);
    unsigned size;result=pm_stat(path,&size);if(result<=0)return result;
    if(size!=833290)return MP_CORRUPT;
    if(pm_wait(mcOpen(pm_slot,0,path,1),&fd) || fd<0)return MP_IO;
    unsigned char *data=memalign(64,4096);if(!data){pm_close(fd);return MP_CALLBACK;}
    pm_reader reader={fd,size,0};unsigned expected=0,crc=~0u;error=0;
    for(unsigned at=0;at<size;){
        unsigned n=size-at;if(n>4096)n=4096;
        if(pm_read(&reader,at,data,n)){error=MP_IO;break;}
        unsigned skip=at?0:4;if(!at)expected=mp_read32(data);
        crc=mp_crc(crc,data+skip,n-skip);at+=n;
    }
    free(data);if(pm_close(fd))error=MP_IO;
    if(error)return error;
    return ~crc==expected?2:MP_CORRUPT;
}
static unsigned pm_serial;
static void pm_uuid(unsigned char uuid[16])
{
    /* UUID is an identity, not a secret. Mix the user-paced EE timer and prior
     * identity; a generated directory is checked absent before it is offered. */
    unsigned long long now=GetTimerSystemTime();
    unsigned state[4];memcpy(state,save_create_uuid,16);
    state[0]^=(unsigned)now;state[1]^=(unsigned)(now>>32);state[2]^=++pm_serial;state[3]^=0x84c7e59b;
    for(unsigned i=0;i<20;i++){
        unsigned t=state[3],u=state[0];state[3]=state[2];state[2]=state[1];state[1]=u;
        t^=t<<11;t^=t>>8;state[0]=t^u^(u>>19);
        if(i>=16)memcpy(uuid+4*(i-16),state,4);
    }
    uuid[6]=(uuid[6]&15)|64;uuid[8]=(uuid[8]&63)|128;
}
static int pm_choose(const char *directory,int create)
{
    ml_target next={ML_SCHEMA,1,(unsigned)pm_slot,ML_OPEN,{0},{0},{0}};mp_info info;
    if(pm_start())return -1;
    if(create){
        for(unsigned attempt=0;attempt<32;attempt++){
            pm_uuid(next.uuid);snprintf(next.directory,sizeof next.directory,"MMIX%02X%02X%02X%02X%02X%02X",
                next.uuid[0],next.uuid[1],next.uuid[2],next.uuid[3],next.uuid[4],next.uuid[5]);
            char path[64];snprintf(path,sizeof path,"/%s",next.directory);unsigned size;
            int exists=pm_stat(path,&size);if(exists<0)return -1;
            if(!exists){next.action=ML_CREATE;break;}
        }
        if(next.action!=ML_CREATE)return -1;
    }else{
        int checked=pm_check(directory,&info);
        if(checked<1){snprintf(pm_status,sizeof pm_status,"Profile missing, damaged or unsupported (%d)",checked);return -1;}
        strcpy(next.directory,directory);
        if(checked==1)memcpy(next.uuid,info.uuid,16);
        else {pm_uuid(next.uuid);next.action=ML_IMPORT;}
    }
    if(!ml_valid(&next))return -1;
    ml_target previous=selected_profile;unsigned char uuid[16];memcpy(uuid,save_create_uuid,16);int format=save_format,prompt=profile_prompt;
    selected_profile=next;memcpy(save_create_uuid,next.uuid,16);save_format=2;profile_prompt=next.action!=ML_OPEN;
    if(cfg_save()){
        selected_profile=previous;memcpy(save_create_uuid,uuid,16);save_format=format;profile_prompt=prompt;
        snprintf(pm_status,sizeof pm_status,"Settings not saved: %.110s",cfg_error_status);return -1;
    }
    snprintf(pm_status,sizeof pm_status,next.action==ML_CREATE?"New profile selected; created on first in-game SAVE":
             next.action==ML_IMPORT?"EMPRESS import selected; original stays unchanged":"Profile verified and selected");return 0;
}
static int pm_resume_pending(void)
{
    /* CREATE/IMPORT permission is session-only. On the next launch, clear the
     * first-save prompt only after the remembered UUID is actually committed.
     * An absent/incomplete destination requires a new explicit menu choice. */
    if(!profile_prompt || !selected_profile.managed || save_config_error)return 0;
    pm_slot=selected_profile.port;mp_info info;
    if(pm_start())return 0;
    int checked=pm_check(selected_profile.directory,&info);
    if(checked!=1 || memcmp(info.uuid,selected_profile.uuid,16)){
        snprintf(pm_status,sizeof pm_status,"Previous profile is not ready; open a profile or choose New");return 0;
    }
    profile_prompt=0;
    if(cfg_save()){profile_prompt=1;snprintf(pm_status,sizeof pm_status,"Could not remember the completed profile");return 0;}
    snprintf(pm_status,sizeof pm_status,"Profile verified and selected");return 1;
}
static int pm_list(void)
{
    if(pm_start())return -1;
    int result,mode=0;unsigned skip=pm_page*16,matched=0;
    pm_count=pm_more=0;
    for(;;){
        if(pm_wait(mcGetDir(pm_slot,0,"/*",mode,16,pm_table),&result) || result<0)return -1;
        mode=1;if(!result)break;
        if(result>16)return -1;
        for(int i=0;i<result;i++){
            const char *name=(const char *)pm_table[i].EntryName;
            unsigned len=0;while(len<32 && name[len])len++;
            if(!(pm_table[i].AttrFile&0x20) || !len || len>=ML_DIRECTORY)continue;
            if(strcmp(name,"BISLPM-55221BM2DX16") && strncmp(name,"MMIX",4))continue;
            if(matched++<skip)continue;
            if(pm_count==16){pm_more=1;return 0;}
            strcpy(pm_dirs[pm_count++],name);
        }
    }
    pm_cursor=0;return 0;
}
static void pm_enter(void)
{
    pm_slot=selected_profile.port;pm_cursor=pm_page=pm_browsing=pm_changing=0;pm_edit=-1;
    strcpy(pm_game_id,selected_game_id);
}
static void pm_draw(void)
{
    char label[128];frame_begin();int y=32,step=gs->Height<480?27:31;
    text_c(y,"SAVE PROFILES",C_TEXT);y+=step+8;
    if(pm_edit>=0){
        text_c(y,pm_game_id[0]?pm_game_id:"No Game ID",C_ON);y+=step;
        if(pm_edit<9){unsigned pos=pm_edit<4?pm_edit:pm_edit+1;
            snprintf(label,sizeof label,"Character %d/9: %c%s",pm_edit+1,pm_game_id[0]?pm_game_id[pos]:'-',pm_changing?"  < edit >":"");}
        else snprintf(label,sizeof label,"Characters 1-9");
        row(y,pm_edit<9,label,C_TEXT);y+=step;
        row(y,pm_edit==9,"Apply",C_TEXT);y+=step;
        row(y,pm_edit==10,"No Game ID",C_TEXT);y+=step;
        row(y,pm_edit==11,"Cancel",C_TEXT);y+=step;
        text_c(y,pm_changing?"UP/DOWN change character; X done":"UP/DOWN select; X edit or accept",C_DIM);
    }else if(pm_browsing){
        unsigned top=pm_cursor>=8?pm_cursor-7:0;
        for(unsigned i=top;i<(unsigned)pm_count && i<top+8;i++){row(y,pm_cursor==(int)i,pm_dirs[i],C_TEXT);y+=step;}
        row(y,pm_cursor==pm_count,"Next page",pm_more?C_TEXT:C_DIM);y+=step;
        row(y,pm_cursor==pm_count+1,"Previous page",pm_page?C_TEXT:C_DIM);y+=step;
        row(y,pm_cursor==pm_count+2,"Back",C_TEXT);
    }else{
        snprintf(label,sizeof label,"Memory card: < slot %d >",pm_slot+1);row(y,pm_cursor==0,label,C_TEXT);y+=step;
        snprintf(label,sizeof label,"Game ID: %s",selected_game_id[0]?selected_game_id:"unchanged");row(y,pm_cursor==1,label,C_TEXT);y+=step;
        row(y,pm_cursor==2,"Open existing profile  >",C_TEXT);y+=step;
        row(y,pm_cursor==3,"New empty profile",C_TEXT);y+=step;
        row(y,pm_cursor==4,"Back",C_TEXT);y+=step;
        snprintf(label,sizeof label,"Current: slot %u / %.40s",selected_profile.port+1,selected_profile.managed?selected_profile.directory:"EMPRESS / MEGAMIX");
        text_c(y,label,C_DIM);
    }
    text_fit(24,gs->Height-52,pm_status,C_ON,gs->Width-48);frame_end();
}
/* START stays in this menu: selecting or binding never accidentally boots. */
static int pm_ui(unsigned nb)
{
    if(pm_edit>=0){
        if(nb&PAD_CIRCLE){pm_edit=-1;pm_changing=0;pm_draw();return 0;}
        if(nb&PAD_LEFT)pm_edit=pm_edit<9?(pm_edit+8)%9:8;
        if(nb&PAD_RIGHT)pm_edit=pm_edit<9?(pm_edit+1)%9:0;
        if(pm_changing){
            if(nb&(PAD_UP|PAD_DOWN)){
                unsigned pos=pm_edit<4?pm_edit:pm_edit+1;
                const char *alphabet=pos<4?"ABCDEFGHIJKLMNOPQRSTUVWXYZ":"0123456789";
                int letters=pos<4?26:10;
                if(!pm_game_id[0])strcpy(pm_game_id,"MMIX-00001");
                const char *p=strchr(alphabet,pm_game_id[pos]);int k=p?p-alphabet:0;
                pm_game_id[pos]=alphabet[(k+((nb&PAD_UP)?1:letters-1))%letters];
            }
            if(nb&PAD_CROSS)pm_changing=0;
            pm_draw();return 0;
        }
        if(nb&PAD_UP)pm_edit=(pm_edit+11)%12;
        if(nb&PAD_DOWN)pm_edit=(pm_edit+1)%12;
        if(nb&PAD_SQUARE){pm_game_id[0]=0;pm_edit=9;}
        if(nb&PAD_CROSS){
            if(pm_edit<9){pm_changing=1;pm_draw();return 0;}
            if(pm_edit==11){pm_edit=-1;pm_draw();return 0;}
            if(pm_edit==10)pm_game_id[0]=0;
            if(!ml_game_id(pm_game_id)){snprintf(pm_status,sizeof pm_status,"Use four letters, a hyphen and five digits");pm_draw();return 0;}
            char before[11];int prompt=profile_prompt;strcpy(before,selected_game_id);strcpy(selected_game_id,pm_game_id);
            int changed=strcmp(before,selected_game_id)!=0;if(changed)profile_prompt=1;
            if(cfg_save()){strcpy(selected_game_id,before);profile_prompt=prompt;snprintf(pm_status,sizeof pm_status,"Game ID settings save failed");}
            else {pm_game_changed|=changed;snprintf(pm_status,sizeof pm_status,changed?"Game ID saved; restart launcher before selecting a profile":"Game ID unchanged");pm_edit=-1;}
        }
        pm_draw();return 0;
    }
    int rows=pm_browsing?pm_count+3:5;
    if(nb&PAD_UP)pm_cursor=(pm_cursor+rows-1)%rows;
    if(nb&PAD_DOWN)pm_cursor=(pm_cursor+1)%rows;
    if(nb&(PAD_CIRCLE|PAD_TRIANGLE)){if(pm_browsing){pm_browsing=0;pm_cursor=2;}else return 1;}
    if(!pm_browsing && pm_cursor==0 && nb&(PAD_LEFT|PAD_RIGHT|PAD_CROSS))pm_slot^=1;
    if(nb&PAD_CROSS){
        if(pm_browsing){
            if(pm_cursor<pm_count){if(!pm_choose(pm_dirs[pm_cursor],0)){pm_browsing=0;pm_cursor=2;}}
            else if(pm_cursor==pm_count && pm_more){pm_page++;pm_cursor=0;pm_list();}
            else if(pm_cursor==pm_count+1 && pm_page){pm_page--;pm_cursor=0;pm_list();}
            else if(pm_cursor==pm_count+2){pm_browsing=0;pm_cursor=2;}
        }else if(pm_cursor==1){
#ifdef MMCE_BENCH
            pm_edit=0;pm_changing=0;strcpy(pm_game_id,selected_game_id);
#else
            snprintf(pm_status,sizeof pm_status,"Game ID requires a compatible memory-card adapter");
#endif
        }
        else if(pm_cursor==4)return 1;
        else if((pm_cursor==2 || pm_cursor==3) && pm_game_changed){snprintf(pm_status,sizeof pm_status,"Restart launcher to apply the new Game ID first");}
        else if(pm_cursor==2){pm_page=pm_cursor=0;if(!pm_list())pm_browsing=1;else snprintf(pm_status,sizeof pm_status,"Could not list profiles on this card");}
        else if(pm_cursor==3)pm_choose(0,1);
    }
    pm_draw();return 0;
}
