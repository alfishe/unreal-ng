#!/usr/bin/env python3
"""Generate the SAA1099 stimulus corpus (corpus/*.saa).

Hand-written edge cases, one per behavior of tdd-saa1099.md section 3.3, and seeded
random register streams. Every write is a bus cycle at an absolute chip clock
(8 MHz); consecutive writes are at least 8 clocks apart, as on a real bus (a Z80 OUT
takes about a microsecond), which also keeps the RTL driver's bus cycles apart.

    ./gen-corpus.py            regenerate corpus/ (deterministic)
"""

import os
import random

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "corpus")
BUS_GAP = 8


class Stream:
    def __init__(self, title):
        self.lines = [f"; {title}"]
        self.t = 16

    def note(self, text):
        self.lines.append(f"; {text}")

    def at(self, clock):
        assert clock >= self.t, "time goes forward"
        self.t = clock

    def wait(self, clocks):
        self.t += clocks

    def reg(self, r, v):
        self.lines.append(f"{self.t} A #{r:02X}")
        self.t += BUS_GAP
        self.lines.append(f"{self.t} D #{v & 0xFF:02X}")
        self.t += BUS_GAP

    def addr(self, r):
        self.lines.append(f"{self.t} A #{r:02X}")
        self.t += BUS_GAP

    def end(self, clock=None):
        self.lines.append(f"{clock if clock is not None else self.t} END")
        return "\n".join(self.lines) + "\n"


def enable_all(s, tone_mask=0x3F, noise_mask=0, amps=0xFF):
    for v in range(6):
        s.reg(v, amps)
    s.reg(0x14, tone_mask)
    s.reg(0x15, noise_mask)
    s.reg(0x1C, 0x01)


def tone_periods():
    s = Stream("tone periods: octaves 0-7, tone numbers 0, 255 and inner values (tdd 3.3 item 1)")
    s.note("voice i: octave and tone chosen so every octave and both extremes are covered")
    tones = [(0, 0), (1, 255), (2, 227), (3, 128), (4, 33), (5, 5)]
    for v, (o, n) in enumerate(tones):
        s.reg(0x08 + v, n)
    s.reg(0x10, 0 | (1 << 4))
    s.reg(0x11, 2 | (3 << 4))
    s.reg(0x12, 4 | (5 << 4))
    s.reg(0x1C, 0x02)  # sync: restart all generators with these numbers
    s.reg(0x1C, 0x01)
    enable_all(s)
    s.wait(300000)
    s.note("now octaves 6 and 7, tone 255 / 0")
    s.reg(0x08, 255)
    s.reg(0x09, 0)
    s.reg(0x10, 7 | (6 << 4))
    s.wait(300000)
    return s.end()


def octave_latch():
    s = Stream("octave / tone latch: new numbers act at the next transition, in any write order (item 1)")
    s.reg(0x08, 100)
    s.reg(0x10, 0x33)
    s.reg(0x1C, 0x02)
    s.reg(0x1C, 0x01)
    enable_all(s, tone_mask=0x03)
    s.wait(20000)
    s.note("tone then octave inside one half period")
    s.reg(0x08, 200)
    s.reg(0x10, 0x34)
    s.wait(30000)
    s.note("octave then tone inside one half period")
    s.reg(0x10, 0x25)
    s.reg(0x08, 50)
    s.wait(30000)
    s.note("tone alone (same octave)")
    s.reg(0x08, 150)
    s.wait(30000)
    s.note("octave alone")
    s.reg(0x10, 0x22)
    s.wait(60000)
    return s.end()


def mixer():
    s = Stream("mixer: tone only, noise only, tone + noise, none; fixed noise rates (item 2)")
    s.reg(0x08, 0)
    s.reg(0x09, 0)
    s.reg(0x0A, 0)
    s.reg(0x0B, 0)
    s.reg(0x10, 0x44)
    s.reg(0x11, 0x44)
    s.reg(0x16, 0x10)  # noise 0 at clock/256, noise 1 at clock/512
    s.reg(0x1C, 0x02)
    s.reg(0x1C, 0x01)
    for v in range(6):
        s.reg(v, 0x9F if v % 2 else 0xF9)
    s.reg(0x14, 0b001011)  # voices 0, 1, 3 tone
    s.reg(0x15, 0b000110)  # voices 1, 2 noise (voice 1: tone + noise)
    s.wait(150000)
    s.reg(0x16, 0x02)  # noise 0 at clock/1024
    s.reg(0x15, 0b011110)
    s.wait(150000)
    return s.end()


def noise_from_tone():
    s = Stream("noise clocked by tone generator 0 / 3 (source 3), and back to a fixed rate (item 5)")
    s.reg(0x08, 200)
    s.reg(0x0B, 100)
    s.reg(0x10, 0x05)
    s.reg(0x11, 0x60)
    s.reg(0x16, 0x33)
    s.reg(0x1C, 0x02)
    s.reg(0x1C, 0x01)
    enable_all(s, tone_mask=0x00, noise_mask=0x3F)
    s.wait(120000)
    s.note("switch both back to fixed rates mid count")
    s.reg(0x16, 0x21)
    s.wait(120000)
    return s.end()


def amplitude_zero():
    s = Stream("silent levels: amplitude 0 with tone on, sound enable off, no mixer (item 7)")
    s.reg(0x08, 128)
    s.reg(0x10, 0x55)
    s.reg(0x00, 0x00)
    s.reg(0x01, 0x5A)
    s.reg(0x14, 0x03)
    s.wait(4000)
    s.reg(0x1C, 0x01)
    s.wait(40000)
    s.reg(0x14, 0x00)
    s.wait(4000)
    s.reg(0x1C, 0x00)
    s.wait(4000)
    return s.end()


def sync_reset():
    s = Stream("#1C bit 1: generators restart and wait; writes during RST act after the first half period (item 6)")
    s.reg(0x08, 100)
    s.reg(0x09, 200)
    s.reg(0x10, 0x34)
    s.reg(0x16, 0x00)
    s.reg(0x1C, 0x01)
    enable_all(s, tone_mask=0x03, noise_mask=0x04)
    s.wait(50000)
    s.reg(0x1C, 0x03)
    s.wait(1000)
    s.note("new numbers while RST is held")
    s.reg(0x08, 10)
    s.reg(0x10, 0x56)
    s.wait(1000)
    s.reg(0x1C, 0x01)
    s.wait(80000)
    return s.end()


def env_shapes(res3, invert, external=False):
    tag = f"{'3' if res3 else '4'}-bit{', inverted right' if invert else ''}"
    s = Stream(f"envelope shapes 0-7, {tag}, internal clock from tone generator 1 / 4 (item 3)")
    s.reg(0x09, 250)
    s.reg(0x0C, 240)
    s.reg(0x10, 0x60)
    s.reg(0x12, 0x06)
    s.reg(0x1C, 0x02)
    s.reg(0x1C, 0x01)
    enable_all(s, tone_mask=0x24, noise_mask=0x00, amps=0xEE)
    for shape in range(8):
        v = 0x80 | (shape << 1) | (0x10 if res3 else 0) | (1 if invert else 0)
        s.reg(0x18, v & 0x7F)  # off, shape preloaded: the next control acts at once
        s.reg(0x19, v & 0x7F)
        s.reg(0x18, v)
        s.reg(0x19, v)
        s.wait(60000)
    return s.end()


def env_buffered():
    s = Stream("envelope buffered write: acts at point 3 / 4, not at once (item 4)")
    s.reg(0x09, 200)
    s.reg(0x10, 0x50)
    s.reg(0x1C, 0x02)
    s.reg(0x1C, 0x01)
    enable_all(s, tone_mask=0x04, amps=0xFF)
    s.reg(0x18, 3 << 1)  # preloaded while off (the MiSTer RTL loads the shape only then)
    s.reg(0x18, 0x80 | (3 << 1))  # repetitive decay
    s.wait(9000)
    s.note("mid-period write of a new shape: waits for the loop point")
    s.reg(0x18, 0x80 | (5 << 1) | 1)
    s.wait(40000)
    s.note("single decay, then a write after it ended: acts at once")
    s.reg(0x18, 0x80 | (2 << 1))
    s.wait(40000)
    s.reg(0x18, 0x80 | (6 << 1))
    s.wait(40000)
    s.note("disable mid-envelope: direct-acting")
    s.reg(0x18, 0x80 | (7 << 1))
    s.wait(5000)
    s.reg(0x18, 0x00)
    s.wait(5000)
    return s.end()


def env_external():
    s = Stream("envelope external clock: address writes #18 / #19 clock their generator (item 4)")
    s.reg(0x1C, 0x01)
    enable_all(s, tone_mask=0x00, amps=0xFF)
    s.reg(0x18, 0x20 | (4 << 1))
    s.reg(0x19, 0x20 | (7 << 1) | 1)
    s.reg(0x18, 0x80 | 0x20 | (4 << 1))  # external, single triangular
    s.reg(0x19, 0x80 | 0x20 | (7 << 1) | 1)  # external, repetitive attack, inverted
    for i in range(40):
        s.addr(0x18)
        s.wait(200)
        if i % 3 == 0:
            s.addr(0x19)
            s.wait(200)
        if i % 5 == 0:
            s.addr(0x38)  # 5-bit address: #38 is #18 again
            s.wait(200)
        s.addr(0x00)
        s.wait(200)
    return s.end()


def env_resolution_switch():
    s = Stream("envelope resolution switched mid-envelope, both ways (item 3)")
    s.reg(0x1C, 0x01)
    enable_all(s, tone_mask=0x00, amps=0xFF)
    s.reg(0x18, 0x20 | (3 << 1))
    s.reg(0x18, 0x80 | 0x20 | (3 << 1))  # external, repetitive decay, 4-bit
    for i in range(30):
        s.addr(0x18)
        s.wait(100)
        if i in (3, 11, 20):
            s.reg(0x18, 0x80 | 0x20 | 0x10 | (3 << 1))  # to 3-bit
        if i in (7, 15, 25):
            s.reg(0x18, 0x80 | 0x20 | (3 << 1))  # back to 4-bit
    return s.end()


def clock_shapes_all_voices():
    s = Stream("all six voices, envelopes on both generators, full mix (output stage, item 3 / 7)")
    for v in range(6):
        s.reg(0x08 + v, 40 * v + 7)
    s.reg(0x10, 0x43)
    s.reg(0x11, 0x54)
    s.reg(0x12, 0x65)
    s.reg(0x16, 0x31)
    s.reg(0x1C, 0x02)
    s.reg(0x1C, 0x01)
    for v in range(6):
        s.reg(v, (v * 37 + 0x5C) & 0xFF)
    s.reg(0x14, 0x3F)
    s.reg(0x15, 0x2A)
    s.reg(0x18, 5 << 1)
    s.reg(0x19, 0x10 | (7 << 1) | 1)
    s.reg(0x18, 0x80 | (5 << 1))
    s.reg(0x19, 0x80 | 0x10 | (7 << 1) | 1)
    s.wait(200000)
    return s.end()


VALID_REGS = [0, 1, 2, 3, 4, 5, 8, 9, 10, 11, 12, 13, 0x10, 0x11, 0x12, 0x14, 0x15, 0x16, 0x18, 0x19, 0x1C]


def random_stream(seed, length=400000, consensus=True, noise=True):
    """Seeded random register stream.

    consensus=True keeps to writes whose effect every reference models the same way
    where we follow it: no RST (#1C bit 1), the noise sources set once at the start,
    and every tone number followed by its octave register (as trackers write them;
    SAASound delays a lone tone number by one more transition). consensus=False
    writes anything (raw streams: golden digests and smoke tests). noise=False never
    enables a noise mixer (the MiSTer RTL has another noise register, so only
    noise-free streams compare its output)."""
    rng = random.Random(seed)
    kind = ("consensus" if consensus else "raw") + ("" if noise else ", no noise")
    s = Stream(f"seeded random register stream ({kind}), seed {seed}")
    s.reg(0x1C, 0x01)
    octaves = [0, 0, 0]
    env_on = [False, False]
    res = [rng.choice([0, 0x10]), rng.choice([0, 0x10])]
    if consensus:
        s.reg(0x1C, 0x03)  # noise sources chosen under RST: both dividers start fresh at release
        s.reg(0x16, rng.randrange(256) & 0x33)
        s.reg(0x1C, 0x01)
    regs = [r for r in VALID_REGS if not (consensus and r == 0x16) and (noise or r not in (0x15, 0x16))]
    while s.t < length:
        r = rng.choice(regs)
        v = rng.randrange(256)
        if r == 0x1C:
            v &= 0x01 if consensus else 0x03
            if v & 2 and rng.random() < 0.7:
                v &= 1  # keep RST rare
        if r in (0x10, 0x11, 0x12):
            v &= 0x77
            octaves[r - 0x10] = v
        if r == 0x16:
            v &= 0x33
        if r in (0x18, 0x19):
            v &= 0xBF
            if consensus:
                # Repeating shapes and one resolution per generator: the MiSTer RTL plays
                # a single shape twice after the write that enables it, and keeps the
                # position LSB on a 3-to-4-bit switch (hand-written streams cover both)
                v = (v & ~0x10) | 0x02 | res[r - 0x18]
            # Consensus streams load a control with the generator off before switching
            # it on (the MiSTer RTL takes shape, clock and inversion only while off)
            if consensus and v & 0x80 and not env_on[r - 0x18]:
                s.reg(r, v & 0x7F)
            env_on[r - 0x18] = bool(v & 0x80)
        s.reg(r, v)
        if consensus and 0x08 <= r <= 0x0D:
            pair = (r - 0x08) // 2
            s.reg(0x10 + pair, octaves[pair])
        if rng.random() < 0.15:
            s.addr(rng.choice([0x18, 0x19]))
        s.wait(rng.randrange(64, 6000))
    return s.end()


def main():
    os.makedirs(OUT, exist_ok=True)
    cases = {
        "tone-periods": tone_periods(),
        "octave-latch": octave_latch(),
        "mixer": mixer(),
        "noise-from-tone": noise_from_tone(),
        "amplitude-zero": amplitude_zero(),
        "sync-reset": sync_reset(),
        "env-shapes-4bit": env_shapes(False, False),
        "env-shapes-3bit": env_shapes(True, False),
        "env-shapes-4bit-inverted": env_shapes(False, True),
        "env-shapes-3bit-inverted": env_shapes(True, True),
        "env-buffered": env_buffered(),
        "env-external": env_external(),
        "env-resolution-switch": env_resolution_switch(),
        "all-voices": clock_shapes_all_voices(),
    }
    for seed in (1, 2, 3, 4, 5, 6):
        cases[f"random-{seed}"] = random_stream(seed)
    for seed in (9, 10):
        cases[f"random-quiet-{seed}"] = random_stream(seed, noise=False)
    for seed in (7, 8):
        cases[f"random-raw-{seed}"] = random_stream(seed, consensus=False)
    for name, text in cases.items():
        with open(os.path.join(OUT, name + ".saa"), "w") as f:
            f.write(text)
    print(f"{len(cases)} streams in {OUT}")


if __name__ == "__main__":
    main()
