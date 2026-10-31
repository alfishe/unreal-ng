#!/usr/bin/env python3
"""A PSG file (AY register writes a frame) to a WAV to listen to: psg2wav.py in.psg out.wav [--clock 1750000]
[--seconds N] [--rate 44100]

AY-3-8910 at frame resolution (the PSG has no finer time): three square tones (toggle every 8 x period clocks),
the 17-bit noise LFSR (a step every 16 x period clocks), the 16 envelope shapes (a step every 16 x period clocks, an
R13 write restarts it), the mixer (R7) and the AY's volume curve; stereo ABC (A left, B both, C right), the DC taken away."""

import argparse
import struct
import sys
import wave

import numpy as np

# AY volume levels (measured curve, 0..1)
LEVELS = np.array([0.0, 0.0137, 0.0205, 0.0291, 0.0423, 0.0618, 0.0847, 0.1369, 0.1691, 0.2647, 0.3527, 0.4499,
                   0.5704, 0.6873, 0.8482, 1.0])
DOWN = list(range(15, -1, -1))
UP = list(range(16))


def envelope_shape(shape):
    """(first 16 steps, the steps repeated after them)"""
    if shape < 4 or shape == 9:
        return DOWN, [0]
    if shape < 8 or shape == 15:
        return UP, [0]
    return {8: (DOWN, DOWN), 10: (DOWN, UP + DOWN), 11: (DOWN, [15]), 12: (UP, UP), 13: (UP, [15]),
            14: (UP, DOWN + UP)}[shape]


def read_psg(path):
    """The register values at each frame: a list of dicts of the registers written in that frame"""
    data = open(path, 'rb').read()
    if data[:4] != b'PSG\x1a':
        raise SystemExit(f'{path}: not a PSG file')
    frames, current, i = [], None, 16
    while i < len(data):
        b = data[i]
        if b == 0xFF:
            current = {}
            frames.append(current)
            i += 1
        elif b == 0xFE:
            for _ in range(4 * data[i + 1]):
                frames.append({})
            current = frames[-1] if frames else None
            i += 2
        else:
            if i + 1 >= len(data):
                break
            if current is None:
                current = {}
                frames.append(current)
            if b < 16:
                current[b] = data[i + 1]
            i += 2
    return frames


def lfsr_table():
    out = np.empty(131071, dtype=np.uint8)
    r = 1
    for k in range(131071):
        out[k] = r & 1
        bit = (r ^ (r >> 3)) & 1
        r = (r >> 1) | (bit << 16)
    return out


def render(frames, clock, rate, fps=50.0):
    regs = [0] * 16
    regs[7] = 0xFF
    noise = lfsr_table()
    tone_bit, tone_rem = [0, 0, 0], [0.0, 0.0, 0.0]
    noise_idx, noise_rem = 0, 0.0
    env_step, env_rem = 0, 0.0
    first, loop = envelope_shape(0)
    cycles_frame = clock / fps
    per_sample = clock / rate
    out = []
    carry = 0.0
    for written in frames:
        for reg, value in written.items():
            regs[reg] = value
        if 13 in written:
            first, loop = envelope_shape(regs[13] & 15)
            env_step, env_rem = 0, 0.0
        carry += rate / fps
        n = int(carry)
        carry -= n
        c = np.arange(n) * per_sample
        # Tones
        bits = []
        for ch in range(3):
            period = (regs[2 * ch] | (regs[2 * ch + 1] << 8)) & 0xFFF or 1
            half = 8.0 * period
            toggles = np.floor((tone_rem[ch] + c) / half).astype(np.int64)
            bits.append((tone_bit[ch] + toggles) & 1)
            total = tone_rem[ch] + cycles_frame
            tone_bit[ch] = (tone_bit[ch] + int(total // half)) & 1
            tone_rem[ch] = total % half
        # Noise
        step = 16.0 * ((regs[6] & 31) or 1)
        idx = (noise_idx + np.floor((noise_rem + c) / step).astype(np.int64)) % len(noise)
        nbit = noise[idx]
        total = noise_rem + cycles_frame
        noise_idx = (noise_idx + int(total // step)) % len(noise)
        noise_rem = total % step
        # Envelope
        estep = 16.0 * ((regs[11] | (regs[12] << 8)) or 1)
        steps = env_step + np.floor((env_rem + c) / estep).astype(np.int64)
        seq_first = np.array(first)
        seq_loop = np.array(loop)
        env = np.where(steps < 16, seq_first[np.minimum(steps, 15)], seq_loop[np.maximum(steps - 16, 0) % len(loop)])
        total = env_rem + cycles_frame
        env_step = min(env_step + int(total // estep), 16 + 32 * 1024 * 1024)
        env_rem = total % estep
        # Mixer and volume
        chans = []
        for ch in range(3):
            tone_off = (regs[7] >> ch) & 1
            noise_off = (regs[7] >> (3 + ch)) & 1
            gate = (bits[ch] | tone_off) & (nbit | noise_off)
            vol = regs[8 + ch]
            level = LEVELS[env] if vol & 0x10 else np.full(n, LEVELS[vol & 15])
            chans.append(gate * level)
        a, b, cc = chans
        out.append(np.stack([a + 0.5 * b, cc + 0.5 * b], axis=1))
    if not out:
        return np.zeros((0, 2))
    pcm = np.concatenate(out)
    # The AY's output is unipolar: a running mean (46 ms) taken away removes the DC
    window = 2048
    for side in range(2):
        x = pcm[:, side]
        csum = np.concatenate([[0.0], np.cumsum(x)])
        lo = np.maximum(np.arange(len(x)) - window + 1, 0)
        mean = (csum[np.arange(len(x)) + 1] - csum[lo]) / (np.arange(len(x)) + 1 - lo)
        pcm[:, side] = x - mean
    return pcm / 1.5


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument('psg')
    p.add_argument('wav')
    p.add_argument('--clock', type=float, default=1750000.0)
    p.add_argument('--rate', type=int, default=44100)
    p.add_argument('--seconds', type=float, default=0, help='at most this long (0: all)')
    args = p.parse_args()
    frames = read_psg(args.psg)
    if args.seconds:
        frames = frames[:int(args.seconds * 50)]
    pcm = render(frames, args.clock, args.rate)
    data = (np.clip(pcm, -1, 1) * 32000).astype('<i2')
    with wave.open(args.wav, 'wb') as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(args.rate)
        w.writeframes(data.tobytes())
    print(f'{args.wav}: {len(frames)} frames, {len(pcm) / args.rate:.1f} s')


if __name__ == '__main__':
    sys.exit(main())
