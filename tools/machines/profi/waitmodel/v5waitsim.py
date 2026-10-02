#!/usr/bin/env python3
"""Gate-level timing model of the Profi v5.06 DRAM arbiter and CPU WAIT path (Spectrum mode, non-turbo).

Every net and pin below is read off the v5.06 P-CAD netlist (profi506-proc-ascii.sch, parsed by parsenet.py):

  master clock ZQ2 14 MHz: NET00004 (= F0 = TRAM = V14 via DD34/DD25, /80DS=1), ZX14MHZ = its inverse (= TCPU)
  CPU clock   DD2:1 (TCPU rising, toggle) -> NET00033; NET00037 = TCPU | NET00033 (DD9:1);
              DD2:2 (NET00037 rising, toggle) -> F2T; F2CPU = F2T (DD22:3/4) -> Z80 pin 6.   F2T = 14/4 = 3.5 MHz
  video count DD7 74161 on F0 rising: QA..QD.  /STBI0 = F0 | QA | QB | QC (DD4:1, DD4:2, DD15:2)
              TCS = /QD (DD5:2) clocks DD44 (registered PROM bits, FLD1 = paper window)
  RAS ring    DD49:1 Q=/RAS  (CLK TRAM, D=NET00075, /S = NET00063 = NAND(/RAS & /REDYT, CPU)  DD13:2, DD26:1)
              DD62 74174 on TRAM: /CAS <- /RAS (D5/Q5), NET00075 <- RAS (D6/Q6)
  slot owner  DD21:2 Q=CPU (CLK /CAS rising, D=/CPU; /S = NET00066 = NOT NET00198 (DD22:1);
              /R = NET00076 = NAND(NET00062, NET00066) (DD26:2); NET00062 = NET00009 & CPU (DD11:1);
              NET00009 = /S_IR22 (DD5:4 via R69 300R) wire-ANDed with /REDYT through VD22 (cathode on /REDYT)
  video req   DD14:2 Q=NET00198 (CLK NET00203 rising, D=VCC, /S = NET00196 = /PS & (FLD1 | NET00194) DD23:4,
              /R = /STBI0).  NET00203 = CPU | NET00198 | /CAS (DD6:1, DD6:2).  NET00194 = BCMR & 80DS (R5, VD1) = 0
  S_IR22      = NOR(/CPU, /CAS) = CPU & CAS (DD20:2)
  /REDYT      = NAND(NET00041, NET00045) (DD26:3); NET00041 = G14 & /RFSH (DD13:1); G14 = NOR(/RAMS, /MREQ) (DD20:1)
              NET00045 = /Q of DD21:1 (CLK S_IR22, D=VCC, /R=NET00041)
  /READY      = (/REDYT & NET00122 & /KBW) | (SB8 ? (/TURBO & /80DS) : 0)  (DD18:3, DD18:4, DD103:4, DD71:4, R12)

Units: 1 tick = 1/8 of a 14 MHz master clock (8.93 ns). CPU T = 32 ticks.
"""
import argparse, collections

VD22 = True    # the 5.06 board has the diode; set False for the variant without it
DFF = 2          # flip-flop clock->Q (LS74/LS174/LS161), ticks (~18 ns)


class Sim:
    def __init__(self, s_phase, dz=5, setup=4, fld_fn=None, gate=1, turbo=False):
        self.turbo = turbo
        self.t = 0
        self.dz, self.setup, self.gate = dz, setup, gate
        self.fld_fn = fld_fn or (lambda master_clock: 1)
        # flip-flop outputs (visible)
        self.cnt = 0                  # DD7
        self.q33 = 0                  # DD2:1
        self.f2t = 0                  # DD2:2
        self.ras_n = 1                # DD49:1
        self.cas_n = 1                # DD62 Q5
        self.n75 = 0                  # DD62 Q6
        self.served = 0               # DD21:1 Q
        self.cpu = 1                  # DD21:2 Q
        self.n198 = 1                 # DD14:2 Q
        self.fld1 = 0
        self.sched = collections.defaultdict(dict)
        self.prev = {}
        self.mc = 0                   # master clock count (TRAM rising edges)
        # start the CPU divider at a chosen phase relative to the video counter
        self.cnt = s_phase & 15
        # Z80 bus
        self.mreq = 1; self.rfsh = 1; self.ram = 0
        self.ready_hist = collections.deque([1] * 64, maxlen=64)
        self.ready = 1

    def set_later(self, name, val, d=DFF):
        self.sched[self.t + d][name] = val

    def comb(self):
        m = 1 if (self.t % 8) < 4 else 0
        n = {}
        n['m'] = m
        n['tcpu'] = 1 - m
        n['n37'] = n['tcpu'] | self.q33
        qa, qb, qc = self.cnt & 1, (self.cnt >> 1) & 1, (self.cnt >> 2) & 1
        n['stbi0'] = m | qa | qb | qc
        n['tcs'] = 1 - ((self.cnt >> 3) & 1)
        g14 = 1 if (self.mreq == 0 and self.ram) else 0
        n41 = g14 & self.rfsh
        n['n41'] = n41
        redyt_n = 1 - (n41 & (1 - self.served))
        n['redyt_n'] = redyt_n
        s_ir22 = self.cpu & (1 - self.cas_n)
        n['s_ir22'] = s_ir22
        n9 = (1 - s_ir22) & (redyt_n if VD22 else 1)   # VD22 wire-AND (absent: no CPU priority)
        n62 = n9 & self.cpu
        n66 = 1 - self.n198
        n['pre_cpu'] = 1 - n66            # active-high "preset active"
        n['clr_cpu'] = n62 & n66          # NET00076 low
        n['pre_ras'] = (self.ras_n & redyt_n & self.cpu)  # NET00063 low
        n['n203'] = self.cpu | self.n198 | self.cas_n
        n['pre198'] = 1 - self.fld1       # NET00196 = FLD1 (Spectrum mode, /PS ignored)
        n['clr198'] = 1 - n['stbi0']
        n['ready'] = redyt_n             # SB8 = PROFI3+, ROM one-shot and /KBW inactive
        n['cas_n'] = self.cas_n
        return n

    def step(self):
        # apply scheduled FF updates
        for k, v in self.sched.pop(self.t, {}).items():
            setattr(self, k, v)
        n = self.comb()
        p = self.prev or n
        rise = lambda k: p[k] == 0 and n[k] == 1
        if rise('m'):                           # TRAM / F0 rising
            self.mc += 1
            self.set_later('cnt', (self.cnt + 1) & 15)
            self.set_later('n75', 1 - self.ras_n)
            self.set_later('cas_n', self.ras_n)
            if not n['pre_ras']:
                self.set_later('ras_n', self.n75)
        if rise('tcs'):
            self.set_later('fld1', self.fld_fn(self.mc))
        if rise('tcpu') and not self.turbo:
            self.set_later('q33', 1 - self.q33)   # DD2:1 /R = /TURBO: held at 0 in turbo
        if rise('n37'):
            self.set_later('f2t', 1 - self.f2t)
        if rise('s_ir22') and n['n41']:
            self.set_later('served', 1)
        if not n['n41']:
            self.set_later('served', 0)
        if rise('cas_n') and not n['pre_cpu'] and not n['clr_cpu']:
            self.set_later('cpu', 1 - self.cpu)
        if rise('n203') and not n['pre198'] and not n['clr198']:
            self.set_later('n198', 1)
        # async
        if n['pre_ras']:
            self.set_later('ras_n', 1)
        if n['pre_cpu']:
            self.set_later('cpu', 1)
        elif n['clr_cpu']:
            self.set_later('cpu', 0)
        if n['pre198']:
            self.set_later('n198', 1)
        elif n['clr198']:
            self.set_later('n198', 0)
        self.ready_hist.append(n['ready'])
        self.prev = n
        self.t += 1
        return n


def run(cycles, s_phase, fld_fn=None, dz=5, setup=4, warm=3000, trace=None, turbo=False):
    """cycles: list of (kind, ram): kind M1 (4T), MR / MW (3T), IO (4T, never waits here), X (1 internal T).
    Returns [(t1_tick, mc_at_t1, cnt_at_t1, fld1_at_t1, kind, ram, waits)]."""
    sim = Sim(s_phase, dz, setup, fld_fn, turbo=turbo)
    for _ in range(warm):
        sim.step()
    while sim.f2t != 0:          # align: start on the next Phi rising edge
        sim.step()
    while sim.f2t != 1:
        sim.step()
    out = []
    pend = []
    idx = 0
    def start():
        nonlocal idx
        c = cycles[idx]; idx += 1
        return dict(kind=c[0], ram=c[1], ts=1, w=0, t1=sim.t, mc=sim.mc, cnt=sim.cnt, fld=sim.fld1, wflag=False)
    cur = start(); sim.ram = cur['ram']
    phi_prev = 1
    nT = {'M1': 4, 'MR': 3, 'MW': 3, 'IO': 4, 'X': 1}
    while True:
        for (tk, a, v) in [x for x in pend if x[0] <= sim.t]:
            setattr(sim, a, v)
        pend = [x for x in pend if x[0] > sim.t]
        n = sim.step()
        if trace is not None:
            trace.append((sim.t, sim.f2t, sim.mreq, sim.rfsh, n['redyt_n'], sim.cpu, sim.ras_n, sim.cas_n, sim.n198, sim.cnt, sim.fld1, cur['kind'], cur['ts']))
        phi = sim.f2t
        if phi == phi_prev:
            continue
        phi_prev = phi
        d = sim.t + sim.dz
        k = cur['kind']
        if phi == 1:   # rising edge: next T-state
            if cur['wflag']:
                cur['w'] += 1; cur['wflag'] = False
                continue
            cur['ts'] += 1
            if cur['ts'] > nT[k]:
                out.append((cur['t1'], cur['mc'], cur['cnt'], cur['fld'], k, cur['ram'], cur['w']))
                if idx >= len(cycles):
                    return out
                cur = start()
                pend.append((sim.t + 1, 'ram', cur['ram']))
                continue
            if k == 'M1' and cur['ts'] == 3:
                pend.append((d, 'mreq', 1)); pend.append((d, 'rfsh', 0))
        else:          # falling edge
            ts = cur['ts']
            if k in ('M1', 'MR', 'MW') and ts == 1:
                pend.append((d, 'mreq', 0))
            if k in ('M1', 'MR', 'MW') and ts == 2:
                cur['wflag'] = sim.ready_hist[-1 - sim.setup] == 0
            if k == 'M1' and ts == 3:
                pend.append((d, 'mreq', 0))
            if k == 'M1' and ts == 4:
                pend.append((d, 'mreq', 1)); pend.append((d + 4, 'rfsh', 1))
            if k in ('MR', 'MW') and ts == 3:
                pend.append((d, 'mreq', 1))


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--dz', type=int, default=5)
    ap.add_argument('--setup', type=int, default=4)
    a = ap.parse_args()
    for fld in (1, 0):
        for s in range(8):
            r = run([('M1', 1)] * 40, s, fld_fn=lambda mc, f=fld: f, dz=a.dz, setup=a.setup)
            print('fld', fld, 's', s, [(x[2], x[6]) for x in r[10:20]])
