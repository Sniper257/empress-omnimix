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
| Chart conversion (time unit, slot→field order, stream trigger) | waiting for probe report |
| Keysound split: player sounds → bank, BGM events → mixed stream | waiting for probe report (bank table layout) |
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
