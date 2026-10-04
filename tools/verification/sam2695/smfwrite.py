#!/usr/bin/env python3
"""Minimal Standard MIDI File (format 0) writer for harness scenarios.

Events are (time in seconds, bytes). By default the file uses 1000 ticks per quarter and a 500000 us tempo (one
tick = 0.5 ms). The harness uses 3 ticks per quarter at 5120 us, so one tick is exactly 64 samples at 37 500 Hz:
FluidSynth applies MIDI events on its 64-sample block grid, and events on that grid reach both synthesizers at
the same sample.
"""
import struct


def _VarLen(value):
    out = [value & 0x7F]
    value >>= 7
    while value:
        out.append(0x80 | (value & 0x7F))
        value >>= 7
    return bytes(reversed(out))


def WriteSmf(path, events, ticksPerQuarter=1000, tempoUs=500000):
    secondsPerTick = tempoUs / 1e6 / ticksPerQuarter
    events = sorted(events, key=lambda e: e[0])
    track = bytearray()
    track += _VarLen(0) + bytes([0xFF, 0x51, 0x03]) + tempoUs.to_bytes(3, 'big')
    last = 0
    for seconds, data in events:
        tick = int(round(seconds / secondsPerTick))
        track += _VarLen(tick - last)
        last = tick
        data = bytes(data)
        if data[0] == 0xF0:
            track += b'\xF0' + _VarLen(len(data) - 1) + data[1:]
        else:
            track += data
    track += _VarLen(0) + b'\xFF\x2F\x00'
    with open(path, 'wb') as f:
        f.write(b'MThd' + struct.pack('>IHHH', 6, 0, 1, ticksPerQuarter))
        f.write(b'MTrk' + struct.pack('>I', len(track)) + track)


def NoteOn(ch, key, vel):
    return bytes([0x90 | ch, key, vel])


def NoteOff(ch, key):
    return bytes([0x80 | ch, key, 0])


def Cc(ch, cc, value):
    return bytes([0xB0 | ch, cc, value])


def Program(ch, program):
    return bytes([0xC0 | ch, program])


def Bend(ch, value14):
    return bytes([0xE0 | ch, value14 & 0x7F, (value14 >> 7) & 0x7F])
