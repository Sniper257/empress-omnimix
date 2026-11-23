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
| Engine: hooks into SLPM_552.21 | `tools/holds/holds_empress.c` + build | next; written from `disasm_sites.py` output |

## Rules (holds.c)

- **CN**: the head is judged by the game as an ordinary note. If it's GOOD or
  better, the lane is held. Releasing it within the tail window judges the tail
  by its offset from the end. Releasing earlier is a POOR. A missed head makes
  the tail a POOR at the end.
- **HCN**: like CN, but every `tick` while held gives combo and gauge
  (`HE_TICK_GAIN`). Letting go breaks combo once (`HE_BREAK`) and costs gauge
  every tick (`HE_TICK_LOSS`). The lane can be grabbed again until the end.
- **BSS**: the turntable must keep moving in the head's direction. Standing
  still is allowed for `spin_grace`. Spinning back judges the tail.
- **HBSS**: BSS with HCN ticks; spinning the head's way again re-grabs.
- Holding past the end: `OVERHOLD_RELEASE` (the tail must be released, POOR
  after the BAD window) or `OVERHOLD_AUTO` (held at the end = PGREAT). Which one
  matches the arcade, and the exact tail windows and tick rate, are settings in
  `holds_cfg`. They should be checked against arcade footage before release.

The engine layer turns `hold_event`s into the game's own judgement, combo and
gauge calls, and asks `holds_is_tail` so the game never judges a tail itself.

## Data (holds.bin)

`MMHL`, version, count, then the 12-byte `hold_def` records sorted by start.
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
