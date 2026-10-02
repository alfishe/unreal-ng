import random, collections, sys
from v5waitsim import run
dz=int(sys.argv[1]); setup=int(sys.argv[2]); fld=int(sys.argv[3]); turbo=sys.argv[4]=='1'
random.seed(1)
stat=collections.defaultdict(collections.Counter)
for s in range(8):
  for trial in range(3):
    cyc=[]
    for i in range(120):
        k=random.choice(['M1','M1','MR','MW','X','X','IO'])
        cyc.append((k, 1 if k!='IO' else 0))
    r=run(cyc, s, fld_fn=lambda mc:fld, dz=dz, setup=setup, turbo=turbo)
    for (t1,mc,cnt,f,k,ram,w) in r[5:]:
        if k in ('M1','MR','MW'):
            stat[cnt%8][w]+=1
print(f'dz={dz} setup={setup} fld={fld} turbo={turbo}:', ' '.join(f'{c}:{dict(stat[c])}' for c in sorted(stat)))
