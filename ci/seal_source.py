"""Refresh the export inventory after deliberate, reviewed source edits."""
from pathlib import Path
import hashlib,json
from source_tree import source_files
ROOT=Path(__file__).resolve().parent.parent
rows={p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest()for p in source_files(ROOT)if p.name!='source-files.json'}
(ROOT/'source-files.json').write_text(json.dumps(rows,indent=2,sort_keys=True)+'\n')
print('Sealed',len(rows),'source files')
