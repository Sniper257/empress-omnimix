"""Reviewed source files, excluding local build products and Git internals."""
from pathlib import Path
import os

# These roots match generated/local directories in the repository .gitignore.
LOCAL_ROOTS = {'dist', '.build-dependencies', 'build', 'work', '.venv', 'venv', '.pytest_cache'}
INTERNAL = {'.git', '__pycache__'}

def source_files(root):
    root = Path(root)
    files = []
    for directory, dirs, names in os.walk(root, followlinks=False):
        folder = Path(directory)
        dirs[:] = sorted(n for n in dirs if n not in INTERNAL and
                         not (folder == root and n in LOCAL_ROOTS))
        for name in dirs:
            if (folder / name).is_symlink():
                raise ValueError('Symlink in public source: ' + str((folder / name).relative_to(root)))
        for name in sorted(names):
            if name in INTERNAL or name.endswith(('.pyc', '.pyo')):
                continue
            path = folder / name
            if path.is_symlink():
                raise ValueError('Symlink in public source: ' + str(path.relative_to(root)))
            if path.is_file():
                files.append(path)
    return sorted(files)
