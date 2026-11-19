"""Static cadence comparison, independent of source importer dependencies."""
import collections

def same(a,b):
    na,nb=a['notes'],b['notes']
    if len(na)!=len(nb)or collections.Counter(x[1:]for x in na)!=collections.Counter(x[1:]for x in nb):return None
    if a['windows']!=b['windows']:return None
    # User explicitly ignores +/-1BPM-only revisions. Larger changes (especially
    # slow THE SAFARI) remain distinct. BPM change event locations must agree.
    if len(a['bpms'])!=len(b['bpms']):return None
    if any(abs(x[1]-y[1])>1 for x,y in zip(a['bpms'],b['bpms'])):return None
    if not na:return None
    x=[n[0]for n in na];y=[n[0]for n in nb]
    if len(na)<3:return None
    # Centered least squares avoids NumPy for this metadata-only matcher.
    mx=sum(x)/len(x);my=sum(y)/len(y)
    denominator=sum((v-mx)**2 for v in x)
    if not denominator:return None
    scale=sum((u-mx)*(v-my)for u,v in zip(x,y))/denominator
    offset=my-scale*mx
    ratio=a['bpms'][0][1]/b['bpms'][0][1] if a['bpms']and b['bpms']else 1.0
    # A reviewed1BPM difference can exceed0.5% on slower songs. Accept the
    # unchanged clock or its precise tempo ratio; never relax note matching.
    if min(abs(scale-1.0),abs(scale-ratio))>.002:return None
    lanes=collections.defaultdict(list)
    for f,c,k in nb:lanes[c,k].append(f)
    old=collections.defaultdict(list)
    for f,c,k in na:old[c,k].append(f)
    error=max(abs(f*scale+offset-g)for key,fs in old.items()for f,g in zip(fs,lanes[key]))
    if error>2:return None
    if any(abs(f*scale+(offset if f else 0)-g)>2 for (f,_),(g,_)in zip(a['bpms'],b['bpms'])):return None
    return {'max_frame_error':float(error),'scale':float(scale),'offset':float(offset),'bpm_difference_max':max((abs(x[1]-y[1])for x,y in zip(a['bpms'],b['bpms'])),default=0)}
