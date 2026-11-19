<!-- Generated from README.txt; run python ci/render_guides.py. -->

# EMPRESS MEGAMIX - public beta

This is an unofficial fan project, not affiliated with or endorsed by KONAMI
or BEMANI. All trademarks belong to their respective owners. MEGAMIX is free.

Back up your original saves before using MEGAMIX. There is still a small chance
that a bug could corrupt them. Use a homebrew memory-card file manager to copy
your saves off the card and keep the backup on another device. For PCSX2, back
up the virtual memory card file.

## Known bugs/limitations

- Some songs may freeze during loading due to memory limits.
- Floating Hi-Speed and LIFT can misbehave together. Keep Lock Cover disabled
  if you have trouble adjusting your GN.
- LIFT shares its graphic with the lane cover.
- Launcher settings may not persist correctly when the ELF is outside the
  MEGAMIX folder.
- STATISTICS and RIVAL menus are probably broken, as no changes were made to them
- Global unlock supplied by MEGAMIX doesn't apply to GALLERY
- BEGINNER menu does not include new songs

## Reporting bugs

Get a hold of me (mr_mary on Discord or @zlewyjuzjado on X/Twitter) and include:

- Console model (e.g. SCPH-50004).
- Storage medium (USB or MMCE).
- Games included in your MEGAMIX build.
- Controllers you had plugged in.
- Your active launcher.cfg. For controller issues, include controllers.0
  and controllers.1 too.
- Your save directory, BISLPM-55221BM2DX16, from the memory card. Include the
  whole directory, not just the original EMPRESS save file.
- What happened and how to reproduce it, including the song, difficulty and
  play mode where relevant.

Settings normally live in the MEGAMIX folder. If you boot the ELF from somewhere
else, use the configuration location selected in the launcher. On MMCE, take
saves from the virtual card used by MEGAMIX.

## Preparation

You need:

- Python 3.10+.
- An ISO backup of your own EMPRESS + PREMIUM BEST Disc 1 (EMPRESS).
  You can add songs from your dumps of the other supported PS2 releases.
- A way to run homebrew on your PS2, or PCSX2. Look up FMCB, PS2BBL,
  FreeDVDBoot, etc. if you need a boot method.
- A USB drive or MMCE device (such as PSxMemCard Gen2), with at least 5 GB free.
  The base is about 4.3 GiB; a build with all supported discs is about 36.2 GiB.

Supported dumps match these Redump SHA-1 hashes.

```text
3rd style      495f5f9fc6b62562c92b27be31c7d1a9bae64b31
4th style      69c73667a7b2fc6e6fd2314899377f62cf16d534
5th style      abda772a65b7d4ed0d7175358af9ca9209c03d37
6th style      0010d44b65c5eeb9360c9eace11f1ba840c01c58
7th style      090ca87e447106368c0c89f63e68a4b6c318b5d9
8th style      01d2a7ffa427d73996b4a19fab14d1c351012377
9th style      6d42643fb992c19737b22813166a12b5d60bd69e
10th style     680edcac40ed72ed318bc1bb8d6e89224b80a330
IIDX RED       8e52f963a33c9fe95022861ef39966bde715ed1a
HAPPY SKY      b82e7939b720b8a1c0875e88a328493f336a758f
DistorteD      706da91072b75262729c96b16aa91181231184d2
GOLD           7cc3625f57f4d77d67ac3fb3675d3ee5de18e726
DJ TROOPERS    6a35e096900dc6e7842f7aa86366ba0f5fe78dfd
EMPRESS        92d95574a6a35a908cc3255a997a15f126993ada
PREMIUM BEST   b23f9799b2cb6106b45c992928c97d6f2e4d3a8b
beatmania USA  497cd0a20420d270047a77009efc7c48ce95f727
```

## Create the base

Extract the builder archive and keep its files together. Open a terminal in
that folder and run:

```sh
python3 megamix.py "path/to/EMPRESS DVD1.iso" "path/to/MEGAMIX"
```

On Windows, use py instead of python3:

```sh
py megamix.py "D:\ISOs\EMPRESS DVD1.iso" "D:\MEGAMIX"
```

Use a new output directory. Default rendering only needs Python; no SDK or
compiler is required. Custom fonts are optional; set them up before creating
your base as described below.

## Custom fonts (optional)

Put .ttf, .otf, .ttc or .otc files in these folders before creating the base:

```text
fonts/compacta/   Large titles and song-title cards (Compacta BT)
fonts/eurostile/  Wheel titles and card genres
fonts/handel/     Takes priority over Eurostile
fonts/helvetica/  Genre and artist text
fonts/gothic/     Japanese Gothic (DF HeiSei Gothic)
fonts/mincho/     Japanese Mincho for titles and card artists (DF HeiSei Mincho)
```

File names don't matter. No OS font installation or JSON editing is needed.
Missing families/characters use bundled fallbacks. Another font can fill any
slot. The builder picks faces/weights from their metadata and lists its choices.

Install the optional packages on the PC creating the base. On Windows:

```sh
py -m pip install Pillow fonttools
```

On Linux/macOS with system-managed Python, use a virtual environment:

```sh
python3 -m venv .venv
.venv/bin/python -m pip install Pillow fonttools
```

Use .venv/bin/python instead of python3 in later commands when using this venv.
Create the base normally; the builder finds the fonts folder automatically.
For another location, add --fonts "path/to/fonts". To ignore custom fonts,
add --no-custom-fonts.

Generate and install append packs normally, with the same builder. They inherit
the base's compiled coverage, even on a PC without the fonts or extra packages.
No --fonts is needed for append packs; supplying it requires a matching base
profile. Packs can be generated before the base.

Keep .megamix/fonts when copying the base. Raw font files aren't copied into
packs or the game. Fonts affect generated text; native artwork, launcher UI
and splash text keep their appearance. To change fonts, create a fresh base
and reinstall your append packs. Don't copy generated artwork over by hand.

## Add songs from another disc

Generate an append pack, then install it into your base:

```sh
python3 megamix.py "path/to/HAPPY SKY.iso" "path/to/HAPPY-SKY-pack"
python3 megamix.py install "path/to/HAPPY-SKY-pack" "path/to/MEGAMIX"
```

Repeat for each disc you want to add. Order doesn't matter. Use the install
command rather than copying pack files by hand.

Copy the finished MEGAMIX folder to your USB drive or MMCE card and safely eject
it. The ISOs and builder files stay on your PC.

## Emulator / HostFs build

For PCSX2, generate a separate HostFs base:

```sh
python3 megamix.py "path/to/EMPRESS DVD1.iso" "path/to/MEGAMIX-host" --target host
```

Enable HostFs in PCSX2 and boot that output's MEGAMIX.ELF. Add songs with the
same append packs and install command. Keep console and HostFs bases separate.

## Verification and interrupted builds

To check an installation:

```sh
python3 megamix.py verify "path/to/MEGAMIX"
```

If a build or install is interrupted, repeat the same command to resume.
Keep the incomplete directory and recovery journal. Ctrl+C exits cleanly.
If a killed process leaves a lock, make sure it has stopped, then remove only
the lock named in the error message.

## Build the launcher cache on PC (optional)

First boot on hardware can take a long time depending on how many discs you include.

You can generate cache on your computer to save time.

Install Wasmtime in the Python environment you'll use for the command:

```sh
python3 -m pip install "wasmtime>=50.0.1,<51"
```

On Windows, use py instead of python3. For system-managed Python, use a venv
as above. Then specify the PC folder and the exact PS2 game-data path:

```sh
python3 megamix.py cache "X:\MEGAMIX" --game-path mmce0:/MEGAMIX/
python3 megamix.py cache "/media/USB/Games/BM" --game-path mass:/Games/BM/
```

For --game-path, use the PS2 game-data path, not the ELF location. MMCE uses
mmce0: for slot 1 and mmce1: for slot 2; USB uses mass:. Match the launcher
path exactly, including mass: versus mass0:, or it will rebuild the cache.
For example: mmce1:/MEGAMIX/ or mass:/Games/BM/. HOST uses host:.

Use your PS2's intended launcher.cfg. If settings live outside the game folder:

```sh
python3 megamix.py cache "X:\MEGAMIX" --game-path mmce1:/MEGAMIX/ --config-dir "Y:\APPS\MEGAMIX" --settings-path mc0:/APPS/MEGAMIX/
```

--config-dir is the PC settings folder; --settings-path is its PS2 path.
Keep debug 0 and prepprobe 0 in debug.cfg. Finish pending settings saves and
pack installs/updates first; active installer locks or damaged core files block
cache generation.

## Launch

Run MEGAMIX.ELF through your preferred homebrew launcher. The ELF can live
outside the game folder. If it cannot find the files, select your MEGAMIX
folder in the storage browser.

## Launcher controls

- DualShock: D-pad to navigate/change values, Cross to select.
- Beatmania controller: 1 down, 2 up, 6 select.
- SELECT switches physical beatmania-controller navigation on or off.
- START starts the game. Submenus also have an Exit option.

Physical PS2 and configured USB controllers can both navigate the launcher.

The main menu contains:

- Video: 480i, 480p with 448 lines, or 480p with all 480 lines.
  The launcher uses the selected mode too. For 480p on a CRT, you need a
  31 kHz-capable display and component cables, or an RGB connection that
  supports Sync-on-Green.
- Songs: enable or disable installed song folders.
- Skin settings: initial lane brightness, keybeam length, note artwork,
  note height and frame pack. More options are in the in-game skin editor.
- Autosave: keep the native save-on-exit behavior, or save after returning
  to song select. AUTOSAVE must still be enabled in the game.
- Bind USB controller: assign and configure a USB controller for either player.
- Change game file location: select another installation, then restart
  the launcher.

The first launch takes longer because the launcher needs to generate its caches.
Later launches reuse them. Adding discs or changing enabled folders can trigger
another rebuild.

## USB controllers

Choose a player, select DualShock, 7-key or 5-key, then bind the controls.
Turntables can use buttons, a stick-style axis or a rotating absolute axis.
Release controls between binding steps and check any missing or duplicate
bindings before saving.

Standard USB HID controllers are supported, including common PC beatmania
controllers. Proprietary protocols and some devices may not work. You can
assign two controllers, including two of the same model, to separate players.

In-game, each player uses either physical PS2 input or USB input. Assign USB in
the launcher if you want to rebind or swap that player's USB controller later;
you cannot switch between physical and USB input during a game.

## In-game

The main flow is the same as EMPRESS. Most additions are in FREE mode.
Songs and native CUSTOMIZE items are unlocked from the start.

## MOD MENU

Open it from song select:

- Single / battle: 1 + 3 + 5 on the player's controller.
- DP: 1 + 3 on both sides.
- DualShock: Square + Cross + Circle, or Square + Cross on both controllers
  for DP. This assumes the default button mapping.

Release the keys before entering the combo.

Navigation:

- DualShock: D-pad, Circle to select, Square to go back.
- 7-key: 1 down, 2 up, 6 select.
- 5-key: 1 down, 2 up, 5 select.

Select a value to edit it, then select again when done.
START or SAVE / EXIT saves and closes the menu. SELECT or EXIT discards changes.
During controller binding, follow that screen's prompts instead; START and
SELECT can themselves be bound. A completed controller save is kept even if
you later discard other menu changes.

## Lane options

- NORMAL, MIRROR, RANDOM, S-RANDOM, H-RAN and ALL-SCR.
- R-RANDOM and Random Trainer.
- RANDOM+, S-RAN+ and MIRROR+: select a compatible base arrangement first.
- Trainer pattern: set an exact seven-key sequence, or choose destination
  colours and randomize within the white/blue groups.
- Field: select the left or right trainer pattern in DP.

R-RANDOM and Trainer are for seven-key play. Trainer allows normal score/lamp
saving in this build.

## Player options

- Gauge Auto Shift: keeps you playing on an easier gauge if HARD runs out.
  Press SELECT on RESULT to switch the displayed gauge result.
- Floating Hi-Speed: enables FHS, with a separate Lock cover / START reset
  option. Controls are listed under Play.
- LIFT: enables the lower cover and raised hit position.

## Adjustment timing

Change the timing offset here instead of returning to the main Options screen.
The offset is per player in battle; DP uses one value for both fields.

## Game options

- REGUL SPEED: constant scroll speed regardless of BPM, aka CMod.
- GAMBOL TIMING: OFF, HYPER or ANOTHER.
- HAZARD: one miss and you're out. Requires HARD.
- QUICK RETRY: enables the retry shortcuts listed under Play.

Pink labels mark options that affect score saving or use assist clears.
ASSIST ON appears outside the menu when a restriction is active.
CMD: shows the active lane options or Trainer pattern.
Some native command options are shared between players.

## Rebind controller

For a player already assigned to USB:

- Rebind buttons: edit the current bindings.
- Assign new controller: unplug the old controller, plug in the new one,
  then go through type selection, button binding and turntable setup.
- Change controller type: switch type without clearing the bindings.

## Customize play scene

Opens the live skin editor. FAST/SLOW and BPM notice settings are here too.

## Skin customization

The editor loads an autoplay preview chart in your selected layout.
Change settings while watching the playfield, then save or cancel.

Available options:

- Lane light.
- Keybeam height.
- Note height.
- Judgement height.
- FAST/SLOW: OFF, TYPE A / B / C.
- F/S counters: show or hide them beside the judge counters.
- BPM notice: OFF, TYPE A / B / C / D.
- Ghost: OFF, TYPE A / B / C.
- Frame: vanilla EMPRESS frames.
- Notes: EMPRESS note artwork.
- Explosion art: vanilla EMPRESS effects, including RANDOM.
- Explosion size: 70–130% in 10% steps; default 100%.
- Turntable.
- Lane cover.
- Play layout: Original / Arcade for single play and DP.
- DP type: Type A / B.

Battle shows P1 and P2 values side by side. Applicable settings and artwork are
per player; frame/layout choices are shared. DP uses one player's settings for
both fields.

Artwork changes keep the preview running where supported. Layout changes
reload it. Original DP uses a central overlay in place of the BGA panel.

## Play

## Floating Hi-Speed (FHS)

Enable FHS in Player options, then hold START and briefly press and release
SELECT to switch between regular and floating hi-speed.

With Lock cover / START reset on, the cover locks when READY begins.
Double-tapping START during play resets the GN to your target at the current
BPM, instead of toggling SUDDEN+. Before READY, the native cover controls apply.
The normal START + SELECT hold-to-exit still works.

## LIFT

Hold START and turn the turntable to adjust the cover. With SUDDEN+ visible,
you adjust SUDDEN+. Double-tap START to hide it, adjust LIFT, then double-tap
again to bring SUDDEN+ back. With the FHS cover lock enabled, set both before
READY.

## Retry and RANDOM retention

With Quick Retry on, tap during the exit fade or STAGE FAILED animation:

- START: retry with a new RANDOM arrangement.
- SELECT: retry with the previous arrangement.

On RESULT, hold SELECT while choosing CONTINUE and through the transition to
keep the arrangement. CONTINUE without SELECT rolls a new one.

## Saves

MEGAMIX uses a compact save format without the original song-count limit.
Scores are saved to the memory card in slot 1, either a real card or the
memory-card side of an MMCE device. The ELF and game-file locations do not
change where scores are saved.

If the card has a vanilla EMPRESS save but no MEGAMIX save, the game loads it.
Saving creates MEGAMIX data alongside it. Your vanilla save stays intact and
usable in EMPRESS, but it won't include later MEGAMIX progress.

If there's no compatible save, create new data in-game and make a manual SAVE.
The PC builder doesn't create a score save. AUTOSAVE must be enabled in-game,
even if you selected save-after-play in the launcher.

The new format keeps two generations so an interrupted save can fall back to
the previous one. It still needs a working card: don't power off or remove it
during saving. FAST/SLOW counters are display-only and aren't saved.

Launcher and MOD MENU settings are separate from score saves. On an MMCE device
with Game ID support, MEGAMIX uses PIZD-00000 by default. If you want to import a
retail save, put it on that virtual card; the game doesn't search other VMCs.

## Mentions

Thanks to the people behind these projects:

- 480p patch by bluric (https://github.com/blurtic2/iidx-ps2-480p) - what kicked off this whole thing
- EMPRESS CS FAST/SLOW patch by 95jack (https://www.youtube.com/watch?v=anNfo8uBubE)
- PS2SDK / ps2dev (https://github.com/ps2dev/ps2sdk) - SDK and drivers.
- gsKit (https://github.com/ps2dev/gsKit) - launcher graphics.
- MMCEMAN (https://github.com/ps2-mmce/mmceman) - MMCE support.
- FatFs, by ChaN (https://elm-chan.org/fsw/ff/) - filesystem support.
- ps2-packer (https://github.com/ps2dev/ps2-packer) - ELF compression.
- 2dx-gsm, by aixxe (https://github.com/aixxe/2dx-gsm) - Gauge Auto Shift reference.
- iidx-ps2tools, by SaxxonPike (https://github.com/SaxxonPike/iidx-ps2tools) -
  PS2 data formats and conversion tools.
- iidx2bms, by Glebsin (https://github.com/Glebsin/iidx2bms) - chart-format reference.
- xdelta (https://github.com/jmacd/xdelta) - binary delta recipes.
- Python (https://www.python.org/), Pillow (https://python-pillow.org/) and
  NumPy (https://numpy.org/) - development and asset tools. Only Python is
  needed to run the builder.

Research and testing also used
PCSX2 (https://github.com/PCSX2/pcsx2),
Ghidra (https://github.com/NationalSecurityAgency/ghidra),
Open PS2 Loader (https://github.com/ps2homebrew/Open-PS2-Loader),
OpenLR2 (https://github.com/GOMazk/OpenLR2),
beatoraja (https://github.com/exch-bms2/beatoraja) and
lr2oraja-endlessdream (https://github.com/seraxis/lr2oraja-endlessdream).

Not every project listed here is bundled. See the supplied licenses directory
for the bundled notices and attribution.
