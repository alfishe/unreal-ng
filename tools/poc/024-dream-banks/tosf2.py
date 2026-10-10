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
"""
import argparse
import math
import struct

import numpy as np

from dreambank import FIXED_KEY, BankError, DreamBank, EffectiveRanges, EnvelopeToSf2, LoopF0, PITCH_C, \
    RootFromPitch, S16

G = {'startAddrsOffset': 0, 'startAddrsCoarseOffset': 4, 'scaleTuning': 56, 'pan': 17, 'attackVolEnv': 34, 'decayVolEnv': 36, 'sustainVolEnv': 37, 'releaseVolEnv': 38,
     'instrument': 41, 'keyRange': 43, 'velRange': 44, 'coarseTune': 51, 'fineTune': 52, 'sampleID': 53,
     'sampleModes': 54, 'exclusiveClass': 57, 'overridingRootKey': 58, 'initialAttenuation': 48}
SF2_RATE = 44100


def Tuning(pitch, fixed=False, cents=0.0):
    """Root key + coarse + fine for a sample declared at SF2_RATE so that the split's pitch word is honored.
    Fixed-pitch splits get scaleTuning 0: the pitch is then coarse + fine relative to the recorded rate."""
    rootf = 12 * math.log2(SF2_RATE) - S16(pitch) / 256.0 - PITCH_C
    if fixed:
        root, total = 60, FIXED_KEY - rootf
    else:
        root = max(0, min(127, int(round(rootf))))
        total = root - rootf                 # semitones to add (coarse + fine)
    total += cents / 100.0
    coarse = int(round(total))
    fine = int(round((total - coarse) * 100))
    return root, coarse, fine


class Builder:
    def __init__(self, bank, levelField=None, levelStep=0.375):
        self.b = bank
        self.samples = {}      # (start, loopStart, end) -> index
        self.sampleList = []
        self.presets = []      # (name, program, bank, [zone gens dict])
        self.skipped = 0
        self.levelField = levelField    # experimental: 'block' (sample-block level pair) or 'tail' (L/R pair)
        self.levelStep = levelStep      # dB per step below 0xFF (unconfirmed)

    def Sample(self, d):
        key = (id(d['_bank']), d['windowStart'], d['loopStart'], d['end'])
        if key not in self.samples:
            self.samples[key] = len(self.sampleList)
            self.sampleList.append(d)
        return self.samples[key]

    def Zones(self, ins, keyFilter=None, exclusive=0, bank=None, layers=None, cents=0.0):
        bank = bank or self.b
        zones = []
        for li, layer in enumerate(ins['layers']):
            if layers is not None and li not in layers:
                continue
            for s, (klo, khi, vlo, vhi) in zip(layer, EffectiveRanges(layer)):
                if keyFilter is not None:
                    if not (klo <= keyFilter <= khi) and len(layer) > 1:
                        continue
                    klo = khi = keyFilter
                try:
                    rec = bank.Split(s['addr'])
                except BankError:
                    rec = {'sample': None}
                d = rec['sample']
                if not d:
                    self.skipped += 1
                    continue
                d['_bank'] = bank
                root, coarse, fine = Tuning(d['pitch'], d['fixedPitch'], cents)
                env = EnvelopeToSf2((rec.get('tail') or {}).get('ampEnvelope'))
                # Drum splits keep their note-off release: the release rates are tailored per drum (cymbals ~4 s,
                # toms ~1.5 s, snares ~0.1 s), which only makes sense if note-off is honored; one-shot samples end
                # on their own anyway.
                g = {G['keyRange']: (klo, khi), G['velRange']: (vlo, vhi), G['overridingRootKey']: root,
                     G['sampleModes']: 0 if d['oneShot'] else 1,
                     G['attackVolEnv']: env['attack'], G['decayVolEnv']: env['decay'],
                     G['sustainVolEnv']: env['sustainCb'], G['releaseVolEnv']: env['release']}
                if d['fixedPitch']:
                    g[G['scaleTuning']] = 0
                off = d['start'] - d['windowStart']    # playback starts inside the looped region
                if off:
                    g[G['startAddrsCoarseOffset']], g[G['startAddrsOffset']] = divmod(off, 32768)
                if coarse:
                    g[G['coarseTune']] = coarse
                if fine:
                    g[G['fineTune']] = fine
                if exclusive:
                    g[G['exclusiveClass']] = exclusive
                if self.levelField:
                    if self.levelField == 'block':
                        lv = max(d['level'] >> 8, d['level'] & 0xFF)
                    else:
                        t = (rec.get('tail') or {}).get('words') or [0, 0, 0xFFFF]
                        lv = max(t[2] >> 8, t[2] & 0xFF) if (t[0] & 0xFF) == 0x0E else 0xFF
                    g[G['initialAttenuation']] = int(round((0xFF - lv) * self.levelStep * 10))
                g[G['sampleID']] = self.Sample(d)
                zones.append(g)
        return zones

    def Build(self, edits=None):
        """edits: {(prog0, var): {'source': (bank, addr), 'layers': set or None, 'recenter': bool}} (user-side)"""
        edits = edits or {}
        for prog, var, a in self.b.Programs():
            e = edits.get((prog, var), {})
            bank, addr = e.get('source', (self.b, a))
            ins = bank.Instrument(addr)
            cents = -LayerMeanCents(bank, ins, e.get('layers')) if e.get('recenter') else 0.0
            zones = self.Zones(ins, bank=bank, layers=e.get('layers'), cents=cents)
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


def LayerCents(bank, layer):
    """median pitch offset (cents) of a layer's looped samples played at their decoded root"""
    out = []
    for s in layer:
        d = bank.Split(s['addr'])['sample']
        if not d or d['oneShot']:
            continue
        loop = bank.Pcm(d['loopStart'], d['end']).astype(float)
        if len(loop) < 16:
            continue
        seg = np.tile(loop, int(np.ceil(8000 / len(loop))))[:8000] if len(loop) < 8000 else loop[:8000]
        f = LoopF0(seg, 44100.0)
        if f and f > 0:
            c = (69 + 12 * math.log2(f / 440) - RootFromPitch(d['pitch'])) * 100
            c -= 1200 * round(c / 1200)
            if abs(c) < 100:
                out.append(c)
    return float(np.median(out)) if out else None


def LayerMeanCents(bank, ins, layers=None):
    cs = [LayerCents(bank, L) for i, L in enumerate(ins['layers']) if layers is None or i in layers]
    cs = [c for c in cs if c is not None]
    return float(np.mean(cs)) if cs else 0.0


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
    smpl = bytearray()
    shdr = []
    for i, d in enumerate(builder.sampleList):
        pcm = d['_bank'].Pcm(d['windowStart'], d['end'])
        start = len(smpl) // 2
        smpl += pcm.astype('<i2').tobytes()
        end = len(smpl) // 2
        smpl += b'\0' * 92                        # 46 zero frames (SF2 7.10)
        if d['oneShot']:
            ls, le = start, end
        else:
            ls, le = start + (d['loopStart'] - d['windowStart']), end
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


def ProgVar(text):
    parts = text.split(':')
    return int(parts[0]) - 1, int(parts[1]) if len(parts) > 1 else 0


def ParseEdits(bank, args):
    edits = {}
    banks = {'self': bank}
    for spec in args.replace:
        dst, src = spec.split('=', 1)
        path, rest = (src.split(':', 1) + ['1'])[:2] if ':' in src else (src, '1')
        if path not in banks:
            banks[path] = DreamBank(path)
        sp, sv = ProgVar(rest)
        recs = {(p, v): a for p, v, a in banks[path].Programs()}
        if (sp, sv) not in recs:
            raise SystemExit('--replace %s: %s has no program %d variation %d' % (spec, path, sp + 1, sv))
        edits.setdefault(ProgVar(dst), {})['source'] = (banks[path], recs[(sp, sv)])
    for spec in args.layer:
        dst, ls = spec.split('=', 1)
        edits.setdefault(ProgVar(dst), {})['layers'] = {int(x) for x in ls.split(',')}
    for spec in args.recenter:
        edits.setdefault(ProgVar(spec), {})['recenter'] = True
    return edits


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('bank')
    ap.add_argument('--out', required=True, help='output .sf2')
    ap.add_argument('--level-field', choices=['block', 'tail'],
                    help='EXPERIMENT: turn an undecoded level byte pair into initialAttenuation (unconfirmed)')
    ap.add_argument('--level-step', type=float, default=0.375, help='dB per level step for --level-field')
    ap.add_argument('--replace', action='append', default=[], metavar='P[:V]=SRC:P[:V]',
                    help='user-side workaround: take program P (GM number 1-128), variation V, from SRC (a .dxb/.b16 '
                         'path, or "self") program P[:V]; e.g. 19=self:19:8 or 19=GMBK5X64.DXB:19')
    ap.add_argument('--layer', action='append', default=[], metavar='P[:V]=L[,L]',
                    help='user-side workaround: keep only these layers (0-based) of program P')
    ap.add_argument('--recenter', action='append', default=[], metavar='P[:V]',
                    help='user-side workaround: shift program P so its layers\' mean measured pitch is 0 cents '
                         '(keeps the detune between layers)')
    args = ap.parse_args()
    bank = DreamBank(args.bank)
    bld = Builder(bank, args.level_field, args.level_step)
    bld.Build(ParseEdits(bank, args))
    WriteSf2(bld, args.out, 'Dream bank conversion: ' + bank.header['copyright'])
    print('%d presets, %d samples, %d splits without a decoded sample block -> %s' % (
        len(bld.presets), len(bld.sampleList), bld.skipped, args.out))


if __name__ == '__main__':
    main()
