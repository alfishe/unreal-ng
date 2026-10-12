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
