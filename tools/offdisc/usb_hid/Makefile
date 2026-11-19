IOP_BIN = megahid.irx
IOP_OBJS = driver.o hid_report.o bindings.o scratch.o imports.o
IOP_SRC_DIR = ./
IOP_CFLAGS += -std=gnu99 -Wall -Wextra -Werror
IOP_LIBS += -lgcc
ifeq ($(BOOT_DIAG),1)
IOP_CFLAGS += -DMU_BOOT_DIAG_BUILD
endif
all: $(IOP_BIN)
include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.iopglobal

$(IOP_OBJS_DIR)driver.o: protocol.h bindings.h hid_report.h scratch.h boot_diag.h
$(IOP_OBJS_DIR)bindings.o: bindings.h hid_report.h
$(IOP_OBJS_DIR)scratch.o: scratch.h bindings.h hid_report.h
$(IOP_OBJS_DIR)hid_report.o: hid_report.h
