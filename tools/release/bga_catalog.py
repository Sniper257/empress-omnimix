"""Maintainer-only source-owned BGA recipe annotation for fixed supported discs."""
from pathlib import Path
import hashlib,json,copy

def attach(plan,catalog=None):
    raw=json.loads(Path(catalog or Path(__file__).with_suffix('.json')).read_text())
    if raw.get('format')!=1:raise ValueError('Unsupported BGA catalog')
    changed=0
    for disc in plan['discs']:
        candidates=raw['sources'].get(disc['source'],{})
        if not candidates:continue
        for tree in disc['targets'].values():
            for row in tree['files'].values():
                if 'derive'not in row and not row.get('generated','').startswith('Pregenerated converted game graphics'):continue
                sha=hashlib.sha256(Path(row['path']).read_bytes()).hexdigest()
                if sha in candidates:
                    row['derive']=copy.deepcopy(candidates[sha]);changed+=1
    return changed
