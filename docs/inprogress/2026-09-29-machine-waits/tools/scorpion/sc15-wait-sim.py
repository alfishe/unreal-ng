# Edge-level simulation of Scorpion SC15.1 / SC15.3 turbo WAIT logic (TRB=1).
# Edge k = rising CLK_7MHZ = start of a turbo CPU T-state. p(k) = DD3 count sampled at edge k (mod 4).
# border(k): True if BORDER_=0 (border) at edge k.
import sys
def h1m(p,paper): return (p>>1)&1 if paper else 0

class Sim:
    def __init__(s, fw, paperfn, k0=0):
        s.fw=fw; s.paper=paperfn; s.k=k0; s.waitq=1; s.pin13=0 if fw=='15.1' else 1
    def edge(s, iorq_, m1_, ram_, wr_en):
        p=s.k & 3; H0=p&1; H1=(p>>1)&1; paper=s.paper(s.k); H1M=h1m(p,paper)
        if s.fw=='15.1':
            d = (iorq_ and m1_ and not H0 and not H1M) or (iorq_ and not wr_en and ram_) \
                or (iorq_ and not m1_ and H0 and not H1M and not s.waitq) or s.pin13
            pin = (not iorq_) and (not s.waitq)
            s.waitq=1 if d else 0; s.pin13=1 if pin else 0
            s.k+=1
            return s.waitq  # value CPU samples at the falling edge of this T-state
        else:
            # SC15.3: WAIT_ combinatorial = Pin13.Q & (idle | (!RAS_.Q & !H1M)); RAS_.Q=H0 sampled at this edge
            pin_new = 1 if (iorq_ or not s.pin13) else 0
            s.pin13 = pin_new
            idle = (not wr_en) and ram_
            w = s.pin13 and (idle or ((not H0) and (not H1M)))
            s.k+=1
            return 1 if w else 0

def run(fw, prog, paperfn, k0, verbose=False):
    """prog: list of machine cycles: ('M1',ram) ('MR',ram) ('MW',ram) ('IO',) ('INT',n) internal Ts"""
    s=Sim(fw,paperfn,k0); log=[]
    for mc in prog:
        kind=mc[0]; start=s.k
        if kind=='X':  # internal T-states, no bus
            for _ in range(mc[1]): s.edge(1,1,1,0)
            log.append((kind,start,mc[1],0)); continue
        ram = mc[1] if len(mc)>1 else False
        m1_ = 0 if kind=='M1' else 1
        # T1 edge: bus signals from previous cycle end: idle
        s.edge(1,1,1,0)
        act = dict(iorq_=1,m1_=m1_,ram_=0 if (ram and kind in('M1','MR')) else 1, wr_en=1 if (ram and kind=='MW') else 0)
        if kind=='IO':
            # T2 edge: IORQ not yet active
            s.edge(1,1,1,0)
            n=0
            while True:  # TW (automatic) + Tw
                w=s.edge(0,1,1,0); n+=1
                if w: break
            s.edge(0,1,1,0)  # T3 edge
            log.append((kind,start,s.k-start,n-1)); continue
        waits=0
        w=s.edge(**act)  # T2
        while not w:
            waits+=1; w=s.edge(**act)
        s.edge(**act)  # T3 edge (signals still active just before)
        if kind=='M1':
            s.edge(1,0,1,0)  # T4 (refresh; RFSH -> RAM_=1, WR_EN=0)
        log.append((kind,start,s.k-start,waits))
    return s.k, log

allpaper=lambda k: True
allborder=lambda k: False
if __name__=='__main__':
    progs={
     'NOP x6':[('M1',True)]*6,
     'LD A,(HL) x3':[('M1',True),('MR',True)]*3,
     'LD (HL),A x3':[('M1',True),('MW',True)]*3,
     'NOP (ROM) x3':[('M1',False)]*3,
     'OUT (n),A':[('M1',True),('MR',True),('IO',)],
    }
    for fw in ['15.1','15.3']:
      for area,fn in [('paper',allpaper),('border',allborder)]:
        for name,pr in progs.items():
          for k0 in range(4):
            end,log=run(fw,pr,fn,k0)
            print(fw,area,name,'start p=%d'%k0,'total',end-k0,[(m,st&3,l,w) for m,st,l,w in log])
