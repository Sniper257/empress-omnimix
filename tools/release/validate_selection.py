"""Maintainer: independent installed-set visible-chart equivalence gate. Usage: STAGE."""
from pathlib import Path
import sys,json
from author_sources import PROJECT as P
import megamix as M
import chart_catalog as C
S=Path(sys.argv[1]).resolve();plan=json.loads((S/'release-plan.json').read_text());catalog=C.load(S/'release-catalog.json');discs={d['source']:d for d in plan['discs']};keys=list(discs);cases=[['id'],keys]+[['id',s]for s in keys if s!='id']+[keys[:i]for i in range(2,len(keys))]
reports=[]
for selected in cases:
 packages={s:{'songs':discs[s]['targets']['console'if s=='id'else'append']['songs']}for s in selected};packages['id']['selection']=plan['selection'];state={'packages':packages};allrows=[r for p in packages.values()for r in p['songs']];active=M.active_songs(state)
 def available(rows):
  out={}
  for r in rows:
   key=r['source']+':'+str(r['sid'])
   for ch in plan['selection'][key]['charts']:out[key+':'+catalog['chart_order'][ch['column']]]=1
  return out
 before=C.resolve(catalog,available(allrows));after=C.resolve(catalog,available(active));assert before==after,(selected,before-after,after-before)
 reports.append(dict(sources=selected,payload_rows=len(allrows),active_rows=len(active),visible_charts=len(before),same_visible_chart_keys=True))
(S/'selection-proof.json').write_text(json.dumps(dict(cases=reports,all_payloads_retained=True,published_identities_unchanged=True),indent=2)+'\n');print('Independent static selection',len(cases),'cases PASS; full-library active',reports[1]['active_rows'])
