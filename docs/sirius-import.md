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
The record field map is in the script's docstring. All My Turn's record confirms the chart field
order (its SPH chart has 971 notes, stored at 0x10a) and the empty DP Beginner (level 0, field 0xdc 0).

Still taken from the template, so an added song shows All My Turn's title card and wheel sprites
and keeps its 0x58 file (probably the BGA movie) until those are converted.
