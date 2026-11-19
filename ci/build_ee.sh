set -eu
cc=mips64r5900el-ps2-elf-gcc
incs="-I$PS2SDK/ee/include -I$PS2SDK/common/include -I. -I$GSKIT/include"
for f in main cont profile_format profile_schema portable_boot usb_modules mmce_modules usb_input usb_input_modules hid_bindings hid_report; do
 $cc -D_EE -G0 -O2 -Wall -DPORTABLE_LAUNCHER -DUSB_HID $incs -c "$f.c" -o "$f.o"
done
$cc -Tlinkfile -O2 -o console-template.elf main.o cont.o profile_format.o profile_schema.o portable_boot.o usb_modules.o mmce_modules.o usb_input.o usb_input_modules.o hid_bindings.o hid_report.o -L$PS2SDK/ee/lib -L$GSKIT/lib -Wl,-zmax-page-size=128 -lgskit -ldmakit -lpad -lmc -liopreboot -lfileXio -lpatches
for f in main cont profile_format profile_schema; do
 $cc -D_EE -G0 -O2 -Wall $incs -c "$f.c" -o "host-$f.o"
done
$cc -Tlinkfile -O2 -o host-template.elf host-main.o host-cont.o host-profile_format.o host-profile_schema.o -L$PS2SDK/ee/lib -L$GSKIT/lib -Wl,-zmax-page-size=128 -lgskit -ldmakit -lpad -lmc -lpatches
for f in console host; do mips64r5900el-ps2-elf-nm -S $f-template.elf > $f-symbols.txt; done
