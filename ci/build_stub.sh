set -eu
cd /src/packer-source/ps2-packer/stub
cc=mips64r5900el-ps2-elf-gcc
for name in Alloc LzmaDec; do
 "$cc" -D_EE -G0 -Os -D_LZMA_SIZE_OPT -D_7ZIP_ST -I../common/lzma -I"$PS2SDK/ee/include" -I"$PS2SDK/common/include" -c "../common/lzma/$name.c" -o "$name.o"
done
for name in main lzma-stub; do
 "$cc" -D_EE -G0 -O3 -DDO_EXECPS2 -I../common/lzma -I"$PS2SDK/ee/include" -I"$PS2SDK/common/include" -c "$name.c" -o "$name.o"
done
"$cc" -nostartfiles ./crt0.s -Wl,--defsym,_start_address=0x1d00000 -Wl,--defsym,_signature=0x414d5a4c -T ./linkfile -L"$PS2SDK/ee/lib" -o unpacking-stub.elf main.o lzma-stub.o Alloc.o LzmaDec.o -lc_nano -lkernel
