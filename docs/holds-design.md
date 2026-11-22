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
