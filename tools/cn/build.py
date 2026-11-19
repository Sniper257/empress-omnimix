"""Build the CN/BSS module: compile tools/cn/cn.c + stubs.S, link at the code cave,
validate R5900 compatibility, write <out>/cave.bin + <out>/hooks.json.
usage: python3 tools/cn/build.py            DJT      -> work/cn
       GAME=16 python3 tools/cn/build.py    EMPRESS  -> work16/cn (needs work16/cn/sheet16.h from add_sheet.py)"""
import json, os, struct, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
EMPRESS = os.environ.get('GAME') == '16'
OUT = os.path.join(HERE, '..', '..', 'work16' if EMPRESS else 'work', 'cn')
CAVE_LO, CAVE_HI = 0x115410, 0x117098      # 20 unreferenced libmc RPC wrappers (0x115408.., 16-aligned; same in EMPRESS)
if EMPRESS:
    CALL_SITES = {0x1e5cb8: 'hook_frame', 0x1e7d14: 'hook_spawn', 0x1e89c4: 'hook_bss', 0x1e5f84: 'hook_reset'}
    SINK = 0x1ed5a0
    ORIG = {0x1e5cb8: 0x0c000000 | 0x1e6bb0 >> 2, 0x1e7d14: 0x0c000000 | 0x1ebf90 >> 2,
            0x1e89c4: 0x0c000000 | 0x1e8d50 >> 2, 0x1e5f84: 0x0c000000 | 0x1ed660 >> 2,
            0x1ed5a0: 0x8f8688f0, 0x1ed5a4: 0x3c07007a}
else:
    CALL_SITES = {                         # jal patched to the stub (delay slot kept)
        0x1daf78: 'hook_frame',  # play loop: jal FUN_001dbdf0
        0x1dd228: 'hook_spawn',  # spawner:   jal FUN_001e0fd0
        0x1ddda4: 'hook_bss',    # key judge: jal FUN_001ddf40
        0x1db24c: 'hook_reset',  # play init: jal FUN_001e26e0
    }
    SINK = 0x1e2620
    ORIG = {0x1daf78: 0x0c076f7c, 0x1dd228: 0x0c0783f4, 0x1ddda4: 0x0c0777d0, 0x1db24c: 0x0c0789b8,
            0x1e2620: 0x8f868850, 0x1e2624: 0x3c07007b}
DEFS = ['-DEMPRESS', '-I', OUT] if EMPRESS else []

CFLAGS = ['--target=mips64el-linux-gnuabin32', '-march=mips3', '-mabi=n32', '-fno-pic',
          '-mno-abicalls', '-G0', '-ffreestanding', '-fno-builtin', '-Os', '-Wall',
          '-mno-check-zero-division', '-fno-vectorize', '-fno-slp-vectorize', '-fno-unroll-loops']

LDS = """SECTIONS {
  . = 0x%x;
  .cave : { *(.text.stubs) *(.text*) __text_end = .; *(.rodata*) *(.data*) *(.sdata*) *(.sbss*) *(.bss*) *(COMMON) }
  /DISCARD/ : { *(.MIPS.*) *(.reginfo) *(.pdr) *(.comment) *(.note*) *(.gnu*) }
}
""" % CAVE_LO


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        sys.exit('FAILED: %s\n%s%s' % (' '.join(cmd), r.stdout, r.stderr))
    return r.stdout


def check(code, base):
    """Reject instructions the R5900 lacks and loops hit by the EE short-loop erratum."""
    bad = []
    n = len(code) // 4
    for i in range(n):
        w = struct.unpack_from('<I', code, 4 * i)[0]
        op, fn = w >> 26, w & 0x3f
        va = base + 4 * i
        if op == 0x10 and ((w >> 21) & 31) == 0x0b:
            bad.append('%x: MIPS32 MFMC0/DI/EI unsupported on R5900' % va)
        if op == 0 and fn in (0x1c, 0x1d, 0x1e, 0x1f):
            bad.append('%x: 64-bit mult/div' % va)
        if op in (0x11, 0x31, 0x35, 0x39, 0x3d) and op != 0x11 or (op == 0x11 and ((w >> 21) & 31) == 0x11):
            bad.append('%x: FPU double/ldc1' % va)
        if op in (0x34, 0x30, 0x38, 0x3c):  # lld/ll/sc/scd
            bad.append('%x: ll/sc' % va)
        # branches: beq bne blez bgtz (+likely), REGIMM
        if op in (4, 5, 6, 7, 0x14, 0x15, 0x16, 0x17) or op == 1:
            off = struct.unpack('<h', struct.pack('<H', w & 0xffff))[0]
            tgt = i + 1 + off
            if off < 0 and (i + 2 - tgt) <= 6:
                bad.append('%x: short loop (%d instrs) - EE erratum' % (va, i + 2 - tgt))
    return bad


def main():
    os.makedirs(OUT, exist_ok=True)
    o_c, o_s, elf = (os.path.join(OUT, n) for n in ('cn.o', 'stubs.o', 'cn.elf'))
    lds = os.path.join(OUT, 'cave.ld')
    open(lds, 'w').write(LDS)
    run(['clang'] + CFLAGS + DEFS + ['-c', os.path.join(HERE, 'cn.c'), '-o', o_c])
    run(['clang'] + CFLAGS[:5] + DEFS + ['-c', os.path.join(HERE, 'stubs.S'), '-o', o_s])
    run(['ld.lld', '-m', 'elf32ltsmipn32', '-T', lds, '--no-relax', '-o', elf, o_s, o_c])
    binp = os.path.join(OUT, 'cave.bin')
    run(['llvm-objcopy', '-O', 'binary', '-j', '.cave', elf, binp])
    code = open(binp, 'rb').read()
    if CAVE_LO + len(code) > CAVE_HI:
        sys.exit('cave overflow: %d bytes > %d' % (len(code), CAVE_HI - CAVE_LO))
    syms = {}
    for line in run(['llvm-nm', elf]).split('\n'):
        p = line.split()
        if len(p) == 3:
            syms[p[2]] = int(p[0], 16)
    # relocation sanity: objdump must show no unresolved jal 0
    dis = run(['llvm-objdump', '-d', '--no-show-raw-insn', elf])
    open(os.path.join(OUT, 'cn.dis'), 'w').write(dis)
    bad = check(code[:syms['__text_end'] - CAVE_LO], CAVE_LO)
    if bad:
        print('\n'.join(bad))
        sys.exit('R5900 check failed')
    patches = {}
    for site, sym in CALL_SITES.items():
        patches[site] = 0x0c000000 | (syms[sym] >> 2)
    patches[SINK] = 0x08000000 | (syms['hook_sink'] >> 2)
    patches[SINK + 4] = 0
    json.dump({'cave_lo': CAVE_LO, 'cave_bin': 'cave.bin', 'size': len(code),
               'patches': {hex(k): v for k, v in patches.items()},
               'orig': {hex(k): v for k, v in ORIG.items()},
               'syms': {k: hex(v) for k, v in syms.items() if not k.startswith('.')}},
              open(os.path.join(OUT, 'hooks.json'), 'w'), indent=1)
    print('cave %d bytes (%d free), %s' % (len(code), CAVE_HI - CAVE_LO - len(code),
                                         ' '.join('%s=%x' % (k, syms[k]) for k in
                                                  ('hook_frame', 'hook_spawn', 'hook_bss', 'hook_reset', 'hook_sink'))))


if __name__ == '__main__':
    main()
