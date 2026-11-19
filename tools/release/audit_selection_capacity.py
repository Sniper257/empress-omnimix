"""Maintainer: enumerate the finite 32768 supported installed-disc sets cheaply.
Uses Gray-code updates to the static coverage winners; no media copying.
"""
from pathlib import Path
import collections,json,sys,time
import megamix as M

def audit(stage):
    stage=Path(stage);plan=json.loads((stage/'release-plan.json').read_text());sel=plan['selection'];discs={d['source']:d for d in plan['discs']};sources=['id']+sorted(set(discs)-{'id'});sourcebit={s:1<<i for i,s in enumerate(sources)}
    rows=[r for d in discs.values()for r in d['targets']['console'if d['source']=='id'else'append']['songs']];keys=[r['source']+':'+str(r['sid'])for r in rows];index={k:i for i,k in enumerate(keys)};groups=collections.defaultdict(dict);peers=[];native=0
    for key,i in index.items():
        r=sel[key];src=rows[i]['source'];bit=sourcebit[src]
        if src=='id':native|=1<<i
        for chart in r['charts']:
            rank=(chart['preferred']>>1,r['rank'],chart['preferred']&1,-int(r['source_id'],16),chart['column']);g=groups[chart['group']]
            if bit not in g or rank>g[bit][0]:g[bit]=(rank,i)
        for p in r.get('peers',[]):
            if p in index:peers.append((bit|sourcebit[rows[index[p]]['source']],i,index[p]))
    candidates=[[(b,v[1])for b,v in sorted(g.items(),key=lambda item:item[1][0],reverse=True)]for g in groups.values()];bybit=collections.defaultdict(list)
    for g,cs in enumerate(candidates):
        for bit,_ in cs:bybit[bit].append(g)
    refs=[0]*len(rows);winners=[None]*len(candidates);bits=0
    for g,cs in enumerate(candidates):
        for b,r in cs:
            if b==1:winners[g]=r;refs[r]+=1;bits|=1<<r;break
    def closed(mask,bits):
        keep=bits|native;changed=True
        while changed:
            old=keep
            for required,a,b in peers:
                if mask&required==required and keep&((1<<a)|(1<<b)):keep|=(1<<a)|(1<<b)
            changed=old!=keep
        return keep
    mask=1;previous=0;top=[];hist=collections.Counter();start=time.monotonic()
    for n in range(1<<(len(sources)-1)):
        gray=n^(n>>1)
        if n:
            changed=(gray^previous)<<1;mask=1|(gray<<1)
            for g in bybit[changed]:
                old=winners[g];new=next((r for b,r in candidates[g]if mask&b),None)
                if old==new:continue
                if old is not None:
                    refs[old]-=1
                    if not refs[old]:bits&=~(1<<old)
                if new is not None:refs[new]+=1;bits|=1<<new
                winners[g]=new
        previous=gray;count=closed(mask,bits).bit_count();hist[count]+=1
        if len(top)<8 or (count,mask)>top[-1]:top.append((count,mask));top.sort(reverse=True);top=top[:8]
    reports=[]
    for count,mask in top:
        selected=[s for s in sources if mask&sourcebit[s]];state={'packages':{s:{'songs':discs[s]['targets']['console'if s=='id'else'append']['songs']}for s in selected}};state['packages']['id']['selection']=sel
        actual=M.active_songs(state);assert len(actual)==count
        reports.append(dict(active_rows=count,sources=selected))
    out=dict(installed_sets=1<<(len(sources)-1),elapsed_seconds=time.monotonic()-start,max_active_rows=top[0][0],worst=reports,histogram=dict(sorted(hist.items())))
    (stage/'selection-capacity.json').write_text(json.dumps(out,indent=2)+'\n');print('Finite capacity audit PASS',out['installed_sets'],'sets; maximum',out['max_active_rows'],'rows',flush=True);return out

if __name__=='__main__':audit(sys.argv[1])
