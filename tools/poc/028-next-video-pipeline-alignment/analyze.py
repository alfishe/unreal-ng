import sys
def load(sfile, efile):
    rows=[]
    for l in open(sfile):
        p=l.split()
        if len(p)==5: rows.append((int(p[0]),int(p[1]),int(p[2]),int(p[3]),int(p[4])))
    lines={}
    for c,vc,hc,rgb,bk in rows:
        lines.setdefault(vc,[]).append((c,hc,rgb,bk))
    ev=[]
    for l in open(efile):
        p=l.split()
        if p and p[0]=='force': ev.append((int(p[1]),int(p[2]),int(p[3]),int(p[4])))
    return lines,ev
def analyze(sfile,efile,verbose=False):
    lines,ev=load(sfile,efile)
    res=[]
    for k,line,hc,cf in ev:
        cur=lines.get(line); base=lines.get(line-2)
        if not cur or not base: continue
        s0=cur[0][0]; b0=base[0][0]
        bmap={c-b0:(rgb,bk) for c,hc2,rgb,bk in base}
        first=None
        for c,hc2,rgb,bk in cur:
            if hc2<20: continue            # the start of the line: the previous scenario's restore settles
            pos=c-s0
            b=bmap.get(pos)
            if b is None: continue
            if b!=(rgb,bk) and bk==1 and b[1]==1:
                first=(c,hc2); break
        res.append((k,hc,cf,first))
    return res
if __name__=='__main__':
    for k,hc,cf,first in analyze(sys.argv[1],sys.argv[2]):
        print(k,hc,cf, None if first is None else (first[0]-cf, first[1]))


def diff(sfile, efile, rfile):
    """Differential: the first sample at or after each force whose output differs from the reference run's (the same sequence, the
    value never changed). Returns (k, line, hc, force clock, clocks to the first change, its line, its hc)."""
    def table(f):
        d = {}
        for l in open(f):
            p = l.split()
            if len(p) == 5:
                d[int(p[0])] = (int(p[3]), int(p[4]), int(p[1]), int(p[2]))
        return d
    run, ref = table(sfile), table(rfile)
    res = []
    for k, line, hc, cf in load(sfile, efile)[1]:
        first = None
        for c in range(cf, cf + 6000):
            a, b = run.get(c), ref.get(c)
            if a is None or b is None:
                continue
            if a[:2] != b[:2] and a[1] == 1 and b[1] == 1:
                first = (c - cf, a[2], a[3])
                break
        res.append((k, line, hc, cf, first))
    return res
