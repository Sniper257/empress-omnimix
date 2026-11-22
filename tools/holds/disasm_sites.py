#!/usr/bin/env python3
"""Disassemble the EMPRESS code the hold module has to hook. Maintainer tool.

  pip install capstone
  python3 tools/holds/disasm_sites.py MEGAMIX/EMPRESS/SLPM_552.21 -o holds-sites.txt

Checks the executable is the retail EMPRESS one, confirms the original words the
tools/cn prototype expected at its hook sites, and writes for every site:
the surrounding code, each called function up to its return, and every other
call site of that function. The engine layer (holds_empress.c) is written from
this output; nothing in it is assumed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

HERE = Path(__file__).resolve().parent
EXE_SHA256 = '0daef4193e51ed3c49f49db9d84aa673d9aab783d004bec4fa6a02f6d520914f'   # tools/release/retail_discs.json

# tools/cn/build.py (EMPRESS): jal sites -> original target, plus the patched sink words
SITES = {
    0x1e5cb8: ('hook_frame', 'play loop'),
    0x1e7d14: ('hook_spawn', 'note spawner'),
    0x1e89c4: ('hook_bss', 'key judge'),
    0x1e5f84: ('hook_reset', 'play init (now also input_start_hook)'),
}
ORIG = {0x1e5cb8: 0x0c000000 | 0x1e6bb0 >> 2, 0x1e7d14: 0x0c000000 | 0x1ebf90 >> 2,
        0x1e89c4: 0x0c000000 | 0x1e8d50 >> 2, 0x1e5f84: 0x0c000000 | 0x1ed660 >> 2,
        0x1ed5a0: 0x8f8688f0, 0x1ed5a4: 0x3c07007a}
# hooks the current runtime already places in the same code (tools/offdisc/patch_catalog.py)
SHARED = {0x1ed5a0: 'fs_ring_hook (prototype SINK)', 0x1e88d4: 'fs_key_hook', 0x1e8fb8: 'fs_scratch_hook',
          0x1fe450: 'fs_play_hook', 0x1f6900: 'fs_counts_single_hook', 0x1f6c34: 'fs_counts_battle_hook'}


class Image:
    def __init__(self, data):
        if data[:4] != b'\x7fELF' or data[4] != 1 or data[5] != 1:
            raise SystemExit('Not a 32-bit little-endian ELF')
        phoff, = struct.unpack_from('<I', data, 0x1c)
        phentsize, phnum = struct.unpack_from('<HH', data, 0x2a)
        self.data, self.segs = data, []
        for i in range(phnum):
            kind, off, va, _, filesz, memsz, flags, _ = struct.unpack_from('<8I', data, phoff + i * phentsize)
            if kind == 1:
                self.segs.append((va, off, filesz, flags))

    def word(self, va):
        for base, off, size, _ in self.segs:
            if base <= va < base + size - 3:
                return struct.unpack_from('<I', self.data, off + va - base)[0]
        return None

    def code(self):
        for base, off, size, flags in self.segs:
            if flags & 1:
                yield base, self.data[off:off + size]


def disassembler():
    try:
        import capstone
    except ImportError:
        raise SystemExit('Needs capstone: pip install capstone')
    md = capstone.Cs(capstone.CS_ARCH_MIPS, capstone.CS_MODE_MIPS64 | capstone.CS_MODE_LITTLE_ENDIAN)
    md.skipdata = True
    return md


def lines(md, img, start, count):
    out = []
    for i in range(count):
        va = start + 4 * i
        w = img.word(va)
        if w is None:
            break
        ins = next(md.disasm(struct.pack('<I', w), va), None)
        text = (ins.mnemonic + ' ' + ins.op_str).strip() if ins and ins.mnemonic != '.byte' else '.word 0x%08x' % w
        out.append((va, w, text))
    return out


def function(md, img, start, limit=600):
    """From start to the first jr $ra (+ delay slot)."""
    rows = lines(md, img, start, limit)
    for i, (_, _, text) in enumerate(rows):
        if text.startswith('jr') and text.endswith('ra'):
            return rows[:i + 2]
    return rows


def callers(img, target):
    want = 0x0c000000 | (target >> 2) & 0x3ffffff
    hits = []
    for base, blob in img.code():
        for at in range(0, len(blob) - 3, 4):
            if struct.unpack_from('<I', blob, at)[0] == want:
                hits.append(base + at)
    return hits


def fmt(rows, mark=()):
    return '\n'.join('%s %08x  %08x  %s' % ('>' if va in mark else ' ', va, w, t) for va, w, t in rows)


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('exe', type=Path)
    p.add_argument('-o', '--output', type=Path, default=Path('holds-sites.txt'))
    p.add_argument('--context', type=int, default=48, help='instructions shown before and after each site')
    p.add_argument('--extra', type=lambda s: int(s, 16), action='append', default=[], help='more functions to dump (hex VA)')
    a = p.parse_args(argv)
    data = a.exe.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    img, md = Image(data), disassembler()
    out = ['# EMPRESS hold-module sites', '# exe sha256 %s (%s)' % (digest, 'retail EMPRESS' if digest == EXE_SHA256 else 'NOT the retail EMPRESS executable'), '']
    out.append('## Original words expected by tools/cn/build.py')
    for va, want in sorted(ORIG.items()):
        got = img.word(va)
        out.append('%08x  want %08x  got %s  %s' % (va, want, '%08x' % got if got is not None else 'none', 'ok' if got == want else 'DIFFERENT'))
    out.append('')
    targets = {}
    for va, (name, what) in sorted(SITES.items()):
        out.append('## %s at %08x (%s)' % (name, va, what))
        out.append(fmt(lines(md, img, va - 4 * a.context, 2 * a.context + 1), {va}))
        w = img.word(va)
        if w is not None and w >> 26 == 3:
            targets[(w & 0x3ffffff) << 2 | (va & 0xf0000000)] = name
        out.append('')
    for va, name in sorted(SHARED.items()):
        out.append('## already hooked by the runtime: %s at %08x' % (name, va))
        out.append(fmt(lines(md, img, va - 64, 33), {va}))
        out.append('')
    for target in sorted(set(targets) | set(a.extra)):
        rows = function(md, img, target)
        out.append('## function %08x (called by %s), %d instructions' % (target, targets.get(target, '--extra'), len(rows)))
        out.append('# call sites: ' + ' '.join('%08x' % c for c in callers(img, target)))
        out.append(fmt(rows))
        out.append('')
    a.output.write_text('\n'.join(out) + '\n', encoding='utf-8')
    print('Wrote', a.output, '(' + ('retail EMPRESS' if digest == EXE_SHA256 else 'unexpected executable') + ')')


if __name__ == '__main__':
    main()
