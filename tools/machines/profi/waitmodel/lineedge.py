"""Paper-window edges. FLD1 = 1 for ticks 0..31 of a 56-tick line (1 tick = 16 master clocks = 4 T).
x = master clocks from the TCS edge that registers FLD1=1 (start of tick 0) to the Phi rising edge that starts T1.
Prints the x range (mod the 896-clock line) of accesses that got a wait, per sub-phase."""
import collections
from v5waitsim import run
def mkfld():
    st = {'n': 0, 'rises': []}
    def f(mc):
        v = 1 if (st['n'] % 56) < 32 else 0
        if st['n'] % 56 == 0: st['rises'].append(mc)
        st['n'] += 1
        return v
    return f, st
for s in range(4, 8):
    waited = collections.Counter(); seen = collections.Counter()
    for seq in (['M1'], ['M1', 'MR'], ['M1', 'MR', 'X'], ['MR'], ['M1', 'X', 'X'], ['MW', 'X'], ['M1','X']):
        f, st = mkfld()
        r = run([(k, 1) for k in seq] * 500, s, fld_fn=f)
        R = st['rises']
        for (t1, mc, cnt, fld, k, ram, w) in r:
            if k == 'X': continue
            prev = [m for m in R if m <= mc]
            if not prev: continue
            x = mc - prev[-1]
            seen[x] += 1
            if w: waited[x] += w
    xs = sorted(waited)
    print('sub-phase start', s, 'x%4 =', {x % 4 for x in seen}, 'wait x range', xs[:3], '...', xs[-3:],
          'max', max(waited.values()) if waited else 0, 'n positions waited', len(xs),
          'waits only on x%8 in', sorted({x % 8 for x in xs}))
