# Song database hooks (EMPRESS SLPM_552.21) for the off-disc launcher; data in `sdb` (songdb.c).
# Site list + original words: build_vdisc.py (J = jal/j site, W = word). Added songs = slots; the game sees alias
# ids 104..122 / 128..254 (sdb.slot_of: alias -> slot, bound by songdb.c when a folder opens), 123 = probe alias.
#   0x1e19a0 j rec_hook        FUN_001e19a0(ctx, id): songlist record pointer
#   0x1e1970 j gate_hook       FUN_001e1970(song): "is a song" (was song < 99) -> present[] / added
#   0x19bca0/0x19bac0/0x19b8e0 j sv_*   save record getters (save, song, style, diff) -> dummies for added songs
#   0x14cf18 j st_hook, 0x14d15c j ld_hook, 0x132080 jal mv_hook   file table lookups: fid >= 1062 -> fid_tab
#   0x229c5c.. jal rv_*        rival panel getters: no rival data for added songs
#   folders: 0x2114c4/0x2115b4 jal fb_hook (after FUN_00213550 builds the folders),
#   thunks 0x2113b0 (count), 0x210d70 (lamp), 0x2113c0 (open list) -> fc_hook / fl_hook / fo_hook (songdb.c),
#   folder menu loops 0x18bbc4/0x18f2e8/0x1926e0 jal next_s3/s1/s0 (id 0x2b is followed by 59),
#   visibility 0x18bb54/0x18f1d8/0x1925cc (menu build), 0x190e78/0x191c34 (saved folder) jal vis_hook,
#   0x18bcfc (current folder re-check) jal vcur_hook; saved folder getter 0x1d85c0 j saved_hook
        .set noreorder
        .set noat
#include "eighth_hud_assets.h"
#include "song_resources.h"

        .equ FIRST, 104
        .equ PROBE, 123
        .equ FID0, 1062
        .equ TAB_END, FID0 * 12
        .equ CUSTOM, 59
        .equ O_N, 4
        .equ O_RECS, 8
        .equ O_SAVES, 12
        .equ O_FIDS, 16
        .equ O_CUSTN, 20
        .equ O_PRESENT, 24
        .equ O_NEXT, 232
        .equ O_SLOT, 508
        .equ O_FOLDN, 488
        .equ FOLD0, 59
        .equ NFOLD, 19

        .macro SQ r, off
        .word 0x7fa00000 | (\r << 16) | \off
        .endm
        .macro LQ r, off
        .word 0x7ba00000 | (\r << 16) | \off
        .endm
        # out = slot of alias id `id` (ids >= 128 arrive sign-extended from byte loads: & 0xff), 0xffff = none
        .macro SLOT out, id
        slti $at, \id, -128
        bne $at, $zero, 9f
        nop
        slti $at, \id, 255
        beq $at, $zero, 9f
        nop
        andi \out, \id, 0xff
        sltiu $at, \out, FIRST
        bne $at, $zero, 9f
        nop
        addiu $at, \out, -124
        sltiu $at, $at, 4
        bne $at, $zero, 9f
        nop
        sll \out, \out, 1
        lui $at, %hi(sdb)
        addiu $at, $at, %lo(sdb)
        addu $at, $at, \out
        lhu \out, O_SLOT($at)
        lui $at, %hi(sdb)
        addiu $at, $at, %lo(sdb)
        lw $at, O_N($at)
        sltu $at, \out, $at
        bne $at, $zero, 8f
        nop
9:      li \out, 0xffff
8:
        .endm
        .macro SDB r                    # r = &sdb
        lui \r, %hi(sdb)
        addiu \r, \r, %lo(sdb)
        .endm
        .macro SAVE
        addiu $sp, $sp, -0xb0
        SQ 16, 0x00
        SQ 17, 0x10
        SQ 18, 0x20
        SQ 19, 0x30
        SQ 20, 0x40
        SQ 21, 0x50
        SQ 22, 0x60
        SQ 23, 0x70
        SQ 30, 0x80
        SQ 31, 0x90
        sd $4, 0xa0($sp)
        .endm
        .macro RESTORE
        LQ 16, 0x00
        LQ 17, 0x10
        LQ 18, 0x20
        LQ 19, 0x30
        LQ 20, 0x40
        LQ 21, 0x50
        LQ 22, 0x60
        LQ 23, 0x70
        LQ 30, 0x80
        LQ 31, 0x90
        addiu $sp, $sp, 0xb0
        .endm

        .section .text.stubs, "ax"

        .globl rec_hook
rec_hook:
#if defined(USB_HID) || defined(PLAY_PREVIEW)
        li $at, 0x1c
        bne $a1, $at, 7f
        nop
        lui $v0, %hi(play_preview_record)
        lw $v0, %lo(play_preview_record)($v0)
        beq $v0, $zero, 7f
        nop
        jr $ra
        nop
7:
#endif                               # FUN_001e19a0(ctx, id): EMPRESS ids 0..103 -> songlist, added -> sdb.recs[slot]
        sltiu $at, $a1, FIRST
        beq $at, $zero, 1f
        li $v0, 0x134
0:      .word 0x00a21818                # mult v1,a1,v0 (original word at 0x1e19a4)
        lui $v0, 0x28
        addiu $v0, $v0, -0x7490
        jr $ra
        addu $v0, $v0, $v1
1:      SLOT $v1, $a1
        li $at, 0xffff
        bne $v1, $at, 2f
        li $v0, 0x134
        lui $v0, %hi(unavailable_song_record)
        jr $ra
        addiu $v0, $v0, %lo(unavailable_song_record)
2:      .word 0x00621818                # mult v1,v1,v0
        SDB $v0
        lw $v0, O_RECS($v0)
        jr $ra
        addu $v0, $v0, $v1

        .globl gate_hook
gate_hook:                              # song exists: EMPRESS 0..98 present, else an added song
        SDB $v0
        sltiu $at, $a0, 99
        beq $at, $zero, 1f
        addu $at, $v0, $a0
        jr $ra
        lbu $v0, O_PRESENT($at)
1:      SLOT $v0, $a0
        xori $v0, $v0, 0xffff
        jr $ra
        sltu $v0, $zero, $v0

        # save getters: added song -> its dummy records; else the displaced prologue
        .macro SAVE_HOOK name, back, added
        .globl \name
\name:
        sltiu $at, $a1, FIRST
        beq $at, $zero, \added
        nop
        addiu $sp, $sp, -0x50
        j \back
        sd $ra, 0x40($sp)
        .endm
        SAVE_HOOK sv_bca0, 0x19bca8, sv_dummy
        SAVE_HOOK sv_bac0, 0x19bac8, sv_primary_style
        SAVE_HOOK sv_b8e0, 0x19b8e8, resource_derived_save_hook

sv_primary_style:                       #19bac0 takes SP/DP0/1, not game mode0..5
        beq $a2, $zero, sv_dummy
        li $at, 1
        bne $a2, $at, resource_derived_save_hook
        nop
        b sv_dummy
        li $a2, 2                     #DP's earned record category

sv_dummy:                               # saves + ((id & 0xff) - FIRST)*0x230 + (mode*5 + diff)*0x1c
                                        # (per alias id; -1 = empty row -> the spare record of 255)
        sltiu $at, $a3, 5
        beq $at, $zero, resource_derived_save_hook # invalid difficulty cannot borrow BLACK's record
        nop
        SLOT $v1, $a1
        li $at, 0xffff
        bne $v1, $at, 1f
        nop
        li $a1, -1                     # invalid/unbound handles use the spare, never an old binding
1:
        li $at, 5
        bne $a2, $at, 4f
        nop
        move $a2, $zero                 # native mode5 reads primary SP records
4:      sltiu $at, $a2, 4
        bne $at, $zero, 5f
        nop
        j resource_derived_save_hook    # no native auxiliary lamp resource for imports
        nop
5:      move $v0, $a2
        move $v1, $a3
        sll $at, $v0, 2
        addu $v0, $at, $v0
        addu $v0, $v0, $v1
        sll $at, $v0, 3
        subu $at, $at, $v0
        sll $v1, $at, 2                 # v1 = chart index * 0x1c
        andi $v0, $a1, 0xff
        addiu $v0, $v0, -FIRST
        sll $at, $v0, 9
        addu $v1, $v1, $at
        sll $at, $v0, 5
        addu $v1, $v1, $at
        sll $at, $v0, 4
        addu $v1, $v1, $at              # + song * 0x230
        SDB $v0
        lw $v0, O_SAVES($v0)
        jr $ra
        addu $v0, $v0, $v1

        # FUN_0014ced0 at 0x14cf18: a1 = fid*12, a2 = DAT base LBA, v1 = slot record
        .globl st_hook
st_hook:
        lui $a0, 0x24
        addiu $a0, $a0, -0x3d10
        sltiu $t0, $a1, TAB_END
        bne $t0, $zero, 1f
        addu $a0, $a0, $a1
        SDB $a0
        lw $a0, O_FIDS($a0)
        addiu $a0, $a0, -TAB_END
        addu $a0, $a0, $a1
1:      lw $a0, 0($a0)
        addu $a0, $a2, $a0
        jr $ra
        sw $a0, 0xc($v1)

        # FUN_0014d0c0 at 0x14d15c: a2 = fid*12, v1 = queue entry + 0xc; out a3 off, a1 sectors, a0 size
        .globl ld_hook
ld_hook:
        lui $a0, 0x24
        addiu $a0, $a0, -0x3d10
        sltiu $t1, $a2, TAB_END
        bne $t1, $zero, 1f
        addu $a1, $a0, $a2
        SDB $a1
        lw $a1, O_FIDS($a1)
        addiu $a1, $a1, -TAB_END
        addu $a1, $a1, $a2
1:      lw $a3, 0($a1)
        lw $a0, 8($a1)
        lw $a2, 0x10($v1)
        j 0x14d190
        lw $a1, 4($a1)

        # FUN_00132050 at 0x132080 (replaces lw v0,0(v0)): v1 = fid*12, v0 = &table[fid].sectors
        .globl mv_hook
mv_hook:
        sltiu $at, $v1, TAB_END
        bne $at, $zero, 1f
        nop
        SDB $v0
        lw $v0, O_FIDS($v0)
        addiu $v0, $v0, 4 - TAB_END
        addu $v0, $v0, $v1
1:      jr $ra
        lw $v0, 0($v0)

        # ---- folders ----

        # replaces jal FUN_00213550 (a0 = ctx): build the game's folders, then count the added songs
        .globl fb_hook
fb_hook:
        SAVE
        jal 0x213550
        nop
        jal fold_build
        ld $a0, 0xa0($sp)
        RESTORE
        jr $ra
        nop

        # C call keeping the game's 128-bit s registers (the C code saves 64 bits)
        .macro CCALL name, fn
        .globl \name
\name:
        SAVE
        jal \fn
        nop
        RESTORE
        jr $ra
        nop
        .endm
        CCALL ft_hook, fold_total       # display/stat counts: complete canonical folder
        CCALL fc_hook, fold_count       # thunk 0x2113b0 (folder song count: ctx, id, diff)
        CCALL fl_hook, fold_lamp        # thunk 0x210d70 (folder lamp: ctx, id, diff)
        CCALL fo_hook, fold_copy        # thunk 0x2113c0 (open folder: ctx, id, diff, songs out, diffs out = t0)
        CCALL window_random_hook, song_window_random

        # MUSIC NUMBER: s2 holds the original interpolated value, s4 the
        # animation params. Keep native <=999 rendering and its interpolation.
        .globl song_count_hook
song_count_hook:
        slti $at, $18, 1000
        bne $at, $zero, 1f
        nop
        SAVE
        move $4, $20
        jal song_count_wide
        move $5, $18
        RESTORE
        j 0x193b0c
        nop
1:      ori $3, $3, 0x851f
        j 0x193a3c
        mult $3, $18

        # Synchronize the scene cursor before native selection/input update.
        # Original prologue: addiu sp,-0x70; sd ra,0x60(sp).
        .globl window_update_hook
window_update_hook:
        # One full-width adapter owns both modal and alias synchronization.
        j menu_update_hook
        nop

        # Before drawing the music-select wheel, move the bounded byte window
        # around the current stable row. Other wheel instances pass through.
        .globl window_tick_hook
window_tick_hook:
        SAVE
        sd $5, 0xa8($sp)
        jal song_window_tick
        nop
        ld $4, 0xa0($sp)
        ld $5, 0xa8($sp)
        RESTORE
        addiu $sp, $sp, -0x60
        j 0x1db4f8
        sd $ra, 0x50($sp)
        # FUN_00135720 (draw sprite: a0 container, a1 sprite, a2 params) = `j draw_hook ; nop`: title slots
        # (containers 0x12a..0x12d) are checked by spr_draw first, then the two displaced instructions
        .globl draw_hook
draw_hook:
        addiu $at, $a0, -0x12a
        sltiu $at, $at, 4
        beq $at, $zero, 1f
        nop
        SAVE
        sd $a1, 0xa8($sp)
        addiu $sp, $sp, -0x10
        sd $a2, 0($sp)
        jal spr_draw
        sd $a3, 8($sp)
        ld $a2, 0($sp)
        ld $a3, 8($sp)
        addiu $sp, $sp, 0x10
        ld $a0, 0xa0($sp)
        ld $a1, 0xa8($sp)
        RESTORE
1:      lui $v1, 0x2b
        lw $v1, -0x2fd0($v1)            # lw v1,-0x7bc0(gp) = 0x2ad030
        j 0x135728
        lui $8, 0x35                    # $8: the game's t0 (n32 names $t0 = $12)

        # FUN_00135540 (sprite draw of every path: a0 = loaded container entry (+0x210 data1 index, +0x214 rect
        # table), a1 = the sprite's rect, a2 params) = `j tint_hook ; nop`: lane sprites of the skins #67..#72 are
        # drawn with the Skin settings tint (cur_tint, read by FUN_00139200's colour loads) around a call of the body;
        # the skin layouts draw lanes both through their callbacks (#67) and as plain anim elements (#69)
        .globl tint_hook
tint_hook:
        lw $1, 0x210($4)
        li $3, 146
        beq $1, $3, 6f
        li $3, 183
        bne $1, $3, 7f
        nop
6:      lui $3, %hi(skin + 20)
        lw $3, %lo(skin + 20)($3)
        addiu $3, $3, -2
        bne $3, $zero, 7f
        nop
        SAVE
        jal eighth_sprite_draw
        nop
        RESTORE
        jr $ra
        nop
7:
        addiu $1, $1, -67
        sltiu $3, $1, 42
        beq $3, $zero, 8f
        lw $3, 0x214($4)
        subu $3, $5, $3
        sra $3, $3, 3                   # sprite index
        lui $2, %hi(lane_spr)
        addiu $2, $2, %lo(lane_spr)
        addu $2, $2, $1
        lbu $2, 0($2)
        bne $2, $3, 8f
        nop
        SAVE                            # lane sprite: songdb.c lane_draw(entry, rect, params, skin index)
        jal lane_draw
        move $7, $1
        RESTORE
        jr $ra
        nop
8:      addiu $sp, $sp, -0x20           # not a lane: the displaced prologue, on into the body
        j 0x135548
        sd $ra, 0($sp)
        .globl tint_body                # the original FUN_00135540 (lane_draw calls it per piece)
tint_body:
        addiu $sp, $sp, -0x20
        j 0x135548
        sd $ra, 0($sp)

        # Original geometry setup first, then apply an optional layout-specific
        # frame pack before FUN_001fac80 queues the frame container.
        .globl frame_setup_hook
frame_setup_hook:
        SAVE
        jal frame_geometry
        nop
        ld $4, 0xa0($sp)
        jal frame_select
        nop
        RESTORE
        jr $ra
        nop

        # Imported root callback100 draws original CS8 numbers/gauge/labels.
        .globl eighth_callback_hook
eighth_callback_hook:
        lw $1, 0($4)
        li $2, 100
        bne $1, $2, 1f
        nop
        SAVE
        jal eighth_hud
        nop
        RESTORE
        jr $ra
        nop
1:      j fhs_hud1_hook
        nop

        .macro EIGHTH_CHECK fallback
        lui $2, %hi(skin + 20)
        lw $2, %lo(skin + 20)($2)
        addiu $2, $2, -2
        bne $2, $zero, \fallback
        nop
        lui $2, 0x2b
        lw $2, -0x2a18($2)
        beq $2, $zero, \fallback
        nop
        lw $2, 0($2)
        addiu $2, $2, -2
        bne $2, $zero, \fallback
        nop
        .endm

        # The hit timer still runs in EMPRESS; only its requested art/time
        # changes to the original CS8 blue burst (native indices advance2/tick).
        .globl eighth_bomb_hook
eighth_bomb_hook:
        EIGHTH_CHECK 1f
        lui $4, 69
        ori $4, $4, E8_BOMB
        sra $5, $5, 1
1:      SAVE
        addiu $sp,$sp,-64
        sd $4,0($sp)
        sd $5,8($sp)
        sd $6,16($sp)
        sd $7,24($sp)
        sd $8,32($sp)
        sd $9,40($sp)
        sd $10,48($sp)
        sd $11,56($sp)
        move $6,$4
        move $7,$5
        move $4,$20                    # native explosion outer field
        jal ca_bomb_map
        move $5,$19                    # native explosion lane
        move $12,$2
        ld $6,16($sp)
        ld $7,24($sp)
        ld $8,32($sp)
        ld $9,40($sp)
        ld $10,48($sp)
        ld $11,56($sp)
        addiu $sp,$sp,64
        RESTORE
        sll $4,$12,0
        dsrl32 $5,$12,0
        sll $12,$20,5
        lui $1,%hi(skin + 24)
        addiu $1,$1,%lo(skin + 24)
        addu $1,$1,$12
        lw $8,28($1)                   # explosion scale, owner-routed
        move $9,$8
        srl $12,$4,16
        sltiu $12,$12,463
        bne $12,$zero,2f
        nop
        lui $1,0x2b
        lw $1,-0x2a18($1)
        lbu $1,5($1)
        beq $1,$zero,2f
        nop
        sll $12,$8,1
        addu $8,$8,$12
        sra $8,$8,2                    # native narrow effect width
2:
#ifdef PLAY_LIFT
        sll $12,$20,2
        lui $1,%hi(lift_y)
        addiu $1,$1,%lo(lift_y)
        addu $1,$1,$12
        lw $12,0($1)
        subu $7,$7,$12
#endif
        j 0x12fae0
        nop

        # RED's separately spawned particles keep the same origin/lifetime.
        # Scale their destination displacement and sprite size at creation;
        # native random generation, clipping, easing and model worker stay.
        .globl ca_particle_hook
ca_particle_hook:
#ifdef PLAY_LIFT
        lui $1,%hi(lift_particle_y)
        lw $12,%lo(lift_particle_y)($1)
        subu $7,$7,$12
        subu $9,$9,$12
#endif
        lui $1,%hi(ca_particle_scale)
        lw $12,%lo(ca_particle_scale)($1)
        addiu $13,$zero,100
        beq $12,$13,1f
        nop
        subu $14,$8,$6
        mult $14,$12
        mflo $14
        div $zero,$14,$13
        mflo $14
        addu $8,$6,$14
        subu $14,$9,$7
        mult $14,$12
        mflo $14
        div $zero,$14,$13
        mflo $14
        addu $9,$7,$14
        mult $10,$12
        mflo $14
        div $zero,$14,$13
        mflo $10
1:      j 0x20fb50
        nop

        # Suppress the EMPRESS turntable emblem; CS8's original disc is already
        # in the frame artwork; eighth_hud draws its original input ring.
        .globl eighth_turntable_hook
eighth_turntable_hook:
        EIGHTH_CHECK 1f
        jr $ra
        nop
1:      j 0x1fb650
        nop

        # The native top-to-bottom timer is independent of the frame root.
        # CS8 already draws its horizontal source timer beside the gauge.
        .globl eighth_progress_hook
eighth_progress_hook:
        EIGHTH_CHECK 1f
        jr $ra
        nop
1:      j 0x1ff7d0
        nop

        # Animation entry (container,animation,frame,flags,params): replace
        # native-only completion/start/fail programs with an empty program, before resolving
        # the animation graph. Beams keep native timing + launcher length.
        .globl eighth_anim_hook
eighth_anim_hook:
        li $2, 146
        bne $4, $2, 9f
        nop
        EIGHTH_CHECK 9f
        li $2, 1
        beq $5, $2, 1f
        li $2, 2
        beq $5, $2, 2f
        li $2, 3
        beq $5, $2, 3f
        li $2, 20
        bne $5, $2, 9f
        nop
        b 8f
        li $5, E8_EMPTY
1:      b 8f
        li $5, E8_EMPTY
2:      b 8f
        li $5, E8_EMPTY
3:      li $5, E8_EMPTY
8:      li $4, 69
9:      addiu $sp, $sp, -0x40
        j 0x135218
        lui $3, 0x35

        # Only reached by FUN_001f65d0's layout2 case. Keep native counter
        # strings/font/values; x420 clears the source gauge housing (ends416).
        # v0 is overwritten at the return site; all other live registers stay.
        .globl judge_pos_hook
judge_pos_hook:
        li $20, 388
        lui $2, %hi(skin + 20)
        lw $2, %lo(skin + 20)($2)
        addiu $2, $2, -2
        bne $2, $zero, 1f
        nop
        li $20, 420
1:      j 0x1f66f4
        nop

        # keybeam draws in FUN_001fe040 (`jal FUN_0012fae0` at 0x1fe284 keys / 0x1fe3ec turntable; a3 = y): the
        # beam anims grow from their top (y 0x41), so a shorter keybeam (zoomY < 100) is moved down by
        # skin.beam_dy = 256 * (1 - length) to keep its end on the judge line
        .globl beam_hook
beam_hook:
        sll $12,$fp,5                  # native key loop field
        b 1f
        nop
        .globl beam_scratch_hook
beam_scratch_hook:
        sll $12,$18,5                  # native scratch loop field
1:
#ifdef PLAY_LIFT
        srl $13,$12,3
        lui $1,%hi(lift_y)
        addiu $1,$1,%lo(lift_y)
        addu $1,$1,$13
        lw $13,0($1)
        subu $7,$7,$13
#endif
        lui $1,%hi(skin + 24)
        addiu $1,$1,%lo(skin + 24)
        addu $1,$1,$12
        lw $12,12($1)
        lw $9,16($1)                   # independently selected zoomY
        j 0x12fae0
        addu $7,$7,$12

        # note drawer FUN_001e6f50: `jal FUN_0012fdd0` (0x1e757c; a0 handle, a2 = $6 y, t0 = $8 zoomY = 100,
        # t2 = $10 pivotY = h / 2, v0 = $2 still the sprite height h) -> note_hook: Skin settings note height for the
        # note containers #109 (SP) / #146 (wide): the game's sprites get skin.note_zoom, note skin sprites
        # (index >= skin_first) skin.note_zoom_skin, measure lines (613 / 614) stay as they are. Notes grow upwards
        # from their bottom (pivot h, y + 3 = where the game's 6-row note with pivot 3 ends: on the measure line)
        .globl note_hook
note_hook:
#ifdef PLAY_LIFT
        sll $12,$17,2
        lui $1,%hi(lift_y)
        addiu $1,$1,%lo(lift_y)
        addu $1,$1,$12
        lw $12,0($1)
        subu $6,$6,$12
#endif
#ifdef HOLDS
        # hold heads / tails: holds_note_hook draws caps and body itself and returns 1 (skip the native
        # draw, back to 0x1e7584). Here $sp is FUN_001e6f50's: its stack args are at 0($sp)..0x18($sp),
        # clip pointer at 0x10. s3 = the note, s1 = its side; v0 (sprite height) and a0..t3 are kept.
        addiu $sp,$sp,-0x60
        sd $2,0x00($sp)
        sd $4,0x08($sp)
        sd $5,0x10($sp)
        sd $6,0x18($sp)
        sd $7,0x20($sp)
        sd $8,0x28($sp)
        sd $9,0x30($sp)
        sd $10,0x38($sp)
        sd $11,0x40($sp)
        sd $31,0x48($sp)
        lw $9,0x70($sp)                 # caller's clip pointer (its 0x10($sp))
        move $7,$6                      # y
        move $6,$5                      # x
        move $5,$4                      # handle
        move $4,$19                     # note
        jal holds_note_hook
        move $8,$17                     # side
        ld $31,0x48($sp)
        beq $2,$zero,4f
        nop
        jr $31
        addiu $sp,$sp,0x60
4:      ld $2,0x00($sp)
        ld $4,0x08($sp)
        ld $5,0x10($sp)
        ld $6,0x18($sp)
        ld $7,0x20($sp)
        ld $8,0x28($sp)
        ld $9,0x30($sp)
        ld $10,0x38($sp)
        ld $11,0x40($sp)
        addiu $sp,$sp,0x60
#endif
        srl $12, $4, 16                 # container ($12-$15 are free here, $4-$11 are the arguments)
        andi $3, $4, 0xffff             # sprite
        lui $1, %hi(skin)
        addiu $1, $1, %lo(skin)
        sll $13,$17,5                  # native node side, retained in s1
        addiu $13,$13,24
        addu $13,$1,$13                # physical-field scalar block
        lw $14,24($13)                 # selected custom note bank, or zero
        beq $14,$zero,7f
        nop
        addiu $15,$zero,109
        bne $12,$15,5f
        lui $15,0x26
        b 6f
        addiu $15,$15,-0xcd0            # native/legacy SP note handles25F330
5:      addiu $1,$zero,146
        bne $12,$1,7f
        addiu $15,$15,-0xca0            # wide25F360
6:      lw $1,0($15)
        beq $4,$1,8f
        nop
        lw $1,4($15)
        beq $4,$1,8f
        addiu $14,$14,1
        lw $1,8($15)
        beq $4,$1,8f
        addiu $14,$14,1
        lw $1,12($15)
        bne $4,$1,7f
        nop
8:      move $4,$14
        lw $8,4($13)
        addiu $10,$zero,6
        j 0x12fdd0
        addiu $6,$6,3
7:      lui $1,%hi(skin)
        addiu $1,$1,%lo(skin)
        addiu $12, $12, -109
        beq $12, $zero, 1f
        addiu $12, $12, 109 - 146
        bne $12, $zero, 9f              # not a note container
        nop
        addiu $12, $3, -614             # wide
        beq $12, $zero, 9f
        lhu $12, 14($1)
        b 2f
        nop
1:      addiu $12, $3, -613             # SP
        beq $12, $zero, 9f
        lhu $12, 12($1)
2:      sltu $12, $3, $12
        bne $12, $zero, 3f
        lw $8, 4($13)                   # owning field, game's sprite
        lw $8, 8($13)                   # owning field, note skin sprite
3:      move $10, $2                    # pivot at the bottom
        addiu $6, $6, 3
9:      j 0x12fdd0
        nop

        .data
lane_spr:                               # #67..72: narrow, narrow battle, wide SP1/SP2, narrow DP, wide DP
        .byte 3, 2, 2, 2, 6, 5
        # Six native colour themes: narrow lanes are sprite1; wide SP bakes
        # its lane into the640x480 background sprite0.
        .rept 6
        .byte 1, 1, 0, 0, 1, 1
        .endr
        .text

        # music-select entry with no saved song (0x192018, replaces jal FUN_001dc460 = folder panel; delay slot
        # loads a0 = panel): the game opens no folder and selects -1. With the `id >= 0` checks gone (sign_nops) -1
        # reads as a song (record 0: 3y3s) -> the folder header row (125) in wheel row 0 (scene s4 +0xe4) and the
        # selection state+0x28. state+0x24 stays -1: the panel setup picks its MUSIC NUMBER label from that (125 gave
        # the BPM label); the BPM number itself is hidden for -1 by bpm_hook
        .globl entry_hook
entry_hook:
        li $2, 125                      # (v0 is free: next written at 0x192044)
        sb $2, 0xe4($20)
        lui $at, 0x2b
        lw $at, -0x2cd8($at)            # music-select state *(0x2ad328)
        j 0x1dc460
        sw $2, 0x28($at)

        CCALL stat_hook, fold_stats     # 0x1918a4: folder stats (ctx, id, diff, grades, lamps = t0, rivals = t1)

        # BPM panel FUN_00192ae0 (song s1 = $17): the game hid ids < 0 and > 98 (`bltz s1` at 0x192b3c, now
        # `j bpm_hook` with its delay slot andi s3 kept). -1 = nothing selected, 99..103 tutorials / folder rows,
        # 124.. wheel rows (RANDOM, folders); aliases 128..254 are negative here and are songs
        .globl bpm_hook
bpm_hook:
        addiu $at, $17, 1
        beq $at, $zero, 1f              # -1
        nop
        bltz $17, 2f                    # alias 128..254
        sltiu $at, $17, 99
        bne $at, $zero, 2f              # EMPRESS song
        sltiu $at, $17, 104
        bne $at, $zero, 1f              # 99..103
        sltiu $at, $17, 124
        beq $at, $zero, 1f              # 124..
        nop
2:      j 0x192b50                      # past the game's own `slti s1, 124 ; beqz`
        nop
1:      SAVE                            # no BPM: label -> MUSIC NUMBER (songdb.c mn_label)
        move $4, $16                    # scene
        jal mn_label
        move $5, $19                    # colour (the andi in the delay slot of `j bpm_hook`)
        RESTORE
        j 0x192ff4
        nop

        # FUN_0018a230 label switch (wheel row v0 = $2, -1 when the wheel is empty): the game's `bltz v0` (removed by
        # sign_nops for the negative aliases) as a -1 test. 0x18a4e0: label MUSIC NUMBER and a song row -> BPM;
        # 0x18af88: label BPM and no song row -> MUSIC NUMBER. With -1 taken as a song, a clean entry (no folder
        # opened yet) flipped the label every frame against bpm_hook / mn_label.
        .globl lbl_hook1
lbl_hook1:
        addiu $at, $2, 1
        beq $at, $zero, 1f
        nop
        j 0x18a4e8
        nop
1:      j 0x18a52c
        nop
        .globl lbl_hook2
lbl_hook2:
        addiu $at, $2, 1
        beq $at, $zero, 1f
        nop
        j 0x18af90
        nop
1:      j 0x18afbc
        nop

        CCALL vcur_hook, vis_cur        # 0x18bcfc: current folder re-check (invisible -> first menu folder)

        # FUN_001d85c0 (saved folder) = `j saved_hook ; lw v0,0x54(a0)` -> a folder that is in the menu
        .globl saved_hook
saved_hook:
        SAVE
        jal saved_folder
        move $a0, $v0
        RESTORE
        jr $ra
        nop
        CCALL var_hook, variant_song    # 0x1f5f18 (NO CRIME check's song getter): added songs' variants
        CCALL spr_hook_w, spr_wheel     # FUN_001e3410..: song -> title sprite handles (added songs: loaded on demand)
        CCALL spr_hook_b, spr_big
        CCALL spr_hook_g, spr_genre
        CCALL spr_hook_a, spr_artist
        CCALL spr_panel_hook_b, spr_panel_big
        CCALL spr_panel_hook_g, spr_panel_genre
        CCALL spr_panel_hook_a, spr_panel_artist

        # our folders (ids FOLD0..FOLD0+NFOLD-1: 59 CUSTOM, 60.. versions): tmp = folder index, else go to 9f
        .macro OURS id_reg, tmp
        addiu \tmp, \id_reg, -FOLD0
        sltiu $at, \tmp, NFOLD
        beq $at, $zero, 9f
        nop
        .endm

        # folder menu loops: replaces `addiu R,R,1 ; slti v0,R,0x2c` with `jal next_R ; nop`:
        # R = sdb.next[R] (the launcher's menu order), v0 = 0 at the end (0xff)
        .macro NEXT name, r
        .globl \name
\name:
        SDB $v0
        addu $v0, $v0, \r
        lbu \r, O_NEXT($v0)
        xori $v0, \r, 0xff
        jr $ra
        sltu $v0, $zero, $v0
        .endm
        NEXT next_s3, $19
        NEXT next_s1, $17
        NEXT next_s0, $16

        # folder visibility FUN_001d8e30(save, id, mode, x): OLD VERSION (2) hidden;
        # ALL VERSION (0) uses the canonical rolling window; ours = has songs
        .globl vis_hook
vis_hook:
        li $at, 2
        bne $a1, $at, 3f
        nop
1:      jr $ra
        move $v0, $zero
3:      OURS $a1, $t0
        SDB $v0
        addu $v0, $v0, $t0
        lbu $v0, O_FOLDN($v0)
        jr $ra
        sltu $v0, $zero, $v0
9:      j 0x1d8e30
        nop

        # FUN_0021bf60 (menu widget: set folder ids) at 0x21bf80: memcpy into the relocated id array (+0x1a8)
        .globl wcopy_hook
wcopy_hook:
        j 0x12ad48
        addiu $a0, $a0, 0x1a8

        # difficulty change with a folder open (0x18c9ec, replaces `slti at,v1,3`; v1 = folder id): at = 1 ->
        # re-open the folder with the new difficulty (game: ALL/EMPRESS/OLD; alphabet/favorites below), ours too
        .globl reo_hook
reo_hook:
        addiu $at, $3, -FOLD0
        sltiu $at, $at, NFOLD
        bne $at, $zero, 1f
        nop
        slti $at, $3, 3
1:      jr $ra
        nop

        # FUN_0018fd40 (open folder) at 0x18fe2c: replaces `bnez v0 ; nop ; li s1,1` (v0 = id < 0x2c, s3 = id,
        # s1 = header rows): folders past 0x2b get no RANDOM row, except ours
        .globl rnd_hook
rnd_hook:
        bne $v0, $zero, 1f
        addiu $at, $19, -FOLD0
        sltiu $at, $at, NFOLD
        bne $at, $zero, 1f
        nop
        li $17, 1
1:      jr $ra
        nop

# Gauge auto shift: all boundaries preserve the full EE saved-register width.
# Native original bodies start with the two displaced, independently checked words.
        .macro GSM_BODY name, word0, word1, resume
        .globl \name
\name:
        .word \word0, \word1
        j \resume
        nop
        .endm
        GSM_BODY menu_update_body, 0x27bdff90, 0xffbf0060, 0x18a238
        GSM_BODY menu_draw_body, 0x27bdff90, 0xffbf0050, 0x1896d8
        GSM_BODY gsm_init_body, 0x27bdffc0, 0xffbf0030, 0x204d58
        GSM_BODY gsm_options_body, 0x27bdffd0, 0x3c06007a, 0x203498
        GSM_BODY gsm_retry_body, 0x27bdffc0, 0x3c02007a, 0x205208
        GSM_BODY gsm_history_body, 0x27bdffc0, 0x3083001f, 0x204998
        GSM_BODY gsm_hard_body, 0x27bdffd0, 0xffbf0020, 0x1d95b8
        GSM_BODY gsm_easy_body, 0x27bdffd0, 0xffbf0020, 0x1d9618
        GSM_BODY gsm_result_body, 0x27bdff80, 0xffbf0060, 0x17e928
        GSM_BODY gsm_segment_body, 0x27bdffa0, 0xffbf0050, 0x17eee8
        GSM_BODY gsm_end_body, 0x27bdffe0, 0xffbf0010, 0x17aaa8

        .globl gsm_update_hook
gsm_update_hook:
        SAVE
        jal gsm_update
        nop
        RESTORE
        jr $31
        nop

        .globl gsm_init_hook
gsm_init_hook:
        addiu $29,$29,-16
        sd $5,0($29)
        SAVE
        jal play_mods_reset
        nop
        ld $4,0xa0($29)
        ld $5,0xb0($29)
        jal gsm_init_body
        nop
        ld $4,0xa0($29)
        ld $5,0xb0($29)
        jal gsm_init
        nop
        RESTORE
        jr $31
        addiu $29,$29,16

        .macro GSM_RESET_HOOK name, body
        .globl \name
\name:
        SAVE
        jal play_mods_reset
        nop
        RESTORE
        j \body
        nop
        .endm
        GSM_RESET_HOOK gsm_options_hook, gsm_options_body
        GSM_RESET_HOOK gsm_retry_hook, gsm_retry_body

        .globl gsm_history_hook
gsm_history_hook:
        SAVE
        jal gsm_history_body
        nop
        jal gsm_history
        nop
        RESTORE
        jr $31
        nop

        .macro GSM_GET_HOOK name, body, calc
        .globl \name
\name:
        SAVE
        jal \calc
        nop
        ld $4,0xa0($29)
        RESTORE
        bltz $2,1f
        nop
        jr $31
        nop
1:      j \body
        nop
        .endm
        GSM_GET_HOOK gsm_hard_hook, gsm_hard_body, gsm_hard
        GSM_GET_HOOK gsm_easy_hook, gsm_easy_body, gsm_easy

        .globl gsm_segment_hook
gsm_segment_hook:
        addiu $29,$29,-16
        sd $5,0($29)
        SAVE
        jal gsm_segment
        nop
        ld $4,0xa0($29)
        ld $5,0xb0($29)
        RESTORE
        bnez $2,1f
        addiu $29,$29,16
        j gsm_segment_body
        nop
1:      jr $31
        nop

        .globl gsm_result_hook
gsm_result_hook:                        # SELECT must run before native background and curve drawing
        SAVE
        jal gsm_result_input
        nop
        ld $4,0xa0($29)
        RESTORE
        j gsm_result_body
        nop

        GSM_GET_HOOK gsm_background_hard_hook, gsm_hard_body, gsm_view_hard

        .globl gsm_end_hook
gsm_end_hook:
        addiu $29,$29,-16
        SAVE
        jal gsm_end_body
        nop
        sd $2,0xb0($29)
        jal play_mods_reset
        nop
        ld $2,0xb0($29)
        RESTORE
        jr $31
        addiu $29,$29,16

        GSM_BODY gsm_hard_raw_body, 0x27bdffd0, 0xffbf0020, 0x1d9f38
        GSM_GET_HOOK gsm_hard_raw_hook, gsm_hard_raw_body, gsm_hard

        .globl gsm_menu_hook
gsm_menu_hook:                          # original scene malloc; also covers abort-without-results
        SAVE
        jal play_mods_reset
        nop
        ld $4,0xa0($29)
        RESTORE
        j 0x1003f0
        nop

        .globl gsm_count_hook
gsm_count_hook:
        SAVE
        jal gsm_result_count
        nop
        RESTORE
        jr $31
        nop
        .globl gsm_sample_hook
gsm_sample_hook:
        SAVE
        jal gsm_result_sample
        nop
        RESTORE
        jr $31
        nop
        .globl gsm_score_sample_hook
gsm_score_sample_hook:
        SAVE
        jal gsm_result_score_sample
        nop
        RESTORE
        jr $31
        nop
        .globl gsm_rate_sample_hook
gsm_rate_sample_hook:
        SAVE
        jal gsm_result_rate_sample
        nop
        RESTORE
        jr $31
        nop

        # Native callback/call boundaries: C n32 preserves only 64-bit saved
        # registers; SAVE/RESTORE retain the EE caller's complete 128-bit values.
        .macro ASSET_HOOK name, body
        .globl \name
\name:
        SAVE
        jal \body
        nop
        RESTORE
        jr $31
        nop
        .endm

        # Native sceOpen zero-ACK exit. The rejected operation still owns s3;
        # native's semaphore/result epilogue has not used the descriptor again.
        # Preserve complete EE saved registers, including gp, across the C
        # cleanup; resume the original epilogue and original -11 return value.
        .globl vd_fio_reject_hook
vd_fio_reject_hook:
        addiu $sp,$sp,-16
        SQ 28,0
        SAVE
        or $4,$19,$zero
        jal vd_fio_reject_cleanup
        nop
        RESTORE
        LQ 28,0
        addiu $sp,$sp,16
        j 0x1143d0
        addiu $2,$zero,-11
#ifdef USB_BOOT_TRACE
#ifdef USB_BOOT_JOURNAL
        ASSET_HOOK boot_journal_publish_hook, boot_trace_io
#endif
        ASSET_HOOK boot_kernel_hook, boot_trace_kernel
        ASSET_HOOK boot_libc_semas_hook, boot_trace_libc_semas
        ASSET_HOOK boot_libc_kernel_hook, boot_trace_libc_kernel
        ASSET_HOOK boot_kernel_timer_patch_hook, boot_trace_kernel_timer_patch
        ASSET_HOOK boot_kernel_timer_hook, boot_trace_kernel_timer
        ASSET_HOOK boot_kernel_alarm_hook, boot_trace_kernel_alarm
        ASSET_HOOK boot_kernel_threads_hook, boot_trace_kernel_threads
        ASSET_HOOK boot_kernel_osd_patch_hook, boot_trace_kernel_osd_patch
        ASSET_HOOK boot_kernel_exec_patch_hook, boot_trace_kernel_exec_patch
        ASSET_HOOK boot_constructors_hook, boot_trace_constructors
        ASSET_HOOK boot_arena_hook, boot_trace_arena
        ASSET_HOOK boot_globals_hook, boot_trace_globals
        ASSET_HOOK boot_audio_reset_hook, boot_trace_audio_reset
        ASSET_HOOK boot_rpc_first_hook, boot_trace_rpc_first
        ASSET_HOOK boot_cd_first_hook, boot_trace_cd_first
        ASSET_HOOK boot_mode_first_hook, boot_trace_mode_first
        ASSET_HOOK boot_rpc_second_hook, boot_trace_rpc_second
        ASSET_HOOK boot_loadfile_hook, boot_trace_loadfile
        ASSET_HOOK boot_fileio_hook, boot_trace_fileio
        ASSET_HOOK boot_cd_second_hook, boot_trace_cd_second
        ASSET_HOOK boot_mode_second_hook, boot_trace_mode_second
        ASSET_HOOK boot_heap_hook, boot_trace_heap
        ASSET_HOOK boot_libsd_hook, boot_trace_libsd
        ASSET_HOOK boot_sound_hook, boot_trace_sound
        ASSET_HOOK boot_sio2_hook, boot_trace_sio2
        ASSET_HOOK boot_mcman_hook, boot_trace_mcman
        ASSET_HOOK boot_mcserv_hook, boot_trace_mcserv
        ASSET_HOOK boot_padman_hook, boot_trace_padman
        ASSET_HOOK boot_cdvdstm_hook, boot_trace_cdvdstm
        ASSET_HOOK boot_filequeue_hook, boot_trace_filequeue
        ASSET_HOOK boot_audio_hook, boot_trace_audio
        ASSET_HOOK boot_graphics_hook, boot_trace_graphics
        ASSET_HOOK boot_pad_hook, boot_trace_pad
#ifdef USB_BOOT_TEXT
        ASSET_HOOK boot_init_hook, boot_trace_init
        ASSET_HOOK boot_loop_pad_hook, boot_trace_loop_pad
        ASSET_HOOK boot_loop_wait_hook, boot_trace_loop_wait
        ASSET_HOOK boot_update_hook, boot_trace_update
        ASSET_HOOK boot_submit_hook, boot_trace_submit
        ASSET_HOOK boot_flip_hook, boot_trace_flip
        ASSET_HOOK boot_render_reset_hook, boot_trace_render_reset
        ASSET_HOOK boot_system_tick_hook, boot_trace_system_tick
        ASSET_HOOK boot_service_hook, boot_trace_service
        ASSET_HOOK boot_draw_hook, boot_trace_draw
        ASSET_HOOK boot_movie_hook, boot_trace_movie
        ASSET_HOOK boot_timer_hook, boot_trace_timer
        ASSET_HOOK boot_tail_hook, boot_trace_tail
        ASSET_HOOK boot_upload_hook, boot_trace_upload
        ASSET_HOOK boot_sound_status_hook, boot_trace_sound_status
        ASSET_HOOK boot_asset_release_hook, boot_trace_asset_release
        ASSET_HOOK boot_bank_submit_hook, boot_trace_bank_submit
        ASSET_HOOK boot_fade_bank_hook, boot_trace_fade_bank
        ASSET_HOOK boot_fade_stream_hook, boot_trace_fade_stream
        ASSET_HOOK boot_stop_stream_hook, boot_trace_stop_stream
        ASSET_HOOK boot_transfer_status_hook, boot_trace_transfer_status
        ASSET_HOOK boot_voice_residency_hook, boot_trace_voice_residency
        ASSET_HOOK boot_voice_tick_hook, boot_trace_voice_tick
        ASSET_HOOK boot_stream_tick_hook, boot_trace_stream_tick
        ASSET_HOOK boot_command_build_hook, boot_trace_command_build
        ASSET_HOOK boot_command_send_hook, boot_trace_command_send
        ASSET_HOOK boot_transfer_publish_hook, boot_trace_transfer_publish
        ASSET_HOOK boot_bank_poll_first_hook, boot_trace_bank_poll_first
        ASSET_HOOK boot_bank_poll_repeat_hook, boot_trace_bank_poll_repeat
        ASSET_HOOK boot_audio_init_hook, boot_trace_audio_init
        ASSET_HOOK boot_alloc_32k_hook, boot_trace_alloc_32k
        ASSET_HOOK boot_alloc_128k_hook, boot_trace_alloc_128k
        ASSET_HOOK boot_alloc_760k_hook, boot_trace_alloc_760k
        ASSET_HOOK boot_status_bank_hook, boot_trace_status_bank
        ASSET_HOOK boot_status_stream_hook, boot_trace_status_stream
        ASSET_HOOK boot_bank_finish_hook, boot_trace_bank_finish
        ASSET_HOOK boot_rpc_before_hook, boot_trace_rpc_before
        ASSET_HOOK boot_rpc_after_hook, boot_trace_rpc_after
        ASSET_HOOK boot_queue_first_hook, boot_trace_queue_first
        ASSET_HOOK boot_queue_repeat_hook, boot_trace_queue_repeat
        ASSET_HOOK boot_upload_first_hook, boot_trace_upload_first
        ASSET_HOOK boot_upload_repeat_hook, boot_trace_upload_repeat
        ASSET_HOOK boot_stream_first_hook, boot_trace_stream_first
        ASSET_HOOK boot_stream_repeat_hook, boot_trace_stream_repeat
        ASSET_HOOK boot_stream_last_first_hook, boot_trace_stream_last_first
        ASSET_HOOK boot_stream_last_repeat_hook, boot_trace_stream_last_repeat
#endif
#endif
        GSM_BODY ranking_load_body, 0x8f848aac, 0x24020001, 0x22ce98
        GSM_BODY ranking_evaluate_body, 0x3c09007b, 0x3c07007b, 0x22ccc8
        ASSET_HOOK menu_update_hook, menu_update_call
        ASSET_HOOK menu_draw_hook, menu_draw_call
        ASSET_HOOK ranking_load_hook, ranking_load
        ASSET_HOOK ranking_evaluate_hook, ranking_evaluate

        ASSET_HOOK resource_graph_target_hook, resource_graph_target
        ASSET_HOOK resource_note_count_hook, resource_note_count
        ASSET_HOOK resource_derived_save_hook, resource_derived_save
        GSM_BODY resource_profile_rebuild_body, 0x27bdff90, 0x0000282d, 0x1e1a68
        GSM_BODY resource_profile_baseline_body, 0x27bdffb0, 0xffbf0040, 0x1e1d98
        ASSET_HOOK resource_profile_rebuild_hook, resource_profile_rebuild
        ASSET_HOOK resource_profile_baseline_hook, resource_profile_baseline

        # The two rival formats own exactly99 songs x2styles x4stored charts.
        # Every reader/writer enters here, including graph, result and reset.
        # Their first native branch is relocated explicitly, including its
        # original delay slot. A raw copied branch would target runtime bytes.
        .macro RESOURCE_RIVAL name, site, word0, cmpreg, delayword, unavailable
        .globl \name
\name:
        beq $a0, $zero, \unavailable
        nop
        sltiu $at, $a1, E16_RESOURCE_SONGS
        beq $at, $zero, \unavailable
        nop
        sltiu $at, $a2, E16_RESOURCE_STYLES
        beq $at, $zero, \unavailable
        nop
        sltiu $at, $a3, E16_RESOURCE_DIFFS
        beq $at, $zero, \unavailable
        nop
        .word \word0
        beq $a3, \cmpreg, 1f
        .word \delayword
        j \site+12
        nop
1:      j \site+0x38
        nop
        .endm
        RESOURCE_RIVAL resource_rival_graph_a, 0x177750, 0x24020004, $v0, 0x240a0005, resource_graph_a_absent
        RESOURCE_RIVAL resource_rival_graph_write, 0x1778c0, 0x24030004, $v1, 0x240a0005, resource_zero
        RESOURCE_RIVAL resource_rival_reset, 0x177970, 0x24020004, $v0, 0x24030005, resource_zero
        RESOURCE_RIVAL resource_rival_lamp_write, 0x1779f0, 0x24030004, $v1, 0x24090005, resource_zero
        RESOURCE_RIVAL resource_rival_lamp_a, 0x177a60, 0x24020004, $v0, 0x24080005, resource_zero
        RESOURCE_RIVAL resource_rival_score_a, 0x177ad0, 0x24020004, $v0, 0x24080005, resource_minus_one
        RESOURCE_RIVAL resource_rival_graph_b, 0x179c30, 0x24030004, $v1, 0x24020005, resource_graph_b_absent
        RESOURCE_RIVAL resource_rival_lamp_b, 0x179dc0, 0x24030004, $v1, 0x24020005, resource_zero
        RESOURCE_RIVAL resource_rival_score_b, 0x179e50, 0x24030004, $v1, 0x24020005, resource_minus_one

resource_graph_a_absent:
        beq $9, $zero, resource_graph_absent
        li $at, -1
        b resource_graph_absent
        sh $at, 0($9)
resource_graph_b_absent:
        beq $10, $zero, resource_graph_absent
        li $at, -1
        sh $at, 0($10)
resource_graph_absent:
        beq $8, $zero, resource_zero
        nop
        sh $zero, 0($8)
resource_zero:
        jr $ra
        move $v0, $zero
resource_minus_one:
        jr $ra
        li $v0, -1
        nop
        nop
        nop

        # Native bitmaps/history/reference flags are optional resources. Bounds
        # refer to their actual storage, including style and difficulty axes.
        .macro RESOURCE_AXIS reg, limit
        sltiu $at, \reg, \limit
        beq $at, $zero, resource_zero
        nop
        .endm
        .macro RESOURCE_RESUME site, word0, word1
        .word \word0
        j \site+8
        .word \word1
        .endm
        .globl resource_favorite_get
resource_favorite_get:
        RESOURCE_AXIS $a1, 2
        RESOURCE_AXIS $a2, E16_RESOURCE_SONGS
        RESOURCE_RESUME 0x1d88c0, 0x27bdffc0, 0xffbf0030
        .globl resource_favorite_set
resource_favorite_set:
        RESOURCE_AXIS $a1, 2
        RESOURCE_AXIS $a2, E16_RESOURCE_SONGS
        RESOURCE_RESUME 0x1d8960, 0x27bdffb0, 0xffbf0040
        .globl resource_history_prepare
resource_history_prepare:
        RESOURCE_AXIS $a1, E16_RESOURCE_SONGS
        RESOURCE_AXIS $a2, 2
        RESOURCE_AXIS $a3, 4
        RESOURCE_RESUME 0x198fe0, 0x00061940, 0x3c010001
        .globl resource_legacy_chart
resource_legacy_chart:
        RESOURCE_AXIS $a0, E16_RESOURCE_SONGS
        RESOURCE_AXIS $a1, 2
        RESOURCE_AXIS $a2, 5
        RESOURCE_RESUME 0x1e3cf0, 0x27bdffc0, 0xffbf0030
        .globl resource_cached_rival_delta
resource_cached_rival_delta:
        RESOURCE_AXIS $a1, 11
        RESOURCE_AXIS $a2, E16_RESOURCE_SONGS
        RESOURCE_AXIS $a3, 5
        RESOURCE_RESUME 0x216cb0, 0x00061080, 0x3c030002
        .globl resource_cached_grade_delta
resource_cached_grade_delta:
        RESOURCE_AXIS $a1, E16_RESOURCE_SONGS
        RESOURCE_AXIS $a2, 5
        RESOURCE_RESUME 0x216d50, 0x27bdffb0, 0xffbf0040
#ifdef PREVIEW_TRACE
        ASSET_HOOK asset_unpack_hook, pvt_unpack_call
#else
        ASSET_HOOK asset_unpack_hook, asset_unpack
#endif
        ASSET_HOOK asset_release_hook, asset_release
        ASSET_HOOK asset_workspace_exit_hook, asset_workspace_exit
#ifdef ASSET_SPLIT_TEXTURE
        ASSET_HOOK asset_texture_flush_hook, asset_texture_flush
#endif

#ifdef PREP_WATCH
        GSM_BODY pw_chart_body, 0x27bdffb0, 0xffbf0040, 0x1f5df8
        ASSET_HOOK pw_chart_hook, pw_chart
        ASSET_HOOK pw_submit_hook, pw_submit
        ASSET_HOOK pw_back_hook, pw_back
        ASSET_HOOK pw_frame_hook, pw_frame
#endif

#ifdef LOAD_PROFILE
#ifdef PREP_PROBE
        ASSET_HOOK prep_workspace_allocate_hook, prep_workspace_allocate
        ASSET_HOOK prep_movie_work_allocate_hook, prep_movie_work_allocate
        ASSET_HOOK prep_movie_frame_allocate_hook, prep_movie_frame_allocate
#endif
        GSM_BODY lp_select_ctor_body, 0x27bdffb0, 0x3c030026, 0x18b1e8
        ASSET_HOOK lp_select_ctor_hook, lp_select_ctor
        GSM_BODY lp_chart_body, 0x27bdffb0, 0xffbf0040, 0x1f5df8
        GSM_BODY lp_play_body, 0x27bdffb0, 0xffbf0040, 0x1f2178
        GSM_BODY lp_result_body, 0x27bdffa0, 0x2402ffff, 0x17bd38
        GSM_BODY lp_result_draw_body, 0x27bdffe0, 0xffbf0010, 0x179fe8
        ASSET_HOOK lp_chart_hook, lp_chart
        ASSET_HOOK lp_play_hook, lp_play
        ASSET_HOOK lp_result_hook, lp_result
        ASSET_HOOK lp_result_draw_hook, lp_result_draw
        ASSET_HOOK lp_update_hook, lp_update
        ASSET_HOOK lp_draw_hook, lp_draw
        ASSET_HOOK lp_submit_hook, lp_submit
        ASSET_HOOK lp_flip_hook, lp_flip
        ASSET_HOOK lp_vblank_hook, lp_vblank
        ASSET_HOOK lp_tail_hook, lp_tail
#endif
#ifdef EXIT_TRACE
        GSM_BODY exit_play_body, 0x27bdffb0, 0xffbf0040, 0x1f2178
        GSM_BODY exit_render_body, 0x27bdfff0, 0x3c050039, 0x136c38
        GSM_BODY exit_result_create_body, 0x27bdffa0, 0x2402ffff, 0x17bd38
        GSM_BODY exit_result_draw_body, 0x27bdffe0, 0xffbf0010, 0x179fe8
        GSM_BODY exit_result_side_body, 0x27bdff50, 0xffbf0090, 0x17c578
        ASSET_HOOK exit_result_create_hook, exit_trace_result_create
        ASSET_HOOK exit_result_draw_hook, exit_trace_result_draw
        ASSET_HOOK exit_result_side_hook, exit_trace_result_side
        ASSET_HOOK exit_play_hook, exit_trace_play
        ASSET_HOOK exit_render_hook, exit_trace_render
        ASSET_HOOK exit_audio_stop_hook, exit_trace_audio_stop
        ASSET_HOOK exit_unload_hook, exit_trace_unload
        ASSET_HOOK exit_movie_tick_hook, exit_trace_movie_tick
        ASSET_HOOK exit_vblank_hook, exit_trace_vblank
        ASSET_HOOK exit_flip_hook, exit_trace_flip
        ASSET_HOOK exit_audio_tick_hook, exit_trace_audio_tick
        ASSET_HOOK exit_frame_tail_hook, exit_trace_frame_tail
        ASSET_HOOK exit_cleanup_hook, exit_trace_cleanup
        ASSET_HOOK exit_score_finish_hook, exit_trace_score_finish
        ASSET_HOOK exit_preferences_hook, exit_trace_preferences
        ASSET_HOOK exit_play_free_hook, exit_trace_play_free
        ASSET_HOOK exit_first_lamp_hook, exit_trace_first_lamp
        ASSET_HOOK exit_play_count_hook, exit_trace_play_count
        ASSET_HOOK exit_lamp_commit_hook, exit_trace_lamp_commit
        ASSET_HOOK exit_score_commit_hook, exit_trace_score_commit
        ASSET_HOOK exit_unlock_commit_hook, exit_trace_unlock_commit
        ASSET_HOOK exit_score_stats_hook, exit_trace_score_stats
        ASSET_HOOK exit_score_rank_hook, exit_trace_score_rank
        ASSET_HOOK exit_score_history_hook, exit_trace_score_history


#endif

        # Native slot resolution has already completed. A failed import can
        # leave a registered slot without metadata; reject it before touching
        # its animation table. v0 is scratch here, original prologue/epilogue
        # retain all arguments and full-width saved registers. No C call.
        .globl asset_anim_guard
asset_anim_guard:
        beqz $3,1f
        nop
        lw $2,0($3)
        beqz $2,1f
        nop
        lw $2,8($3)
        sltu $2,$5,$2
        beqz $2,1f
        nop
        lui $10,0x35
        lw $5,0x218($3)
        j 0x135278
        nop
1:      j 0x135408
        nop

        # The native length query also walks the same graph. Return a finite
        # empty clip (one frame) for an absent/failed/out-of-range animation;
        # this avoids both stale-pointer traversal and modulo-zero callers.
        .globl asset_length_guard
asset_length_guard:
        beqz $7,1f
        nop
        lw $3,0($7)
        beqz $3,1f
        nop
        lw $3,8($7)
        sltu $3,$5,$3
        beqz $3,1f
        nop
        sll $3,$5,2
        addu $2,$2,$3
        j 0x135468
        nop
1:      jr $ra
        li $2,1
        ASSET_HOOK asset_workspace_hook, asset_workspace
        ASSET_HOOK asset_movie_alloc_hook, asset_movie_alloc
        ASSET_HOOK asset_movie_free_hook, asset_movie_free
#ifdef ASSET_FRAME_ORDER
        ASSET_HOOK asset_movie_frame_alloc_hook, asset_movie_frame_alloc
        ASSET_HOOK asset_movie_frame_free_hook, asset_movie_frame_free
#endif

        # Native queue s3 points at the current request. Consumed large-BGA
        # requests join the native advance block; all other requests retain
        # the original yield, read, callback and workspace accounting.
        .globl asset_queue_hook
asset_queue_hook:
        SAVE
        move $4,$19
        jal asset_queue
        nop
        RESTORE
        bnez $2,1f
        nop
        j 0x13bed0
        nop
1:      j 0x14cc50
        nop

        # Recoverable saves run only inside native menu/card controllers.
        # Physical slot routing never changes the native primary/rival role.
        # No calls or polling: preserve all128 bits of the only two scratch
        # registers, plus SP/HI/LO/SA and every argument except mapped a0.
        .macro PROFILE_PORT name, word0, word1, continuation
        .globl \name
\name:
        addiu $sp,$sp,-32
        SQ 24,0
        SQ 25,16
        bnez $a0,9f
        nop
        lui $24,%hi(save_location)
        addiu $24,$24,%lo(save_location)
        lw $25,4($24)
        beqz $25,9f
        nop
        lui $25,0x2b
        lw $25,-0x2c5c($25)         # logical native role at2ad3a4
        bnez $25,9f
        nop
        lw $a0,8($24)
9:      LQ 24,0
        LQ 25,16
        addiu $sp,$sp,32
        .word \word0
        .word \word1
        j \continuation
        nop
        .endm
        PROFILE_PORT profile_mc_open,0x27bdff70,0x3c02002b,0x11ec70
        PROFILE_PORT profile_mc_info,0x27bdff60,0xffb60070,0x11f3d0
        PROFILE_PORT profile_mc_dir,0x27bdff60,0x3c02002b,0x11f5c0
        PROFILE_PORT profile_mc_format,0x27bdffa0,0x3c02002b,0x11f8e0
        PROFILE_PORT profile_mc_delete,0x27bdff80,0x3c02002b,0x11f9b0
        PROFILE_PORT profile_mc_setinfo,0x27bdff60,0x3c02002b,0x11fb90
        PROFILE_PORT profile_mc_rename,0x27bdff70,0x3c02002b,0x11fd60
        PROFILE_PORT profile_mc_unformat,0x27bdffa0,0x3c02002b,0x11feb8
        PROFILE_PORT profile_mc_space,0x27bdff90,0x3c02002b,0x11ff88
        # Slot-number artwork lookup only. Do not mutate logical primary/rival
        # state while drawing; route its four local port loads instead.
        .macro PROFILE_DISPLAY name, continuation
        .globl \name
\name:
        addiu $sp,$sp,-32
        SQ 24,0
        SQ 25,16
        lui $24,0x2b
        lw $a0,-0x2c5c($24)
        bnez $a0,9f
        nop
        lui $24,%hi(save_location)
        addiu $24,$24,%lo(save_location)
        lw $25,4($24)
        beqz $25,9f
        nop
        lw $a0,8($24)
9:      LQ 24,0
        LQ 25,16
        addiu $sp,$sp,32
        .word 0x3c0500c6
        j \continuation
        nop
        .endm
        PROFILE_DISPLAY profile_display_0,0x1d76b8
        PROFILE_DISPLAY profile_display_1,0x1d76d4
        PROFILE_DISPLAY profile_display_2,0x1d76fc
        PROFILE_DISPLAY profile_display_3,0x1d7718
        GSM_BODY save_profile_presence_body,0x27bdffa0,0x3c060027,0x1d43e8
        GSM_BODY save_profile_startup_body,0x27bdfff0,0x3c020079,0x1d2738
        ASSET_HOOK save_profile_presence_hook,save_profile_presence
        ASSET_HOOK save_profile_startup_hook,save_profile_startup
        ASSET_HOOK save_profile_role_hook,save_profile_role
        GSM_BODY save_write_body, 0x27bdfff0, 0x3c020079, 0x1d32b8
        GSM_BODY save_load_body, 0x27bdfff0, 0x3c020079, 0x1d2eb8
        GSM_BODY save_auto_body, 0x27bdffc0, 0xffbf0030, 0x1d50e8
        GSM_BODY save_manual_body, 0x27bdffc0, 0x3c020079, 0x1d4518
#ifdef SAVE_PROFILE
        GSM_BODY save_probe_chart_body, 0x27bdffb0, 0xffbf0040, 0x1f5df8
        GSM_BODY save_probe_select_body, 0x27bdffb0, 0x3c030026, 0x18b1e8
        ASSET_HOOK save_probe_chart_hook, save_probe_chart
        ASSET_HOOK save_probe_select_hook, save_probe_select
        ASSET_HOOK save_probe_tail_hook, save_probe_tail
        GSM_BODY save_load_ui_body, 0x27bdffd0, 0x3c020079, 0x1d5f48
        GSM_BODY save_load_auto_body, 0x27bdffd0, 0x3c020079, 0x1d64e8
        ASSET_HOOK save_load_ui_hook, save_load_ui
        ASSET_HOOK save_load_auto_hook, save_load_auto
#endif
        GSM_BODY save_result_body, 0x27bdffb0, 0xffbf0040, 0x1a0bc8
        GSM_BODY save_message_body, 0x27bdff50, 0x3c020026, 0x1d6d48
        GSM_BODY save_result_exit_body, 0x27bdffa0, 0x24020005, 0x17a398
        ASSET_HOOK save_write_hook, save_write
        ASSET_HOOK save_load_hook, save_load
        ASSET_HOOK save_auto_hook, save_auto
        ASSET_HOOK save_manual_hook, save_manual
        ASSET_HOOK save_result_hook, save_result
        ASSET_HOOK save_message_hook, save_message
        ASSET_HOOK save_result_exit_hook, save_result_exit

#ifdef INPUT_LOG
        GSM_BODY input_frame_body, 0x27bdffc0, 0xffbf0030, 0x130738
        GSM_BODY input_read_body, 0x0080382d, 0x24030070, 0x11b9e0
        ASSET_HOOK input_frame_hook, input_probe_frame
        ASSET_HOOK input_read_hook, input_probe_read
        ASSET_HOOK input_start_hook, input_probe_start
        # Original scePadRead stack: selected snapshot +58 sequence, +60 length.
        # Only AT and A2 scratch; restore original A2 length/A0 destination.
        # No extra cache invalidation, DMA read, pad call, or stack adjustment.
        .globl input_snapshot_hook
input_snapshot_hook:
        lui $1,%hi(input_probe_selected)
        lw $1,%lo(input_probe_selected)($1)
        beqz $1,1f
        nop
        lw $6,0x58($sp)
        sw $6,0($1)
        lw $6,0x60($sp)
        sw $6,4($1)
1:      lw $6,0x60($sp)
        move $4,$16
        j 0x11ba30
        nop
#endif

#ifdef AUDIO_PROBE
        GSM_BODY ap_render_body, 0x27bdfff0, 0x3c050039, 0x136c38
        ASSET_HOOK ap_render_hook, ap_render
        ASSET_HOOK ap_wait_hook, ap_wait
        ASSET_HOOK ap_service_hook, ap_service
        ASSET_HOOK ap_sample_hook, ap_sample
        ASSET_HOOK ap_batch_hook, ap_batch
        ASSET_HOOK ap_rpc_hook, ap_rpc
#endif

#ifdef USB_HID
        CCALL usb_setup_hook, usb_input_setup
        CCALL usb_read_hook, usb_input_read
        CCALL usb_type_hook, usb_input_type
#endif

        ASSET_HOOK ad_frame_wait_hook, ad_frame_wait
        ASSET_HOOK ad_wake_hook, ad_wake
        ASSET_HOOK ad_request_hook, ad_request
        GSM_BODY fs_ring_body, 0x8f8688f0, 0x3c07007a, 0x1ed5a8
        GSM_BODY fs_play_body, 0x27bdffc0, 0xffbf0030, 0x1fe458
        GSM_BODY fs_result_body, 0x27bdff50, 0xffbf0090, 0x17c578
        GSM_BODY fs_input_body, 0x27bdffc0, 0xffbf0030, 0x13bbf8
        ASSET_HOOK fs_ring_hook, fs_ring
#ifdef HOLDS
        # replaces jal 0x1e6bb0(tick) at 0x1e5cb8; a0 = tick (the delay slot's ld)
        ASSET_HOOK holds_frame_entry, holds_frame_hook
        ASSET_HOOK holds_spawn_entry, holds_spawn_hook
#endif
        ASSET_HOOK fs_play_hook, fs_play
        GSM_BODY fs_ghost_body,0x27bdfe10,0xffbf00a0,0x1fb958
        ASSET_HOOK fs_ghost_draw_hook,fs_ghost_call
        GSM_BODY cs_score_body,0x27bdffd0,0xffbf0020,0x204188
        GSM_BODY ca_load_body,0x27bdffb0,0x3c030028,0x1fac88
        ASSET_HOOK ca_load_hook,ca_load_call
        GSM_BODY ca_bomb_init_body,0x27bdffd0,0x0000382d,0x1ff558
        ASSET_HOOK ca_bomb_init_hook,ca_bomb_init_call
        GSM_BODY ca_bomb_hit_body,0x27bdff80,0xffbf0070,0x1ff348
        ASSET_HOOK ca_bomb_hit_hook,ca_bomb_hit_call
        GSM_BODY ca_cover_top_body,0x27bdff30,0xffbf00a0,0x1fbf18
        ASSET_HOOK ca_cover_top_hook,ca_cover_top_call
        GSM_BODY ca_cover_bottom_body,0x27bdff00,0x24020002,0x1fc198
        ASSET_HOOK ca_cover_bottom_hook,ca_cover_bottom_call
        ASSET_HOOK ca_cover_gate_hook,ca_cover_gate
        .globl ca_turn_hook
ca_turn_hook:
        SAVE
        move $6,$17
        jal ca_turn_call
        nop
        RESTORE
        jr $ra
        nop
        ASSET_HOOK cs_score_hook,cs_score_call
        ASSET_HOOK cs_judge_visible_hook,cs_judge_visible_call
        ASSET_HOOK cs_judge_bad_hook,cs_judge_bad
        ASSET_HOOK cs_judge_good_hook,cs_judge_good
        ASSET_HOOK cs_judge_pgreat_hook,cs_judge_pgreat
        ASSET_HOOK fs_result_hook, fs_result
        ASSET_HOOK fs_input_hook, fs_input
        # Native text has a NINTH argument at caller SP. Reserve independent
        # outgoing slots below SAVE (never overwrite saved s0), forward that
        # word and pass the native single-breakdown side (s1) as argument10.
        .macro FS_COUNTS name, body, side
        .globl \name
\name:
        SAVE
        addiu $sp, $sp, -0x10
        ld $24,0xc0($sp)
        sd $24,0x00($sp)
        sd \side,0x08($sp)
        jal \body
        nop
        addiu $sp, $sp, 0x10
        RESTORE
        jr $31
        nop
        .endm
        FS_COUNTS fs_ghost_hook, fs_ghost_text, $21
        FS_COUNTS fs_counts_single_hook, fs_counts_single, $17
        FS_COUNTS fs_counts_battle_hook, fs_counts_battle, $zero
        .macro FS_TIMED name, side, delta
        .globl \name
\name:
        SAVE
        move $6, \side
        move $7, \delta
        jal fs_timed
        nop
        RESTORE
        jr $31
        nop
        .endm
        FS_TIMED fs_key_hook, $19, $22
        FS_TIMED fs_scratch_hook, $20, $16

#ifdef PLAY_LIFT
        ASSET_HOOK lift_hidden_hook,lift_hidden_call
        GSM_BODY lift_hidden_body,0x00041080,0x00441821,0x2059d8
#endif
# FHS: preserve original bodies and full EE saved registers.
        ASSET_HOOK fhs_restore_hook, fhs_restore_call
        GSM_BODY fhs_restore_body, 0x27bdff80, 0xffbf0070, 0x1f5858
        ASSET_HOOK fhs_commit_hook, fhs_commit_call
        GSM_BODY fhs_commit_body, 0x27bdffb0, 0x27828978, 0x1f06f8
        ASSET_HOOK fhs_speed_hook, fhs_speed_call
        GSM_BODY fhs_speed_body, 0x27bdfe40, 0x3c020026, 0x1f4238
        ASSET_HOOK fhs_cover_hook, fhs_cover_call
        GSM_BODY fhs_cover_body, 0x27bdff50, 0x24030010, 0x1f3cc8
        ASSET_HOOK fhs_index_hook, fhs_index_call
        GSM_BODY fhs_index_body, 0x00041080, 0x00441821, 0x2058b8
        ASSET_HOOK fhs_index_set_hook, fhs_index_set_call
        GSM_BODY fhs_index_set_body, 0x00041880, 0x00643021, 0x2058e8
        ASSET_HOOK fhs_ab_hook, fhs_ab_call
        .globl fhs_ab_body
fhs_ab_body:
        .word 0x2ca10008
        beq $at,$zero,1f
        .word 0x24070004
        j 0x19b0dc
        nop
1:      j 0x19b10c
        nop
        ASSET_HOOK fhs_ab_set_hook, fhs_ab_set_call
        .globl fhs_ab_set_body
fhs_ab_set_body:
        .word 0x2ca10008
        beq $at,$zero,1f
        .word 0x24080004
        j 0x19b17c
        nop
1:      j 0x19b1ac
        nop
        ASSET_HOOK fhs_velocity_hook, fhs_velocity_call
        GSM_BODY fhs_velocity_body, 0x27bdffe0, 0xffbf0010, 0x1e5a98
        ASSET_HOOK fhs_ring_hook, fhs_ring_call
        GSM_BODY fhs_ring_body, 0x8c820194, 0x24420001, 0x1482f8
        ASSET_HOOK fhs_hud0_hook, fhs_hud0_call
        GSM_BODY fhs_hud0_body, 0x27bdff70, 0xffbf0060, 0x1ffee8
        ASSET_HOOK fhs_hud1_hook, fhs_hud1_call
        GSM_BODY fhs_hud1_body, 0x27bdff70, 0xffbf0060, 0x200248
        ASSET_HOOK fhs_hud2_hook, fhs_hud2_call
        GSM_BODY fhs_hud2_body, 0x27bdff10, 0xffbf00b0, 0x2005a8
        ASSET_HOOK fhs_hud3_hook, fhs_hud3_call
        GSM_BODY fhs_hud3_body, 0x27bdff10, 0xffbf00b0, 0x200ae8
        ASSET_HOOK fhs_hud4_hook, fhs_hud4_call
        GSM_BODY fhs_hud4_body, 0x27bdff10, 0xffbf00b0, 0x201028
        ASSET_HOOK fhs_hud5_hook, fhs_hud5_call
        GSM_BODY fhs_hud5_body, 0x27bdff10, 0xffbf00b0, 0x201568
        FS_COUNTS fhs_gn_text_hook, fhs_gn_text, $zero
        FS_COUNTS fhs_label_text_hook, fhs_label_text, $zero

        ASSET_HOOK fhs_bar_index_hook, fhs_bar_index

        ASSET_HOOK fhs_chart_hook, fhs_chart_call
        GSM_BODY fhs_chart_body, 0x27bdff70, 0xffbf0070, 0x1e65d8
        ASSET_HOOK fhs_prime_hook, fhs_prime_call
        GSM_BODY fhs_prime_body, 0x27bdff90, 0xffbf0060, 0x1e5b88

        ASSET_HOOK lq_setup_hook, lq_setup_call
        GSM_BODY lq_setup_body, 0x27bdffa0, 0xffbf0050, 0x1ea878

        ASSET_HOOK lq_hran_hook, lq_hran_call
        GSM_BODY lq_hran_body, 0x3c02007a, 0x8c4232c4, 0x205c18

        ASSET_HOOK lq_allscr_hook, lq_allscr_call
        GSM_BODY lq_allscr_body, 0x3c02007a, 0x904232c8, 0x205b88

        ASSET_HOOK lq_note_link_hook, lq_note_link
        ASSET_HOOK qr_play_hook, qr_play_call
        ASSET_HOOK qr_decision_hook, qr_decision_call
        ASSET_HOOK qr_choice_hook, qr_choice_call
        ASSET_HOOK qr_continue_hook, qr_continue_call

        .globl player_note_timing_hook
player_note_timing_hook:
        SAVE
        move $5,$18                  # native note object, side byte at +0xa
        jal player_note_timing
        nop
        RESTORE
        jr $31
        nop

#if defined(USB_HID) || defined(PLAY_PREVIEW)
        ASSET_HOOK pve_ctor_hook, pve_ctor_call
        ASSET_HOOK pve_select_init_hook, pve_select_init_call
#endif

#if defined(USB_HID) || defined(PLAY_PREVIEW)
        ASSET_HOOK pve_graph_hook,pve_graph_call
        ASSET_HOOK pve_rival_hook,pve_rival_call
        ASSET_HOOK pve_graph_save_hook,pve_graph_save_call
        ASSET_HOOK pve_cover_save_hook,pve_cover_save_call
        ASSET_HOOK pve_options_save_hook,pve_options_save_call
        ASSET_HOOK pve_options_hook,pve_options_call
#endif

#ifdef PREVIEW_TRACE
        ASSET_HOOK pvt_vblank_hook,pvt_vblank_call
        .globl pvt_set_handler
pvt_set_handler:
        li $3,14
        syscall
        jr $ra
        nop
        .globl pvt_set_tlb_handler
pvt_set_tlb_handler:
        li $3,13
        syscall
        jr $ra
        nop
        .globl pvt_gif_qword
pvt_gif_qword:
        .word 0x78820000             # lq v0,0(a0)
        lui $3,0x1000
        .word 0x7c626000             # sq v0,0x6000(v1), GIF FIFO
        jr $ra
        nop
        .globl pvt_exception_vector
pvt_exception_vector:
        nop
        nop
        # KERNEL exception vector only: reserved k0 is appropriate here.
        lui $26,%hi(spr_buf)
        addiu $26,$26,%lo(spr_buf)
        .word 0x7f400000 # sq register0
        .word 0x7f410010 # sq register1
        .word 0x7f420020 # sq register2
        .word 0x7f430030 # sq register3
        .word 0x7f440040 # sq register4
        .word 0x7f450050 # sq register5
        .word 0x7f460060 # sq register6
        .word 0x7f470070 # sq register7
        .word 0x7f480080 # sq register8
        .word 0x7f490090 # sq register9
        .word 0x7f4a00a0 # sq register10
        .word 0x7f4b00b0 # sq register11
        .word 0x7f4c00c0 # sq register12
        .word 0x7f4d00d0 # sq register13
        .word 0x7f4e00e0 # sq register14
        .word 0x7f4f00f0 # sq register15
        .word 0x7f500100 # sq register16
        .word 0x7f510110 # sq register17
        .word 0x7f520120 # sq register18
        .word 0x7f530130 # sq register19
        .word 0x7f540140 # sq register20
        .word 0x7f550150 # sq register21
        .word 0x7f560160 # sq register22
        .word 0x7f570170 # sq register23
        .word 0x7f580180 # sq register24
        .word 0x7f590190 # sq register25
        .word 0x7f5b01b0 # sq register27
        .word 0x7f5c01c0 # sq register28
        .word 0x7f5d01d0 # sq register29
        .word 0x7f5e01e0 # sq register30
        .word 0x7f5f01f0 # sq register31

        mfc0 $8,$12
        sw $8,512($26)
        mfc0 $8,$13
        sw $8,516($26)
        mfc0 $8,$14
        sw $8,520($26)
        mfc0 $8,$30
        sw $8,524($26)
        mfc0 $8,$8
        sw $8,528($26)
        lui $sp,%hi(spr_buf+18432)
        addiu $sp,$sp,%lo(spr_buf+18432)
        jal pvt_exception_capture
        nop
1:      nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        b 1b
        nop
        ASSET_HOOK pvt_release_hook,pvt_release_call
        ASSET_HOOK pvt_batch_hook,pvt_batch_call
        ASSET_HOOK pvt_idle_hook,pvt_idle_call
        ASSET_HOOK pvt_decode_hook,pvt_decode_call
        GSM_BODY pvt_decode_body, 0x27bdef50, 0x0000402d, 0x1361e8
        ASSET_HOOK pvt_register_hook,pvt_register_call
        GSM_BODY pvt_register_body, 0x8c830000, 0x8c650008, 0x135868
        ASSET_HOOK pvt_submit_hook,pvt_submit_call
        ASSET_HOOK pvt_query_hook,pvt_query_call
        GSM_BODY pvt_query_body, 0x8f878440, 0x3c090035, 0x135778
        ASSET_HOOK pvt_sprite_hook,pvt_sprite_call
        GSM_BODY pvt_sprite_body, 0x27bdffe0, 0x3c0b0035, 0x135678
#endif
