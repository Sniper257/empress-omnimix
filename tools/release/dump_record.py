"""Write one EMPRESS song record (0x134 bytes) from the game executable, for use as a record.bin template.

  python3 dump_record.py MEGAMIX_BASE 2 record_2.bin        (2 = All My Turn)

MEGAMIX_BASE is the built folder that holds EMPRESS/SLPM_552.21.
"""
import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from sirius_probe import EXE, EXE_TABLE, RECORD  # noqa: E402


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('megamix', type=Path)
    p.add_argument('song_id', type=int)
    p.add_argument('output', type=Path)
    a = p.parse_args(argv)
    exe = (a.megamix / EXE).read_bytes()
    at = EXE_TABLE + a.song_id * RECORD
    if a.song_id < 0 or at + RECORD > len(exe):
        sys.exit('song id %d is outside the record table' % a.song_id)
    a.output.write_bytes(exe[at:at + RECORD])
    print('wrote %s (%d bytes, song %d)' % (a.output, RECORD, a.song_id))


if __name__ == '__main__':
    main()
