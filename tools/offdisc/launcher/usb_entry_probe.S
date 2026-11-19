/* All ordinary GPRs (including native gp, eight arguments and 128-bit saved
 * halves) survive each SDK drawing call. Native routines run with native gp.
 * The arena hook is a tail call: allocator may overwrite all launcher code. */
        .set noreorder
        .set noat
        .text
        .macro SAVE_ALL
        addiu $sp,$sp,-512
        .irp r,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,28,30,31
        sq $\r,(\r*16)($sp)
        .endr
        .endm
        .macro RESTORE_ALL
        .irp r,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,28,30,31
        lq $\r,(\r*16)($sp)
        .endr
        addiu $sp,$sp,512
        .endm
        .macro MARK stage
        SAVE_ALL
        lui $gp,%hi(_gp)
        addiu $gp,$gp,%lo(_gp)
        addiu $4,$zero,\stage
        jal usb_entry_probe_stage
        nop
        RESTORE_ALL
        .endm
        .macro WRAP name,target,stage
        .globl usb_probe_\name
usb_probe_\name:
        addiu $sp,$sp,-16
        sq $ra,0($sp)
        MARK \stage
        jal \target
        nop
        MARK (\stage+1)
        lq $ra,0($sp)
        addiu $sp,$sp,16
        jr $ra
        nop
        .endm
        WRAP kernel,0x119158,1
        WRAP semas,0x118f08,3
        WRAP kernel_patch,0x119048,5
        WRAP timer_patch,0x119638,7
        WRAP timer,0x1197b0,9
        WRAP alarm,0x1199f0,11
        WRAP threads,0x10fa38,13
        WRAP osd,0x119280,15
        WRAP exec_patch,0x118778,17
        WRAP constructors,0x14c8a0,19
        .globl usb_probe_arena
usb_probe_arena:
        MARK 21
        j 0x1530c0
        nop
        .globl usb_entry_probe
usb_entry_probe:
        lui $gp,%hi(_gp)
        addiu $gp,$gp,%lo(_gp)
        lui $sp,0x1ff
        ori $sp,$sp,0xf000
        jal usb_entry_probe_begin
        nop
        j 0x100008
        nop
