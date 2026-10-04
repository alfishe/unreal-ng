#!/usr/bin/env python3
"""Minimal SoundFont 2.04 writer for synthetic test banks.

Independent of the library's C++ bank builder (tests/sf2builder.h) on purpose: the harness renders the
same bank with FluidSynth and with libsam2695, so a bank written by a second implementation also checks
that both readers agree on the file.

A bank is a list of presets; each preset has one instrument; each instrument has zones that reference
samples by name and carry generator values (SF2 generator number -> 16-bit amount).
"""
import math
import struct

# SF2 2.04 generator numbers used by the harness (section 8.1.2)
GEN = {
    'startAddrsOffset': 0, 'endAddrsOffset': 1, 'startloopAddrsOffset': 2, 'endloopAddrsOffset': 3,
    'modLfoToPitch': 5, 'vibLfoToPitch': 6, 'modEnvToPitch': 7, 'initialFilterFc': 8, 'initialFilterQ': 9,
    'modLfoToFilterFc': 10, 'modEnvToFilterFc': 11, 'modLfoToVolume': 13, 'chorusEffectsSend': 15,
    'reverbEffectsSend': 16, 'pan': 17, 'delayModLFO': 21, 'freqModLFO': 22, 'delayVibLFO': 23,
    'freqVibLFO': 24, 'delayModEnv': 25, 'attackModEnv': 26, 'holdModEnv': 27, 'decayModEnv': 28,
    'sustainModEnv': 29, 'releaseModEnv': 30, 'keynumToModEnvHold': 31, 'keynumToModEnvDecay': 32,
    'delayVolEnv': 33, 'attackVolEnv': 34, 'holdVolEnv': 35, 'decayVolEnv': 36, 'sustainVolEnv': 37,
    'releaseVolEnv': 38, 'keynumToVolEnvHold': 39, 'keynumToVolEnvDecay': 40, 'instrument': 41,
    'keyRange': 43, 'velRange': 44, 'keynum': 46, 'velocity': 47, 'initialAttenuation': 48,
    'coarseTune': 51, 'fineTune': 52, 'sampleID': 53, 'sampleModes': 54, 'scaleTuning': 56,
    'exclusiveClass': 57, 'overridingRootKey': 58,
}


class Sample:
    def __init__(self, name, frames, rate, root=60, correction=0, loop=None):
        self.name = name
        self.frames = [max(-32768, min(32767, int(round(v)))) for v in frames]
        self.rate = rate
        self.root = root
        self.correction = correction
        self.loop = loop  # (start, end) relative to the sample start, end exclusive


def SineSample(name, rate=44000, cycles=10, period=100, amplitude=16384, root=69):
    """A looped sine: `period` frames per cycle, so the pitch at the root key is rate / period"""
    n = cycles * period
    frames = [amplitude * math.sin(2 * math.pi * i / period) for i in range(n)]
    return Sample(name, frames, rate, root, 0, (0, n))


def _Pad20(name):
    raw = name.encode('ascii')[:19]
    return raw + b'\0' * (20 - len(raw))


def _Chunk(fourcc, payload):
    data = fourcc + struct.pack('<I', len(payload)) + payload
    if len(payload) & 1:
        data += b'\0'
    return data


def _List(fourcc, chunks):
    payload = fourcc + b''.join(chunks)
    return b'LIST' + struct.pack('<I', len(payload)) + payload


def _Gen(oper, amount):
    if oper in (GEN['keyRange'], GEN['velRange']):
        lo, hi = amount
        return struct.pack('<HBB', oper, lo, hi)
    return struct.pack('<Hh', oper, amount) if amount < 0 else struct.pack('<HH', oper, amount)


def _OrderedGens(gens):
    """keyRange first, velRange second, everything else after (spec 8.1.2 ordering rules)"""
    items = sorted(gens.items(), key=lambda kv: (kv[0] != GEN['keyRange'], kv[0] != GEN['velRange'], kv[0]))
    return items


def WriteSf2(path, samples, presets, name='libsam2695 harness bank'):
    """presets: list of dicts {name, bank, program, zones: [ {sample: name, gens: {gen: amount}} ]}"""
    # sdta: every sample followed by 46 zero frames (spec 7.10)
    smpl = bytearray()
    headers = []
    index = {}
    for s in samples:
        start = len(smpl) // 2
        smpl += struct.pack('<%dh' % len(s.frames), *s.frames)
        end = len(smpl) // 2
        smpl += b'\0' * (46 * 2)
        loop = s.loop or (0, len(s.frames))
        index[s.name] = len(headers)
        headers.append(struct.pack('<20sIIIIIBbHH', _Pad20(s.name), start, end, start + loop[0],
                                   start + loop[1], s.rate, s.root, s.correction, 0, 1))
    headers.append(struct.pack('<20sIIIIIBbHH', _Pad20('EOS'), 0, 0, 0, 0, 0, 0, 0, 0, 0))

    phdr, pbag, pgen, inst, ibag, igen = [], [], [], [], [], []
    for pi, p in enumerate(presets):
        # instrument
        inst.append(struct.pack('<20sH', _Pad20(p['name']), len(ibag)))
        for z in p['zones']:
            ibag.append(struct.pack('<HH', len(igen), 0))
            gens = dict(z.get('gens', {}))
            for oper, amount in _OrderedGens(gens):
                igen.append(_Gen(oper, amount))
            igen.append(_Gen(GEN['sampleID'], index[z['sample']]))
        # preset
        phdr.append(struct.pack('<20sHHHIII', _Pad20(p['name']), p['program'], p['bank'], len(pbag), 0, 0, 0))
        pbag.append(struct.pack('<HH', len(pgen), 0))
        for oper, amount in _OrderedGens(dict(p.get('presetGens', {}))):
            pgen.append(_Gen(oper, amount))
        pgen.append(_Gen(GEN['instrument'], pi))
    phdr.append(struct.pack('<20sHHHIII', _Pad20('EOP'), 0, 0, len(pbag), 0, 0, 0))
    pbag.append(struct.pack('<HH', len(pgen), 0))
    pgen.append(struct.pack('<HH', 0, 0))
    inst.append(struct.pack('<20sH', _Pad20('EOI'), len(ibag)))
    ibag.append(struct.pack('<HH', len(igen), 0))
    igen.append(struct.pack('<HH', 0, 0))
    mod = struct.pack('<HHhHH', 0, 0, 0, 0, 0)

    info = _List(b'INFO', [_Chunk(b'ifil', struct.pack('<HH', 2, 4)), _Chunk(b'isng', b'EMU8000\0'),
                           _Chunk(b'INAM', name.encode('ascii') + b'\0')])
    sdta = _List(b'sdta', [_Chunk(b'smpl', bytes(smpl))])
    pdta = _List(b'pdta', [_Chunk(b'phdr', b''.join(phdr)), _Chunk(b'pbag', b''.join(pbag)), _Chunk(b'pmod', mod),
                           _Chunk(b'pgen', b''.join(pgen)), _Chunk(b'inst', b''.join(inst)),
                           _Chunk(b'ibag', b''.join(ibag)), _Chunk(b'imod', mod), _Chunk(b'igen', b''.join(igen)),
                           _Chunk(b'shdr', b''.join(headers))])
    body = b'sfbk' + info + sdta + pdta
    with open(path, 'wb') as f:
        f.write(b'RIFF' + struct.pack('<I', len(body)) + body)
