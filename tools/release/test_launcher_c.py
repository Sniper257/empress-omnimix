"""Launcher C tests, built 32-bit on the host (the launcher keeps pointers in u32 fields).
Skipped where no 32-bit C compiler is available."""
import shutil, subprocess, tempfile, unittest
from pathlib import Path

OFFDISC = Path(__file__).resolve().parent.parent / 'offdisc'
LAUNCHER = OFFDISC / 'launcher'


def _cc():
    cc = shutil.which('gcc') or shutil.which('cc')
    if not cc:
        return None
    with tempfile.TemporaryDirectory() as d:
        src = Path(d) / 'm.c'
        src.write_text('int main(void){return sizeof(void*)!=4;}\n')
        r = subprocess.run([cc, '-m32', str(src), '-o', str(Path(d) / 'm')], capture_output=True)
        if r.returncode or subprocess.run([str(Path(d) / 'm')]).returncode:
            return None
    return cc


class LauncherC(unittest.TestCase):
    def run_test(self, name):
        cc = _cc()
        if not cc:
            self.skipTest('no 32-bit C compiler')
        with tempfile.TemporaryDirectory() as d:
            out = Path(d) / name
            r = subprocess.run([cc, '-m32', '-std=gnu99', '-DHOST_TEST', '-w', '-I' + str(LAUNCHER), '-I' + str(OFFDISC),
                                '-I' + str(OFFDISC / 'usb_hid'), str(LAUNCHER / (name + '.c')), str(LAUNCHER / 'cont.c'),
                                '-o', str(out)], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr[-2000:])
            r = subprocess.run([str(out)], capture_output=True, text=True, cwd=d)
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)

    def test_share_runs(self):
        self.run_test('test_share_runs')


if __name__ == '__main__':
    unittest.main()
