#ifndef MEGAMIX_USB_INPUT_H
#define MEGAMIX_USB_INPUT_H
#include "usb_hid/native.h"
#include "usb_hid/profile_store.h"
typedef mu_saved_player mui_player;
extern mui_player mui_players[2];
extern unsigned mui_navigation;
int mui_boot(void);
void mui_trace_enable(int enabled,const char *root);
void mui_trace_phase(const char *phase,int result);
int mui_start_boot_trace(void);
int mui_boot_game(void);
int mui_quiesce(void);
int mui_game_target(unsigned address, unsigned *gate_address, unsigned *token);
int mui_ensure_boot(void);
int mui_call(int operation, mu_request *request, mu_reply *reply);
int mui_list(mu_info out[MU_DEVICES]);
int mui_configure(unsigned player);
int mui_target(unsigned address);
void mui_load(const char *root);
int mui_save(const char *root);
unsigned mui_buttons(void);
const char *mui_status(void);
void mui_diagnostic_save(const char *root);
#endif
