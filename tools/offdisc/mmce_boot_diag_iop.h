/* IOP startup diagnostics only. Supplied trace storage belongs to the EE loader. */
#ifndef MEGAMIX_MMCE_BOOT_DIAG_IOP_H
#define MEGAMIX_MMCE_BOOT_DIAG_IOP_H
void megamix_diag_bind(int argc,char **argv);
void megamix_diag(unsigned stage,unsigned a,unsigned b);
void megamix_diag_end(void);
#endif
