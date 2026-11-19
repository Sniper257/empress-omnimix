#ifndef MEGAMIX_MMCE_DIAG_H
#define MEGAMIX_MMCE_DIAG_H
int mmce_diag_open(char *args,unsigned capacity);
void mmce_diag_close(void);
int mmce_diag_enabled(void);
int mmce_diag_load(void *ptr,int arg_len,const char *args,int *modres);
#endif
