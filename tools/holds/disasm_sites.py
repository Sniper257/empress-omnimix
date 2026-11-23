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


# second round: whole functions around play, judging and note spawning (any address inside works)
FUNCTIONS = {
    0x1e5c50: 'play loop (tick catch-up)', 0x1e85d0: 'key judge (calls the turntable judge at 1e89c4)',
    0x1e7d14: 'note spawner (contains hook_spawn)', 0x1e6f50: 'per-lane nearest-note scan',
    0x1e9790: 'play loop call after judging', 0x1e9be0: 'play loop call per tick',
    0x1e9350: 'note update per tick', 0x13acd0: 'turntable input', 0x1ebe40: 'button state',
    0x13c6c0: 'chart event type', 0x1ebec0: 'active note list head', 0x1ebee0: 'link note',
    0x1ebf00: 'free note', 0x1ebf20: 'unlink note', 0x1f0ac0: 'called on note at its tick',
    0x1e4ab0: 'called on note at its tick', 0x205d40: 'mode flag', 0x205d50: 'mode flag (autoplay?)',
    0x205780: 'called for judge code 4', 0x140260: 'play keysound', 0x1fec30: 'side lookup',
}
GP_OFFSETS = (-0x7710, -0x7714, -0x7788, -0x7780, -0x7770, -0x77b4, -0x77b0, -0x776c, -0x7720)
ABS_DATA = {0x79e300: 'judgement ring (code)', 0x79e302: 'judgement ring (lane id)', 0x79d960: 'judge windows',
            0x79dae0: 'per-lane nearest note table', 0x79db60: 'per-lane notes-in-range count',
            0x79e2f0: 'turntable spin state', 0x79da50: 'per-side keysound slots'}


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


REG = ['zero', 'at', 'v0', 'v1', 'a0', 'a1', 'a2', 'a3', 't0', 't1', 't2', 't3', 't4', 't5', 't6', 't7',
       's0', 's1', 's2', 's3', 's4', 's5', 's6', 's7', 't8', 't9', 'k0', 'k1', 'gp', 'sp', 'fp', 'ra']


def ee_text(w):
    """R5900 instructions capstone decodes as MIPS32/MSA: LQ, SQ and the MMI group."""
    op = w >> 26
    if op in (0x1e, 0x1f):
        imm = w & 0xffff
        imm = imm - 0x10000 if imm & 0x8000 else imm
        return '%s $%s, %s($%s)' % ('lq' if op == 0x1e else 'sq', REG[w >> 16 & 31], hex(imm), REG[w >> 21 & 31])
    if op == 0x1c:
        return 'mmi.%02x $%s, $%s, $%s (fn %02x)' % (w >> 6 & 31, REG[w >> 11 & 31], REG[w >> 21 & 31], REG[w >> 16 & 31], w & 63)
    return None


def lines(md, img, start, count):
    out = []
    for i in range(count):
        va = start + 4 * i
        w = img.word(va)
        if w is None:
            break
        text = ee_text(w)
        if text is None:
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


def function_start(img, va, limit=8192):
    """Walk back to the function prologue (addiu $sp, $sp, -N) containing va."""
    for i in range(limit):
        w = img.word(va - 4 * i)
        if w is None:
            break
        if w >> 16 == 0x27bd and w & 0x8000:
            return va - 4 * i
    return va


def gp_refs(img, offsets):
    """Every load/store/addiu using $gp with one of these offsets: {offset: [(va, text)]}."""
    want = {o & 0xffff: o for o in offsets}
    out = {o: [] for o in offsets}
    for base, blob in img.code():
        for at in range(0, len(blob) - 3, 4):
            w = struct.unpack_from('<I', blob, at)[0]
            if w >> 21 & 31 == 28 and (w & 0xffff) in want and w >> 26 in (0x09, 0x20, 0x21, 0x23, 0x24, 0x25, 0x27, 0x28, 0x29, 0x2b, 0x37, 0x3f):
                out[want[w & 0xffff]].append(base + at)
    return out


def abs_refs(img, address):
    """lui/addiu (or lui + load/store) pairs that build this absolute address: [va of the lui]."""
    hi, lo = (address + 0x8000) >> 16 & 0xffff, address & 0xffff
    hits = []
    for base, blob in img.code():
        for at in range(0, len(blob) - 3, 4):
            w = struct.unpack_from('<I', blob, at)[0]
            if w >> 26 == 0x0f and w & 0xffff == hi:
                reg = w >> 16 & 31
                for k in range(1, 12):
                    if at + 4 * k + 4 > len(blob):
                        break
                    v = struct.unpack_from('<I', blob, at + 4 * k)[0]
                    if v >> 21 & 31 == reg and v & 0xffff == lo and v >> 26 in (0x09, 0x20, 0x21, 0x23, 0x24, 0x25, 0x28, 0x29, 0x2b):
                        hits.append(base + at)
                        break
    return hits


def megamix_original(data):
    """Undo the MEGAMIX build's byte edits (release-inputs/recipes/id-console.json) to check the retail hash."""
    recipe = HERE.parents[1] / 'release-inputs' / 'recipes' / 'id-console.json'
    if not recipe.is_file():
        return None
    edit = json.loads(recipe.read_text(encoding='utf-8'))['files'].get('EMPRESS/SLPM_552.21', {})
    if edit.get('length') != len(data):
        return None
    raw = bytearray(data)
    for e in edit.get('edits', []):
        after, before = bytes.fromhex(e['after']), bytes.fromhex(e['before'])
        if raw[e['at']:e['at'] + len(after)] != after:
            return None
        raw[e['at']:e['at'] + len(before)] = before
    return bytes(raw)


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
    p.add_argument('--sites-only', action='store_true', help='only the hook sites (round 1)')
    p.add_argument('--extra', type=lambda s: int(s, 16), action='append', default=[], help='more functions to dump (hex VA)')
    a = p.parse_args(argv)
    data = a.exe.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    kind = 'retail EMPRESS'
    if digest != EXE_SHA256:
        orig = megamix_original(data)
        kind = 'MEGAMIX build of retail EMPRESS' if orig and hashlib.sha256(orig).hexdigest() == EXE_SHA256 else 'NOT a known EMPRESS executable'
    img, md = Image(data), disassembler()
    out = ['# EMPRESS hold-module sites', '# exe sha256 %s (%s)' % (digest, kind), '']
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
    if not a.sites_only:
        out.append('# ===== round 2: whole functions =====')
        done = set()
        for va, what in sorted(FUNCTIONS.items()):
            start = function_start(img, va)
            if start in done:
                continue
            done.add(start)
            rows = function(md, img, start, limit=4000)
            out.append('## %s: function %08x (from %08x), %d instructions' % (what, start, va, len(rows)))
            out.append('# call sites: ' + ' '.join('%08x' % c for c in callers(img, start)))
            out.append(fmt(rows, {va}))
            out.append('')
        out.append('# ===== $gp variables: every instruction using them =====')
        for off, hits in gp_refs(img, GP_OFFSETS).items():
            out.append('## gp%s: %d uses' % (hex(off), len(hits)))
            for va in hits:
                start = function_start(img, va)
                out.append('  %08x in function %08x  %s' % (va, start, lines(md, img, va, 1)[0][2]))
            out.append('')
        out.append('# ===== absolute data: code building these addresses =====')
        for address, what in sorted(ABS_DATA.items()):
            hits = abs_refs(img, address)
            out.append('## %08x %s: %d references' % (address, what, len(hits)))
            out.append('  ' + ' '.join('%08x(fn %08x)' % (h, function_start(img, h)) for h in hits))
            out.append('')
    a.output.write_text('\n'.join(out) + '\n', encoding='utf-8')
    print('Wrote', a.output, '(' + kind + ')')


if __name__ == '__main__':
    main()
