import random, collections, sys
from v5waitsim import run
dz=int(sys.argv[1]) if len(sys.argv)>1 else 5
setup=int(sys.argv[2]) if len(sys.argv)>2 else 4
random.seed(1)
stat=collections.defaultdict(collections.Counter)
for s in range(16):
  for trial in range(6):
    cyc=[]
    for i in range(150):
        k=random.choice(['M1','M1','MR','MW','X','X','IO'])
        cyc.append((k, 1 if k!='IO' else 0))
    r=run(cyc, s, fld_fn=lambda mc:1, dz=dz, setup=setup)
    for (t1,mc,cnt,fld,k,ram,w) in r[5:]:
        if k in ('M1','MR','MW'):
            stat[(k, cnt%8)][w]+=1
for k in sorted(stat): print(k, dict(stat[k]))
