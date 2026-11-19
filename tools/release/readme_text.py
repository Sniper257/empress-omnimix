"""Maintainer-only README export and Notepad-friendly release guides."""
import re
from pathlib import Path

GUIDE_NAMES = ('README.txt', 'README-ja.txt', 'README-ko.txt')


def notepad_bytes(text):
    """UTF-8 BOM makes Japanese/Korean unambiguous in older Windows Notepad."""
    text = text.lstrip('\ufeff').replace('\r\n', '\n').replace('\r', '\n')
    text = text.replace('\u2014', '-')
    return b'\xef\xbb\xbf' + text.replace('\n', '\r\n').encode('utf-8')


def guides(directory):
    directory = Path(directory)
    return {name: notepad_bytes((directory / name).read_text(encoding='utf-8-sig'))
            for name in GUIDE_NAMES}


def render(text):
    """Legacy Markdown-to-text export; current guides are authored as plain text."""
    text=re.sub(r'\[([^\]]+)\]\((https?://[^)]+)\)',r'\1 (\2)',text)
    lines=[];code=False
    for line in text.splitlines():
        if line.startswith('```'):
            code=not code
            continue
        if line.startswith('#'):
            label=line.lstrip('#').strip()
            lines.extend([label,'='*len(label) if line.startswith('# ') else '-'*len(label)])
            continue
        if not code:line=line.replace('**','').replace('`','')
        lines.append('    '+line if code else line)
    return '\n'.join(lines)+'\n'
