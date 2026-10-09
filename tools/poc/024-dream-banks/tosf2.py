#!/usr/bin/env python3
"""Experimental Dream bank (.DXB / .B16) -> SoundFont 2 converter.

The SF2 model is what libsam2695's ISoundBank holds in memory (BankModel: presets -> zones -> samples), so
writing an SF2 is the shortest path to play a Dream bank through libsam2695 without touching core code. A core
DreamBankLoader would fill the same BankModel directly (see the research doc).

What is carried over (confidence in the doc):
    programs / variations -> presets (bank = variation, the Dream "C0" value), drum kits -> bank 128 presets
    layers / splits       -> instrument zones (key range, velocity range)
    sample block          -> SF2 sample (start, loop, end; one-shot when the loop starts before the sample)
    pitch word            -> overridingRootKey + coarse / fine tune (exact within 1/256 semitone)
    amplitude envelope    -> attack / decay / sustain / release (approximate rate mapping)
    exclusive groups      -> exclusiveClass on drum zones
What is not: filter, LFOs, pitch / filter envelopes, velocity curves, key tables, the two level words, effects sends.

The output is a derivative of a licensed bank: write it under scratch/ and never distribute it.
"""
import argparse
import math
import struct

from dreambank import BankError, DreamBank, EffectiveRanges, EnvelopeToSf2, PITCH_C, S16

G = {'pan': 17, 'attackVolEnv': 34, 'decayVolEnv': 36, 'sustainVolEnv': 37, 'releaseVolEnv': 38,
     'instrument': 41, 'keyRange': 43, 'velRange': 44, 'coarseTune': 51, 'fineTune': 52, 'sampleID': 53,
     'sampleModes': 54, 'exclusiveClass': 57, 'overridingRootKey': 58, 'initialAttenuation': 48}
SF2_RATE = 44100


def Tuning(pitch):
    """Root key + coarse + fine for a sample declared at SF2_RATE so that the split's pitch word is honored."""
    rootf = 12 * math.log2(SF2_RATE) - S16(pitch) / 256.0 - PITCH_C
    root = max(0, min(127, int(round(rootf))))
    total = root - rootf                 # semitones to add (coarse + fine)
    coarse = int(round(total))
    fine = int(round((total - coarse) * 100))
    return root, coarse, fine


class Builder:
    def __init__(self, bank):
        self.b = bank
        self.samples = {}      # (start, loopStart, end) -> index
        self.sampleList = []
        self.presets = []      # (name, program, bank, [zone gens dict])
        self.skipped = 0

    def Sample(self, d):
        key = (d['start'], d['loopStart'], d['end'])
        if key not in self.samples:
            self.samples[key] = len(self.sampleList)
            self.sampleList.append(d)
        return self.samples[key]

    def Zones(self, ins, keyFilter=None, exclusive=0):
        zones = []
        for layer in ins['layers']:
            for s, (klo, khi, vlo, vhi) in zip(layer, EffectiveRanges(layer)):
                if keyFilter is not None:
                    if not (klo <= keyFilter <= khi) and len(layer) > 1:
                        continue
                    klo = khi = keyFilter
                try:
                    rec = self.b.Split(s['addr'])
                except BankError:
                    rec = {'sample': None}
                d = rec['sample']
                if not d:
                    self.skipped += 1
                    continue
                root, coarse, fine = Tuning(d['pitch'])
                env = EnvelopeToSf2((rec.get('tail') or {}).get('ampEnvelope'))
                g = {G['keyRange']: (klo, khi), G['velRange']: (vlo, vhi), G['overridingRootKey']: root,
                     G['sampleModes']: 0 if d['oneShot'] else 1,
                     G['attackVolEnv']: env['attack'], G['decayVolEnv']: env['decay'],
                     G['sustainVolEnv']: env['sustainCb'], G['releaseVolEnv']: env['release']}
                if coarse:
                    g[G['coarseTune']] = coarse
                if fine:
                    g[G['fineTune']] = fine
                if exclusive:
                    g[G['exclusiveClass']] = exclusive
                g[G['sampleID']] = self.Sample(d)
                zones.append(g)
        return zones

    def Build(self):
        for prog, var, a in self.b.Programs():
            zones = self.Zones(self.b.Instrument(a))
            if zones:
                self.presets.append(('P%03d V%03d' % (prog, var), prog, var, zones))
        for var, ka in self.b.Kits():
            kit = self.b.Kit(ka)
            excl = {}
            for gi, notes in enumerate(kit['exclusiveGroups']):
                for n in notes:
                    excl[n] = gi + 1
            zones = []
            for ia, notes in kit['instruments'].items():
                ins = self.b.Instrument(ia)
                for n in notes:
                    zones += self.Zones(ins, keyFilter=n, exclusive=excl.get(n, 0))
            if zones:
                self.presets.append(('Kit %03d' % var, var, 128, zones))
        self.presets.sort(key=lambda p: (p[2], p[1]))


def Chunk(fourcc, payload):
    return fourcc + struct.pack('<I', len(payload)) + payload + (b'\0' if len(payload) & 1 else b'')


def List(fourcc, chunks):
    payload = fourcc + b''.join(chunks)
    return b'LIST' + struct.pack('<I', len(payload)) + payload


def Gen(oper, amount):
    if isinstance(amount, tuple):
        return struct.pack('<HBB', oper, amount[0], amount[1])
    return struct.pack('<Hh', oper, int(amount))


def WriteSf2(builder, path, name):
    b = builder.b
    smpl = bytearray()
    shdr = []
    for i, d in enumerate(builder.sampleList):
        pcm = b.Pcm(d['start'], d['end'])
        start = len(smpl) // 2
        smpl += pcm.astype('<i2').tobytes()
        end = len(smpl) // 2
        smpl += b'\0' * 92                        # 46 zero frames (SF2 7.10)
        if d['oneShot']:
            ls, le = start, end
        else:
            ls, le = start + (d['loopStart'] - d['start']), end
        shdr.append(struct.pack('<20sIIIIIBbHH', ('S%05d' % i).encode(), start, end, ls, le, SF2_RATE, 60, 0, 0, 1))
    shdr.append(struct.pack('<20sIIIIIBbHH', b'EOS', 0, 0, 0, 0, 0, 0, 0, 0, 0))
    phdr, pbag, pgen, inst, ibag, igen = [], [], [], [], [], []
    for pi, (pname, prog, bank, zones) in enumerate(builder.presets):
        inst.append(struct.pack('<20sH', pname.encode()[:19], len(ibag)))
        for z in zones:
            ibag.append(struct.pack('<HH', len(igen), 0))
            order = sorted(z.items(), key=lambda kv: (kv[0] != G['keyRange'], kv[0] != G['velRange'],
                                                       kv[0] == G['sampleID'], kv[0]))
            for oper, amount in order:
                igen.append(Gen(oper, amount))
        phdr.append(struct.pack('<20sHHHIII', pname.encode()[:19], prog, bank, len(pbag), 0, 0, 0))
        pbag.append(struct.pack('<HH', len(pgen), 0))
        pgen.append(Gen(G['instrument'], pi))
    phdr.append(struct.pack('<20sHHHIII', b'EOP', 0, 0, len(pbag), 0, 0, 0))
    pbag.append(struct.pack('<HH', len(pgen), 0))
    pgen.append(struct.pack('<HH', 0, 0))
    inst.append(struct.pack('<20sH', b'EOI', len(ibag)))
    ibag.append(struct.pack('<HH', len(igen), 0))
    igen.append(struct.pack('<HH', 0, 0))
    mod = struct.pack('<HHhHH', 0, 0, 0, 0, 0)
    info = List(b'INFO', [Chunk(b'ifil', struct.pack('<HH', 2, 4)), Chunk(b'isng', b'EMU8000\0'),
                          Chunk(b'INAM', name.encode('latin1')[:250] + b'\0')])
    sdta = List(b'sdta', [Chunk(b'smpl', bytes(smpl))])
    pdta = List(b'pdta', [Chunk(b'phdr', b''.join(phdr)), Chunk(b'pbag', b''.join(pbag)), Chunk(b'pmod', mod),
                          Chunk(b'pgen', b''.join(pgen)), Chunk(b'inst', b''.join(inst)),
                          Chunk(b'ibag', b''.join(ibag)), Chunk(b'imod', mod), Chunk(b'igen', b''.join(igen)),
                          Chunk(b'shdr', b''.join(shdr))])
    body = b'sfbk' + info + sdta + pdta
    with open(path, 'wb') as f:
        f.write(b'RIFF' + struct.pack('<I', len(body)) + body)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('bank')
    ap.add_argument('--out', required=True, help='output .sf2 (keep it under scratch/)')
    args = ap.parse_args()
    bank = DreamBank(args.bank)
    bld = Builder(bank)
    bld.Build()
    WriteSf2(bld, args.out, 'Dream bank conversion (local use only): ' + bank.header['copyright'])
    print('%d presets, %d samples, %d splits without a decoded sample block -> %s' % (
        len(bld.presets), len(bld.sampleList), bld.skipped, args.out))


if __name__ == '__main__':
    main()
