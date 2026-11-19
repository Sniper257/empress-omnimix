CUSTOM FONTS
============

Put your font files in the matching subfolders. The builder automatically uses
this fonts folder when it contains .ttf, .otf, .ttc or .otc files. File names do
not matter. You do not need to install the fonts in Windows or edit a JSON file.
Do not put font files directly next to this README.

    fonts/
        compacta/   Compacta for large titles and generated song-title cards
        eurostile/  Eurostile for generated wheel text and card genre text
        handel/     Handel Gothic instead of Eurostile, when both are supplied
        helvetica/  Helvetica for generated genre and artist text
        gothic/     Optional Japanese Gothic faces
        mincho/     Optional Japanese Mincho faces for large titles/card artists

Any family can be omitted. Missing families and characters use bundled fallback
coverage. With several files, the builder chooses upright/normal-width faces
and the appropriate weight from the font's metadata, and prints its choices.
You can use a different font in a named slot if you prefer its appearance.

Custom fonts need Pillow and fontTools on the PC creating the base. On Windows:

    py -m pip install Pillow fonttools

On Linux/macOS, use a virtual environment if your Python is system-managed:

    python3 -m venv .venv
    .venv/bin/python -m pip install Pillow fonttools

Use .venv/bin/python instead of python3 in the commands below when using that
virtual environment.

Then create your base normally:

    python3 megamix.py "EMPRESS DVD1.iso" "MEGAMIX"

To use a fonts folder elsewhere:

    python3 megamix.py "EMPRESS DVD1.iso" "MEGAMIX" --fonts "path/to/fonts"

To ignore populated font folders and use bundled default rendering:

    python3 megamix.py "EMPRESS DVD1.iso" "MEGAMIX" --no-custom-fonts

Build append packs normally and install them with the same builder. Installation
uses the base's stored font coverage, even on another PC without the fonts or
those Python packages. An independent append pack can be generated before the
base. You do not need --fonts on append commands; if supplied, it records a
requirement for a base with that exact profile.

The original font files are never copied into the game or append pack. The base
keeps compiled coverage and rendering recipes in .megamix/fonts for the PC
installer. Keep those files when copying your MEGAMIX folder.

This changes generated text, including fallback folder captions. Native artwork
extracted from ISOs, the compiled launcher UI, and the authored splash text retain
their existing appearance. Selecting different fonts is not an in-place update
of an existing game folder: create a fresh base and install your append packs.

Do not copy generated artwork over an existing installation by hand. The builder
updates its verification receipts and cache/source identity automatically.
