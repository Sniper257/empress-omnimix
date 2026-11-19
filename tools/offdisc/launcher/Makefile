EE_BIN = LAUNCHER.ELF
EE_OBJS = main.o cont.o profile_format.o profile_schema.o
EE_LIBS = -L$(GSKIT)/lib -lgskit -ldmakit -lpad -lmc
EE_INCS += -I$(GSKIT)/include
EE_CFLAGS += -Wall -O2
ifeq ($(LOAD_PROFILE),1)
EE_CFLAGS += -DLOAD_PROFILE
endif
ifeq ($(PORTABLE_LAUNCHER),1)
EE_CFLAGS += -DPORTABLE_LAUNCHER
EE_OBJS += portable_boot.o usb_modules.o mmce_modules.o
EE_LIBS += -liopreboot -lfileXio
endif
ifeq ($(USB_BENCH),1)
EE_CFLAGS += -DUSB_BENCH
EE_OBJS += usb_boot.o usb_trace.o usb_modules.o
EE_LIBS += -liopreboot -lfileXio
EE_LDFLAGS += -Wl,--wrap=SifIopReset,--wrap=SifIopSync,--wrap=send_romdrv_rom1_payloads,--wrap=sbv_patch_romdrv_set_rom1_info,--wrap=SifAllocIopHeap,--wrap=SifInitIopHeap,--wrap=SifLoadFileInit,--wrap=_SifLoadModule
endif
ifeq ($(USB_BOOT_JOURNAL),1)
EE_CFLAGS += -DUSB_BOOT_JOURNAL
EE_OBJS += usb_journal.o
endif
ifeq ($(MMCE_BENCH),1)
EE_CFLAGS += -DMMCE_BENCH
EE_OBJS += mmce_boot.o mmce_trace.o mmce_modules.o
EE_LIBS += -liopreboot -lfileXio
EE_LDFLAGS += -Wl,--wrap=SifIopReset,--wrap=SifIopSync,--wrap=send_romdrv_rom1_payloads,--wrap=sbv_patch_romdrv_set_rom1_info,--wrap=SifAllocIopHeap,--wrap=SifInitIopHeap,--wrap=SifLoadFileInit,--wrap=_SifLoadModule
ifeq ($(MMCE_DIAG),1)
EE_CFLAGS += -DMMCE_DIAG
EE_OBJS += mmce_diag.o
EE_LDFLAGS += -Wl,--wrap=_SifLoadModuleBuffer
endif
ifeq ($(MMCE_RETAIL_BRIDGE),1)
EE_CFLAGS += -DMMCE_RETAIL_BRIDGE
endif
ifeq ($(MMCE_EXIT_TRACE),1)
EE_CFLAGS += -DMMCE_EXIT_TRACE
endif
endif
ifeq ($(USB_HID),1)
EE_CFLAGS += -DUSB_HID
ifeq ($(MUI_BOOT_DIAG),1)
EE_CFLAGS += -DMUI_BOOT_DIAG=1
endif
EE_OBJS += usb_input.o usb_input_modules.o hid_bindings.o hid_report.o
endif
EE_LIBS += -lpatches
EE_LINKFILE = linkfile

all: $(EE_BIN)

main.o: menu_settings.h build_cache.h file_meta.h debug_config.h load_profile_launcher.h songdb.h cont.h gfx.h catalog.h alphabet_catalog.h coalesce_catalog.h input_probe.h hardware_boot.h profile_manager.h profile_location.h profile_format.h profile_schema.h wheel_bank.h wheel_cache.h
ifeq ($(PORTABLE_LAUNCHER),1)
main.o: deployment.h deployment_ui.h portable_boot.h
portable_boot.o: deployment.h portable_boot.h usb_memory.h
endif
profile_format.o: profile_format.h profile_wire.h
profile_schema.o: profile_schema.h profile_format.h
usb_boot.o usb_trace.o: usb_boot.h
mmce_boot.o mmce_trace.o: mmce_boot.h debug_config.h
mmce_diag.o: mmce_diag.h mmce_boot.h
cont.o: cont.h
ifeq ($(USB_HID),1)
main.o: usb_input.h usb_input_ui.h usb_runtime_layout.h navigation.h usb_hid/protocol.h usb_hid/native.h usb_hid/profile_store.h usb_hid/capture.h usb_hid/bindings.h
usb_input.o: usb_input.h usb_hid/protocol.h usb_hid/native.h usb_hid/profile_store.h usb_hid/bindings.h
hid_bindings.o hid_report.o: usb_hid/bindings.h usb_hid/hid_report.h
endif

clean:
	rm -f $(EE_BIN) $(EE_OBJS)

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
