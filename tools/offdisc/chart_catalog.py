"""Stable v1 metadata for the reviewed chart catalog, without game assets.

Emit the launcher's text catalog: python3 chart_catalog.py OUTPUT
The registry is intentionally independent of enabled games and installation order.
"""
import hashlib
import json
from pathlib import Path
import sys

REGISTRY = Path(__file__).with_suffix('.json')


def uid(kind, key):
    return hashlib.sha256(('empress-megamix/identity/v1/'+kind+'/'+key).encode()).digest()[:16]


def load(path=REGISTRY):
    data = json.loads(Path(path).read_text())
    assert data['schema'] == 1
    keys = {f"{r['source']}:{r['sid']}:{ch}" for r in data['sources'] for ch in r['charts']}
    assert len({(r['source'], r['sid']) for r in data['sources']}) == len(data['sources'])
    for a, b in data['aliases'].items():
        assert a in keys and b in keys, (a, b)
    rows = {(r['source'], r['sid']): r for r in data['sources']}
    for key,family in data.get('listing_families',{}).items():
        source,sid=key.rsplit(':',1)
        assert (source,int(sid)) in rows and family in {r['family'] for r in rows.values()}
    def display_family(source,sid):
        return data.get('listing_families',{}).get(source+':'+str(sid),rows[source,sid]['family'])
    for a, b in data.get('listing_groups', {}).items():
        assert a in keys and b in keys, (a, b)
        sa, ia, ca = a.rsplit(':', 2)
        sb, ib, cb = b.rsplit(':', 2)
        assert ca[:2] == cb[:2] and display_family(sa,int(ia)) == display_family(sb,int(ib))
    for key,group in data.get('reviewed_groups',{}).items():
        assert key in keys and isinstance(group,str)and group.startswith('audit/')and len(group)==70
    for key,value in data.get('display_preferences',{}).items():
        assert key in keys and type(value)is int and 0<=value<=127
    owners=set()
    for rule in data.get('coalesces',[]):
        owner,donor=rule['owner'],rule['donor'];a,ai=owner.rsplit(':',1);b,bi=donor.rsplit(':',1)
        assert owner!=donor and owner not in owners and rule['movie']=='same';owners.add(owner)
        assert rows[a,int(ai)]['family']==rows[b,int(bi)]['family']
        assert not set(rule['borrow'])&set(rule['shared'])
        for ch in rule['borrow']:
            assert ch in ('SPN','SPH','SPA','SPB','DPN','DPH','DPA','DPB')
            assert owner+':'+ch in keys and donor+':'+ch in keys
            assert canonical(data,owner+':'+ch)==canonical(data,donor+':'+ch)
        for ch in rule['shared']:assert listing_key(data,owner+':'+ch)==listing_key(data,donor+':'+ch)
    return data


def canonical(data, key):
    seen = set()
    while key in data['aliases']:
        assert key not in seen, ('identity cycle', key)
        seen.add(key)
        key = data['aliases'][key]
    return key


def listing_key(data, key):
    """Display coverage may combine variants without rewriting published scores."""
    return data.get('reviewed_groups',{}).get(key,canonical(data, data.get('listing_groups', {}).get(key, key)))


def emit(data):
    lines = ['V 1']
    seen = {}

    def identity(kind, key):
        value = uid(kind, key)
        previous = seen.setdefault(value, (kind, key))
        assert previous == (kind, key), ('identity collision', previous, (kind, key))
        return value.hex()

    for row in data['sources']:
        source, sid = row['source'], row['sid']
        key = f'{source}:{sid}'
        lines.append(f"S {source} {sid} {row['rank']} {identity('source', key)} {identity('song', row['family'])}")
        for ch in row['charts']:
            chart = key+':'+ch
            target = canonical(data, chart)
            lines.append(f"C {source} {sid} {data['chart_order'].index(ch)} {int(target == chart)+2*data.get('display_preferences',{}).get(chart,0)} {identity('chart', target)}")
            display = listing_key(data, chart)
            if display != target:
                lines.append(f"G {source} {sid} {data['chart_order'].index(ch)} {identity('listing', display)if display.startswith('audit/')else identity('chart', display)}")
    for key,family in sorted(data.get('listing_families',{}).items()):
        source,sid=key.rsplit(':',1)
        lines.append(f"E {source} {sid} {identity('song',family)}")
    for rule in data.get('coalesces',[]):
        a,ai=rule['owner'].rsplit(':',1);b,bi=rule['donor'].rsplit(':',1)
        mask=lambda names:sum(1<<data['chart_order'].index(ch)for ch in names)
        lines.append(f"M {a} {ai} {b} {bi} {mask(rule['borrow']):x} {mask(rule['shared']):x}")
    return '\n'.join(lines)+'\n' 


def resolve(data, available):
    """Independent metadata resolver: available maps source-chart keys to levels.

    Returns the visible keys. The installation cannot affect the canonical UID.
    """
    sources = {(r['source'], r['sid']): r for r in data['sources']}
    winners = {}
    for key, level in available.items():
        if not level:
            continue
        source, sid, ch = key.rsplit(':', 2)
        row = sources[source, int(sid)]
        target = listing_key(data, key)
        rank = (data.get('display_preferences',{}).get(key,0),row['rank'], int(canonical(data, key) == key),
                -int.from_bytes(uid('source', source+':'+sid), 'big'),
                data['chart_order'].index(ch))
        if target not in winners or rank > winners[target][0]:
            winners[target] = rank, key
    visible={key for _,key in winners.values()}
    for rule in data.get('coalesces',[]):
        if any(k.startswith(rule['owner']+':')for k in available) and any(k.startswith(rule['donor']+':')for k in available):
            for ch in rule['borrow']:
                old=rule['donor']+':'+ch;new=rule['owner']+':'+ch
                if old in visible and new not in available:visible.remove(old);visible.add(new)
    return visible


if __name__ == '__main__':
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    output = Path(sys.argv[1])
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(emit(load()))
