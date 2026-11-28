# IIDX 17 SIRIUS import (work in progress)

SIRIUS was released only in arcades, so there is no PS2 disc to fingerprint.
Instead MEGAMIX converts the arcade `data/` folder into ordinary MEGAMIX song
folders, which the launcher already loads (`record`, `F`, `card`, `D` and `S`
lines in `song.txt`, see `tools/offdisc/launcher/main.c`).

## Status

| Part | State |
| --- | --- |
| Read a newer arcade omnimix (e.g. Sparkle Shower): `.s3p` WMA keysounds (decoded with ffmpeg), `data_mods` layers, SIRIUS = ids 17000–17999 | done: `arcade_data.read_s3p`, `sirius_probe.py` |
| CN/BSS for EMPRESS | later: needs a new runtime module written against `EMPRESS/SLPM_552.21` (the `tools/cn` prototype's sources are not public and its hook sites are now used by FAST/SLOW and input hooks) |
| Read SIRIUS as BMS (one folder per song; WAV keysounds, OGG/FLAC via ffmpeg) | done: `bms.py` |
| Read arcade `.1` charts, `.2dx` keysounds (PCM/MS-ADPCM WAV), `music_data.bin` | done: `arcade_data.py` (the tested dump's `.2dx` files are wrapped in `%hid` and are not read) |
| SPU2 ADPCM encoder/decoder, EMPRESS frame disguise, backing-stream writer | done: `spu_adpcm.py` |
| Measure arcade→EMPRESS mappings on songs that are in both games | done: `sirius_probe.py` |
| Arcade song archives (`.ifs`, kbinxml manifest) | done: `ifs.py` |
| Chart conversion: arcade `.1` → encrypted EMPRESS chart with hold trailer | done: `arcade_convert.py` (tested on 23069: 6 charts) |
| Backing stream rendered from one chart's keysounds (autoplay mix) | done: `arcade_convert.py --stream CHART` (numpy) |
| Keysound split: player sounds → bank, BGM events → mixed stream | done: `arcade_convert.py --split` writes SP.bank / DP.bank in the real `wp\x03` layout and a BGM-only stream (`keysound_bank.py`); not yet heard in PCSX2 |
| `record.bin` from `music_data.bin` (levels, BPM, genre, file fields) | waiting for probe report (entry layout) |
| Title/genre/artist sprites, title card | will reuse `text_assets` / `font_folder` |
| BGA (`movie/`) | later; songs work without it |
| `sirius` source in `megamix.py`, catalogs, append pack, guide | after the conversions are confirmed in PCSX2 |

## What is already known from this repository

- EMPRESS charts: blowfish-encrypted (`chart_transform.KEYS['empress']`)
  chart-LZ. Decoded, they hold a header `(u32 8, u32 16683)` and then 8-byte
  events `(u32 time, u8 type, u8 param, u16 value)`, ended by time `0x7fff`.
  The events use the same type codes as the arcade `.1`.
- Keysound banks start with `wp\x03\x00`. At `0x8000` they hold
  `(count, data size, 1, ...)`, then 16-byte entries from `0x8010`, then SPU
  ADPCM frames with byte0 XOR `0xff` and byte2 minus 36.
- Backing streams: a 0x800-byte header
  `(0x08640001, 0, 0x800, size, 0, 0, rate, channels, 1, 16, volume, 1)`.
  Stereo is interleaved every 16-byte frame, with byte2 minus 2.
- Song record: 0x134 bytes. File ids at `0x58` (stream), `0xc0..0xd4` (six
  charts), `0xe8..0x107` (banks), `0x94` (title card), version byte `0x54`
  (`17` = the existing `sirius` style id in `source_versions.VERSIONS`).

## What the probe measures

With a newer arcade omnimix, point it at the game folder that holds `data/`
and `data_mods/` (ffmpeg on PATH, or `--ffmpeg`, decodes the WMA keysounds):

```sh
python3 sirius_probe.py /path/to/contents /path/to/MEGAMIX -o sirius-probe.json
```

With BMS songs, run it on the BMS folder and a built MEGAMIX base,
the folder `megamix.py` created from EMPRESS Disc 1:

```sh
cd tools/release
python3 sirius_probe.py --bms /path/to/sirius-bms /path/to/MEGAMIX -o sirius-probe.json
```

BMS charts are read into the same event list as arcade `.1` charts (times in
ms), so the conversion is the same for both sources. Titles, artist, genre,
BPM and levels come from the BMS headers.

With arcade data, run it on the data folder and a built MEGAMIX base. Song files are found
under `sound/` or `sd_data/` (or pass `--sound FOLDER`):

```sh
cd tools/release
python3 sirius_probe.py /path/to/sirius/data /path/to/MEGAMIX -o sirius-probe.json
# or choose the pairs yourself:
python3 sirius_probe.py DATA MEGAMIX --pair empress/id_97=<Bad Maniacs id> --pair empress/id_98=<DOMINION id>
```

For each song found in both games, it reports:

- the least-squares fit of EMPRESS note times against the arcade ones (time
  unit and offset), and which arcade slot each EMPRESS chart came from
- the keysound value ranges and BGM-event counts on both sides
- the EMPRESS bank table and header rows, the stream header, and whether the
  ADPCM disguise rule holds for every frame
- the EMPRESS 0x134 record next to the hex of the matching `music_data.bin`
  entry, so the field mapping can be read off directly

The report contains structure and statistics only: no audio, charts or
artwork. It is safe to attach to an issue.

## Converting a song (current state)

```sh
cd tools/release
python3 arcade_convert.py /path/to/23069.ifs OUT --stream SPA      # --hell for HCN charts
```

This gives `OUT/SPN.bin` … `OUT/DPA.bin` (EMPRESS charts), `OUT/stream.bin`
and `OUT/convert.json`. Arcade times are ms (the 23069 end event is at
126000), and they become 60 Hz ticks. The chart's own timing-window
events (type 8) are kept. The song folder (`record.bin`, `song.txt`, title
sprites) is the next step.

## HCN Ver. songs (Sparkle Shower omnimix)

`music_omni.bin` (version 32, 2461 songs) lists the HCN versions as separate songs: 17100 Almagest
(HCN Ver.) and 18102 Kailua (HCN Ver.). The 18102 entry also stores "18021" (the normal Kailua) at
entry offset 0x7d4. 18102.ifs has the same notes and hold lengths as 18021.ifs; the only chart
difference is one extra event `(0 ms, type 13, 0, 0)` at the start of every chart. `arcade_convert.py`
treats a chart with a type-13 event as HCN/HBSS without needing `--hell`, and checks show
18102 → identical output to 18021 `--hell`.

## Keysound banks

Read from a real bank (All My Turn's "SP" bank: 497 slots, 495 samples, 5.3 MB) and from the
play-keysound function `0x140260`. The full layout is in the `keysound_bank.py` docstring. In short:

- One bank per play side, named in the header ("SP", "DP"), holding every player sound of that
  side's charts. Real banks are 5.3 and 6.1 MB, far more than the 2 MB SPU RAM, so the game
  uploads sounds as it needs them; there is no need to lower sample rates to fit.
- `play(slot | bank << 16 | flags)`, slot 1..0x7ff: 16-byte slot entries from the start of the
  bank (the header fills slot 0): priority, exclusive group (0xff), in use (2), length in ms,
  volume (0x7f), sample index, and a byte that is 76 for nearly every slot (86 for a few, 0 for slot 1;
  meaning unknown).
- `0x8000`: sample count and data size; `0x8010`: 16-byte sample entries (offset, bytes per channel,
  channels = 2, rate code). Rate codes: `0x3e44` = 44.1 kHz (489 of 495), `0x447d` = 32 kHz,
  `0x4800` = 24 kHz, checked against the slot lengths.
- Samples are stereo, channel 0 then channel 1; each channel is a zero frame, the ADPCM frames
  (last one flag 1) and a `00 07 77…` end frame, all with the bank disguise.

EMPRESS's own songs play the BGM from the bank too: All My Turn's SPN chart has 2,498 BGM events
`(tick, 0x87, 0, slot)` over 286 bank sounds (slot 1 = a 3.4 s intro at tick 0), and its 37 MB file is
not audio (probably the BGA). `arcade_convert.py --split` now does the same: every sound in the bank,
no stream. Kailua (HCN Ver.) gives 11.9 MB banks (443 sounds), more than the 6.1 MB largest seen, so
the record needs the launcher's large-bank workspace (record byte 0xbd; up to 0x1fff800 bytes).
`--split --stream SPA` keeps the earlier layout: player sounds only in the bank, BGM in a stream.

### `--split --native`: the CS disc's layout

Checked against All My Turn's CS files (arcade 16017; bank, stream and nine charts):

- **Bank**: every arcade sound, slot n = arcade sound n, at full level (gain 1.00, correlation > 0.99),
  each with the 28-sample zero frame. Slot rows: priority 0, group 0xff, used 2, b8 1, volume 127.
  Seven sounds are stored at 32 or 24 kHz and two pairs of sounds share one sample; that looks hand-picked
  (sounds of the same length stayed at 44.1 kHz), so `--native` keeps 44.1 kHz unless `--bank-budget` forces it.
- **Stream**: arcade sound 1 alone, the whole 121 s backing track, at 0.965 (0.9647-0.9652 in every
  10 s window; the residual is high-frequency ADPCM noise), after a 28-sample lead. Its header volume
  is 0x33 for arcade volume 65 (music_data +0x680), taken as volume x 100 / 127.
- **Stream start**: the game starts the stream at the chart's event for the track's slot, not at the chart's
  start, so the stream begins with the track itself (plus the 28-sample lead). Light Shine (17026) starts its track
  at 875 ms: with that silence in front, a PCSX2 capture had the backing track 0.9 s behind every keysound.
  Songs converted before (`native_revision` 1 in convert.json) are redone by the batch when their track starts after 0.
- **Per-chart keysound files** (music_data +0x684, 10 characters SPB SPN SPH SPA SPX DPB DPN DPH DPA DPX; `0` =
  `<id>.2dx`, another character c = `<id>c.2dx`, bm2dx.dll `%05d/%05d%c.2dx`; 74 songs, 13 from SIRIUS on, e.g.
  quell `000a0000a0`, GENE `0123001230`): the record's bank groups are picked by difficulty (0xe8 H/X and the
  fallback, 0xf0 A, 0xf8 N, 0x100 B; bank +0, stream +4), so each group gets a bank of the files its charts use and
  the stream of the file most of them use. When SP and DP of one difficulty use different files (crew `0512005340`),
  the group's bank holds both files one after the other and that chart's sounds are renumbered; the other file's
  backing track then plays from the bank. `arcade_batch.py` passes `--variants` from the database.
- **More sounds than a bank's 2,047 slots** (RESIDENT: VØID 2,496, Swing My Wonderland 2,140, Somnidiscotheque
  2,204): every chart plays every sound, so the group's BGM that all its charts play at the same time, from sounds
  no note plays, is mixed into the group's own stream (`stream_<field>.bin`) with the backing track and dropped
  from the charts; the bank (`keys_<field>.bank`) keeps the rest. Negative BPM (MAX 360's reverse scroll) plays
  forwards: it only sets the scroll speed.
- **Slot 1** (the backing track) keeps its row, pointed at slot 2's sample with tail 0, and every chart
  keeps its BGM event for it at tick 0: that is how the song avoids playing the backing track twice.
- **Charts**: every arcade event, BGM bank-triggered, on the NTSC frame clock about one frame later than
  rounding: `floor(ms * 60/1001 + 1.63)` (0 stays 0). The six arcade charts come out 98.0-98.1% identical,
  event for event; the rest differ by one frame (the arcade's times are whole ms). The CS disc adds three
  charts of its own.

Result for 16017: 5.70 MB bank against the CS 5.32 MB (the difference is the resampled sounds), stream
6,121,344 bytes against 6,121,248. Hold sprites: the launcher packs every song's `holds/*.spr` into note containers #109 and #146, which take only
byte-identical palettes, so all installed songs must carry the same set ("sprites use different palettes").
`megamix.py index` (which `--install` runs) copies one set over the others: the 17 px one, the most used if several.

`arcade_batch.py` uses this mode by default, with each song's volume (`--legacy-audio` for the earlier layout).
In PCSX2 the converted All My Turn plays as a near-perfect match for the CS one.

Song folders for added songs (launcher/main.c `add_song`): `record record.bin` (0x134 bytes),
`F <field hex> <bytes> <path>` per file id (bank fields 0xe8..0x107: four 8-byte groups, u16 file ids
at +0 and +2), `card`, `D <n> <path>`, `S <cont> <field> <sprite>`. `tools/release/dump_record.py`
writes a native record (e.g. All My Turn, id 2) as the template.

Before that change: Kailua (HCN Ver.) converted to two 5.9 MB banks (417 sounds each, all 44.1 kHz stereo) plus a
BGM-only stream. Still to check in PCSX2: that the game loads a converted bank, and how the song
record chooses which bank a chart uses (the bank index in the play call).

## Song folders (arcade_songfolder.py)

`arcade_songfolder.py CONVERTED record_2.bin OUT/<folder> --music music_omni.bin --id <arcade id>`
writes a custom song folder: `record.bin` from a native record (All My Turn), `song.txt`
(`record`, `F` lines for the charts and banks), the charts and banks, and `catalog-lines.txt`
(S/C lines for `catalog/charts.txt`; the launcher rejects an added song with no identity).
The record field map is in the script's docstring. The chart fields follow the launcher's own table
(launcher/coalesce_catalog.h): SPN d0, SPH c0, SPA c8, SPB d8, SPX e0, DPN d4, DPH c4, DPA cc,
DPB dc, DPX e4. (A first guess from All My Turn's file order put SPN at c0, and SP Normal then
loaded a DP chart in PCSX2.)

Still taken from the template, so an added song shows All My Turn's title card and wheel sprites
and keeps its 0x58 file (probably the BGA movie) until those are converted.

## Batch conversion (arcade_batch.py)

`python3 tools/release/arcade_batch.py ARCADE_DATA MEGAMIX --music music_data.bin [--versions 17-33] [--jobs 4] [--install]`
finds every `<id>.ifs` and `<id>/<id>.1` song under ARCADE_DATA, takes each song's version from the music
data (entry +988, u16: 0 1st style, 1 substream, 2 2nd style, ... 32 Pinky Crush) and converts it into
`MEGAMIX/arcade-batch/songs/<style>/<folder>/` (arcade_convert --split, arcade_songfolder), one style folder
per version (sirius, resort_anthem, ... sparkle_shower; folder art from arcade_style for versions MEGAMIX has
no folder for). Reruns skip finished songs (`batch.json`). `--install` copies the songs into MEGAMIX, lists them in
their styles' `index.txt` and new styles in `songs/styles.txt`, then runs `megamix.py index`.

Songs carry their identity in `song.txt` (`catalog S/C ...` lines, launcher catalog.h), so `catalog/charts.txt`
stays the release's and the PC cache builder accepts the installation. A version whose titles need glyphs the
bundled atlas lacks gets an atlas built from the pinned open fonts (fetched once; Pillow and fonttools).
Capacity: today's launcher holds about 1,200 added songs in its reserved block (docs/omnimix-capacity.md).
