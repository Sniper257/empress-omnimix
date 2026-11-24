r"""Build the launcher runtime for EMPRESS (virtual disc tools/offdisc/vdisc.c + song database songdb.c/.S)
and its exe patch list.

usage: python3 tools/offdisc/build_vdisc.py [outdir=offdisc/launcher]
-> vdisc.bin (blob linked at RES_LO) and vdisc.txt, read by the launcher:
   base <hex>                       load address of vdisc.bin
   conf <hex>                       address of the vd_conf_t the launcher fills
   sdb <hex>                        address of the sdb_t (songdb.c) the launcher fills
   W <va> <orig> <new>              exe word patch (orig checked)
   J <va> <orig> <symbol address>   jal site -> runtime function (orig checked)
   S <lui va> <addiu va> <path>     boot module path (FUN_0012f790): the launcher writes <device><path> into the
                                    block and repoints the lui/addiu pair (cdrom0:\MODULES\X;1 -> host:EMPRESS/MODULES/X)
Reserved block = 1.5 MB right after the bss (0x7c4700..0x944700): the libc heap (SetupHeap arg at 0x1001d0) and
libc sbrk's own pointer (data word 0x2360f4), the game's memory manager (FUN_00152dc0, start at 0x152dc4, its big block = memsize - start - 0x105000) both start
0x100000 later. The crt0 bss clear (0x100120) still stops at 0x7c4700, so the launcher's copy survives."""
import os, struct, subprocess, sys, hashlib, argparse
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'cn'))
sys.path.insert(0, os.path.join(HERE, '..', 'e16'))
from build import CFLAGS, check, run
import ceiling

RES_LO, RES_HI = 0x7c4700, 0x944700
JALS = {0x14cbac: (0x11ddb0, 'vd_read'), 0x14cbbc: (0x11d378, 'vd_sync'), 0x14cbd4: (0x11d378, 'vd_sync'),
        0x14ca74: (0x11db90, 'vd_ready'), 0x14cb8c: (0x11db90, 'vd_ready'), 0x14ccac: (0x11df90, 'vd_seek'),
        0x14cc18: (0x11e060, 'vd_error'), 0x14ca90: (0x11e2b0, 'vd_st_init'), 0x14caa4: (0x11e2e0, 'vd_st_start'),
        0x14c984: (0x11e318, 'vd_st_seek'), 0x14c9b0: (0x11e3b0, 'vd_st_read'),
        0x14cab4: (0x11e378, 'vd_st_stop'), 0x14cad8: (0x11e378, 'vd_st_stop'), 0x14cda8: (0x11e378, 'vd_st_stop'),
        0x14cdc8: (0x11e378, 'vd_st_stop')}
MODULES = [(0x12f7b8, 0x12f7c0, 0x2641e0)] + [(lui, lui + (8 if lui == 0x12f828 else 4), 0x264200 + 0x20 * i)
           for i, lui in enumerate((0x12f828, 0x12f848, 0x12f868, 0x12f888, 0x12f8a8, 0x12f8c8, 0x12f8e8))]
GAME_DIR = 'EMPRESS/'
WORDS = {0x192a40: (0x0c04c398, 0), # RANDOM: let native title intro play instead of seeking to its final frame
         0x21a294: (0x28630008, 0x28630009), # parent folder close: draw final217px anchor before normal state
         0x1001d0: (0x3c04007c, 0x3c040094),     # SetupHeap(0x7c4700) -> 0x944700
         0x152dc4: (0x3c03007c, 0x3c030094),     # arena start, same
         0x2360f4: (0x007c4700, 0x00944700),     # libc sbrk heap pointer (data word, init = _end)
         0x1f66c0: (0x1000000c, 0x2413ffee)}    # judge x hook delay: original layout2 y offset -18
# The USB launcher has just rebooted the original IOPRP310 from an EE buffer
# and restored USB. Avoid rebooting it again from mass: in FUN_0012f790.
# Keep native service/client initialization and all seven original module loads.
USB_WORDS = {0x12f7bc: (0x0c046146, 0x24020001),
             0x12f7d8: (0x0c046132, 0x24020001),
             # sceCdInit(SCECdINoD=1): retain service initialization without
             # waiting for an optical disc in this loose-file USB build.
             0x12f7ac: (0x0000202d, 0x24040001),
             0x12f814: (0x0000202d, 0x24040001)}
# Native 2.4 SIO2MAN uses an event-worker protocol, not MMCEMAN's unified
# semaphore interface. The MMCE launcher preloads SDK SIO2MAN (exports 2.7).
# Skip only the duplicate native load; retain retail PADMAN/MCMAN/MCSERV.
MMCE_WORDS = dict(USB_WORDS)
MMCE_WORDS[0x12f874] = (0x0c045ffa, 0x24020001)

def validate_module_loads(exe, words, skip_sio2=False, preloaded=False):
    """Decode the retail paths, not a hand-maintained module index. A wrong
    site can skip MCMAN while leaving incompatible SIO2MAN active (R1-R4)."""
    read = exe.word if hasattr(exe,'word')else lambda va: struct.unpack_from('<I', exe, va - 0x100000 + 0x80)[0]
    found = {}
    for lui, _, path in MODULES[1:]:
        pos = path - 0x100000 + 0x80
        text=exe.cstring(path)if hasattr(exe,'cstring')else exe[pos:exe.index(b'\0',pos)].decode()
        name=text.split('\\')[-1].split(';')[0]
        calls = [va for va in range(lui, lui+24, 4) if read(va)==JAL(0x117fe8)]
        assert len(calls)==1, (name, calls)
        va = calls[0]
        patched = words.get(va, (read(va),read(va)))[1]
        expected = 0x24020001 if preloaded or (skip_sio2 and name=='SIO2MAN.IRX') else read(va)
        assert patched==expected, ('Wrong hardware module load patch',name,hex(va),hex(patched))
        found[name] = {'call':va,'skipped':patched==0x24020001}
    assert len(found)==7 and 'SIO2MAN.IRX' in found and 'MCMAN.IRX' in found, found
    return found

def validate_mmce_modules(exe, words):
    return validate_module_loads(exe,words,skip_sio2=True)
NOP = 0
J = lambda t: 2 << 26 | t >> 2
JAL = lambda t: 3 << 26 | t >> 2
# song database hooks (songdb.S): va -> (orig word, kind, symbol) with kind 'j' / 'jal' / None (= NOP)
HOOKS = {0x1896d0: (0x27bdff90, 'j', 'menu_draw_hook'), 0x1896d4: (0xffbf0050, None, None),
         0x22ce90: (0x8f848aac, 'j', 'ranking_load_hook'),
         0x22ce94: (0x24020001, None, None),
         0x22ccc0: (0x3c09007b, 'j', 'ranking_evaluate_hook'),
         0x22ccc4: (0x3c07007b, None, None),
         0x1359d0: (0x27bdfe80, 'j', 'asset_unpack_hook'),
         0x1359d4: (0xffbf0070, None, None),
         0x135270: (0x3c0a0035, 'j', 'asset_anim_guard'),
         0x135274: (0x8c650218, None, None),
         0x135460: (0x00051880, 'j', 'asset_length_guard'),
         0x135464: (0x00431021, None, None),
         0x135830: (JAL(0x13a4b0), 'jal', 'asset_release_hook'),
         0x1f5e7c: (JAL(0x13a420), 'jal', 'asset_workspace_hook'),
         0x1f1258: (JAL(0x13a420), 'jal', 'asset_workspace_exit_hook'),
         0x1f212c: (JAL(0x13a420), 'jal', 'asset_workspace_exit_hook'),
         0x1f2f40: (JAL(0x13a420), 'jal', 'asset_workspace_exit_hook'),
         0x14cb78: (JAL(0x13bed0), 'jal', 'asset_queue_hook'),
         0x1322e4: (JAL(0x13a4c0), 'jal', 'asset_movie_alloc_hook'),
         0x131bd8: (JAL(0x13a4b0), 'jal', 'asset_movie_free_hook'),
         0x1e19a0: (0x24020134, 'j', 'rec_hook'), 0x1e19a4: (0x00a21818, None, None),
         0x1e1970: (0x03e00008, 'j', 'gate_hook'), 0x1e1974: (0x28820063, None, None),
         0x19bca0: (0x27bdffb0, 'j', 'sv_bca0'), 0x19bca4: (0xffbf0040, None, None),
         0x19bac0: (0x27bdffb0, 'j', 'sv_bac0'), 0x19bac4: (0xffbf0040, None, None),
         0x19b8e0: (0x27bdffb0, 'j', 'sv_b8e0'), 0x19b8e4: (0xffbf0040, None, None),
         0x14cf18: (0x3c040024, 'j', 'st_hook'), 0x14cf1c: (0x2484c2f0, None, None),
         0x14d15c: (0x3c040024, 'j', 'ld_hook'), 0x14d160: (0x2484c2f0, None, None),
         0x132080: (0x8c420000, 'jal', 'mv_hook'),
         # CUSTOM folder
         0x2114c4: (JAL(0x213550), 'jal', 'fb_hook'), 0x2115b4: (JAL(0x213550), 'jal', 'fb_hook'),
         0x2113b0: (J(0x212960), 'j', 'fc_hook'), 0x210d70: (J(0x212c80), 'j', 'fl_hook'),
         0x2113c0: (J(0x2118b0), 'j', 'fo_hook'),
         # Display/summary counters need the whole folder, not the native view.
         **{va: (JAL(0x2113b0), 'jal', 'ft_hook') for va in
            (0x192eb8, 0x1918e0, 0x19190c, 0x191930, 0x191944, 0x191958)},
         # More than three MUSIC NUMBER digits: reuse the native font/space.
         0x193a34: (0x3463851f, 'j', 'song_count_hook'), 0x193a38: (0x00720018, None, None),
         # Music-select update: synchronize the scene/wheel cursor before input.
         0x18a230: (0x27bdff90, 'j', 'window_update_hook'), 0x18a234: (0xffbf0060, None, None),
         0x1db4f0: (0x27bdffa0, 'j', 'window_tick_hook'), 0x1db4f4: (0xffbf0050, None, None),
         0x18afd4: (JAL(0x1d7aa0), 'jal', 'window_random_hook'),
         0x18c154: (JAL(0x1d7aa0), 'jal', 'window_random_hook'),
         # Static RANDOM SELECT panel; native roulette selection remains intact.
         **{va: (JAL(target), 'jal', sym) for target,sym,sites in
            ((0x1e33d0,'spr_panel_hook_b',(0x19032c,0x190bb8,0x194c5c)),
             (0x1e3390,'spr_panel_hook_g',(0x1902f0,0x190b78)),
             (0x1e3350,'spr_panel_hook_a',(0x190368,0x190bf8))) for va in sites},
         0x18bbc4: (0x26730001, 'jal', 'next_s3'), 0x18bbc8: (0x2a62002c, None, None),
         0x18f2e8: (0x26310001, 'jal', 'next_s1'), 0x18f2ec: (0x2a22002c, None, None),
         0x1926e0: (0x26100001, 'jal', 'next_s0'), 0x1926e4: (0x2a02002c, None, None),
         0x18bb54: (JAL(0x1d8e30), 'jal', 'vis_hook'), 0x18f1d8: (JAL(0x1d8e30), 'jal', 'vis_hook'),
         0x1925cc: (JAL(0x1d8e30), 'jal', 'vis_hook'), 0x18bcfc: (JAL(0x1d8e30), 'jal', 'vcur_hook'),
         0x190e78: (JAL(0x1d8e30), 'jal', 'vis_hook'), 0x191c34: (JAL(0x1d8e30), 'jal', 'vis_hook'),
         # song variants (hold SELECT [+ turntable] while a song loads): the NO CRIME check's song getter
         0x1f5f18: (JAL(0x205e00), 'jal', 'var_hook'),
         # music-select entry without a saved song: open a preview of the current folder too
         0x192018: (JAL(0x1dc460), 'jal', 'entry_hook'),
         # BPM panel FUN_00192ae0 (song s1): its `bltz s1` (a sign_nops site) -> bpm_hook: hide -1 / 99..103 / 124..
         0x192b3c: (0x0620012d, 'j', 'bpm_hook'),
         # per-frame panel label switch in FUN_0018a230 (row: lb ; `bltz` = sign_nops sites): -1 (empty wheel) is
         # no song again, aliases 128..254 still are
         0x18a4e0: (0x04400012, 'j', 'lbl_hook1'), 0x18af88: (0x0440000c, 'j', 'lbl_hook2'),
         # folder stats FUN_00210d80 (grade / lamp / rival counts): -1s for ids >= 0x2b, no added songs
         0x1918a4: (JAL(0x210d80), 'jal', 'stat_hook'),
         # note drawer's sprite draw: Skin settings note height (per sprite: game / note skin / measure line)
         0x1e757c: (JAL(0x12fdd0), 'jal', 'note_hook'),
         0x1fb950:(0x27bdfe10,'j','fs_ghost_draw_hook'),0x1fb954:(0xffbf00a0,None,None),
         0x204180:(0x27bdffd0,'j','cs_score_hook'),0x204184:(0xffbf0020,None,None),
         0x1fe48c: (JAL(0x16a400), 'jal', 'cs_judge_visible_hook'),
         **{va:(JAL(target),'jal',sym)for target,sym,sites in [(0x1fe5a0,'cs_judge_bad_hook',(0x1fe548,0x1fe560)),(0x1fe690,'cs_judge_good_hook',(0x1fe518,0x1fe530)),(0x1fe8f0,'cs_judge_pgreat_hook',(0x1fe500,))]for va in sites},
         # every sprite draw (FUN_00135540: addiu sp,-0x20 ; sd ra,0(sp)): lane brightness for the skins' lane sprites
         0x135540: (0x27bdffe0, 'j', 'tint_hook'), 0x135544: (0xffbf0000, None, None),
         # keybeam draws: y shift for the keybeam length (zoomY itself = launcher words at 0x1fe26c / 0x1fe3d4)
         0x1fe284: (JAL(0x12fae0), 'jal', 'beam_hook'), 0x1fe3ec: (JAL(0x12fae0), 'jal', 'beam_scratch_hook'),
         0x1fa880: (JAL(0x1fa3e0), 'jal', 'frame_setup_hook'),
         # P1 wide judge-count text: clear the wider 8th gauge housing, using native text/assets
         0x1f66bc: (0x24140184, 'j', 'judge_pos_hook'),
         # Original CS8 HUD callback + effects, scoped at runtime to pack2/layout2.
         0x200240: (0x27bdff70, 'j', 'eighth_callback_hook'), 0x200244: (0xffbf0060, None, None),
         0x135210: (0x27bdffc0, 'j', 'eighth_anim_hook'), 0x135214: (0x3c030035, None, None),
         0x1ff298: (JAL(0x12fae0), 'jal', 'eighth_bomb_hook'),
         0x1ff50c: (JAL(0x20fb50), 'jal', 'ca_particle_hook'),
         0x1fafe0: (JAL(0x1ff7d0), 'jal', 'eighth_progress_hook'),
         0x1faff0: (JAL(0x1fb650), 'jal', 'eighth_turntable_hook'),
         # sprite draw FUN_00135720 (lw v1,-0x7bc0(gp) ; lui t0,0x35): blank title slots reloaded first
         0x135720: (0x8f838440, 'j', 'draw_hook'), 0x135724: (0x3c080035, None, None),
         # saved folder getter FUN_001d85c0 (jr ra ; lw v0,0x54(a0)): hidden / none -> the first menu folder
         0x1d85c0: (0x03e00008, 'j', 'saved_hook'),
         # our folders (59 CUSTOM, 60.. versions) get the RANDOM row like the game's song folders
         0x18fe2c: (0x14400002, 'jal', 'rnd_hook'), 0x18fe34: (0x24110001, None, None),
         # folder menu widget: ids copied to the relocated array (FUN_0021bf60's memcpy)
         0x21bf80: (JAL(0x12ad48), 'jal', 'wcopy_hook'),
         # difficulty change with a folder open: re-open ours like ALL/EMPRESS (their lists are per difficulty)
         0x18c9ec: (0x28610003, 'jal', 'reo_hook'), 0x18c9f0: (0x14200086, None, None),
         # title sprite getters (wheel / big / genre / artist): added songs' sprites come from songdb.c's cache
         **{va: (0x27bdffe0, 'j', sym) for va, sym in ((0x1e3410, 'spr_hook_w'), (0x1e33d0, 'spr_hook_b'),
                                                      (0x1e3390, 'spr_hook_g'), (0x1e3350, 'spr_hook_a'))},
         **{va + 4: (0xffbf0010, None, None) for va in (0x1e3410, 0x1e33d0, 0x1e3390, 0x1e3350)}}
# Native Customize producer boundaries; no persistent native selected-byte patch.
for _at,_w0,_w1,_name in [(0x1fac80,0x27bdffb0,0x3c030028,'load'),(0x1ff550,0x27bdffd0,0x0000382d,'bomb_init'),(0x1ff340,0x27bdff80,0xffbf0070,'bomb_hit'),(0x1fbf10,0x27bdff30,0xffbf00a0,'cover_top'),(0x1fc190,0x27bdff00,0x24020002,'cover_bottom')]:
    assert _at not in HOOKS and _at+4 not in HOOKS
    HOOKS[_at]=(_w0,'j','ca_'+_name+'_hook');HOOKS[_at+4]=(_w1,None,None)
for _at,_target,_name in [(0x1fb86c,0x131410,'ca_turn_hook'),(0x1fbfa0,0x1f6430,'ca_cover_gate_hook'),(0x1fc260,0x1f6430,'ca_cover_gate_hook')]:
    assert _at not in HOOKS;HOOKS[_at]=(JAL(_target),'jal',_name)
# Resource entry hooks protect all callers, rather than selected UI call sites.
RESOURCE_HOOKS = (
    (0x177750,0x24020004,0x10e2000c,'resource_rival_graph_a'),
    (0x1778c0,0x24030004,0x10e3000c,'resource_rival_graph_write'),
    (0x177970,0x24020004,0x10e2000c,'resource_rival_reset'),
    (0x1779f0,0x24030004,0x10e3000c,'resource_rival_lamp_write'),
    (0x177a60,0x24020004,0x10e2000c,'resource_rival_lamp_a'),
    (0x177ad0,0x24020004,0x10e2000c,'resource_rival_score_a'),
    (0x179c30,0x24030004,0x10e3000c,'resource_rival_graph_b'),
    (0x179dc0,0x24030004,0x10e3000c,'resource_rival_lamp_b'),
    (0x179e50,0x24030004,0x10e3000c,'resource_rival_score_b'),
    (0x1d88c0,0x27bdffc0,0xffbf0030,'resource_favorite_get'),
    (0x1d8960,0x27bdffb0,0xffbf0040,'resource_favorite_set'),
    (0x198fe0,0x00061940,0x3c010001,'resource_history_prepare'),
    (0x1e3cf0,0x27bdffc0,0xffbf0030,'resource_legacy_chart'),
    (0x216cb0,0x00061080,0x3c030002,'resource_cached_rival_delta'),
    (0x216d50,0x27bdffb0,0xffbf0040,'resource_cached_grade_delta'),
    (0x1e3650,0x27bdffc0,0xffbf0030,'resource_graph_target_hook'),
    (0x1e3700,0x27bdffb0,0xffbf0040,'resource_note_count_hook'),
    (0x1e1a60,0x27bdff90,0x0000282d,'resource_profile_rebuild_hook'),
    (0x1e1d90,0x27bdffb0,0xffbf0040,'resource_profile_baseline_hook'))
for _site,_word0,_word1,_symbol in RESOURCE_HOOKS:
    assert _site not in HOOKS and _site+4 not in HOOKS
    HOOKS[_site]=(_word0,'j',_symbol)
    HOOKS[_site+4]=(_word1,None,None)
# Preserve original menu/UI coordinators; replace only their native data
# transaction boundary and wrap manual/automatic entry for save policy.
for _va,_original,_name in (
    (0x1d76b0,(0x8f8487b4,0x3c0500c6),'profile_display_0'),
    (0x1d76cc,(0x8f8487b4,0x3c0500c6),'profile_display_1'),
    (0x1d76f4,(0x8f8487b4,0x3c0500c6),'profile_display_2'),
    (0x1d7710,(0x8f8487b4,0x3c0500c6),'profile_display_3'),
    (0x1d2260,(0x03e00008,0xaf8487b4),'save_profile_role_hook'),
    (0x1d2730,(0x27bdfff0,0x3c020079),'save_profile_startup_hook'),
    (0x11ec68,(0x27bdff70,0x3c02002b),'profile_mc_open'),
    (0x11f3c8,(0x27bdff60,0xffb60070),'profile_mc_info'),
    (0x11f5b8,(0x27bdff60,0x3c02002b),'profile_mc_dir'),
    (0x11f8d8,(0x27bdffa0,0x3c02002b),'profile_mc_format'),
    (0x11f9a8,(0x27bdff80,0x3c02002b),'profile_mc_delete'),
    (0x11fb88,(0x27bdff60,0x3c02002b),'profile_mc_setinfo'),
    (0x11fd58,(0x27bdff70,0x3c02002b),'profile_mc_rename'),
    (0x11feb0,(0x27bdffa0,0x3c02002b),'profile_mc_unformat'),
    (0x11ff80,(0x27bdff90,0x3c02002b),'profile_mc_space'),
    (0x1d43e0,(0x27bdffa0,0x3c060027),'save_profile_presence_hook'),
    (0x1d32b0,(0x27bdfff0,0x3c020079),'save_write_hook'),
    (0x1d2eb0,(0x27bdfff0,0x3c020079),'save_load_hook'),
    (0x1d50e0,(0x27bdffc0,0xffbf0030),'save_auto_hook'),
    (0x1d4510,(0x27bdffc0,0x3c020079),'save_manual_hook'),
    (0x1a0bc0,(0x27bdffb0,0xffbf0040),'save_result_hook'),
    (0x1d6d40,(0x27bdff50,0x3c020026),'save_message_hook'),
    (0x17a390,(0x27bdffa0,0x24020005),'save_result_exit_hook')):
    HOOKS[_va]=(_original[0],'j',_name)
    HOOKS[_va+4]=(_original[1],None,None)
# Music-select construction clears abandoned FREE sessions before any option UI.
HOOKS[0x153fec]=(JAL(0x1003f0),'jal','gsm_menu_hook')
HOOKS[0x1a112c]=(JAL(0x204980),'jal','gsm_count_hook')
HOOKS[0x1a1154]=(JAL(0x204920),'jal','gsm_sample_hook')
# The same RESULT transfer copies three cumulative-score and three rate curves.
# Scope reduction to these copy calls; live PLAY/native history getters stay intact.
HOOKS[0x1a11e8]=(JAL(0x204910),'jal','gsm_count_hook')
HOOKS[0x1a1284]=(JAL(0x2048b0),'jal','gsm_score_sample_hook')
HOOKS[0x1a12bc]=(JAL(0x204830),'jal','gsm_rate_sample_hook')
# This one draw-only HARD query follows SELECT; earned-gauge getters stay intact.
HOOKS[0x17eb18]=(JAL(0x1d95b0),'jal','gsm_background_hard_hook')

# Entry hooks with native-body trampolines in songdb.S. No mode-specific native
# code is removed; disabled and non-FREE sessions fall through unchanged.
for va,w0,w1,sym in (
    (0x204d30,0x3c02007a,0x00041980,'gsm_update_hook'),
    (0x204d50,0x27bdffc0,0xffbf0030,'gsm_init_hook'),
    (0x203490,0x27bdffd0,0x3c06007a,'gsm_options_hook'),
    (0x205200,0x27bdffc0,0x3c02007a,'gsm_retry_hook'),
    (0x204990,0x27bdffc0,0x3083001f,'gsm_history_hook'),
    (0x1d95b0,0x27bdffd0,0xffbf0020,'gsm_hard_hook'),
    (0x1d9610,0x27bdffd0,0xffbf0020,'gsm_easy_hook'),
    (0x1d9f30,0x27bdffd0,0xffbf0020,'gsm_hard_raw_hook'),
    (0x17e920,0x27bdff80,0xffbf0060,'gsm_result_hook'),
    (0x17eee0,0x27bdffa0,0xffbf0050,'gsm_segment_hook'),
    (0x17aaa0,0x27bdffe0,0xffbf0010,'gsm_end_hook')):
    assert va not in HOOKS and va+4 not in HOOKS
    HOOKS[va]=(w0,'j',sym)
    HOOKS[va+4]=(w1,None,None)

# FHS entry trampolines; all original words checked against the input ELF.
for va,w0,w1,sym in (
    (0x1f5850,0x27bdff80,0xffbf0070,'fhs_restore_hook'),
    (0x1f06f0,0x27bdffb0,0x27828978,'fhs_commit_hook'),
    (0x1f4230,0x27bdfe40,0x3c020026,'fhs_speed_hook'),
    (0x1f3cc0,0x27bdff50,0x24030010,'fhs_cover_hook'),
    (0x2058b0,0x00041080,0x00441821,'fhs_index_hook'),
    (0x2058e0,0x00041880,0x00643021,'fhs_index_set_hook'),
    (0x19b0d0,0x2ca10008,0x1020000d,'fhs_ab_hook'),
    (0x19b170,0x2ca10008,0x1020000d,'fhs_ab_set_hook'),
    (0x1e5a90,0x27bdffe0,0xffbf0010,'fhs_velocity_hook'),
    (0x1482f0,0x8c820194,0x24420001,'fhs_ring_hook'),
    (0x1ffee0,0x27bdff70,0xffbf0060,'fhs_hud0_hook'),
    (0x2005a0,0x27bdff10,0xffbf00b0,'fhs_hud2_hook'),
    (0x200ae0,0x27bdff10,0xffbf00b0,'fhs_hud3_hook'),
    (0x201020,0x27bdff10,0xffbf00b0,'fhs_hud4_hook'),
    (0x201560,0x27bdff10,0xffbf00b0,'fhs_hud5_hook'),):
    assert va not in HOOKS and va+4 not in HOOKS
    HOOKS[va]=(w0,'j',sym);HOOKS[va+4]=(w1,None,None)
# Lane setup and per-side native special getters. All sites have straight-line
# displaced instructions; retail note/key sample consumers keep their map.
for va,w0,w1,sym in ((0x1ea870,0x27bdffa0,0xffbf0050,'lq_setup_hook'),(0x205c10,0x3c02007a,0x8c4232c4,'lq_hran_hook'),(0x205b80,0x3c02007a,0x904232c8,'lq_allscr_hook')):
    assert va not in HOOKS
    HOOKS[va]=(w0,'j',sym);HOOKS[va+4]=(w1,None,None)
HOOKS[0x1e7c28]=(JAL(0x16a3c0),'jal','player_note_timing_hook')
HOOKS[0x1e7998]=(JAL(0x205d40),'jal','lq_note_link_hook')
# Wrap the two FREE single-chart update callers, leaving optional profiler/
# diagnostic entry hooks composable. Native held exit and I/O-draining phase6
# are untouched. RESULT's confirmed panel choice is a separate boundary.
for va,target,sym in ((0x1a16cc,0x1f2170,'qr_play_hook'),
                       (0x226dec,0x1f2170,'qr_play_hook'),
                       (0x1f2b84,0x1f4f20,'qr_decision_hook'),
                       (0x17a7b8,0x21f4c0,'qr_choice_hook'),
                       (0x1a17d4,0x1f57a0,'qr_continue_hook')):
    assert va not in HOOKS
    HOOKS[va]=(JAL(target),'jal',sym)
HOOKS[0x2059d0]=(0x00041080,'j','lift_hidden_hook')
HOOKS[0x2059d4]=(0x00441821,None,None)
#1FF2E8 positions the red fake-key flashes below the judge line. Keep native.
# It is not a main explosion/spark producer and must NOT follow LIFT.
HOOKS[0x1fc464]=(JAL(0x1e1730),'jal','fhs_gn_text_hook')
HOOKS[0x1fc518]=(JAL(0x1e1730),'jal','fhs_label_text_hook')

HOOKS[0x1ffbb0]=(JAL(0x2058b0),'jal','fhs_bar_index_hook')
HOOKS[0x1ffca8]=(JAL(0x2058b0),'jal','fhs_bar_index_hook')

# Loaded chart construction/prime.1e5e80 is PRE-load reset: never consume opening BPM there.
for va,w0,w1,sym in ((0x1e65d0,0x27bdff70,0xffbf0070,'fhs_chart_hook'),(0x1e5b80,0x27bdff90,0xffbf0060,'fhs_prime_hook')):
    assert va not in HOOKS and va+4 not in HOOKS
    HOOKS[va]=(w0,'j',sym);HOOKS[va+4]=(w1,None,None)

# plain words: folder 59 bar graphic = the VERSION bar (sprite 0x1c)
DATA = {0x25f050 + 59 * 4: (0, 0x1c),
        # EMPRESS MEGAMIX song/chart unlocks. FUN_0019cdd0(save record) = "unlocked" bit (byte 3 bit 7), the one
        # test behind song unlocked FUN_001e3920, chart unlocked FUN_001e3b90 and the ANOTHER/BLACK lock layer;
        # -> return 1. Nothing is written to the save.
        0x19cdd0: (0x90840003, 0x03e00008), 0x19cdd4: (0x24030001, 0x24020001),
        # CUSTOMIZE owns a separate six-category block. FUN_00196a90 checks
        # category/item bounds, then reads the owned byte. Return owned after
        # that validation for every native item, including completion bonuses.
        # Keep its prologue/epilogue, selections, grant and NEW-flag paths intact;
        # do not manufacture unlock writes or alter either profile on the card.
        0x196ad4: (0x90420004, 0x24020001),
        # folder menu: room for 88 folders. Scene object (FUN_00153fe0) 0x7cc -> 0x830 bytes, the folder lamp bytes
        # (obj+0x6e8, 44) move to obj+0x7cc, so the folder id list obj+0x6bc can use 0x6bc..0x713 (see LAMP_SITES)
        0x153fe4: (0x240407cc, 0x24040830),
        0x18c9f4: (0x00000000, 0x14200085),                                          # bnez at -> re-open (after reo_hook)
        # FUN_001913b0 (song panel setup): `bltz v1 (state+0x24), x ; nop` -> `addiu at,v1,1 ; beqz at,x`: only -1
        # (no song) gets the folder panel, not ids 128..254 (negative bytes); `li v0,125` runs in the delay slot,
        # which the game's own `== 125` path to x also leaves in v0
        0x19152c: (0x046000a8, 0x24610001), 0x191530: (0x00000000, 0x102000a7),
        # folder menu widget (3 x 0x1a8 bytes, gp-0x78c0/-0x78bc/-0x78b8; FUN_0021a330 (intro draw)..FUN_0021bf60): its id
        # array (+0, 44) and lamp array (+0x2c, 44) move to +0x1a8 / +0x1e8 (64 each), its per-entry int array
        # (+0x8c, 44: bar x positions; past it the scroll state +0x13c..) to +0x228 (64), object 0x328 bytes
        0x191d28: (0x240401a8, 0x24040328), 0x191d34: (0x240401a8, 0x24040328), 0x191d40: (0x240401a8, 0x24040328),
        0x21ba24: (0x240600b0, 0x24060100),                                          # clear the int array
        **{va: (w, w & 0xffff0000 | 0x228) for va, w in ((0x21aeec, 0xac89008c), (0x21aef4, 0x248a008c), (0x21b1dc, 0x8c53008c), (0x21b38c, 0xacaa008c), (0x21b394, 0x24ab008c), (0x21b4b4, 0x8c72008c), (0x21b4c0, 0x2477008c), (0x21b614, 0x8c72008c), (0x21b620, 0x2477008c), (0x21b87c, 0x8c74008c), (0x21ba18, 0x2644008c))},
        0x21b9f4: (0x0240202d, 0x264401a8), 0x21ba04: (0x2406002c, 0x24060080),      # clear ids + lamps
        0x21bf54: (0x2484002c, 0x248401e8),                                          # lamp memcpy dest
        **{va: (w, w | 0x1a8) for va, w in ((0x21abd4, 0x817e0000), (0x21b00c, 0x80560000), (0x21b4ec, 0x82be0000),
                                           (0x21b570, 0x82b50000), (0x21b64c, 0x82be0000), (0x21b6d0, 0x82b50000),
                                           (0x21b888, 0x80520000), (0x21a6f8, 0x80570000))},  # lb id, 0(widget + row)
        **{va: (w, w & 0xffff0000 | 0x1e8) for va, w in ((0x21ac08, 0x8164002c), (0x21b014, 0x8044002c),
                                                        (0x21b4d0, 0x82a4002c), (0x21b630, 0x82a4002c),
                                                        (0x21b88c, 0x8044002c), (0x21b4b8, 0x26a2002c),
                                                        (0x21b618, 0x26a2002c), (0x21a748, 0x8044002c))}}  # lamp loads / ptrs
LAMP_RANGE = (0x187000, 0x197000)                # music-select scene: every byte access at offset 0x6e8


# sprite packets (FUN_00139200): vertex colour `lui $13,0x80 ; ori $13,$13,0x8080` per blend mode -> `lui $13, hi ;
# lw $13, lo($13)` of cur_tint (songdb.c; draw_hook sets it for lane sprites only)
TINT = ((0x1393b0, 0x1393b4), (0x139430, 0x13943c), (0x139480, 0x13948c))


def sign_nops(word):
    """Added song ids 128..254 arrive as negative numbers from the game's byte loads. Next to the song-row
    compares (ceiling.py `slti r, 124/125`, within 12 instructions) the game also rejects negative ids (`bltz r, skip`):
    those bltz -> nop
    (only where the bltz goes to the compare's own skip label, or the compare has none nearby)."""
    out = {}
    tgt = lambda va, w: va + 4 + 4 * struct.unpack('<h', struct.pack('<H', w & 0xffff))[0]
    for va, new in ceiling.SITES.items():
        w = word(va)
        if new not in (0x7c, 0x7d) or w >> 26 not in (0x0a, 0x0b):
            continue
        r, rd, skip = (w >> 21) & 31, (w >> 16) & 31, None
        for k in range(1, 4):
            x = word(va + 4 * k)
            if x >> 26 == 4 and (x >> 21) & 31 == rd and (x >> 16) & 31 == 0:
                skip = tgt(va + 4 * k, x)
        for k in range(-12, 13):
            a = va + 4 * k
            x = word(a)
            if x >> 26 == 1 and (x >> 21) & 31 == r and (x >> 16) & 31 == 0 and (skip is None or tgt(a, x) == skip):
                out[a] = (x, NOP)
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('outdir', nargs='?', default=os.path.join(HERE, '..', '..', 'offdisc', 'launcher'))
    parser.add_argument('--exe', default=os.path.join(HERE, '..', '..', 'work16', 'SLPM_552.21'),
                        help='Clean reference EXE; release builds supply the EXE extracted from their input disc')
    parser.add_argument('--reference-words',help='Source-only CI patch preconditions, not an executable')
    parser.add_argument('--export-reference',help='Maintainer: export checked patch preconditions from own original EXE')
    args = parser.parse_args()
    from reference_words import Reference
    reference=Reference(exe=args.exe,words=args.reference_words)
    out = args.outdir
    os.makedirs(out, exist_ok=True)
    o, elf, lds, binp = (os.path.join(out, n) for n in ('vdisc.o', 'vdisc.elf', 'vdisc.ld', 'vdisc.bin'))
    open(lds, 'w').write('PROVIDE(memcpy = 0x12ad48);\nPHDRS { resident PT_LOAD FLAGS(7); binding PT_LOAD FLAGS(7); preview PT_LOAD FLAGS(7); }\nSECTIONS {\n  . = 0x%x;\n  .blob : { *(.text*) __text_end = .; *(.rodata*) *(.data*) '
                         '*(.got*) *(.sdata*) *(.sbss*) *(.bss*) *(COMMON) } :resident\n'
                         ' .ui_bind spr_buf : AT(LOADADDR(.blob)+SIZEOF(.blob)) { *(.ui_bind.text*) __ui_bind_text_end = .; *(.ui_bind.rodata*) *(.ui_bind.data*) *(.ui_bind.bss*) } :binding\n'
                         ' .ui_preview spr_buf : AT(LOADADDR(.ui_bind)+SIZEOF(.ui_bind)) { *(.ui_preview.text*) __ui_preview_text_end = .; *(.ui_preview.rodata*) *(.ui_preview.data*) *(.ui_preview.bss*) } :preview\n'
                         ' /DISCARD/ : { *(.MIPS.*) *(.reginfo) *(.pdr) '
                         '*(.comment) *(.note*) *(.gnu*) }\n}\n' % RES_LO)
    o2, o3 = os.path.join(out, 'songdb.o'), os.path.join(out, 'songdb_s.o')
    o4 = os.path.join(out, 'save_checkpoint.o')
    defs = ['-DINPUT_LOG'] if os.environ.get('INPUT_LOG')=='1' else []
    if os.environ.get('FILE_IO_LOG')=='1':
        assert defs, 'FILE_IO_LOG requires INPUT_LOG=1'
        defs.append('-DFILE_IO_LOG')
    portable=os.environ.get('PORTABLE_LAUNCHER')=='1'
    host_bench=os.environ.get('HOST_BENCH')=='1'
    assert not portable or not any(os.environ.get(k)=='1' for k in ('HOST_BENCH','USB_BENCH','MMCE_BENCH','USB_NATIVE_PRELOAD')), 'Portable runtime has its own service ownership'
    if portable:defs.append('-DPORTABLE_RUNTIME')
    frame_order=os.environ.get('ASSET_FRAME_ORDER')=='1'
    if frame_order:defs.append('-DASSET_FRAME_ORDER')
    split_texture=os.environ.get('ASSET_SPLIT_TEXTURE')=='1'
    if split_texture:
        assert frame_order, 'Split texture candidate requires paired frame ownership'
        defs.append('-DASSET_SPLIT_TEXTURE')
    prep_watch=os.environ.get('PREP_WATCH')=='1'
    if prep_watch:
        assert (portable or host_bench) and os.environ.get('INPUT_LOG')!='1' and os.environ.get('FILE_IO_LOG')!='1', 'Pre-READY watch uses an explicit portable or host diagnostic without logging buffers'
        assert all(os.environ.get(k)!='1' for k in ('SAVE_PROFILE','LOAD_PROFILE','SELECT_PROFILE','AUDIO_PROBE','EXIT_TRACE','PREP_PROBE','USB_BOOT_TRACE','PREVIEW_TRACE')), 'Use the small pre-READY observer alone'
        defs.append('-DPREP_WATCH')
    gap_trace=os.environ.get('ASSET_GAP_TRACE')=='1'
    if gap_trace:
        assert prep_watch and frame_order, 'Gap capture requires the private frame-order pre-READY runtime'
        defs.append('-DASSET_GAP_TRACE')
    unused_ring=os.environ.get('USB_UNUSED_RING')=='1'
    assert not unused_ring or portable or os.environ.get('USB_BENCH')=='1', 'Unused optical ring optimization is USB-only until qualified'
    assert not (host_bench and any(os.environ.get(k)=='1' for k in ('MMCE_BENCH','USB_BENCH'))), 'Choose host or hardware transport'
    save_probe=os.environ.get('SAVE_PROFILE')=='1'
    if save_probe:
        assert defs and (host_bench or os.environ.get('MMCE_BENCH')=='1'), 'Save profiler requires an explicit diagnostic host or MMCE build'
        assert all(os.environ.get(k)!='1' for k in ('FILE_IO_LOG','LOAD_PROFILE','AUDIO_PROBE','EXIT_TRACE','PREP_PROBE','USB_BOOT_TRACE')), 'Use save profiler separately from other capture buffers'
        defs.append('-DSAVE_PROFILE')
    load_profile=os.environ.get('LOAD_PROFILE')=='1'
    if load_profile:
        assert defs and os.environ.get('MMCE_BENCH')=='1', 'Load profiler requires private MMCE bench'
        assert os.environ.get('EXIT_TRACE')!='1' and os.environ.get('USB_BOOT_TRACE')!='1', 'Do not mix performance capture and crash instrumentation'
        defs.append('-DLOAD_PROFILE')
    select_profile=os.environ.get('SELECT_PROFILE')=='1'
    if select_profile:
        assert load_profile, 'SELECT_PROFILE requires LOAD_PROFILE=1'
        defs.append('-DSELECT_PROFILE')
    audio_probe=os.environ.get('AUDIO_PROBE')=='1'
    if audio_probe:
        assert defs and os.environ.get('MMCE_BENCH')=='1', 'Audio probe requires private diagnostic MMCE build'
        assert not load_profile and os.environ.get('PREP_PROBE')!='1' and os.environ.get('EXIT_TRACE')!='1' and os.environ.get('FILE_IO_LOG')!='1', 'Do not mix latency observation with other profilers'
        defs.append('-DAUDIO_PROBE')
    exit_trace=os.environ.get('EXIT_TRACE')=='1' 
    preview_trace=os.environ.get('PREVIEW_TRACE')=='1'
    if preview_trace:
        assert defs and os.environ.get('MMCE_BENCH')=='1', 'Preview trace requires explicit diagnostic MMCE runtime'
        assert not any([load_profile,audio_probe,exit_trace,save_probe]) and os.environ.get('FILE_IO_LOG')!='1', 'Do not combine preview diagnostics with profilers'
        defs.append('-DPREVIEW_TRACE')
    prep_probe=os.environ.get('PREP_PROBE')=='1'
    if prep_probe:
        assert load_profile, 'PREP_PROBE requires private externally enabled load-profile runtime'
        defs.append('-DPREP_PROBE')
    if exit_trace:
        assert defs and (os.environ.get('MMCE_BENCH')=='1' or os.environ.get('USB_BENCH')=='1'), 'Exit trace requires a separate hardware diagnostic'
        defs.append('-DEXIT_TRACE')
    post_arena_trace=os.environ.get('USB_POST_ARENA_TRACE')=='1'
    boot_trace=os.environ.get('USB_BOOT_TRACE')=='1' or post_arena_trace
    if boot_trace:
        assert (defs or post_arena_trace) and os.environ.get('USB_BENCH')=='1', 'Boot trace requires the separate USB diagnostic'
        defs.append('-DUSB_BOOT_TRACE')
    if post_arena_trace:
        assert os.environ.get('USB_NATIVE_PRELOAD')=='1' and not defs.count('-DINPUT_LOG'), 'Use the quiet paired post-arena USB control'
        defs.append('-DUSB_BOOT_TEXT')
    boot_journal=os.environ.get('USB_BOOT_JOURNAL')=='1'
    if boot_journal:
        assert boot_trace, 'Startup journal requires USB boot trace'
        defs.append('-DUSB_BOOT_JOURNAL')
    usb_hid=os.environ.get("USB_HID")=="1"
    if usb_hid: defs.append("-DUSB_HID")
    # The native preview is also useful on PCSX2's host filesystem. It must
    # not require a hardware USB driver or injected controller ownership.
    play_preview=usb_hid or host_bench
    if host_bench: defs.append('-DPLAY_PREVIEW')
    defs+=['-DSPR_ASYNC','-DPLAY_LIFT']
    holds=os.environ.get('HOLDS')=='1'   # CN/HCN/BSS module (tools/holds), off in release builds
    if holds:defs.append('-DHOLDS')
    probe_id=None
    if os.environ.get('INPUT_LOG')=='1':
        h=hashlib.sha256()
        for name in ('vdisc.c','songdb.c','songdb.S','random_title.h','save_runtime.c','save_text.h','save_compact.c','profile_location.h','save_checkpoint.c','save_checkpoint.h','profile_format.c','profile_format.h','profile_wire.h','profile_schema.c','profile_schema.h','profile_async.c','profile_async.h','profile_transaction.c','profile_transaction.h','profile_mc.c','profile_mc.h','songdb.h','song_window.c','menu_settings.h','mod_menu.c','play_preview.c','play_preview_editor.c','customize_settings.h','custom_art.h','customize_render.c','customize_judgement.c','customize_assets.c','ui_overlay.c','live_controller.c','quick_retry.c','lane_options.c','native_modifiers.c','floating_hispeed.c','lift.c','bpm_notice.c','gauge_shift.c','fast_slow.c','audio_dispatch.c','ranking.c','song_reference.c','song_resources.c','song_resources.h','result_title.c','select_artwork.c','wheel_cache.h','asset_memory.c','eighth_hud.c','eighth_hud_assets.h','input_probe.c','input_probe.h','io_probe.c','io_probe.h','build_vdisc.py'):
            h.update(name.encode()+b'\0'+open(os.path.join(HERE,name),'rb').read())
        h.update(('FILE_IO_LOG='+os.environ.get('FILE_IO_LOG','0')+' USB_BENCH='+os.environ.get('USB_BENCH','0')+' MMCE_BENCH='+os.environ.get('MMCE_BENCH','0')+' PORTABLE_LAUNCHER='+os.environ.get('PORTABLE_LAUNCHER','0')).encode())
        if holds:
            for name in ('holds.h','holds.c','holds_empress.c'):h.update(b'HOLDS/'+name.encode()+b'\0'+open(os.path.join(HERE,'..','holds',name),'rb').read())
        if frame_order:h.update(b'ASSET_FRAME_ORDER=1')
        if split_texture:h.update(b'ASSET_SPLIT_TEXTURE=1')
        if host_bench:h.update(b'HOST_BENCH=1')
        if load_profile:
            for name in ('load_profile.c','load_profile.h'):h.update(name.encode()+b'\0'+open(os.path.join(HERE,name),'rb').read())
        if select_profile:
            for name in ('capture.c','capture.h','config.h','protocol.h','video_marker.h'):
                h.update(name.encode()+b'\0'+open(os.path.join(HERE,'select_profiler',name),'rb').read())
        if preview_trace:h.update(b'PREVIEW_TRACE=1\0'+open(os.path.join(HERE,'preview_trace.c'),'rb').read()+open(os.path.join(HERE,'preview_fault.c'),'rb').read())
        if prep_probe:
            for name in ('prep_probe.c','prep_probe.h'):h.update(name.encode()+b'\0'+open(os.path.join(HERE,name),'rb').read())
        if audio_probe:h.update(b'AUDIO_PROBE=1\0'+open(os.path.join(HERE,'audio_probe.c'),'rb').read())
        if save_probe:
            for name in ('save_probe.c','save_probe.h'):h.update(name.encode()+b'\0'+open(os.path.join(HERE,name),'rb').read())
        if exit_trace:h.update(b'EXIT_TRACE=1\0'+open(os.path.join(HERE,'exit_trace.c'),'rb').read())
        if unused_ring:h.update(b'USB_UNUSED_RING=1\0'+open(os.path.join(HERE,'optical_ring.py'),'rb').read())
        if boot_trace:h.update(b'USB_BOOT_TRACE=1\0'+open(os.path.join(HERE,'boot_trace.c'),'rb').read())
        if post_arena_trace:
            for name in ('boot_text.c','boot_text_font.h'):h.update(name.encode()+b'\0'+open(os.path.join(HERE,name),'rb').read())
        if boot_journal:
            for name in ('boot_journal.c','usb_journal/shared.h','usb_journal/main.c'):h.update(name.encode()+b'\0'+open(os.path.join(HERE,name),'rb').read())
        if usb_hid:
            for name in ('native.c','native.h','protocol.h','bindings.c','bindings.h','hid_report.h','capture.h','profile_store.h'):
                h.update(open(os.path.join(HERE,'usb_hid',name),'rb').read())
        h.update(b'EMPRESS original EXE SHA256\0'+reference.digest)
        probe_id=h.digest()[:16]
        defs.append('-DINPUT_LOG_ID={'+','.join('0x%02x'%b for b in probe_id)+'}')
    run(['clang'] + CFLAGS + defs + ['-fno-stack-protector', '-c', os.path.join(HERE, 'vdisc.c'), '-o', o])
    # Keep the full-library MPEG tail inside the existing reserve as menus grow.
    run(['clang'] + [('-Oz' if f=='-Os' else f) for f in CFLAGS] + defs + ['-fno-stack-protector', '-c', os.path.join(HERE, 'songdb.c'), '-o', o2])
    run(['clang'] + CFLAGS[:5] + defs + ['-c', os.path.join(HERE, 'songdb.S'), '-o', o3])
    run(['clang'] + CFLAGS + ['-fno-stack-protector', '-c', os.path.join(HERE, 'save_checkpoint.c'), '-o', o4])
    profile_objects=[]
    for name in ('profile_format','profile_schema','profile_async','profile_transaction','profile_mc'):
        obj=os.path.join(out,name+'.o');profile_objects.append(obj)
        # The game uses the resumable API; synchronous host/import entry points
        # need not occupy its reserved metadata block.
        run(['clang']+CFLAGS+(['-DSAVE_PROFILE'] if save_probe else [])+['-DMP_ASYNC_ONLY','-fno-stack-protector','-c',os.path.join(HERE,name+'.c'),'-o',obj])
    if save_probe:
        obj=os.path.join(out,'save_probe.o');profile_objects.append(obj)
        run(['clang']+CFLAGS+['-DSAVE_PROFILE','-fno-stack-protector','-c',os.path.join(HERE,'save_probe.c'),'-o',obj])
    run(['ld.lld', '-m', 'elf32ltsmipn32', '-T', lds, '--no-relax', '--no-check-sections', '-o', elf, o3, o, o2, o4]+profile_objects)
    run(['llvm-objcopy', '-O', 'binary', '-j', '.blob', elf, binp])
    syms = {p[2]: int(p[0], 16) for p in (l.split() for l in run(['llvm-nm', elf]).split('\n')) if len(p) == 3}
    sizes = {p[3]: int(p[1],16) for p in (l.split() for l in run(['llvm-nm','-S',elf]).splitlines()) if len(p)==4}
    assert 80 < sizes['menu_control'] <= 1024
    if play_preview:
        import zlib
        raw=bytearray(open(binp,'rb').read())
        for index,kind in enumerate(('bind','preview')):
            if kind=='bind' and not usb_hid:continue
            module=os.path.join(out,kind+'-ui.bin')
            run(['llvm-objcopy','-O','binary','-j','.ui_'+kind,elf,module])
            payload=open(module,'rb').read()
            assert 0<len(payload)<=sizes['spr_buf'],('UI overlay too large',kind,len(payload))
            bad_ui=check(payload[:syms['__ui_'+kind+'_text_end']-syms['spr_buf']],syms['spr_buf'])
            assert not bad_ui,bad_ui
            struct.pack_into('<2I',raw,syms['ui_specs']-RES_LO+index*8,len(payload),zlib.crc32(payload))
        open(binp,'wb').write(raw)
    blob = open(binp, 'rb').read()
    bad = check(blob[:syms['__text_end'] - RES_LO], RES_LO)
    assert not bad, bad
    open(os.path.join(out, 'vdisc.dis'), 'w').write(run(['llvm-objdump', '-d', '--no-show-raw-insn', elf]))
    exe=reference
    word=reference.word
    lines = ['base %x' % RES_LO, 'conf %x' % syms['vd'], 'sdb %x' % syms['sdb'], 'skin %x' % syms['skin'],
             'settings 1 %x %d' % (syms['menu_control'],sizes['menu_control']), 'customize 5 %x %d' % (syms['customize'],sizes['customize']), 'gsm %x' % syms['gauge_shift_enabled'], 'fast_slow %x' % syms['fast_slow_enabled'], 'audio_dispatch 1', 'catalog 4', 'alphabet 1', 'coalesce 1', 'listing_family 1', 'asset_stream 3', 'asset_workspace 1', 'asset_memory %x' % syms['asset_memory'],
             'identity %x' % syms['chart_identity_layout'],
             'save 1 %x %x' % (syms['save_policy'],syms['save_diag']),
             'profile 2 %x' % syms['save_profile_config'],
             'profile_location 1 %x %d' % (syms['save_location'],sizes['save_location'])]
    lines.append('wheel_cache 1 %x 8' % syms['wheel_cache'])
    if play_preview:
        lines.append('preview 2 %x' % syms['preview_assets'])
        # Factory pointer, not a J/JAL instruction. All other hooks are scoped
        # call sites; native ordinary test and SELECT callers remain identical.
        assert word(0x25c314)==0x154930
        lines.append('W 25c314 00154930 %08x' % syms['pve_ctor_hook'])
        for site,body,hook in ((0x18b46c,0x1919a0,'pve_select_init_hook'),
            (0x1f2428,0x1e4d70,'pve_graph_hook'),(0x1f2cd0,0x1e4d70,'pve_graph_hook'),
            (0x1f2464,0x1e4b40,'pve_rival_hook'),(0x1f2d0c,0x1e4b40,'pve_rival_hook'),
            (0x1f2ed8,0x1e4eb0,'pve_graph_save_hook'),(0x1f2f08,0x203150,'pve_cover_save_hook'),
            (0x1f2f10,0x1f3930,'pve_options_save_hook')):
            assert word(site)==0x0c000000|body>>2,(hex(site),hex(word(site)))
            lines.append('J %x %08x %x' % (site,word(site),syms[hook]))
        # Locate the unambiguous native203490 call in chart initialization.
        sites=reference.scan('preview_options_call',lambda:[at for at in range(0x1f5df0,0x1f6200,4)if word(at)==0x0c000000|0x203490>>2])
        assert len(sites)==1,sites
        lines.append('J %x %08x %x' % (sites[0],word(sites[0]),syms['pve_options_hook']))
    if preview_trace:
        lines.append('preview_trace 1 %x' % syms['preview_trace_snapshot'])
        for site,body,hook in [(0x14cce8,0x13a530,'pvt_release_hook'),(0x15183c,0x10ece0,'pvt_vblank_hook'),(0x14cd88,0x14cb20,'pvt_batch_hook'),(0x1f22a8,0x14cf70,'pvt_idle_hook'),(0x1f2380,0x14cf70,'pvt_idle_hook')]:
            assert word(site)==JAL(body),(hex(site),hex(word(site)))
            lines.append('J %x %08x %x' % (site,word(site),syms[hook]))
        for site,orig,hook in [(0x1361e0,0x27bdef50,'pvt_decode_hook'),(0x1361e4,0x0000402d,None),(0x135860,0x8c830000,'pvt_register_hook'),(0x135864,0x8c650008,None),(0x1519d0,JAL(0x136c30),'pvt_submit_hook'),(0x135770,0x8f878440,'pvt_query_hook'),(0x135774,0x3c090035,None),(0x135670,0x27bdffe0,'pvt_sprite_hook'),(0x135674,0x3c0b0035,None)]:
            assert word(site)==orig
            lines.append('J %x %08x %x' % (site,orig,syms[hook])) if site==0x1519d0 else lines.append('W %x %08x %08x' % (site,orig,J(syms[hook])if hook else 0))
    if host_bench:lines.append('host 1')
    if probe_id is not None:
        lines.append('input 1 %x %x %s' % (syms['input_probe'],syms['input_probe_build_id'],probe_id.hex()))
        lines.append('autoplay 1 %x' % (syms['input_probe']+100))
        if os.environ.get('FILE_IO_LOG')=='1':lines.append('io 1 %x 2720' % syms['io_capture'])
        for va,orig,kind,sym in (
            (0x130730,0x27bdffc0,2,'input_frame_hook'),(0x130734,0xffbf0030,0,None),
            (0x11b9d8,0x0080382d,2,'input_read_hook'),(0x11b9dc,0x24030070,0,None),
            (0x11ba28,0x8fa60060,2,'input_snapshot_hook'),(0x11ba2c,0x0200202d,0,None),
            (0x1e5f84,0x0c07b598,3,'input_start_hook')):
            assert word(va)==orig,(hex(va),hex(word(va)),hex(orig))
            if audio_probe and va in (0x11b9d8,0x11b9dc,0x11ba28,0x11ba2c):continue
            target=(kind<<26|syms[sym]>>2) if sym else 0
            lines.append('P %x %08x %08x' % (va,orig,target))
    if save_probe:
        lines.append('saveprobe 1 %x 112' % syms['save_probe'])
        for va,orig,kind,sym in ((0x1f5df0,0x27bdffb0,2,'save_probe_chart_hook'),
                                (0x1f5df4,0xffbf0040,0,None),
                                (0x18b1e0,0x27bdffb0,2,'save_probe_select_hook'),
                                (0x18b1e4,0x3c030026,0,None),
                                (0x151a00,JAL(0x14e250),3,'save_probe_tail_hook')):
            assert word(va)==orig
            lines.append('W %x %08x %08x' % (va,orig,(kind<<26|syms[sym]>>2) if sym else 0))
        for va,sym in ((0x1d5f40,'save_load_ui_hook'),(0x1d64e0,'save_load_auto_hook')):
            assert (word(va),word(va+4))==(0x27bdffd0,0x3c020079)
            lines+=['W %x %08x %08x' % (va,word(va),2<<26|syms[sym]>>2),'W %x %08x 00000000' % (va+4,word(va+4))]
    assert (word(0x1134d0),word(0x1134d4))==(0x27bdffd0,0xffb10010), 'Unexpected retail FILEIO handle-count entry'
    # Both runtime variants fix ONLY the explicit zero-ACK rejection exit.
    # The cleanup gets native's own s3 descriptor and preserves -11; normal
    # completion, negative completion and submission-error exits stay native.
    for va,expected in ((0x114170,0x27bdfeb0),(0x114208,JAL(0x113550)),
                        (0x1142dc,0xae350414),(0x114328,0x04410007),
                        (0x114354,0x16200005),(0x114364,0x1000001a),
                        (0x114368,0x2402fff5),(0x114390,0xae600004),
                        (0x1143c0,0xae630004)):
        assert word(va)==expected, ('Unexpected native FILEIO open layout',hex(va),hex(word(va)))
    lines.append('W %x %08x %08x' % (0x114364,word(0x114364),2<<26|syms['vd_fio_reject_hook']>>2))
    if audio_probe:
        lines.append('audio_probe 1 %x' % syms['ap_data'])
        # This pass keeps capture/export but no on-screen diagnostic panel.
        for va,target,symbol in (
            (0x13f5f8,0x10eca0,'ap_wait_hook'),
            (0x13f6b8,0x13f870,'ap_service_hook'),
            (0x141eb8,0x1411b0,'ap_sample_hook'),
            (0x13f898,0x1414b0,'ap_batch_hook'),
            (0x13f8a0,0x146310,'ap_rpc_hook')):
            assert word(va)==JAL(target),(hex(va),hex(word(va)))
            lines.append('P %x %08x %08x' % (va,word(va),JAL(syms[symbol])))
    # Always active in quiet and diagnostic builds; unchanged delay slots.
    assert word(0x1519e4)==0x8f848620
    for va,target,symbol in [(0x1519e0,0x10eca0,'ad_frame_wait_hook'),(0x1519f0,0x13f5b0,'ad_wake_hook')]+[(a,0x140260,'ad_request_hook') for a in (0x1e8944,0x1e9028,0x1e9118,0x1e91b8)]:
        assert word(va)==JAL(target),(hex(va),hex(word(va)))
        lines.append('W %x %08x %08x' % (va,word(va),JAL(syms[symbol])))
    # Timed key/scratch events feed the same ring writer once. Other events
    # retain their native writer through its displaced prologue.
    for va,sym in [(0x1e88d4,'fs_key_hook'),(0x1e8fb8,'fs_scratch_hook')]:
        assert word(va)==JAL(0x1ed5a0)
        lines.append('W %x %08x %08x' % (va,word(va),JAL(syms[sym])))
    for va,w0,w1,sym in [(0x1ed5a0,0x8f8688f0,0x3c07007a,'fs_ring_hook'),(0x1fe450,0x27bdffc0,0xffbf0030,'fs_play_hook'),(0x17c570,0x27bdff50,0xffbf0090,'fs_result_hook'),(0x13bbf0,0x27bdffc0,0xffbf0030,'fs_input_hook')]:
        assert (word(va),word(va+4))==(w0,w1)
        lines += ['W %x %08x %08x' % (va,w0,2<<26|syms[sym]>>2),'W %x %08x 00000000' % (va+4,w1)]
    if holds:
        # per-tick hold rules after the native note update; delay slot (ld a0 tick) unchanged
        assert word(0x1e5cb8)==JAL(0x1e6bb0),(hex(word(0x1e5cb8)),)
        lines.append('W %x %08x %08x' % (0x1e5cb8,word(0x1e5cb8),JAL(syms['holds_frame_entry'])))
    for va,sym in [(0x1fbd24,'fs_ghost_hook'),(0x1f6900,'fs_counts_single_hook'),(0x1f6c34,'fs_counts_battle_hook')]:
        assert word(va)==JAL(0x1e1730)
        lines.append('W %x %08x %08x' % (va,word(va),JAL(syms[sym])))
    if usb_hid:
        assert tuple(word(0x10f020+i*4) for i in range(4))==(0x24030077,0xc,0x03e00008,0), 'Unexpected native SIF DMA syscall wrapper'
        lines.append('usb_hid 2 %x %x' % (syms['usb_input_config'],syms['usb_input_frames']))
        for va,target,symbol in ((0x13bc14,0x13ba60,'usb_setup_hook'),(0x13b764,0x11b9d8,'usb_read_hook'),(0x13bc24,0x13b620,'usb_type_hook')):
            assert word(va)==JAL(target)
            lines.append('W %x %08x %08x' % (va,word(va),JAL(syms[symbol])))
    words = dict(WORDS)
    if split_texture:
        assert word(0x13693c)==JAL(0x10eee0)
        words[0x13693c]=(JAL(0x10eee0),JAL(syms['asset_texture_flush_hook']))
        lines.append('asset_split_texture 1')
    if frame_order:
        lines.append('asset_frame_order 1')
        assert word(0x131c00)==JAL(0x13a4b0)
        words[0x131c00]=(JAL(0x13a4b0),JAL(syms['asset_movie_frame_free_hook']))
        if not prep_watch:
            assert word(0x132304)==JAL(0x13a4c0)
            words[0x132304]=(JAL(0x13a4c0),JAL(syms['asset_movie_frame_alloc_hook']))
    assert not (os.environ.get('USB_BENCH')=='1' and os.environ.get('MMCE_BENCH')=='1'), 'Choose one hardware backend'
    if portable:
        lines.append('portable 1')
        words.update(MMCE_WORDS)
        from optical_ring import patches as optical_ring_patches
        words.update(optical_ring_patches(word,JALS))
        lines.append('portable_stream_ring 1 14d300 28010')
        for row in validate_module_loads(exe,words,skip_sio2=True).values():
            words[row['call']]=(JAL(0x117fe8),0x24020001)
        validate_module_loads(exe,words,preloaded=True)
    if os.environ.get('MMCE_BENCH')=='1':
        lines.append('mmce 1')
        words.update(MMCE_WORDS)
        validate_mmce_modules(exe, words)
    lines.append('sprite_async 1')
    lines.append('path_prefix 1 %x %u' % (syms['vd_path_prefix'],sizes['vd_path_prefix']))
    if load_profile:
        lines.append('load_profile 1 %x %u' % (syms['load_profile'],16))
        if select_profile:
            lines.append('select_profile 2 %x %u' % (syms['select_profile'],sizes['select_profile']))

        for va,orig,kind,sym in (
            (0x18b1e0,0x27bdffb0,2,'lp_select_ctor_hook'),(0x18b1e4,0x3c030026,0,None),
            (0x1f5df0,0x27bdffb0,2,'lp_chart_hook'),(0x1f5df4,0xffbf0040,0,None),
            (0x1f2170,0x27bdffb0,2,'lp_play_hook'),(0x1f2174,0xffbf0040,0,None),
            (0x17bd30,0x27bdffa0,2,'lp_result_hook'),(0x17bd34,0x2402ffff,0,None),
            (0x179fe0,0x27bdffe0,2,'lp_result_draw_hook'),(0x179fe4,0xffbf0010,0,None),
            (0x1519c0,JAL(0x14e670),3,'lp_update_hook'),
            (0x1519c8,JAL(0x14e280),3,'lp_draw_hook'),
            (0x1519d0,JAL(0x136c30),3,'lp_submit_hook'),
            (0x1519e8,JAL(0x139be0),3,'lp_flip_hook'),
            (0x151a00,JAL(0x14e250),3,'lp_tail_hook')):
            assert word(va)==orig,(hex(va),hex(word(va)),hex(orig))
            words[va]=(orig,kind<<26|syms[sym]>>2 if sym else 0)
    if exit_trace:
        lines.append('exit_trace 1 %x %x' % (syms['exit_trace_armed'],syms['exit_trace_stage']))
        for va,orig,kind,sym in (
            (0x1f2170,0x27bdffb0,2,'exit_play_hook'),(0x1f2174,0xffbf0040,0,None),
            (0x136c30,0x27bdfff0,2,'exit_render_hook'),(0x136c34,0x3c050039,0,None),
            (0x17bd30,0x27bdffa0,2,'exit_result_create_hook'),(0x17bd34,0x2402ffff,0,None),
            (0x179fe0,0x27bdffe0,2,'exit_result_draw_hook'),(0x179fe4,0xffbf0010,0,None),
            (0x17c570,0x27bdff50,2,'exit_result_side_hook'),(0x17c574,0xffbf0090,0,None),
            (0x1a180c,JAL(0x13db90),3,'exit_audio_stop_hook'),
            (0x1a1814,JAL(0x1357e0),3,'exit_unload_hook'),
            (0x1519d8,JAL(0x131aa0),3,'exit_movie_tick_hook'),
            (0x1519e8,JAL(0x139be0),3,'exit_flip_hook'),
            (0x151a00,JAL(0x14e250),3,'exit_frame_tail_hook'),
            (0x1f2e90,JAL(0x13a510),3,'exit_cleanup_hook'),
            (0x1f2ee0,JAL(0x1e48f0),3,'exit_score_finish_hook'),
            (0x1f2f10,JAL(0x1f3930),3,'exit_preferences_hook'),
            (0x1f2f18,JAL(0x1f9db0),3,'exit_play_free_hook'),
            (0x1a0c90,JAL(0x1a06e0),3,'exit_first_lamp_hook'),
            (0x1a0d74,JAL(0x19ce10),3,'exit_play_count_hook'),
            (0x1a0d7c,JAL(0x1a0420),3,'exit_lamp_commit_hook'),
            (0x1a0d84,JAL(0x19fea0),3,'exit_score_commit_hook'),
            (0x1a0d8c,JAL(0x19fc00),3,'exit_unlock_commit_hook'),
            (0x1a004c,JAL(0x1e1a60),3,'exit_score_stats_hook'),
            (0x1a0334,JAL(0x22ccc0),3,'exit_score_rank_hook'),
            (0x1a03c4,JAL(0x199910),3,'exit_score_history_hook'),

            ):
            assert word(va)==orig,(hex(va),hex(word(va)),hex(orig))
            words[va]=(orig,kind<<26|syms[sym]>>2 if sym else 0)
    if os.environ.get('USB_BENCH')=='1':
        lines.append('usb 1')
        words.update(USB_WORDS)
        if unused_ring:
            from optical_ring import patches as optical_ring_patches
            words.update(optical_ring_patches(word,JALS))
            lines.append('usb_stream_ring 1 14d300 28010')
        # Explicit diagnostic pair: the launcher loads these exact original
        # files, once, before ExecPS2 and refuses handoff on any failure.
        # Ordinary USB and MMCE builds keep their existing startup policy.
        preloaded=os.environ.get('USB_NATIVE_PRELOAD')=='1'
        if preloaded:
            assert not boot_trace or post_arena_trace, 'Keep module-preload isolation separate from early GS probes'
            for row in validate_module_loads(exe,words).values():
                words[row['call']]=(JAL(0x117fe8),0x24020001)
            lines.append('usb_native_preload 1')
        validate_module_loads(exe,words,preloaded=preloaded)
    elif os.environ.get('USB_NATIVE_PRELOAD')=='1':
        raise AssertionError('Native module preloading is a private USB diagnostic only')
    if boot_trace:
        lines.append('boot_trace 1 %x' % syms['boot_trace_state'])
        if post_arena_trace:
            lines.append('boot_loop 1 %x 16' % syms['boot_loop_state'])
            lines.append('boot_detail 1 %x 136 %x 12' % (syms['boot_detail'],syms['boot_workers']))
            lines.append('boot_sound 2 %x 240 %x 12 %x 16' % (syms['boot_sound'],syms['boot_sound_cursor'],syms['boot_rpc']))
            lines.append('boot_alloc 1 %x 8' % syms['boot_alloc'])
            lines.append('iop_budget 1 %x 136' % syms['boot_iop_budget'])
        if boot_journal:lines.append('boot_journal 4 %x %x %x' % (syms['boot_journal_config'],syms['boot_journal_publish_hook'],syms['boot_journal_diag']))
        for name,site,target in (
            ('kernel',0x1001e8,0x119158),
            ('libc_semas',0x119160,0x118f08),
            ('libc_kernel',0x119168,0x119048),
            ('kernel_timer_patch',0x119170,0x119638),
            ('kernel_timer',0x119178,0x1197b0),
            ('kernel_alarm',0x119180,0x1199f0),
            ('kernel_threads',0x119188,0x10fa38),
            ('kernel_osd_patch',0x119190,0x119280),
            ('kernel_exec_patch',0x119198,0x118778),
            ('constructors',0x151888,0x14c8a0),
            ('arena',0x151894,0x1530c0),
            ('globals',0x1518ac,0x14c3a0),
            ('audio_reset',0x12f798,0x13a680),
            ('rpc_first',0x12f7a0,0x1123a8),
            ('cd_first',0x12f7a8,0x11d690),
            ('mode_first',0x12f7b0,0x11e0f8),
            ('rpc_second',0x12f7f8,0x1123a8),
            ('loadfile',0x12f800,0x117778),
            ('fileio',0x12f808,0x113f08),
            ('cd_second',0x12f810,0x11d690),
            ('mode_second',0x12f818,0x11e0f8),
            ('heap',0x12f820,0x117098),
            ('libsd',0x12f834,0x117fe8),
            ('sound',0x12f854,0x117fe8),
            ('sio2',0x12f874,0x117fe8),
            ('mcman',0x12f894,0x117fe8),
            ('mcserv',0x12f8b4,0x117fe8),
            ('padman',0x12f8d4,0x117fe8),
            ('cdvdstm',0x12f8f4,0x117fe8),
            ('filequeue',0x12f904,0x14d260),
            ('audio',0x12f90c,0x13e480),
            ('graphics',0x12f914,0x13a040),
            ('pad',0x12f91c,0x13bd00),
            *((('init',0x151988,0x151880),('loop_pad',0x151990,0x13bf70),
               ('loop_wait',0x1519a0,0x10eca0),('update',0x1519c0,0x14e670),
               ('submit',0x1519d0,0x136c30),('flip',0x1519e8,0x139be0),
               ('render_reset',0x1519a8,0x139a20),('system_tick',0x1519b0,0x14c160),
               ('service',0x1519b8,0x130730),('draw',0x1519c8,0x14e280),
               ('movie',0x1519d8,0x131aa0),('timer',0x1519f8,0x13bf40),
               ('tail',0x151a00,0x14e250),('upload',0x13f600,0x145910),
               ('sound_status',0x13f6b8,0x13f870),('asset_release',0x14cce8,0x13a530),
               ('bank_submit',0x13e8a8,0x13fd20),
               ('fade_bank',0x13f630,0x141a20),
               ('fade_stream',0x13f670,0x141a10),
               ('stop_stream',0x13f678,0x1439a0),
               ('transfer_status',0x13f878,0x144fa0),
               ('voice_residency',0x13f880,0x141d90),
               ('voice_tick',0x13f888,0x141ba0),
               ('stream_tick',0x13f890,0x143870),
               ('command_build',0x13f898,0x1414b0),
               ('command_send',0x13f8a0,0x146310),
               ('transfer_publish',0x13f8d8,0x1450a0),
               ('bank_poll_first',0x13e8b0,0x13fd10),
               ('bank_poll_repeat',0x13e8d0,0x13fd10),
               ('audio_init',0x13e490,0x13f900),
               ('alloc_32k',0x13fa70,0x117120),
               ('alloc_128k',0x13fa80,0x117120),
               ('alloc_760k',0x13fa9c,0x117120),
               ('status_bank',0x145030,0x1470a0),
               ('status_stream',0x145064,0x1470a0),
               ('bank_finish',0x13e8e0,0x13f310),
               ('rpc_before',0x14713c,0x112f30),
               ('rpc_after',0x1471f8,0x112f30),
               ('queue_first',0x13f328,0x145630),
               ('queue_repeat',0x13f340,0x145630),
               ('upload_first',0x13f360,0x145c00),
               ('upload_repeat',0x13f378,0x145c00),
               ('stream_first',0x13f398,0x13fb80),
               ('stream_repeat',0x13f3b8,0x13fb80),
               ('stream_last_first',0x13f3d0,0x13fb80),
               ('stream_last_repeat',0x13f3fc,0x13fb80)) if post_arena_trace else ()),
        ):
            if post_arena_trace and name not in ('arena','globals','audio_reset','rpc_first','cd_first','mode_first','rpc_second','loadfile','fileio','cd_second','mode_second','heap','filequeue','audio','graphics','pad','init','loop_pad','loop_wait','update','submit','flip','render_reset','system_tick','service','draw','movie','timer','tail','upload','sound_status','asset_release','bank_submit','fade_bank','fade_stream','stop_stream','transfer_status','voice_residency','voice_tick','stream_tick','command_build','command_send','transfer_publish','bank_poll_first','bank_poll_repeat','audio_init','alloc_32k','alloc_128k','alloc_760k','status_bank','status_stream','bank_finish','rpc_before','rpc_after','queue_first','queue_repeat','upload_first','upload_repeat','stream_first','stream_repeat','stream_last_first','stream_last_repeat'):
                continue
            assert site not in words, hex(site)
            assert word(site)==JAL(target), hex(site)
            words[site]=(JAL(target),JAL(syms['boot_'+name+'_hook']))
    words.update(DATA)
    for va, (o_, kind, sym) in HOOKS.items():
        words[va] = (o_, NOP if kind is None else (2 if kind == 'j' else 3) << 26 | (syms[sym] >> 2) & 0x3ffffff)
    lamp_sites=reference.scan('lamp_sites',lambda:[[va,word(va)]for va in range(*LAMP_RANGE,4)if word(va)&0xffff==0x6e8 and word(va)>>26 in (0x20,0x24,0x28,0x09)])
    for va,w in lamp_sites:words[va]=(w,w&0xffff0000|0x7cc)
    t = syms['cur_tint']
    for lui, ori in TINT:
        words[lui] = (0x3c0d0080, 0x3c0d0000 | ((t + 0x8000) >> 16) & 0xffff)
        words[ori] = (0x35ad8080, 0x8dad0000 | t & 0xffff)
    for va,o,n in reference.scan('sign_nops',lambda:[[va,*pair]for va,pair in sign_nops(word).items()]):
        v=(o,n)               # ids 128..254 = negative bytes: no `id >= 0` checks
        words.setdefault(va, v)                          # (hooks on a site win)
    for va, (o_, n_) in ceiling.patches(word).items():          # wheel special rows 99-101 -> 124-126
        assert va not in words, hex(va)
        words[va] = (o_, n_)
    if prep_probe:
        lines.append('prep_probe 1 %x' % syms['prep_probe_snapshot'])
        for va,target,sym in ((0x13a44c,0x100320,'prep_workspace_allocate_hook'),(0x1322e4,0x13a4c0,'prep_movie_work_allocate_hook'),(0x132304,0x13a4c0,'prep_movie_frame_allocate_hook')):
            assert word(va)==JAL(target)
            words[va]=(JAL(target),JAL(syms[sym]))
    if prep_watch:
        lines.append('prep_watch 1 %x %x' % (syms['prep_watch_config'],syms['prep_watch_state']))
        if gap_trace:lines.append('asset_gap_trace 1 %x %d' % (syms['asset_gaps'],sizes['asset_gaps']))
        for va,orig,new in (
            (0x1f5df0,0x27bdffb0,2<<26|syms['pw_chart_hook']>>2),
            (0x1f5df4,0xffbf0040,0),
            (0x1519d0,JAL(0x136c30),JAL(syms['pw_submit_hook'])),
            (0x13a400,JAL(0x100320),JAL(syms['pw_back_hook'])),
            (0x13a44c,JAL(0x100320),JAL(syms['pw_back_hook'])),
            (0x132304,JAL(0x13a4c0),JAL(syms['pw_frame_hook']))):
            assert va not in words and va not in JALS,hex(va)
            assert word(va)==orig
            words[va]=(orig,new)
    for va, (o_, n_) in sorted(words.items()):
        assert word(va) == o_, '%x is %08x, expected %08x' % (va, word(va), o_)
        lines.append('W %x %08x %08x' % (va, o_, n_))
    for va, (target, sym) in sorted(JALS.items()):
        assert word(va) == 0x0c000000 | target >> 2, hex(va)
        lines.append('J %x %08x %x' % (va, word(va), syms[sym]))
    for lui, addiu, s_va in MODULES:
        assert word(lui) >> 16 == 0x3c04 and word(lui) & 0xffff == (s_va + 0x8000) >> 16, hex(lui)
        assert word(addiu) >> 16 == 0x2484 and word(addiu) & 0xffff == s_va & 0xffff, hex(addiu)
        o_ = s_va - 0x100000 + 0x80
        name=reference.cstring(s_va)
        rel = GAME_DIR + name.split(':', 1)[1].lstrip('\\').split(';')[0].replace('\\', '/')
        # Portable SDK preloads every required original module and skips all
        # native loads/reset. Do not retain dead absolute module paths: long
        # data roots must not consume the native MPEG workspace margin.
        if not portable:lines.append('S %x %x %s' % (lui, addiu, rel))
    owned = {}
    for line in lines:
        p=line.split()
        if p[0] in ('W','P','J'):
            at=int(p[1],16)
            assert at not in owned, ('Competing native hook owners',hex(at),owned.get(at),line)
            owned[at]=line
    if args.export_reference:reference.export(args.export_reference)
    open(os.path.join(out, 'vdisc.txt'), 'w').write('\n'.join(lines) + '\n')
    print('runtime %d bytes at %x (block %d KB), conf %x, sdb %x, %d jal sites, %d words'
          % (len(blob), RES_LO, (RES_HI - RES_LO) >> 10, syms['vd'], syms['sdb'], len(JALS), len(words)))


if __name__ == '__main__':
    main()
