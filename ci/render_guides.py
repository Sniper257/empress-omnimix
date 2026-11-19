"""Render the release TXT guides as Markdown for GitHub."""
import argparse
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GUIDES = {
    'README.txt': 'guide.md',
    'README-ja.txt': 'guide-ja.md',
    'README-ko.txt': 'guide-ko.md',
}


def render(text, source):
    lines = text.lstrip('\ufeff').splitlines()
    out = [f'<!-- Generated from {source}; run python ci/render_guides.py. -->', '']
    i = 0
    while i < len(lines):
        line = lines[i]
        if line and i + 1 < len(lines) and re.fullmatch(r'=+|-+', lines[i + 1]):
            level = '#' if lines[i + 1].startswith('=') else '##'
            out.extend([f'{level} {line}', ''])
            i += 2
        elif line.startswith('    '):
            code = []
            while i < len(lines) and lines[i].startswith('    '):
                code.append(lines[i][4:])
                i += 1
            language = 'sh' if code[0].startswith(('python3 ', 'py ', '.venv/')) else 'text'
            out.extend([f'```{language}', *code, '```'])
        else:
            out.append(line)
            i += 1
    return re.sub(r'\n{3,}', '\n\n', '\n'.join(out)).rstrip() + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true', help='Reject stale Markdown guides')
    args = parser.parse_args()
    stale = []
    for source, target in GUIDES.items():
        path = ROOT / 'docs' / target
        rendered = render((ROOT / 'tools/release' / source).read_text(encoding='utf-8-sig'), source)
        if args.check:
            if not path.is_file() or path.read_bytes() != rendered.encode('utf-8'):
                stale.append(str(path.relative_to(ROOT)))
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(rendered.encode('utf-8'))
    if stale:
        parser.exit(1, 'Stale guides: ' + ', '.join(stale) + '; run python ci/render_guides.py\n')
    print('Markdown guides ' + ('checked' if args.check else 'updated'))


if __name__ == '__main__':
    main()
