"""Human and machine-readable documentation of the exact generated patch list."""
from pathlib import Path
import json

DESCRIPTIONS = {
    'storage': 'Map native disc reads and module paths to this installation; original judgement timing stays unchanged.',
    'memory': 'Reserve runtime/tables outside the native heap and manage imported graphics within that budget.',
    'assets': 'Load large imported BGA graphics and retire them at native scene boundaries; keep source video and audio.',
    'saves': 'Connect native SAVE/LOAD and autosave to recoverable imported-score checkpoints; native AUTOSAVE remains the enable switch.',
    'audio': 'Dispatch the existing sound worker before the frame wait on queued PLAY input requests; one wake per loop, native frame wait, input/judgement, IOP timer and stream clocks retained.',
    'fast_slow': 'Optional native-judgement FAST/SLOW display and per-player counters; shares retry/reset ownership with GAS; disabled by default.',
    'gauge': 'Optional FREE-only HARD to EASY auto shift, NORMAL promotion and SELECT result views; disabled by default.',
    'skin': 'Independent skin controls and source-frame hooks; native frame and settings are the default.',
    'songlist': 'Stable chart identities, windowed aliases, folders, counters, duplicate visibility and sprite binding.',
    'resources': 'Resolve note counts and graph targets through the catalog; bound native optional-resource readers/writers and include distinct installed charts in DJ POINT aggregates.',
    'retry': 'FREE exit/failure tap START draws a new arrangement and SELECT retains the realized lane maps through the native I/O-draining retry path. RESULT Continue with held SELECT retains maps; normal result saving and native held exit remain intact.',
    'unlock': 'Expose installed songs/charts and all native CUSTOMIZE items without writing unlock bits to the memory card; native category/item validation, selections and NEW flags remain intact.',
    'diagnostics': 'Optional input sampling; not enabled in this base build.',
    'video': 'Optional component 480p modes; original 480i is the default. RGB is not hardware-qualified.'}


def write(runtime, output, builder, input_hash, level_changes, *, bundled_custom_art=True):
    records = []
    for line in Path(runtime).read_text().splitlines():
        p = line.split()
        if not p or p[0] not in ('W', 'J', 'P', 'S'):
            continue
        va = int(p[1], 16)
        symbol = builder.HOOKS.get(va, (None, None, ''))[2] or ''
        if not symbol and va in builder.HOOKS:
            symbol = builder.HOOKS.get(va-4, (None, None, ''))[2] or ''
        audio_sites={0x1519e0:'ad_frame_wait_hook',0x1519f0:'ad_wake_hook',**{a:'ad_request_hook' for a in (0x1e8944,0x1e9028,0x1e9118,0x1e91b8)}}
        fs_sites={0x1ed5a0:'fs_ring_hook',0x1ed5a4:'fs_ring_hook',0x1fe450:'fs_play_hook',0x1fe454:'fs_play_hook',0x17c570:'fs_result_hook',0x17c574:'fs_result_hook',0x1e88d4:'fs_key_hook',0x1e8fb8:'fs_scratch_hook',0x13bbf0:'fs_input_hook',0x13bbf4:'fs_input_hook',0x1f6900:'fs_counts_single_hook',0x1f6c34:'fs_counts_battle_hook'}
        symbol=audio_sites.get(va,fs_sites.get(va,symbol))
        group = 'songlist'
        if p[0] == 'S' or va in builder.JALS or va in builder.USB_WORDS:
            group = 'storage'
        elif symbol.startswith('save_'):
            group = 'saves'
        elif symbol.startswith('qr_'):
            group = 'retry'
        elif symbol.startswith('ad_'):
            group = 'audio'
        elif symbol.startswith('fs_'):
            group = 'fast_slow'
        elif symbol.startswith(('gsm_',)):
            group = 'gauge'
        elif symbol.startswith('asset_'):
            group = 'assets'
        elif symbol.startswith(('resource_', 'ranking_')):
            group = 'resources'
        elif symbol.startswith(('eighth_', 'note_', 'tint_', 'beam_', 'frame_', 'judge_')):
            group = 'skin'
        elif va in (0x1001d0, 0x152dc4, 0x2360f4):
            group = 'memory'
        elif va in (0x19cdd0, 0x19cdd4, 0x196ad4):
            group = 'unlock'
        elif p[0] == 'P':
            group = 'diagnostics'
        record = dict(kind=p[0], address=p[1], group=group,
                      implementation='tools/offdisc/build_vdisc.py', symbol=symbol, recipe=line)
        if p[0] in ('W', 'J', 'P'):
            record.update(expected=p[2], replacement=p[3])
        else:
            sva = next(s for lui, addiu, s in builder.MODULES if lui == va)
            record.update(addiu=p[2], path=p[3],
                          expected_lui=f'{0x3c040000 | ((sva+0x8000)>>16):08x}',
                          expected_addiu=f'{0x24840000 | (sva&0xffff):08x}')
        records.append(record)
    # Optional video patches are separate, checked only if selected in the launcher.
    root = Path(output)
    for path in sorted((root/'mods').glob('480p*/mod.txt')):
        for line in path.read_text().splitlines():
            p = line.split()
            if p and p[0] == 'W':
                records.append(dict(kind='W', address=p[1], expected=p[2], replacement=p[3],
                                    group='video', optional=str(path.relative_to(root)), recipe=line))
    data = dict(schema=1, input_exe_sha256=input_hash,
                application='Runtime instructions are validated and applied to EE RAM at launcher START, not written into the input ISO.',
                rating_changes=level_changes, groups=DESCRIPTIONS, patches=records,
                limits=['USB boot and selected 2P AUTO charts passed on SCPH-50004; this is not universal device/scene qualification.',
                        'R157 Cyber allocation and earliest FHS/LIFT changes still await hardware acceptance.',
                        'This documents static recipes; relocated folder/catalog tables are also built from installed data at START.'])
    if bundled_custom_art:
        data['limits'].insert(1, 'Some large custom-explosion combinations can still exhaust a fragmented scene heap.')
    (root/'patch-catalog.json').write_text(json.dumps(data, indent=2)+'\n')
    lines = ['# Patches in this base build', '', data['application'], '',
             'Input EXE SHA256: `'+input_hash+'`.', '',
             'The input ISO is never modified. The installed EXE receives only validated rating-byte updates; the launcher applies code hooks in RAM.', '']
    for group, description in DESCRIPTIONS.items():
        matches = [r for r in records if r['group'] == group]
        if not matches:
            continue
        lines += ['## '+group.capitalize(), '', description, '',
                  '| Address | Kind | Expected → replacement/target | Hook or module |',
                  '| --- | --- | --- | --- |']
        for r in matches:
            change = r.get('expected', r.get('addiu', ''))+' → '+r.get('replacement', r.get('path', ''))
            lines.append('| `'+r['address']+'` | '+r['kind']+' | `'+change+'` | '+r.get('symbol', r.get('optional', ''))+' |')
        lines += ['']
    lines += ['## Difficulty metadata', '', f'{len(level_changes)} level-byte changes from the offline snapshot; chart notes, slot assignments and BEGINNER ratings stay unchanged.', '',
              '## Validation limits', '', *['- '+v for v in data['limits']]]
    (root/'PATCHES.md').write_text('\n'.join(lines)+'\n')
