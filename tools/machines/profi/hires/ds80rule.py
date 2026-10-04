"""Per-access wait rule in DS80 (v5.06): for random instruction mixes, the wait count of each RAM M-cycle against
the time of its T1 start relative to the latest video request edge (/STBI0 falling), in ns (request period 666.7 ns).
Also the overall wait statistics (requests on). Usage: python3 ds80rule.py [xmhz] [turbo 0/1]"""
import sys, random, collections, bisect
from ds80waitsim import run, UNIT_NS
xmhz = int(sys.argv[1]) if len(sys.argv) > 1 else 16
turbo = len(sys.argv) > 2 and sys.argv[2] == '1'
random.seed(7)
bins = collections.defaultdict(collections.Counter)
tot = collections.Counter()
BIN = 25.0
for trial in range(15):
    cyc = []
    for i in range(160):
        k = random.choice(['M1', 'M1', 'MR', 'MW', 'X', 'X', 'IO'])
        cyc.append((k, 1 if k not in ('IO', 'X') else 0))
    res, req = run(cyc, xmhz=xmhz, f0_phase=random.randrange(20), cpu_phase=random.randrange(4), req_on=1, turbo=turbo)
    for (t1, k, ram, w) in res[8:]:
        if k not in ('M1', 'MR', 'MW'): continue
        i = bisect.bisect_right(req, t1) - 1
        if i < 0: continue
        dt = (t1 - req[i]) * UNIT_NS
        bins[int(dt // BIN)][w] += 1
        tot[w] += 1
n = sum(tot.values())
print(f'ZQ3 {xmhz} MHz, CPU {xmhz / (2 if turbo else 4):g} MHz, requests on: waits per RAM M-cycle {dict(tot)}; '
      f'mean {sum(k * v for k, v in tot.items()) / n:.3f}')
print('T1 - request edge (ns)  -> wait counts')
for b in sorted(bins):
    print(f'  {b * BIN:5.0f}..{(b + 1) * BIN:5.0f}  {dict(bins[b])}')
# requests off
tot0 = collections.Counter()
for trial in range(10):
    cyc = [(k, 1 if k not in ('IO', 'X') else 0) for k in (random.choice(['M1', 'M1', 'MR', 'MW', 'X', 'X', 'IO']) for _ in range(160))]
    res, _ = run(cyc, xmhz=xmhz, f0_phase=random.randrange(20), cpu_phase=random.randrange(4), req_on=0, turbo=turbo)
    for (t1, k, ram, w) in res[8:]:
        if k in ('M1', 'MR', 'MW'): tot0[w] += 1
print('requests off: waits per RAM M-cycle', dict(tot0))
