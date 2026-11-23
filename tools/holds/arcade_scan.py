#!/usr/bin/env python3
"""Look for judgement and hold-note constants in an arcade IIDX bm2dx.dll. Maintainer tool.

  pip install capstone
  python3 tools/holds/arcade_scan.py path/to/bm2dx.dll -o arcade-scan.txt

Works on 32-bit (SIRIUS era) and 64-bit (recent) DLLs. It reports:
  - the PE header (machine, build timestamp, sections)
  - strings about judging, charge notes, BSS/HCN and the gauge, with the code
    that uses each one
  - tables shaped like judgement windows: rising ints in frames or ms, or
    floats in ms, with the code that reads them
Code is shown as short disassembly around each reference, so the values the
hold rules need (CN/BSS release window, BSS stop grace, HCN tick spacing and
any high-BPM limit) can be followed from there. Nothing is written except the
report; the report holds addresses, small constants and short disassembly only.
"""
import argparse
import hashlib
from pathlib import Path
import re
import struct

KEYWORDS = re.compile(rb'(?i)(judg|charge|chrg|hell|hcn|bss|backspin|spin|gauge|groove|pgreat|p-great|great|good|poor|timing|window|release|long ?note|\bcn\b)')
SJIS_WORDS = ['判定', 'チャージ', 'ヘル', 'ゲージ', 'バックスピン', '離し']


class PE:
    def __init__(self, data):
        if data[:2] != b'MZ':
            raise SystemExit('Not a PE file (no MZ header)')
        pe = struct.unpack_from('<I', data, 0x3c)[0]
        if data[pe:pe + 4] != b'PE\0\0':
            raise SystemExit('Not a PE file (no PE header)')
        self.machine, nsec, self.timestamp = struct.unpack_from('<HHI', data, pe + 4)
        opt_size = struct.unpack_from('<H', data, pe + 20)[0]
        opt = pe + 24
        magic = struct.unpack_from('<H', data, opt)[0]
        self.bits = 64 if magic == 0x20b else 32
        self.base = struct.unpack_from('<Q', data, opt + 24)[0] if self.bits == 64 else struct.unpack_from('<I', data, opt + 28)[0]
        self.sections = []
        at = opt + opt_size
        for i in range(nsec):
            name, vsize, va, rsize, rptr = struct.unpack_from('<8sIIII', data, at + 40 * i)
            flags = struct.unpack_from('<I', data, at + 40 * i + 36)[0]
            self.sections.append({'name': name.rstrip(b'\0').decode('latin-1'), 'va': va, 'vsize': vsize,
                                  'raw': rptr, 'rsize': rsize, 'exec': bool(flags & 0x20000000), 'flags': flags})
        self.data = data

    def blob(self, s):
        return self.data[s['raw']:s['raw'] + min(s['rsize'], max(s['vsize'], s['rsize']))]

    def rva_of(self, off):
        for s in self.sections:
            if s['raw'] <= off < s['raw'] + s['rsize']:
                return s['va'] + off - s['raw']
        return None

    def off_of(self, rva):
        for s in self.sections:
            if s['va'] <= rva < s['va'] + s['rsize']:
                return s['raw'] + rva - s['va']
        return None

    def section_of(self, rva):
        for s in self.sections:
            if s['va'] <= rva < s['va'] + max(s['vsize'], s['rsize']):
                return s
        return None


# ------------------------------------------------------------------ references

def references(pe, targets):
    """{target rva: [code rva]} for absolute (32-bit) or RIP-relative (64-bit) operands."""
    targets = set(targets)
    out = {t: [] for t in targets}
    if not targets:
        return out
    for s in pe.sections:
        if not s['exec']:
            continue
        code = pe.blob(s)
        if pe.bits == 32:
            for t in targets:
                needle = struct.pack('<I', pe.base + t)
                at = code.find(needle)
                while at >= 0 and len(out[t]) < 64:
                    out[t].append(s['va'] + at)
                    at = code.find(needle, at + 1)
            continue
        # x64: disp32 at p refers to (p + 4 + tail) + disp, tail = bytes after the displacement (0..4)
        want = {}
        for t in targets:
            for tail in (0, 1, 2, 4):
                want[t - tail] = t
        for shift in range(4):
            usable = (len(code) - shift) // 4 * 4
            base = s['va'] + shift + 4
            for j, (disp,) in enumerate(struct.iter_unpack('<i', code[shift:shift + usable])):
                hit = want.get(base + 4 * j + disp)
                if hit is not None and len(out[hit]) < 64:
                    out[hit].append(s['va'] + shift + 4 * j)
    for t in out:
        out[t] = sorted(set(out[t]))
    return out


def disassembler(pe):
    try:
        import capstone
    except ImportError:
        raise SystemExit('Needs capstone: pip install capstone')
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64 if pe.bits == 64 else capstone.CS_MODE_32)
    md.skipdata = True
    return md


def around(pe, md, rva, before=14, after=10):
    """Disassembly around a referencing location, re-synchronised so one instruction covers rva."""
    off = pe.off_of(rva)
    if off is None:
        return []
    best = None
    for back in range(64, 0, -1):
        start = off - back
        if start < 0 or pe.rva_of(start) is None:
            continue
        rows = list(md.disasm(pe.data[start:off + 96], pe.base + pe.rva_of(start)))
        target = pe.base + rva
        for k, ins in enumerate(rows):
            if ins.address <= target < ins.address + ins.size:
                if best is None or k > best[1]:
                    best = (rows, k)
                break
    if not best:
        return []
    rows, k = best
    return [(ins.address, ins.bytes.hex(), ins.mnemonic + ' ' + ins.op_str, i == k)
            for i, ins in enumerate(rows[max(0, k - before):k + after + 1], max(0, k - before))]


def fmt_code(rows):
    return '\n'.join('%s %08x  %-20s %s' % ('>' if mark else ' ', a, b[:20], t) for a, b, t, mark in rows)


# ------------------------------------------------------------------- strings

def strings(pe, limit=120):
    out = []
    for s in pe.sections:
        if s['exec']:
            continue
        blob = pe.blob(s)
        for m in re.finditer(rb'[\x20-\x7e]{4,80}\x00', blob):
            text = m.group()[:-1]
            if KEYWORDS.search(text):
                out.append((s['va'] + m.start(), text.decode('latin-1')))
        for word in SJIS_WORDS:
            enc = word.encode('cp932')
            at = blob.find(enc)
            while at >= 0:
                start = at
                while start > 0 and blob[start - 1] >= 0x20:
                    start -= 1
                end = blob.find(b'\0', at)
                try:
                    text = blob[start:end].decode('cp932')
                except UnicodeDecodeError:
                    text = blob[at:at + 24].decode('cp932', 'replace')
                out.append((s['va'] + start, text))
                at = blob.find(enc, end if end > at else at + 1)
    seen, uniq = set(), []
    for rva, text in sorted(out):
        if rva not in seen:
            seen.add(rva)
            uniq.append((rva, text))
    return uniq[:limit]


# ------------------------------------------------------------ window tables

def window_tables(pe, limit=40):
    """Rising tables of 4..8 values that look like judgement windows, best first."""
    found = []
    for s in pe.sections:
        if s['exec']:
            continue
        blob = pe.blob(s)
        for fmt, size, kind in (('<i', 4, 'int32'), ('<f', 4, 'float32'), ('<d', 8, 'float64'), ('<h', 2, 'int16')):
            for at in range(0, len(blob) - size * 8, size):
                vals = []
                for k in range(8):
                    v = struct.unpack_from(fmt, blob, at + size * k)[0]
                    if not (0 < v <= 500) or (vals and v <= vals[-1]):
                        break
                    if kind.startswith('float') and not (abs(v * 100 - round(v * 100)) < 1e-3):
                        break
                    vals.append(v)
                if len(vals) < 4:
                    continue
                if at >= size:   # only report the start of a run
                    prev = struct.unpack_from(fmt, blob, at - size)[0]
                    if 0 < prev < vals[0]:
                        continue
                score = 0
                if all(float(v).is_integer() for v in vals) and [int(v) for v in vals] == list(range(int(vals[0]), int(vals[0]) + len(vals))):
                    continue                          # 1,2,3,4... is not a window table
                if vals[0] in (1, 2) and len(vals) >= 4 and vals[-1] <= 60:
                    score += 2                        # frames: PGREAT 1, GREAT 2, ...
                if any(16 <= v <= 17.5 for v in vals) and any(33 <= v <= 34 for v in vals):
                    score += 3                        # ms: 16.67 / 33.33
                if kind.startswith('float') and any(abs(v - 16.666) < 0.01 or abs(v - 33.333) < 0.01 for v in vals):
                    score += 2
                if score:
                    found.append((score, s['va'] + at, kind, vals))
    found.sort(key=lambda r: (-r[0], r[1]))
    return found[:limit]


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('dll', type=Path)
    p.add_argument('-o', '--output', type=Path, default=Path('arcade-scan.txt'))
    p.add_argument('--refs', type=int, default=3, help='code references shown per string/table')
    p.add_argument('--near', type=lambda s: int(s, 16), action='append', default=[], help='also disassemble around this RVA (hex)')
    a = p.parse_args(argv)
    data = a.dll.read_bytes()
    pe = PE(data)
    md = disassembler(pe)
    out = ['# arcade scan of %s' % a.dll.name, '# sha256 %s, %d bytes' % (hashlib.sha256(data).hexdigest(), len(data)),
           '# machine %04x, %d-bit, image base %x, timestamp %d' % (pe.machine, pe.bits, pe.base, pe.timestamp), '']
    out.append('## sections')
    for s in pe.sections:
        out.append('  %-8s va %08x vsize %08x raw %08x rsize %08x %s' % (s['name'], s['va'], s['vsize'], s['raw'], s['rsize'], 'exec' if s['exec'] else ''))
    if not any(s['exec'] and s['rsize'] for s in pe.sections):
        out.append('  no executable section with data: the DLL is probably packed or encrypted')
    out.append('')
    found = strings(pe)
    tables = window_tables(pe)
    refs = references(pe, [r for r, _ in found] + [t[1] for t in tables])
    out.append('## strings about judging / holds / gauge (%d)' % len(found))
    for rva, text in found:
        hits = refs.get(rva, [])
        out.append('### %08x %r  (%d code refs)' % (rva, text, len(hits)))
        for h in hits[:a.refs]:
            out.append(fmt_code(around(pe, md, h)))
            out.append('')
    out.append('## window-shaped tables (best first)')
    for score, rva, kind, vals in tables:
        hits = refs.get(rva, [])
        shown = ', '.join(('%g' % v) for v in vals)
        out.append('### %08x %s [%s] score %d (%d code refs)' % (rva, kind, shown, score, len(hits)))
        for h in hits[:a.refs]:
            out.append(fmt_code(around(pe, md, h)))
            out.append('')
    for rva in a.near:
        out.append('## near %08x' % rva)
        out.append(fmt_code(around(pe, md, rva, 40, 40)))
        out.append('')
    a.output.write_text('\n'.join(out) + '\n', encoding='utf-8')
    print('Wrote', a.output, '(%d strings, %d tables)' % (len(found), len(tables)))


if __name__ == '__main__':
    main()
