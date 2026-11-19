# EMPRESS MEGAMIX

An unofficial PS2 IIDX fan project based on EMPRESS, with a multi-disc song
library, USB controller support and additional play/customization options.

## This software is entirely vibe-coded.

I built this by exclusively iterating with ChatGPT and Claude. I didn't write
any of the code myself. I did, however, do extensive hardware testing and
hand-pick the libraries and tools used for the workflow. I wrote and edited
the English user guide.

## Download

Get the [latest release](https://github.com/marysiamzawka/empress-megamix/releases/latest).

## How to use

[English](docs/guide.md) | [日本語](docs/guide-ja.md) | [한국어](docs/guide-ko.md)

Back up your original saves before using MEGAMIX. The download includes TXT
versions of these guides.

Report bugs through [GitHub Issues](https://github.com/marysiamzawka/empress-megamix/issues). Include the information
listed under Reporting bugs in the user guide.

## Build the download

The maintainer/CI build requires Linux x86-64, Python 3.13 or later, Docker and
zstd. End users need only Python 3.10 or later for ISO generation and installation.
Cache generation is optional and additionally needs the documented Wasmtime
Python package.

```sh
docker pull ps2dev/ps2dev@sha256:5476fdfbf34d35eb644fde0a6f6533fb554a4a8171ce53dad7cd1df6077dfb05
python ci/build_builder.py --work-dir /tmp/megamix-build --output dist/megamix.zip
```

The GitHub Actions workflow runs the tests on Linux, Windows and macOS before
building the download on Linux. A successful manual run or `v*` tag build
publishes a GitHub Release named with its compile date (`DD-MM-YYYY`, Warsaw
time), containing `megamix.zip`. Each run has a unique release tag, including
reruns, so multiple builds on the same day can coexist. The ZIP and build
receipt are also retained as Actions artifacts.

The script downloads only the pinned open-source compiler/driver archives in
`ci/dependencies.json`. SHA256 and sizes are checked before use. Compilation runs
with networking disabled and only a new private source/build folder mounted in
the SDK container. Old object files, SDK installation paths, game ISOs, private
fonts and a previous release capsule are not build inputs.

For a fully cached build, add `--offline --dependencies /path/to/dependencies`.
Choose a new `--work-dir` for each build. The same entry point runs in Actions.
Generated ZIPs use fixed member order, timestamps and permissions.

## Notices

Third-party licenses are retained under `tools/release/licenses`,
`tools/release/cache-engine/licenses` and `tools/offdisc/licenses`; driver sources
come from pinned upstream archives. The unpacking stub's corresponding GPL
source is included in the generated install. Original MEGAMIX code is under the [MIT licence](LICENSE),
Copyright (c) 2026 MEGAMIX contributors. Third-party components retain their own
licences; see [NOTICE.txt](NOTICE.txt).
