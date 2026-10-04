#!/usr/bin/env python3
"""Profi v5.06 DRAM arbiter + CPU WAIT path in DS80 (512x240) mode: three clocks.

Extends tools/machines/profi/waitmodel/v5waitsim.py (same nets, same gates; see its docstring).
What changes with 80DS = 1 (v5.06 netlist):
  DD25 (74257, S = /80DS = 0 -> A inputs): 1Y TCPU = XMHZ (pin 2), 2Y TRAM = XMHZ (pin 5)
      -> CPU divider and DRAM ring both on ZQ3 (DD31), SAME polarity (Spectrum mode: TCPU = ZX14MHZ = inverse of TRAM)
  DD34 (74257, S = 80DS = 1 -> B inputs): 1Y F0 = 12MHZ (ZQ1, DD27), 2Y T_IR10 = F0
      -> video counter DD7 (74161) on 12 MHz, asynchronous to XMHZ
  /STBI0 = F0 | QA | QB | QC (DD4, DD15): a video request every 8 F0 = 666.7 ns
  DD14:2 /S = NET00196 = /PS & (FLD1 | NET00194), NET00194 = BCMR & 80DS (R5, VD1; BCMR = #7FFD bit 5 = DD35 Q6)
      -> requests in FLD1 (paper) only, or in the whole line when #7FFD bit 5 = 1
  /READY: SB8 term NET00067 = /TURBO & /80DS = 0 in DS80 -> SB8 never removes waits
Units: 1 unit = 1/240 MHz = 4.1667 ns (16, 20, 24 MHz and 12 MHz are whole numbers of units).
"""
import collections, random

UNIT_NS = 1000 / 240.0
VD22 = True
DFF = 3          # flip-flop clock->Q, 1533 (ALS) ~12.5 ns


class Sim:
    def __init__(self, xmhz=20, f0mhz=12, f0_phase=0, cpu_phase=0, dz=9, setup=5, req_on=1, turbo=False,
                 base=240, tcpu_inv=False, dff=DFF, rom_pulse=43):
        # defaults: v5.06 parts - Z84C0020 (MREQ delay ~38 ns, WAIT setup ~21 ns), 1533 (ALS) flip-flops ~12.5 ns;
        # rom_pulse: the ROM-read one-shot NET00122 (C7 82 pF, R11 2k, album 'TI = 200nS'), 43 units = 180 ns
        # base: simulation step rate in MHz (240 -> 4.17 ns); spectrum-mode check: base=112, xmhz=f0mhz=14, tcpu_inv=True
        self.px = round(base / xmhz)          # XMHZ period in units
        self.pf = round(base / f0mhz)         # F0 period
        assert abs(self.px - base / xmhz) < 1e-9 and abs(self.pf - base / f0mhz) < 1e-9
        self.tcpu_inv = tcpu_inv; self.dff = dff
        self.f0_phase = f0_phase % self.pf
        self.turbo = turbo
        self.t = 0
        self.dz, self.setup = dz, setup
        self.req_on = req_on
        self.cnt = 0; self.q33 = 0 if turbo else cpu_phase & 1; self.f2t = (cpu_phase >> 1) & 1   # DD2:1 is held clear in turbo
        self.ras_n = 1; self.cas_n = 1; self.n75 = 0
        self.served = 0; self.cpu = 1; self.n198 = 1
        self.sched = collections.defaultdict(dict)
        self.prev = {}
        self.mreq = 1; self.rfsh = 1; self.ram = 0
        self.ready_hist = collections.deque([1] * 64, maxlen=64)
        self.rfsh_lag = 8 if base == 240 else 4
        self.rom_pulse = rom_pulse; self.rom_until = -1
        self.req_times = []                  # /STBI0 falling edges (video request), units

    def set_later(self, name, val, d=None):
        self.sched[self.t + (self.dff if d is None else d)][name] = val

    def comb(self):
        n = {}
        x = 1 if (self.t % self.px) < self.px / 2 else 0
        f = 1 if ((self.t + self.f0_phase) % self.pf) < self.pf / 2 else 0
        n['x'] = x                              # TRAM = TCPU = XMHZ
        n['f0'] = f
        tc = (1 - x) if self.tcpu_inv else x
        n['tc'] = tc
        n['n37'] = tc | self.q33                # DD9:1 = TCPU | NET00033
        qa, qb, qc = self.cnt & 1, (self.cnt >> 1) & 1, (self.cnt >> 2) & 1
        n['stbi0'] = f | qa | qb | qc
        g14 = 1 if (self.mreq == 0 and self.ram) else 0
        n41 = g14 & self.rfsh
        n['n41'] = n41
        redyt_n = 1 - (n41 & (1 - self.served))
        n['redyt_n'] = redyt_n
        s_ir22 = self.cpu & (1 - self.cas_n)
        n['s_ir22'] = s_ir22
        n9 = (1 - s_ir22) & (redyt_n if VD22 else 1)
        n62 = n9 & self.cpu
        n66 = 1 - self.n198
        n['pre_cpu'] = 1 - n66
        n['clr_cpu'] = n62 & n66
        n['pre_ras'] = (self.ras_n & redyt_n & self.cpu)
        n['n203'] = self.cpu | self.n198 | self.cas_n
        n['pre198'] = 1 - self.req_on           # NET00196 low = no requests
        n['clr198'] = 1 - n['stbi0']
        n['ready'] = redyt_n & (0 if self.t < self.rom_until else 1)   # /READY = /REDYT & NET00122 (& /KBW)
        n['cas_n'] = self.cas_n
        return n

    def step(self):
        for k, v in self.sched.pop(self.t, {}).items():
            setattr(self, k, v)
        n = self.comb()
        p = self.prev or n
        rise = lambda k: p[k] == 0 and n[k] == 1
        if rise('x'):                           # TRAM rising: DRAM ring
            self.set_later('n75', 1 - self.ras_n)
            self.set_later('cas_n', self.ras_n)
            if not n['pre_ras']:
                self.set_later('ras_n', self.n75)
        if rise('f0'):
            self.set_later('cnt', (self.cnt + 1) & 15)
        if p['stbi0'] == 1 and n['stbi0'] == 0:
            self.req_times.append(self.t)
        if rise('tc') and not self.turbo:       # TCPU rising: DD2:1 toggles (held clear in turbo)
            self.set_later('q33', 1 - self.q33)
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


NT = {'M1': 4, 'MR': 3, 'MW': 3, 'IO': 4, 'X': 1}


def run(cycles, warm=3000, **kw):
    """cycles: list of (kind, ram). Returns [(t1_units, kind, ram, waits)] and the request edge list."""
    sim = Sim(**kw)
    for _ in range(warm):
        sim.step()
    while sim.f2t != 0:
        sim.step()
    while sim.f2t != 1:
        sim.step()
    out, pend, idx = [], [], 0

    def start():
        nonlocal idx
        c = cycles[idx]; idx += 1
        return dict(kind=c[0], ram=c[1], ts=1, w=0, t1=sim.t, wflag=False)
    cur = start(); sim.ram = cur['ram']
    phi_prev = 1
    while True:
        for (tk, a, v) in [x for x in pend if x[0] <= sim.t]:
            setattr(sim, a, v)
        pend = [x for x in pend if x[0] > sim.t]
        sim.step()
        phi = sim.f2t
        if phi == phi_prev:
            continue
        phi_prev = phi
        d = sim.t + sim.dz
        k = cur['kind']
        if phi == 1:
            if cur['wflag']:
                cur['w'] += 1; cur['wflag'] = False
                continue
            cur['ts'] += 1
            if cur['ts'] > NT[k]:
                out.append((cur['t1'], k, cur['ram'], cur['w']))
                if idx >= len(cycles):
                    return out, sim.req_times
                cur = start()
                pend.append((sim.t + 1, 'ram', cur['ram']))
                continue
            if k == 'M1' and cur['ts'] == 3:
                pend.append((d, 'mreq', 1)); pend.append((d, 'rfsh', 0))
        else:
            ts = cur['ts']
            if k in ('M1', 'MR', 'MW') and ts == 1:
                pend.append((d, 'mreq', 0))
                if k in ('M1', 'MR') and not cur['ram'] and sim.rom_pulse:
                    pend.append((d, 'rom_until', d + 2 + sim.rom_pulse))   # MRD & /RAMS -> one-shot
            if k in ('M1', 'MR', 'MW') and ts == 2:
                cur['wflag'] = sim.ready_hist[-1 - sim.setup] == 0
            if k == 'M1' and ts == 3:
                pend.append((d, 'mreq', 0))
            if k == 'M1' and ts == 4:
                pend.append((d, 'mreq', 1)); pend.append((d + sim.rfsh_lag, 'rfsh', 1))
            if k in ('MR', 'MW') and ts == 3:
                pend.append((d, 'mreq', 1))
