#!/usr/bin/env python3
"""Reader for Dream SAM5000 compiled sound banks (.DXB for DreamBlaster X2/X3, .B16 for X16).

Reverse-engineered from the bank files (no public specification exists). The layout, the field meanings and how
each was confirmed are in docs/inprogress/2026-10-03-zx-multisound/sam7-dream-banks.md.

Units: a bank is an image of 16-bit little-endian words. Every address in the file is a word address in that image.

Subcommands:
    info      header fields, counts, consistency checks
    programs  program / variation / drum-kit map; --map compares with a Dream bank-map PDF (pdftotext -layout text)
    dump      the whole parsed structure as JSON
    extract   unique samples as WAV (with a 'smpl' loop chunk), named after the first program that uses them
    pitchcheck  pitch of the sustained loops at their decoded root key vs equal temperament
    selfcheck structural self-checks (exit status 1 on failure)
"""
import argparse
import json
import math
import os
import re
import struct
import subprocess
import sys

import numpy as np

# Empirical pitch constant (see the research doc, "Pitch"): a split plays its sample at
#   f(note) = 2 ** ((note + pitch / 256 + PITCH_C) / 12) samples per second.
# Fitted on GXSCC GM.dxb against its source SF2 (256 splits, all 44.1 kHz, one value) and on GUD_104.DXB against
# GeneralUser GS 1.44 (121 splits at 11 different sample rates: slope 1 in log2(rate) confirmed).
PITCH_C = 120.384

# Fixed-pitch splits (bit 5 of the word before the pitch word: every Dream drum, a few effect sounds) ignore the key.
# They play as if the key were FIXED_KEY. Empirical: the decoded roots of the kit splits cluster at 61.1 (GMBK5X128
# 2.03, 66 notes, IQR 0.6) and 61.09 for the GeneralUser 44.1 kHz drums in GUD_104, i.e. 61.1 plays a 44.1 kHz drum
# sample at its recorded pitch.
FIXED_KEY = 61.1

GM_NAMES = [
    'Acoustic Grand Piano', 'Bright Acoustic Piano', 'Electric Grand Piano', 'Honky-tonk Piano', 'Electric Piano 1',
    'Electric Piano 2', 'Harpsichord', 'Clavinet', 'Celesta', 'Glockenspiel', 'Music Box', 'Vibraphone', 'Marimba',
    'Xylophone', 'Tubular Bells', 'Dulcimer', 'Drawbar Organ', 'Percussive Organ', 'Rock Organ', 'Church Organ',
    'Reed Organ', 'Accordion', 'Harmonica', 'Tango Accordion', 'Acoustic Guitar (nylon)', 'Acoustic Guitar (steel)',
    'Electric Guitar (jazz)', 'Electric Guitar (clean)', 'Electric Guitar (muted)', 'Overdriven Guitar',
    'Distortion Guitar', 'Guitar Harmonics', 'Acoustic Bass', 'Electric Bass (finger)', 'Electric Bass (pick)',
    'Fretless Bass', 'Slap Bass 1', 'Slap Bass 2', 'Synth Bass 1', 'Synth Bass 2', 'Violin', 'Viola', 'Cello',
    'Contrabass', 'Tremolo Strings', 'Pizzicato Strings', 'Orchestral Harp', 'Timpani', 'String Ensemble 1',
    'String Ensemble 2', 'Synth Strings 1', 'Synth Strings 2', 'Choir Aahs', 'Voice Oohs', 'Synth Choir',
    'Orchestra Hit', 'Trumpet', 'Trombone', 'Tuba', 'Muted Trumpet', 'French Horn', 'Brass Section', 'Synth Brass 1',
    'Synth Brass 2', 'Soprano Sax', 'Alto Sax', 'Tenor Sax', 'Baritone Sax', 'Oboe', 'English Horn', 'Bassoon',
    'Clarinet', 'Piccolo', 'Flute', 'Recorder', 'Pan Flute', 'Blown Bottle', 'Shakuhachi', 'Whistle', 'Ocarina',
    'Lead 1 (square)', 'Lead 2 (sawtooth)', 'Lead 3 (calliope)', 'Lead 4 (chiff)', 'Lead 5 (charang)',
    'Lead 6 (voice)', 'Lead 7 (fifths)', 'Lead 8 (bass + lead)', 'Pad 1 (new age)', 'Pad 2 (warm)',
    'Pad 3 (polysynth)', 'Pad 4 (choir)', 'Pad 5 (bowed)', 'Pad 6 (metallic)', 'Pad 7 (halo)', 'Pad 8 (sweep)',
    'FX 1 (rain)', 'FX 2 (soundtrack)', 'FX 3 (crystal)', 'FX 4 (atmosphere)', 'FX 5 (brightness)',
    'FX 6 (goblins)', 'FX 7 (echoes)', 'FX 8 (sci-fi)', 'Sitar', 'Banjo', 'Shamisen', 'Koto', 'Kalimba', 'Bagpipe',
    'Fiddle', 'Shanai', 'Tinkle Bell', 'Agogo', 'Steel Drums', 'Woodblock', 'Taiko Drum', 'Melodic Tom',
    'Synth Drum', 'Reverse Cymbal', 'Guitar Fret Noise', 'Breath Noise', 'Seashore', 'Bird Tweet', 'Telephone Ring',
    'Helicopter', 'Applause', 'Gunshot']

FILLER = 0x1DEF          # unused header table slots
HDR_PAGES = 0xE9         # 64K-word page numbers of the structure area
HDR_PROGRAMS = 0x129     # 128 program pointers
HDR_KITS = 0x1A9         # drum-kit records follow the program pointers


class BankError(Exception):
    pass


def S16(x):
    return x - 0x10000 if x >= 0x8000 else x


class DreamBank:
    def __init__(self, path):
        self.path = path
        raw = np.fromfile(path, dtype='<u2')
        if raw.size < 0x200 or raw[:2].tobytes() != b'Bank':
            raise BankError('no "Bank" magic')
        self.fileWords = int(raw.size)
        self.raw = np.concatenate([raw, np.zeros(64, dtype='<u2')])
        self.size = int(raw[4]) | int(raw[5]) << 16
        # Banks bigger than the file leave a hole of (size - file words) right after the header area: the image
        # address of everything past the hole is file index + gap (seen on 11 of 16 DXB, never on B16).
        self.gap = self.size - self.fileWords
        if self.gap < 0:
            raise BankError('file longer than the image size field')
        self.header = self._Header()
        self._pages = self._Pages()

    # --- addressing ---------------------------------------------------------------------------------------------
    def Index(self, addr):
        i = addr - self.gap if (self.gap and addr >= self.gap) else addr
        if i < 0 or i >= self.fileWords:
            raise BankError('address 0x%x outside the file' % addr)
        return i

    def W(self, addr):
        return int(self.raw[self.Index(addr)])

    def Words(self, addr, n):
        i = self.Index(addr)
        return [int(x) for x in self.raw[i:i + n]]

    def Pcm(self, start, endInclusive):
        i, j = self.Index(start), self.Index(endInclusive)
        return self.raw[i:j + 1].view('<i2')

    # --- header -------------------------------------------------------------------------------------------------
    def _Header(self):
        r = self.raw
        b = r[:0x200].tobytes()
        modern = int(r[0x47]) != 0x0100        # 2014/2015 compiler: no version string, no word-8 checksum
        pathOff = 0x12 if modern else 0x10
        text = b[0xA4:0x1D0]
        copyright = b''.join(re.findall(rb'[\x20-\x7e\xa9]{4,}', text)).decode('latin1')
        return {
            'magic': 'Bank',
            'imageWords': self.size,
            'fileWords': self.fileWords,
            'imageGapWords': self.gap,
            'gapField': int(r[2]) | int(r[3]) << 16,
            'loadAddress': int(r[6]) | int(r[7]) << 16,
            'checksum16': int(r[8]) if modern else None,
            'checksumOk': (int(r[:self.fileWords].astype(np.uint64).sum()) & 0xFFFF) == 0 if modern else None,
            'sourcePath': b[pathOff:0x88].split(b'\0')[0].decode('latin1'),
            'version': b[0x88:0x8E].split(b'\0')[0].decode('latin1'),
            'w47': int(r[0x47]), 'w48': int(r[0x48]),
            'target': b[0x92:0x98].decode('latin1'),
            'w4c': int(r[0x4C]), 'w4d': int(r[0x4D]),
            'imageWords2': int(r[0x4E]) | int(r[0x4F]) << 16,
            'word50': int(r[0x50]) | int(r[0x51]) << 16,
            'extraTable': [int(r[0x52 + 2 * i]) << 16 | int(r[0x53 + 2 * i]) for i in range(3)
                           if r[0x52 + 2 * i] != FILLER],
            'copyright': copyright,
            'wE8': int(r[0xE8]),
        }

    def _Pages(self):
        pg = [int(x) for x in self.raw[HDR_PAGES:HDR_PROGRAMS]]
        while len(pg) > 1 and pg[-1] == 0:
            pg.pop()
        return pg

    def Far(self, pageIndex, low):
        return (self._pages[pageIndex] << 16) | low

    # --- program and drum-kit maps ------------------------------------------------------------------------------
    def Programs(self):
        """[(program, variation, instrumentAddr)]; records are (pageIndex << 8 | variation, low16) word pairs"""
        out = []
        for prog in range(128):
            p = int(self.raw[HDR_PROGRAMS + prog])
            while not (self.raw[p] == 0xFFFF and self.raw[p + 1] == 0xFFFF):
                w = int(self.raw[p])
                out.append((prog, w & 0xFF, self.Far(w >> 8, int(self.raw[p + 1]))))
                p += 2
        return out

    def Kits(self):
        """[(variation, kitAddr)]; records are (variation, pageIndex << 8, low16) word triples"""
        out = []
        p = HDR_KITS
        while not (self.raw[p] == 0xFFFF and self.raw[p + 1] == 0xFFFF):
            out.append((int(self.raw[p]), self.Far(int(self.raw[p + 1]) >> 8, int(self.raw[p + 2]))))
            p += 3
        return out

    def Kit(self, addr):
        w = self.Words(addr, 2)
        if w[0] != 0xFFFF:
            raise BankError('kit at 0x%x: no 0xFFFF lead word' % addr)
        lo, hi = w[1] & 0xFF, w[1] >> 8
        n = hi - lo + 1
        notes = {}
        page = addr & ~0xFFFF
        for i, ptr in enumerate(self.Words(addr + 2, n)):
            notes.setdefault(page | ptr, []).append(lo + i)
        # exclusive (choke) groups: byte stream [count, note...]... terminated by 0xFF
        b = self.raw[self.Index(addr + 2 + n):self.Index(addr + 2 + n) + 64].tobytes()
        groups, i = [], 0
        while i < len(b) and b[i] not in (0xFF, 0) and b[i] < 16:
            groups.append(list(b[i + 1:i + 1 + b[i]]))
            i += 1 + b[i]
        return {'noteLo': lo, 'noteHi': hi, 'instruments': notes, 'exclusiveGroups': groups}

    # --- instruments --------------------------------------------------------------------------------------------
    def Instrument(self, addr):
        w0 = self.W(addr)
        flags, table = w0 & 0xFF, None
        if (w0 >> 8) == 0:
            # early layout (GMBK5X128 2015): w1 = layers << 8 | splits of layer 0, then one count per further layer
            w1 = self.W(addr + 1)
            nl = max(1, w1 >> 8)
            counts = [w1 & 0x7F] + [self.W(addr + 1 + i) & 0x7F for i in range(1, nl)]
            p = addr + 1 + nl
        else:
            nl = (w0 >> 8) & 0x7F
            base = addr + 1
            if flags & 1:  # 128-word per-key table (seen on effect sounds: reverse cymbal, helicopter, gunshot)
                table = self.Words(addr + 1, 128)
                base = addr + 129
            counts = [self.W(base + i) & 0x7F for i in range(nl)]
            p = base + nl
        layers = []
        page = addr & ~0xFFFF
        for n in counts:
            splits = []
            for _ in range(n):
                lo, hi, ptr = self.Words(p, 3)
                p += 3
                splits.append({'loKey': lo >> 8, 'loVel': lo & 0xFF, 'hiKey': hi >> 8, 'hiVel': hi & 0xFF,
                               'addr': page | ptr})
            layers.append(splits)
        return {'addr': addr, 'word0': w0, 'flags': flags, 'keyTable': table, 'layers': layers}

    # --- split records ------------------------------------------------------------------------------------------
    def Split(self, addr):
        """Decode the parts of a split record that are understood: sample block, levels, amplitude envelope."""
        head = self.W(addr)
        lo = addr & 0xFFFF
        ptrs = []
        i = 1
        while True:
            x = self.W(addr + i)
            if 0 < x - lo < 0x80:
                ptrs.append(x - lo)
                i += 1
            else:
                break
        sample = self._FindSampleBlock(addr, 1 + len(ptrs))
        out = {'addr': addr, 'head': head, 'subBlockOffsets': ptrs, 'sample': sample}
        if sample:
            out['tail'] = self._Tail(addr + sample['offset'] + sample['words'])
        return out

    def _DecodeSampleBlock(self, a, k, alt):
        v = self.Words(a + k - 1, 13)
        pre, v = v[0], v[1:]
        ext = v[1]
        hi = ((ext >> 6) & 0xF) << 24                # address bits 24-27 (banks over 16 M words)
        ls = hi + ((v[2] << 8) | (v[3] >> 8))        # loop start - 1
        st = hi + ((v[6] << 8) | (v[5] & 0xFF))
        if alt:   # rare variant (BURAN/GUD, about 1 %): end before the level word, then a second end address
            en = hi + ((v[8] << 8) | (v[7] & 0xFF))
            level, en2, n = v[9], hi + ((v[11] << 8) | (v[10] & 0xFF)), 12
        else:
            level, en, en2, n = v[7], hi + ((v[9] << 8) | (v[8] & 0xFF)), None, 10
        return {'offset': k, 'words': n, 'pre': pre, 'pitch': v[0], 'ext': ext, 'loopStart': ls + 1, 'start': st,
                'end': en, 'end2': en2, 'level': level, 'lsLowByte': v[3] & 0xFF, 'w4': v[4], 'w5hi': v[5] >> 8,
                'variant': 'B' if alt else 'A'}

    def _FindSampleBlock(self, a, first, last=44):
        best = None
        for k in range(first, last):
            for alt in (False, True):
                try:
                    d = self._DecodeSampleBlock(a, k, alt)
                except BankError:
                    return best[1] if best else None
                if d['lsLowByte'] != 1 or not (d['pre'] & 0x80):
                    continue
                st, ls, en = d['start'], d['loopStart'], d['end']
                if not (st > 0x100 and st <= en and ls <= en + 1 and en - st < (1 << 22) and st - ls < (1 << 16)):
                    continue
                if en >= self.size or (d['end2'] is not None and abs(d['end2'] - en) >= (1 << 20)):
                    continue
                try:
                    sep = self.W(st - 1) == 0
                except BankError:
                    continue
                score = 2 * sep + (not alt)
                if best is None or score > best[0]:
                    best = (score, d)
            if best and best[0] >= 2:
                break
        if best:
            # A loop that starts at (or before) the sample start is the compiler's encoding of "no loop": 431 of 450
            # such splits in GUD_104 use GeneralUser samples that are not looped (sampleModes 0) in the source, all
            # 38 GXSCC drums too. Real whole-sample loops are stored starting one word later (GXSCC: 1018 of 1018).
            # A loop that starts well before the start (808 hi-hat and Reverse Cymbal in GMBK5X128 2.03, GUD 90) is a
            # real loop: the split starts inside its looped region. windowStart is where the sample data begins.
            d0 = best[1]
            d0['oneShot'] = d0['start'] - 1 <= d0['loopStart'] <= d0['start']
            d0['windowStart'] = min(d0['start'], d0['loopStart'])
            best[1]['fixedPitch'] = bool(best[1]['pre'] & 0x20)
        return best[1] if best else None

    def _Tail(self, a):
        """Words after the sample block: [amp word][amp word][L/R level pair] then envelope blocks."""
        try:
            w = self.Words(a, 24)
        except BankError:
            return None
        env, j = None, None
        for i in range(3, 20):
            if (w[i] >> 8) == 0x60:
                j = i
                break
        if j is not None:
            s = j - 1
            while s > 2 and not ((w[s] >> 8) in (0x00, 0x7F) and (w[s] & 0xFF) < 0x10):
                s -= 1
            env = w[s:j + 1]
        return {'words': w[:16], 'levelPair': w[2] if (w[0] & 0xFF) == 0x0E else None, 'ampEnvelope': env}


# --- envelope approximation (see the doc: correlation with GeneralUser GS 1.44 on GUD_104, r = -0.72) -------------
def RateToTimecents(byte, kind):
    if kind == 'release':
        return int(round(10424.5 - 142.7 * byte))
    if kind == 'decay':
        return int(round(8395.7 - 84.9 * byte))
    return -12000 if byte >= 0xF0 else int(round(10424.5 - 142.7 * byte))


def EnvelopeToSf2(env):
    """Map an envelope word list to SF2 volume-envelope generators (approximate; heuristics in the doc)."""
    g = {'attack': -12000, 'decay': -12000, 'sustainCb': 0, 'release': -3000}
    if not env:
        return g
    segs = env[1:]
    att = [x for x in segs if (x >> 8) == 0x1F]
    if att:
        g['attack'] = RateToTimecents(att[0] & 0xFF, 'attack')
    dec = [x for x in segs if 0x80 <= (x >> 8) <= 0x8F]
    if dec:
        g['decay'] = RateToTimecents(dec[0] & 0xFF, 'decay')
        g['sustainCb'] = 1000
    rel = [x for x in segs if (x >> 8) == 0x60]
    if rel:
        g['release'] = RateToTimecents(rel[-1] & 0xFF, 'release')
    return g


def EffectiveRanges(splits):
    """Stored low key / velocity is the previous split's high (exclusive) except for the first split."""
    out = []
    for i, s in enumerate(splits):
        klo, khi = s['loKey'] & 0x7F, s['hiKey'] & 0x7F
        vlo, vhi = s['loVel'] & 0x7F, s['hiVel'] & 0x7F
        if klo and (i > 0 or klo == 0x7F):
            klo = min(127, klo + 1)
        if vlo:
            vlo = min(127, vlo + 1)
        if s['hiKey'] & 0x80 and khi < klo:
            khi = klo
        out.append((klo, max(klo, khi), vlo, max(vlo, vhi)))
    return out


def RootFromPitch(pitch, rate=44100.0):
    """Fractional root key for a sample stored at `rate` so that the split's pitch word is honored."""
    return 12 * math.log2(rate) - S16(pitch) / 256.0 - PITCH_C


# --- traversal --------------------------------------------------------------------------------------------------
def Walk(bank):
    """Every (kind, program/kit, variation, layer, split index, split dict, decoded record)."""
    for prog, var, a in bank.Programs():
        ins = bank.Instrument(a)
        for li, layer in enumerate(ins['layers']):
            for si, s in enumerate(layer):
                yield 'melodic', prog, var, li, si, s, ins
    for var, ka in bank.Kits():
        kit = bank.Kit(ka)
        for ia, notes in kit['instruments'].items():
            ins = bank.Instrument(ia)
            for li, layer in enumerate(ins['layers']):
                for si, s in enumerate(layer):
                    yield 'drum', notes, var, li, si, s, ins


def Stats(bank):
    ok = bad = 0
    variants = {}
    samples = set()
    for kind, p, v, li, si, s, ins in Walk(bank):
        try:
            rec = bank.Split(s['addr'])
        except BankError:
            rec = {'sample': None}
        if rec['sample']:
            ok += 1
            d = rec['sample']
            variants[d['variant']] = variants.get(d['variant'], 0) + 1
            samples.add((d['start'], d['loopStart'], d['end']))
        else:
            bad += 1
    words, cur = 0, None   # union of [start, end] intervals
    for st, en in sorted((st, en) for st, ls, en in samples):
        if cur and st <= cur[1] + 1:
            cur[1] = max(cur[1], en)
        else:
            if cur:
                words += cur[1] - cur[0] + 1
            cur = [st, en]
    if cur:
        words += cur[1] - cur[0] + 1
    return {'splitsDecoded': ok, 'splitsUndecoded': bad, 'layouts': variants, 'uniqueSampleRegions': len(samples),
            'uniqueSampleStarts': len({st for st, ls, en in samples}), 'sampleWords': words}


# --- commands ---------------------------------------------------------------------------------------------------
def CmdInfo(args):
    b = DreamBank(args.bank)
    progs = b.Programs()
    info = dict(b.header)
    info['pages'] = b._pages
    info['programRecords'] = len(progs)
    info['variations'] = len(progs) - len({p for p, v, a in progs})
    info['kits'] = [v for v, a in b.Kits()]
    info.update(Stats(b))
    print(json.dumps(info, indent=2))


def ParseBankMap(path):
    """(program 1-based, C0) pairs from a Dream bank-map PDF ('SOUND VARIATIONS' table, pdftotext -layout)."""
    if path.lower().endswith('.pdf'):
        text = subprocess.run(['pdftotext', '-layout', path, '-'], capture_output=True, text=True, check=True).stdout
    else:
        text = open(path, encoding='utf-8', errors='replace').read()
    sec = text.split('SOUND VARIATIONS', 1)[-1]
    sec = re.split(r'DRUM SET|MT-32 SOUND|MT‐32', sec)[0]
    pairs = set()
    for line in sec.splitlines():
        m = re.match(r'\s{0,6}(\d{1,3})\s+(\d{1,3})\s+\S', line)
        if not m:
            continue
        prog = int(m.group(1))
        rest = line[m.start(2):]
        for c0 in re.findall(r'(?:^|\s{2,})(\d{1,3})\s+(?=\S)', rest):
            if 1 <= prog <= 128 and 0 <= int(c0) <= 127:
                pairs.add((prog, int(c0)))
    return pairs


def CmdPrograms(args):
    b = DreamBank(args.bank)
    progs = b.Programs()
    byProg = {}
    for p, v, a in progs:
        byProg.setdefault(p, []).append((v, a))
    for p in range(128):
        vs = byProg.get(p, [])
        print('%3d %-26s %s' % (p + 1, GM_NAMES[p], ' '.join('%d@%x' % (v, a) for v, a in vs)))
    for v, a in b.Kits():
        k = b.Kit(a)
        print('kit %3d: notes %d-%d, %d instruments, exclusive groups %s' % (
            v, k['noteLo'], k['noteHi'], len(k['instruments']), k['exclusiveGroups']))
    if args.map:
        want = ParseBankMap(args.map)
        have = {(p + 1, v) for p, v, a in progs if v not in (0, 127)}
        print('bank map: %d variations (C0 1..126) listed; bank has %d; in both %d; only in map %s; only in bank %s' % (
            len(want), len(have), len(want & have), sorted(want - have)[:20], sorted(have - want)[:20]))


def CmdDump(args):
    b = DreamBank(args.bank)
    out = {'header': b.header, 'pages': b._pages, 'programs': [], 'kits': []}
    for p, v, a in b.Programs():
        ins = b.Instrument(a)
        layers = []
        for layer in ins['layers']:
            rng = EffectiveRanges(layer)
            sl = []
            for s, r in zip(layer, rng):
                rec = b.Split(s['addr'])
                sl.append({'keys': r[:2], 'vels': r[2:], 'stored': s, 'record': rec})
            layers.append(sl)
        out['programs'].append({'program': p, 'variation': v, 'addr': a, 'word0': ins['word0'],
                                'hasKeyTable': ins['keyTable'] is not None, 'layers': layers})
    for v, a in b.Kits():
        k = b.Kit(a)
        k['instruments'] = {hex(x): n for x, n in k['instruments'].items()}
        out['kits'].append({'variation': v, 'addr': a, **k})
    s = json.dumps(out, indent=1 if args.pretty else None, default=lambda o: None)
    if args.out:
        open(args.out, 'w').write(s)
    else:
        print(s)


def WriteWav(path, pcm, rate, loop=None, root=60):
    pcm = np.asarray(pcm, dtype='<i2')
    fmt = struct.pack('<HHIIHH', 1, 1, int(rate), int(rate) * 2, 2, 16)
    chunks = b'fmt ' + struct.pack('<I', len(fmt)) + fmt
    if loop:
        smpl = struct.pack('<9I', 0, 0, int(1e9 / rate), root, 0, 0, 0, 1, 0)
        smpl += struct.pack('<6I', 0, 0, loop[0], loop[1], 0, 0)
        chunks += b'smpl' + struct.pack('<I', len(smpl)) + smpl
    data = pcm.tobytes()
    chunks += b'data' + struct.pack('<I', len(data)) + data
    with open(path, 'wb') as f:
        f.write(b'RIFF' + struct.pack('<I', 4 + len(chunks)) + b'WAVE' + chunks)


def CmdExtract(args):
    b = DreamBank(args.bank)
    os.makedirs(args.out, exist_ok=True)
    done = set()
    n = 0
    for kind, p, v, li, si, s, ins in Walk(b):
        if args.program is not None and (kind != 'melodic' or p != args.program):
            continue
        try:
            rec = b.Split(s['addr'])
        except BankError:
            continue
        d = rec['sample']
        if not d or d['start'] in done:
            continue
        done.add(d['start'])
        rootf = RootFromPitch(d['pitch'])
        root = int(round(rootf))
        rate = 2 ** ((root + S16(d['pitch']) / 256.0 + PITCH_C) / 12)   # pitch-exact rate for an integer root
        pcm = b.Pcm(d['windowStart'], d['end'])
        loop = None if d['oneShot'] else (d['loopStart'] - d['windowStart'], d['end'] - d['windowStart'])
        tag = ('p%03d-v%03d' % (p, v)) if kind == 'melodic' else ('kit%03d-n%s' % (v, '-'.join(map(str, p[:3]))))
        name = '%s-l%d-s%02d-%06x.wav' % (tag, li, si, d['start'])
        WriteWav(os.path.join(args.out, name), pcm, round(rate), loop, max(0, min(127, root)))
        n += 1
        if args.limit and n >= args.limit:
            break
    print('%d WAV files in %s' % (n, args.out))


def SelfCheck(b, verbose=True):
    fails = []

    def Check(cond, what):
        if not cond:
            fails.append(what)
        if verbose:
            print(('ok   ' if cond else 'FAIL ') + what)

    h = b.header
    Check(h['imageWords'] == h['imageWords2'], 'image size at word 4 equals the copy at word 0x4E')
    Check(h['gapField'] == h['imageGapWords'], 'words 2-3 (0x%x) = image size - file size (the hole)' % h['gapField'])
    Check(h['loadAddress'] == 0x800000, 'load address 0x800000')
    Check(h['target'] == '5000IS', 'target tag 5000IS')
    if h['checksumOk'] is not None:
        Check(h['checksumOk'], '16-bit word sum of the file is zero (word 8 is the checksum)')
    progs = b.Programs()
    missing = sorted(set(range(128)) - {p for p, v, a in progs})
    kits = b.Kits()
    if kits:
        Check(len(missing) <= 2, 'programs with no record: %s (GMBK5X64 has none for 56 Orchestra Hit)' % missing)
        Check(kits[0][0] == 0, 'drum kit 0 present')
    elif verbose:
        print('info partial bank: %d programs, no drum kit' % (128 - len(missing)))
    Check(all(v == 0 for p, v, a in progs[:1]), 'first record is variation 0')
    for v, a in kits:
        k = b.Kit(a)
        Check(0 <= k['noteLo'] <= k['noteHi'] <= 127, 'kit %d note range %d..%d' % (v, k['noteLo'], k['noteHi']))
    st = Stats(b)
    total = st['splitsDecoded'] + st['splitsUndecoded']
    Check(st['splitsDecoded'] >= 0.98 * total, 'sample block decoded for %d of %d splits' % (st['splitsDecoded'], total))
    # samples are 16-bit PCM: neighbor-difference energy far below that of random words
    if st['uniqueSampleStarts']:
        diffs = []
        for kind, p, v, li, si, s, ins in Walk(b):
            rec = b.Split(s['addr'])
            if rec['sample']:
                pcm = b.Pcm(rec['sample']['start'], min(rec['sample']['end'], rec['sample']['start'] + 4096)).astype(float)
                if len(pcm) > 64 and np.mean(np.abs(pcm)) > 64:
                    diffs.append(np.mean(np.abs(np.diff(pcm))) / np.mean(np.abs(pcm)))
            if len(diffs) >= 200:
                break
        med = float(np.median(diffs)) if diffs else 9
        Check(med < 0.8, 'sample data is smooth 16-bit PCM (median |diff|/|x| = %.2f; random words give ~1.3)' % med)
    return fails


def CmdSelfCheck(args):
    fails = []
    for path in args.bank:
        print('==', path)
        fails += SelfCheck(DreamBank(path))
    print('%d failed checks' % len(fails))
    sys.exit(1 if fails else 0)



def LoopF0(seg, rate, lo=20.0, hi=4000.0):
    """autocorrelation pitch of a (tiled) loop, Hz"""
    seg = seg - seg.mean()
    n = len(seg)
    ac = np.fft.irfft(np.abs(np.fft.rfft(seg * np.hanning(n), 2 * n)) ** 2)[:n]
    if ac[0] <= 0:
        return None
    ac /= ac[0]
    a, b = int(rate / hi), int(rate / lo)
    lag = a + int(np.argmax(ac[a:b]))
    for div in (4, 3, 2):
        l2 = lag // div
        if l2 > a:
            w = max(1, l2 // 20)
            k = l2 - w + int(np.argmax(ac[l2 - w:l2 + w + 1]))
            if ac[k] > 0.85 * ac[lag]:
                lag = k
                break
    if ac[lag] < 0.3 or not 1 <= lag < n - 1:
        return None
    y0, y1, y2 = ac[lag - 1], ac[lag], ac[lag + 1]
    den = y0 - 2 * y1 + y2
    return rate / (lag + ((y0 - y2) / (2 * den) if den else 0))


def CmdPitchCheck(args):
    """Pitch of every looped sample of programs 17-24 and 41-80 (sustained families) played at its decoded root,
    against equal temperament. Validates the pitch word decoding on banks without a known source."""
    b = DreamBank(args.bank)
    errs, seen = [], set()
    for p, v, a in b.Programs():
        if not (16 <= p <= 23 or 40 <= p <= 79) or v != 0:
            continue
        for layer in b.Instrument(a)['layers']:
            for s in layer:
                d = b.Split(s['addr'])['sample']
                if not d or d['oneShot'] or d['start'] in seen:
                    continue
                seen.add(d['start'])
                loop = b.Pcm(d['loopStart'], d['end']).astype(float)
                seg = np.tile(loop, int(np.ceil(8000 / len(loop))))[:8000] if len(loop) < 8000 else loop[:8000]
                f0 = LoopF0(seg, 44100.0)
                if not f0 or not f0 > 0:
                    continue
                c = 1200 * math.log2(f0 / (440 * 2 ** ((RootFromPitch(d['pitch']) - 69) / 12)))
                errs.append(c - 1200 * round(c / 1200))
    e = np.abs(errs)
    print('%d looped samples: median |error| %.1f cents, %.0f %% within 25 cents (octave-folded)' % (
        len(e), float(np.median(e)), 100 * float(np.mean(e < 25))))

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    s = sub.add_parser('info')
    s.add_argument('bank')
    s.set_defaults(fn=CmdInfo)
    s = sub.add_parser('programs')
    s.add_argument('bank')
    s.add_argument('--map', help='Dream bank-map PDF (or its pdftotext -layout text) to compare variations with')
    s.set_defaults(fn=CmdPrograms)
    s = sub.add_parser('dump')
    s.add_argument('bank')
    s.add_argument('--out')
    s.add_argument('--pretty', action='store_true')
    s.set_defaults(fn=CmdDump)
    s = sub.add_parser('extract')
    s.add_argument('bank')
    s.add_argument('--out', required=True, help='output directory')
    s.add_argument('--program', type=int, help='only this program (0-based)')
    s.add_argument('--limit', type=int, default=0)
    s.set_defaults(fn=CmdExtract)
    s = sub.add_parser('pitchcheck')
    s.add_argument('bank')
    s.set_defaults(fn=CmdPitchCheck)
    s = sub.add_parser('selfcheck')
    s.add_argument('bank', nargs='+')
    s.set_defaults(fn=CmdSelfCheck)
    args = ap.parse_args()
    args.fn(args)


if __name__ == '__main__':
    main()
