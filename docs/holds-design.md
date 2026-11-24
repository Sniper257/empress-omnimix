# CN / HCN / BSS / HBSS for EMPRESS MEGAMIX (in progress)

EMPRESS has no hold notes: charge notes and backspin scratches arrived in SIRIUS.
The `tools/cn` prototype is not part of the current runtime: its C/assembly
sources are not public, and two of its hook sites are now used by FAST/SLOW
(`0x1ed5a0`, `fs_ring_hook`) and the input hook (`0x1e5f84`). This is a new
module built in three layers.

| Layer | Files | State |
| --- | --- | --- |
| Rules: when a hold is held, released, judged, ticked | `tools/holds/holds.c`, `holds.h`, `test_holds.c` | done; host tested, builds freestanding for the EE (n32, no libc, ~2.5 KB) |
| Data: which notes are holds | `tools/release/holds_format.py` (`holds.bin` per chart) | done |
| Engine: hooks into SLPM_552.21 | `tools/holds/holds_empress.c`, `test_holds_empress.c` | in the runtime with `HOLDS=1` (off by default); host tested, not yet run in PCSX2 |

## Rules (holds.c)

- **CN**: the head is judged by the game as an ordinary note. Releasing inside
  the tail window is a PGREAT: one window, wider than a normal PGREAT
  (`tail_graded = 1` grades releases instead). Releasing earlier is a POOR.
  Still holding after the window is a POOR, per the project owner
  (`overhold_judge`; the SP攻略 wiki lists BAD for current arcade versions, so
  that is a setting). A missed head makes the tail a POOR at the end.
- **BSS**: keep the turntable moving in the head's direction (standing still
  is allowed for `spin_grace`). Spinning back, or stopping, inside the tail
  window releases it like a CN.
- **HCN / HBSS** (from copula; there were none in SIRIUS charts, but omnimix
  and BMS charts can have them):
  - While held: `HE_TICK_GAIN` every 16th note, the same recovery as a hit
    note, with no combo.
  - Letting go, or missing the head: a real POOR and an empty POOR at once
    (`HE_BREAK` + `HE_TICK_LOSS`; -8% on NORMAL), then an empty POOR
    (`HE_TICK_LOSS`, -2% on NORMAL) every 16th counted from that moment.
  - The lane can be grabbed again until the end.
  - The end never adds a POOR or a gauge loss. Once broken, the tail scores
    nothing (`J_NONE`), even after re-grabbing.
  - The 16th length is stored per hold (`tick64`, from the BPM at the head)
    in 1/64 ticks: 6.16 frames at BPM 146.
- A release or re-grab on the same tick as a 16th boundary decides that tick.

Sources (Japanese community measurements and wikis):
- beatmania IIDX SP攻略 @ wiki, ゲーム仕様解説 (CN end judgements, HCN
  16th-note empty POORs, gauge damage table):
  https://w.atwiki.jp/bemani2sp/pages/4662.html
- まったり観戦・予想・回顧, HCNのゲージ減少率について その2 (frame-by-frame
  measurement of release, head miss, tick interval and recovery on NORMAL):
  https://leisurely1.blog.jp/archives/10099011.html
- beatmania IIDX 攻略wiki, ゲームシステム (combo while holding an HCN):
  https://wikiwiki.jp/iidx/
- e-amusement, beatmania IIDX 26 Rootage HELL CHARGE how-to (official):
  https://p.eagate.573.jp/game/2dx/26/howto/play/hell_charge.html

Still to check against play: the width of the tail window, and the high-BPM
case. The wiki says the 16th interval behaves differently there and needs
testing.

## Data (holds.bin)

`MMHL`, version, count, then the 12-byte `hold_def` records sorted by start
(start, end, lane, kind, tick64).
Heads stay ordinary notes in the native chart (`strip_lengths`). Score and gauge
totals count each hold twice (`note_total`). Hell variants are chosen for the
whole chart (`hell=True`; BMS `#LNMODE 3`).

## Engine layer: what has to be found in SLPM_552.21

Run on the retail executable (`pip install capstone`):

```sh
python3 tools/holds/disasm_sites.py MEGAMIX/EMPRESS/SLPM_552.21 -o holds-sites.txt
```

It confirms the executable and the prototype's original words. It then dumps
the four hook sites, the functions they call (with all their call sites), and
the runtime hooks that already sit nearby. From that we need:

1. Where the play loop's chart time and per-lane key/turntable state live (hook_frame).
2. How the spawner creates a note object, to draw the hold body (hook_spawn).
3. The judge routine's inputs and outputs, to report heads and suppress tails (hook_bss).
4. Play start, to call `holds_reset` with this chart's `holds.bin`, chained
   with `input_start_hook` (hook_reset).
5. The combo, gauge and judgement-counter routines the events are applied with.

## Engine findings, round 1 (from a MEGAMIX build of retail SLPM_552.21)

MEGAMIX builds make 25 byte edits to the executable (rating changes, around
`0x23a0xx` and `0x2789xx`). They are nowhere near the play code, and all six
original words at the prototype's hook sites match.

- **Time**: the play loop (`0x1e5c50`) steps a processed-tick counter
  (`gp-0x7780`, u64) up to the target tick (`gp-0x7788`) and calls
  `0x1e6bb0(tick)` once per tick. Note times are s16 at `note+0x18`, which
  matches the `0x7fff` chart terminator and the 16683 µs header clock: one tick
  is one 60 Hz frame. Arcade/BMS milliseconds convert as `ms * 1000 / 16683`,
  and holds.bin uses the same ticks.
- **Note object**: `+0x00` chart event, `+0x04` next, `+0x08` keysound slot,
  `+0x09` lane (7 = turntable; `0x40` once consumed), `+0x0a` side,
  `+0x0d` bit 0 = entered judge range (21 ticks ahead), `+0x18` time,
  `+0x1e` flags. Notes are freed 60 ticks after their time. `0x1ebec0` gives
  the active list head; `0x1ebf20`/`0x1ebf00` unlink and free.
- **Per-tick lane table** `0x79dae0`: per side, 8 lanes of (u32 note,
  u32 distance), reset to (0, 0x400) every tick, filled by `0x1e6f50`.
- **Turntable judge** `0x1e8d50` (both sides; called at `0x1e89c4`): input
  `0x13acd0(side)`, nonzero = moving, bit `0x80` = direction. Spin state
  per side at `0x79e2f0` (u8 direction 1/2, s16 distance, s16 tick of the
  last spin). A direction flip, or a spin after 4 still ticks, starts a new
  judgement. During a BSS body the hook must not let this judge treat each
  spin as a new note. `gp-0x77b4` = auto-scratch side bits.
- **Windows** `0x79d960`: an outer window, then five thresholds giving
  codes 4–8 (9–11 are special cases).
- **Judgement log** `0x1ed5a0(code, side<<15 | lane)`: a 128-entry ring at
  `0x79e300`. Every key and turntable judgement goes through it, as do codes
  `0x10`–`0x14` from the per-tick code. FAST/SLOW already hooks it. If the
  ring's reader applies score, combo, gauge and the judgement sprite, the hold
  module can apply tails and HCN ticks by logging into it (round 2 checks this).
- **Keysounds**: `0x140260(slot | 0x1000000 or 0x2000000 side | 0x20000000)`.
- The prototype's `hook_spawn` site (`0x1e7d14`) is on the auto-scratch path
  only. Drawing hold bodies needs the general spawner (round 2).

Round 2 (`disasm_sites.py` without `--sites-only`) dumps these functions in
full. It also lists every instruction that uses the `$gp` variables above, and
every reference to the ring and tables.

## Engine findings, round 2

- **Spawn**: `0x1e7720` makes a note object for each chart event. Notes the
  player must judge get `+0x0b = 1` (at `0x1e7fac`) and continue to
  `0x1e7d14`, so every spawned note passes the prototype's `hook_spawn`
  site. It is the one place to mark hold heads. Lane comes from a per-side
  table at `0x79dad0` (the lane-shuffle options rewrite `+0x09` right after).
- **Key judge** `0x1e85d0`: presses are a bitmask (P1 bits 0–6, P2 bits 7–13).
  It comes from `0x130950()`, or from `gp-0x7770` (notes at this tick) when
  autoplay bits `gp-0x77b0` are set. That is a press edge, so CN releases
  need a held-key reading (round 3). Each lane takes its nearest note from
  `0x79dae0` and compares the distance with the key windows at `0x79d940`
  (outer, then 5 thresholds):
  - 4–8 = PGREAT, GREAT, GOOD, BAD, POOR
  - 9 = POOR that still uses up the note
  - `0xb` = no note in range

  Notes with `+0x0b == 0` are never judged, so a hold module can take a
  note out of native judging by clearing that byte.
- **Misses**: `0x1e6f50` logs `0xe`/`0xf` (`0xf` when `+0x0c` is clear) for
  a note that leaves its window with `+0x0b` still set.
- **One ring for everything**: key, turntable and miss judgements, plus the
  per-tick codes `0x10`–`0x14`, are all written with `0x1ed5a0`. Its read
  index `gp-0x7714` is only used inside the block that starts at `0x1ec860`.
  That block is the reader that should apply score, combo and gauge
  (round 3 dumps it as a raw range).
- Round 2's "function" attribution for frameless leaf functions was wrong (it
  walked back to an older prologue). `function_start` now also stops at the
  previous `jr $ra`.

## Arcade constants

`tools/holds/arcade_scan.py bm2dx.dll` looks for the values the rules still
guess: the CN/BSS release window, the BSS stop grace, the HCN tick spacing
and any high-BPM limit. It handles 32-bit (SIRIUS era) and 64-bit (recent)
DLLs. It lists judgement, hold and gauge strings and window-shaped tables
(rising ints in frames or ms, floats like 16.67/33.33), each with short
disassembly of the code that uses it.

SIRIUS's DLL is the era-correct source for CN and BSS. HCN exists only in
copula (23) and later, so its values have to come from a newer DLL.

### Findings: Sparkle Shower bm2dx.dll (64-bit, built 2025-08-18)

- The DLL isn't packed: the code section is readable (`.text` 12.6 MB) and
  disassembles cleanly.
- The standard windows are 60 Hz frames. The only symmetric window table,
  at RVA `0xcf29e0`, is -15, -7, -2, -1, 0, 1, 2, 7, 15 (BAD, GOOD, GREAT,
  PGREAT each side): ±16.67/33.33/116.67/250 ms. The function at
  `0x18082e280` uses it to place the timing display.
- Judging doesn't read windows from a fixed table. Arcade `.1` charts carry
  them as event type 8 (about six per SIRIUS chart), so the converter can
  read each chart's own windows. Mapping them onto EMPRESS's tables
  (`0x79d940` keys, `0x79d960` turntable) is part of chart conversion.
- Gauge damage isn't stored as floats or as nearby integer constants at 1/10
  or 1/100 % scale. The CN release window and the HCN tick spacing aren't
  in a recognisable table either. They have to be traced from the gameplay
  code, which has no symbols. A possible lead is the HCN chart flag: the
  `icon_hell` song-select code (`0x18080b790`) tests it.

## Engine layer (holds_empress.c)

Round 3 showed the ring reader (`0x1ec860`) handing each code to the
game's judgement-counter, score, gauge, combo and display routines. So the
hold module reads heads from the ring and writes its results back as ring
codes, and the game applies them like any other judgement:

| Hold result | Ring code |
| --- | --- |
| tail PGREAT | 4 |
| tail BAD (`overhold_judge`) | 7 |
| tail POOR, HCN let go (`HE_BREAK`) | `0xf` (missed POOR) |
| HCN empty POOR (`HE_TICK_LOSS`) | `0xb` |
| `J_NONE` | nothing |

- **Heads**: `holds_on_judgement` is called from `fast_slow.c`'s
  `fs_ring()` for every native code. It matches the nearest pending hold on
  that lane within the BAD window (`holds_head_near`).
- **Per tick**: `holds_frame_hook` replaces `jal 0x1e6bb0` at `0x1e5cb8`.
  That site is unused by the current runtime. Held keys come from
  `0x724cf0 + side*0x1c0` (as `fs_keys`). A falling tick counter means a new
  play and re-arms the chart given to `holds_set_chart`.
- **Totals**: tails are not native notes, so the converter writes the chart's
  note-count events with `holds_format.note_total` (each hold counts twice).

Building and loading:
- `HOLDS=1 python3 tools/offdisc/build_vdisc.py OUT --reference-words tools/offdisc/reference-words.json`
  does three things:
  - includes `holds.c` and `holds_empress.c` from `songdb.c`
  - patches `0x1e5cb8` to `holds_frame_entry` (an `ASSET_HOOK` wrapper in `songdb.S`)
  - calls `holds_on_judgement` from both `fs_timed` and `fs_ring`

  Without `HOLDS=1` the runtime is byte-identical to before.
- Holds travel inside the chart: `arcade_convert.py` writes an 8-byte
  `0x7fff` terminator and then the holds.bin records. At play start the
  module scans from the spawner's chart cursor (`gp-0x77dc`) to the
  terminator and reads the records. Native charts have no trailer, so
  nothing changes for them. No `song.txt` field is needed.

To do before enabling by default:
1. Check in PCSX2:
   - whether `0x13acd0(side)` reports continuous turntable motion (BSS)
   - the CN release window (default: PGREAT within 7 ticks)
2. HCN gain ticks: find a gauge-only routine (a candidate is
   `0x204d30(side, kind)`, called from the ring reader) for `HOLDS_GAIN_GAUGE`.
   Until then held HCN ticks add no gauge.
3. Hold bodies are not drawn yet: the head shows as a normal note and the
   tail is invisible. Drawing needs the note renderer, one of the active-list
   walkers in round 3.

## Charge-note graphics (Sparkle Shower bm2dx.dll)

The DLL names two graphic files without a folder: `notes_cn00.ifs` (string at file offset 0xcf3318)
and `lnote00.ifs` (0xcf3258). Static initialisers store them as globals (code at RVA 0xa0034 and
0x9faff). The other graphic files are `/data/graphic/` + `/0/<name>.ifs`. "00" is probably the
note-skin number (DEFAULT, 6DOT, … list at 0xcf5310).

A 64-bit pointer table at file 0x1071bf0 (RVA 0x1073df0) lists the note textures in the order the
renderer uses them:

- normal: `notes00`–`notes09`, `notes_white`, `notes_black`, `notes_turn`, `notes_auto_turn`
- set 1: `notes_bss_start/end`, `notes_cn_white_start/end`, `notes_cn_black_start/end`, with
  bodies `notes_bss_beam0-2`, `notes_cn_white_beam0-2`, `notes_cn_black_beam0-2`
- set 2: `notes_bss2_*`, `notes_cn2_white_*`, `notes_cn2_black_*` with beam0-2 plus beam3-4
  (two extra body states). This is probably HCN/HBSS but is not confirmed.
- assist: `notes_bss_auto_start/end/beam0/joint`
- MSS (multi-spin): `notes_mss_start/middle/end`

A second table at file 0x1070470 lists lane lights `lnote00_lo_{siro,ao,scr}` plus `_push` and
`2_push` (white key, blue key, scratch), indexed per lane (siro/ao alternating, scr last). These are
probably the beams shown while a hold is pressed (2 = set 2).

### The graphics files

`tools/release/konami_tex.py` turns a graphic .ifs into PNGs (texturelist.xml + per-image LZ77 data,
argb8888rev). Checked on the user's files:

- `notes_cn00.ifs` (53 images): set 1 = CN/BSS, set 2 (`cn2`, `bss2`) = HCN/HBSS. Set 2 caps
  have red markers, and beam3/beam4 are red bodies (released/damage). beam0 = idle body,
  beam1/2 = held (lit). `notes_bss_start_1..3`, `notes_bss_middle_1..3`, `notes_mss*` are multi-spin.
  White-key caps are 54 px wide, black 42, scratch 92; caps 26 px tall; bodies are 26-px tiles.
- `notes_cn00_long/short/veryshort.ifs` (43 images): the note-height options, caps 29/23/20 px.
- `lnote00.ifs` (176 images): key-press flashes `lnote00_lo_{siro,ao,scr}` (13 frames),
  `_push` held loops (12 frames), `2_push` HCN held loops (25 frames), `notes_break_` (26 frames,
  the broken-HCN shards). Black backgrounds: drawn additively.

## Drawing holds in EMPRESS

### What the game does (FUN_001e6f50, per note per frame; disassembly dump 2)

- Note sprite category `s0` (0..10) comes from the lane (jump table 0x280f30) and the side/style flags.
- Scroll: the note's s64 position at +0x10 is reduced each frame by the speed entry
  `0x7ad0f0 + side * 0x408 + 16 * index`; y = f(position) is stored at +0x1c (s16), x is at +0x1a.
- Draw, at 0x1e757c: `0x12fdd0(a0 handle, a1 x, a2 y, a3 100, t0 100 (zoomY), t1 w / 2, t2 h / 2,
  t3 100, stack: 0, 0, 0x25f2b0 + side * 8, 0x10)`. Handle, w and h come from the table
  `0x7ad9f0 + 8 * s0` (u32 handle = container << 16 | sprite, s16 w, s16 h).
- The runtime already hooks that call (songdb.S `note_hook`) for note skins and note height:
  note sprites are in containers #109 (SP) and #146 (wide), handle tables 0x25f330 / 0x25f360
  (white, black, turntable, auto). The launcher appends skin sprites to those containers
  (`note_skin_build`, `cont_add`) from 8-bit SPR1 files.

### Plan

1. Sprites: `tools/holds/hold_sprites.py` makes 36 SPR1 sprites (CN, HCN, BSS, HBSS caps and
   body states) at EMPRESS size: 16 / 12 / 28 px wide (white / blue / turntable; measured on a
   capture), 6-row caps, one body tile each. The launcher appends them to #109 and #146 the same way
   as note skins and records their handles for the runtime.
2. Tail position: the tail is put in the chart as a note that is never judged (+0xb = 0), so the
   game scrolls and positions it exactly like any other note; the hold module keeps the pairing.
   To find: which chart event value makes the spawner leave +0xb at 0 (spawner 0x1e7720).
3. Drawing: in `note_hook`, for a hold head or tail note, record its y per lane; after the notes are
   drawn, draw each active hold's body with `0x12fdd0` (body handle, lane x, y from the tail down
   to the head, or to the judge line once the head is hit; zoomY = length / tile height * 100),
   then the caps. The body state comes from holds.c: beam0 not held, beam1/2 held (alternating),
   beam3/4 HCN released or broken.
