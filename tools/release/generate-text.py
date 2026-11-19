#!/usr/bin/env python3
"""Generate native/formatted MEGAMIX text assets without a PS2 SDK.

Input is a reviewed text-job JSON plan, not a game ISO detection frontend.
Default rendering needs only Python3.10+. Optional --fonts compiles explicit
local font paths with Pillow/fontTools; it does not copy the font files.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import tempfile
from text_assets import Generator,output_name
from text_raster import Atlas


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('plan',type=Path);p.add_argument('output',type=Path)
    p.add_argument('--atlas',type=Path,default=Path(__file__).with_name('text-fonts')/'open-v1.mga')
    p.add_argument('--iso',type=Path);p.add_argument('--base-iso',type=Path)
    p.add_argument('--fonts',type=Path,help='Explicit per-role local font profile; requires Pillow/fontTools')
    a=p.parse_args();stage=None
    try:
        plan=json.loads(a.plan.read_text(encoding='utf-8'))
        if plan.get('format')!=1 or not isinstance(plan.get('jobs'),dict) or not plan['jobs']:raise ValueError('Unsupported text plan')
        if a.output.exists():raise ValueError('Output already exists')
        output=a.output.absolute();output.parent.mkdir(parents=True,exist_ok=True)
        stage=Path(tempfile.mkdtemp(prefix=output.name+'.building-',dir=output.parent))
        atlas_path=a.atlas
        if a.fonts:
            from prepare_text_fonts import compile_profile
            corpus=[]
            for row in plan['jobs'].values():
                corpus.extend(row.get('texts',{}).values())
                if 'text'in row:corpus.append(row['text'])
            cp=stage/'corpus.json';cp.write_text(json.dumps(corpus,ensure_ascii=False),encoding='utf-8');atlas_path=stage/'private-fonts.mga'
            compile_profile(a.fonts,cp,atlas_path,a.atlas);cp.unlink()
        atlas_bytes=atlas_path.read_bytes();atlas=Atlas(atlas_bytes)
        font_hash=hashlib.sha256(atlas_bytes).hexdigest()
        if not a.fonts and plan.get('atlas_sha256') and plan['atlas_sha256']!=font_hash:raise ValueError('Text plan requires a different font atlas')
        inputs={'iso':a.iso,'base':a.base_iso};handles={}
        def read(which,at,n):
            if inputs[which] is None:raise ValueError('This text job requires '+('--base-iso' if which=='base' else '--iso'))
            if which not in handles:handles[which]=inputs[which].open('rb')
            f=handles[which]
            if at+n>os.fstat(f.fileno()).st_size:raise ValueError('Text extent exceeds ISO')
            f.seek(at);return f.read(n)
        generator=Generator(atlas,read);files={};folded=set()
        try:
            for name,spec in sorted(plan['jobs'].items()):
                rel=output_name(name)
                if name.casefold() in folded or name in ('text-assets.json','private-fonts.mga'):raise ValueError('Text output collision')
                folded.add(name.casefold());data=generator.generate(spec);sha=hashlib.sha256(data).hexdigest()
                if not a.fonts and spec.get('expected') and spec['expected']!={'bytes':len(data),'sha256':sha}:raise ValueError('Generated text hash differs: '+name)
                dest=stage/rel;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(data)
                files[name]={'bytes':len(data),'sha256':sha}
        finally:
            for f in handles.values():f.close()
        receipt=dict(format=1,profile=atlas.info['profile'],atlas_sha256=font_hash,files=files,scope='Graphics only; chart/score identities are unchanged')
        (stage/'text-assets.json').write_text(json.dumps(receipt,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        # Keep private raster font coverage only when explicitly requested.
        if a.fonts:atlas_path.unlink()
        if output.exists():raise ValueError('Output appeared while generating')
        stage.rename(output);stage=None
        print(f'Generated {len(files)} text assets: {output}')
    except (ValueError,OSError,KeyError,TypeError) as e:
        p.exit(2,str(e)+'\n')
    finally:
        if stage is not None:shutil.rmtree(stage)
if __name__=='__main__':main()
