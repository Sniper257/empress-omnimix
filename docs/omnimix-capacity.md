# Omnimix capacity: about 2,600 added songs

Goal: every arcade song from 1st to 33rd (the Pinky Crush omnimix data lists 2,461; Sparkle Shower adds about
130), on real hardware as well as PCSX2. This note records where the limits are and the plan for each.

## Where the memory goes today

Measured on a build with 100 EMPRESS songs and 815 added (launcher.log, 24 Nov 2026):

- Reserved block (0x7c4700..0x944700, 1536 KB): runtime about 293 KB, tables about 732 KB, path strings 88 KB.
  The rest (about 420 KB) is borrowed by the BGA texture reserve and the movie work area when free.
- Per added song in the block, about 940 bytes:

  | item | bytes | read by |
  |---|---|---|
  | record (0x134) | 308 | everything; see below |
  | file table (12 per file) + disc extent (16 per file), ~12 files | ~336 | loading a song |
  | chart identities (16 per chart) | ~120 | save / load keys |
  | path strings (after prefix compaction) | ~108 | vdisc |
  | slot tables, sprite offsets, chart index, sort rank, title card entries | ~70 | music select |

- Per added song in the game heap: the score store `slot_saved`, 20 x 28 = 560 bytes (4 modes x 5 difficulties,
  whether the chart exists or not). The save file keeps only played charts (48 bytes each, 4 MB budget), but no
  second copy stays resident during play, so `slot_saved` is the only full copy and must stay lossless.

At 2,600 songs that is 2.4 MB of block (it has about 1.1 MB for songs) and 1.46 MB of heap.

## What music select needs from songs that are not bound

The game only ever sees the 145 alias ids that are bound (songdb.h); everything native that reads a whole record
(note counts, graph targets, loading) asks for a bound alias. For all other songs the runtime reads, from the
record:

- the levels, +0x4a..+0x53 (LEVEL folders, sorting, DJ POINT)
- the BPM, +0x42 / +0x44 (sorting)
- the title's first byte (A-Z folders; titles are otherwise ordered by the launcher's sort_rank)
- through the game's chart_listed (FUN_00214f60, called on the probe alias for every song when a folder list is
  built): the levels and the 10 chart file id words +0xc0..+0xe7, tested for non-zero only (call-graph scan of
  every load from a FUN_001e19a0 result in the 18 functions it reaches)

So a 20-byte summary per song is enough until a song is bound.

## Plan

1. Shared container sprites (done): converted songs no longer add 37 hold sprites each to the note containers.
2. 45 folder slots (done): one per version plus extras.
3. Records off the block (built, Nov 2026; needs a device / PCSX2 run): the launcher writes all records to one
   file on the virtual disc (`records`); the block keeps a 20-byte summary per song (songdb.h SUM_*) and a cache of
   full records per alias id 104..255 (152 x 308 = 46 KB) with the slot each holds (`rec_key`). rec_hook hands
   added songs to `rec_added` (songdb.c), which reads the record again when the alias was bound to another slot
   since, through a 16 KB buffer of 53 aligned records (a folder's songs, numbered together, take one or two
   reads). The probe alias gets a record built from the summary (no read). Music select's levels / BPM / A-Z
   reads (song_window.c, in_vf, the imports in song_resources.c) use the summaries. Per song: 308 -> 20 bytes.
4. Song packs: the converter writes each song as one file (its charts, banks, stream and sprites, sector aligned,
   with an index). The block keeps one disc extent and one path per song; the song's file table is read with its
   record when it is bound, into a per-alias file-id window (145 x 16 files x 12 bytes = 28 KB). Song folders in
   today's layout keep working with today's costs.
5. Chart identities off the block: the 16-byte ids are only needed to key save records, at load and save; they
   move to a file read at those times. The block keeps the u16 chart index.
6. Scores: `slot_saved` keeps 28-byte records only for (chart, category) pairs that exist: SP charts have two
   (modes 0 and 1 share one; mode 3 has its own), DP charts one. Lossless, and the same size whether nothing or
   everything has been played: about 320 bytes for a typical song instead of 560.

Measured on the next build: the launcher logs `records: ... KB in a file, ... KB in the block`.

Estimated per song after 3-5: about 130 bytes of block (2,600 songs: about 340 KB) and about 320 bytes of heap
(about 840 KB). The heap headroom during play of a 16 MB-bank song has not been measured yet; the debug runtime
should report it before step 6 is sized finally. Debug runtimes (INPUT_LOG=1) now write it into each input log:
header word +108 is free KB << 16 | largest free block KB, sampled when the chart starts (after its assets are loaded).

## Testing

The launcher builds only in the pinned ps2dev image (ci/build_builder.py). Logic that can be isolated is tested
on the host, built 32-bit (tools/release/test_launcher_c.py). Each step needs a device or PCSX2 run with the
debug runtime before the next.
