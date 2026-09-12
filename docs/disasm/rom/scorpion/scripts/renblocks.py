#!/usr/bin/env python3
"""Rename/split/free the remaining blkNNN data blocks in the Scorpion
monitor block files so every region carries a meaningful name.

Idempotent: operations are keyed on the CURRENT block name, so a second
run is a no-op.  Backups are written as <file>.bak2 before the first
modification only.

Operations per file:
  RENAME  {old: new}
  SHRINK  {old: (start, end)}          - adjust boundaries
  RETYPE  {old: 'code'}                - bytedata -> code
  SPLIT   {old: [(name, start, end, type), ...]}
"""

import os
import shutil

HERE = os.path.dirname(os.path.abspath(__file__))

FILLERS = {
    'blk000': 'Filler0013', 'blk001': 'Filler001B', 'blk002': 'Filler0023',
    'blk003': 'Filler002D', 'blk004': 'Filler0033', 'blk005': 'Filler0053',
}

BASE = {
    'file': 'mon_scorpion_p2.blocks',
    'RENAME': dict(FILLERS, **{
        'blk009': 'Filler00F1',
        'blk020': 'KeyJumpTable',
        'blk039': 'KeyScanTableA',
        'blk040': 'KeyScanTableB',
        'blk041': 'RegisterNameStrings',
        'blk050': 'RegHeaderLine',
        'blk073': 'AnalyzeDataPool',
        'blk084': 'MnemonicTable',
        'blk092': 'MsgOff', 'blk093': 'MsgOn',
        'blk095': 'PopupTabStops',
        'blk096': 'MsgBase', 'blk097': 'MsgOption', 'blk098': 'MsgIntMode',
        'blk099': 'MsgTapeTurbo',
        'blk116': 'MsgTotal', 'blk117': 'MsgGold',
    }),
    'SHRINK': {'blk084': (0x2012, 0x2422)},
    'SPLIT': {
        'Filler00F1': [
            ('Filler00F1', 0x00F1, 0x00FC, 'bytedata'),
            ('XorKeys', 0x00FC, 0x0100, 'bytedata'),
        ],
        'blk121': [
        ('MenuBuilder', 0x35FC, 0x3690, 'code'),
        ('EncodedTable3690', 0x3690, 0x3746, 'bytedata'),
        ('KeywordTable', 0x3746, 0x3818, 'bytedata'),
        ('ErrorBitMap', 0x3818, 0x3820, 'bytedata'),
        ('WordLists', 0x3820, 0x3B7F, 'bytedata'),
        ('ErrorShortStrings', 0x3B7F, 0x3E45, 'bytedata'),
        ('EncodedTable3E45', 0x3E45, 0x3F69, 'bytedata'),
        ('RamInitTable', 0x3F69, 0x3F91, 'bytedata'),
        ('ZeroPad3F91', 0x3F91, 0x4000, 'bytedata'),
    ]},
}

S295 = {
    'file': 'mon_scorp295_p2.blocks',
    'RENAME': dict(FILLERS, **{
        'blk008': 'Filler00F1',
        'xorkeys': 'XorKeys',
        'blk019': 'KeyJumpTable',
        'blk026': 'KeyScanCode0A19',
        'blk042': 'KeyScanTableA',
        'blk043': 'KeyScanTableB',
        'blk044': 'RegisterNameStrings',
        'blk052': 'RegHeaderLine',
        'blk073': 'AnalyzeDataPool',
        'blk083': 'MsgOff', 'blk084': 'MsgOn',
        'blk086': 'PopupTabStops',
        'blk087': 'MsgBase', 'blk088': 'MsgOption', 'blk089': 'MsgIntMode',
        'blk090': 'MsgTapeTurbo',
        'blk102': 'InlineBlob3044',
        'blk108': 'MsgBad',
    }),
    'RETYPE': {'blk026': 'code'},
    'SPLIT': {
        'blk082': [
            ('MnemonicTable', 0x2409, 0x25CE, 'bytedata'),
            ('OpcodeClassTable', 0x25CE, 0x27B2, 'bytedata'),
            ('EditorKeyTable', 0x27B2, 0x27E2, 'bytedata'),
            ('RegisterKeyTable', 0x27E2, 0x2818, 'bytedata'),
            ('HelpCode2818', 0x2818, 0x298C, 'code'),
        ],
        'blk112': [
            ('MenuBuilder', 0x3629, 0x36BD, 'code'),
            ('EncodedTable36BD', 0x36BD, 0x3773, 'bytedata'),
            ('KeywordTable', 0x3773, 0x3845, 'bytedata'),
            ('WordLists', 0x3845, 0x3D19, 'bytedata'),
            ('ErrorShortStrings', 0x3D19, 0x3F96, 'bytedata'),
            ('RamInitTable', 0x3F96, 0x3FBE, 'bytedata'),
            ('ZeroPad3FBE', 0x3FBE, 0x4000, 'bytedata'),
        ],
    },
}

PROF = {
    'file': 'mon_prof401_p2.blocks',
    'RENAME': dict(FILLERS, **{
        'blk027': 'DeadBytes0B23',
        'blk032': 'EditorSyntaxChars',
        'blk036': 'KeyScanTableA',
        'blk037': 'KeyScanTableB',
        'blk038': 'RegisterNameList',
        'blk039': 'DeadBytes1062',
        'blk041': 'MnemonicTable',
        'blk087': 'RegHeaderLine',
        'blk097': 'MsgOff', 'blk098': 'MsgOn',
        'blk100': 'PopupTabStops',
        'blk101': 'MsgAnalyser', 'blk102': 'MsgBase',
        'blk103': 'MsgOption', 'blk104': 'MsgIntMode',
        'scrtab': 'ScreenModeValues',
        'blk122': 'EncodedTail',
    }),
    'SPLIT': {'blk008': [
        ('Filler00F1', 0x00F1, 0x00FC, 'bytedata'),
        ('XorKeys', 0x00FC, 0x0100, 'bytedata'),
    ]},
}


def apply(path, spec):
    lines = [l.rstrip('\n') for l in open(path) if l.strip()]
    entries = []  # (start, end, type, name)
    for l in lines:
        parts = l.split()
        name = parts[0].rstrip(':')
        start = int(parts[2], 16)
        end = int(parts[4], 16)
        typ = parts[6]
        entries.append([start, end, typ, name])

    changed = False
    for e in entries:
        start, end, typ, name = e
        if name in spec.get('RENAME', {}):
            e[3] = spec['RENAME'][name]
            changed = True
        if name in spec.get('SHRINK', {}):
            e[0], e[1] = spec['SHRINK'][name]
            changed = True
        if name in spec.get('RETYPE', {}):
            e[2] = spec['RETYPE'][name]
            changed = True

    for old, parts in spec.get('SPLIT', {}).items():
        if parts[-1][0] in {e[3] for e in entries}:
            continue                          # already split (idempotent re-run)
        for i, e in enumerate(entries):
            if e[3] == old:
                entries[i:i + 1] = [[s, t, ty, nm] for nm, s, t, ty in parts]
                changed = True
                break

    entries.sort(key=lambda e: e[0])
    for a, b in zip(entries, entries[1:]):
        assert a[1] <= b[0], 'overlap at $%04X/$%04X (%s/%s)' % (
            a[1], b[0], a[3], b[3])

    if changed:
        shutil.copy2(path, path + '.bak2')
    with open(path, 'w') as f:
        for start, end, typ, name in entries:
            f.write('%s: start 0x%04X end 0x%04X type %s\n' %
                    (name, start, end, typ))
    left = [e[3] for e in entries if e[3].startswith('blk')]
    print('%s: %d blocks, remaining blk*=%d %s' %
          (os.path.basename(path), len(entries), len(left), left or ''))


for spec in (BASE, S295, PROF):
    apply(os.path.join(HERE, spec['file']), spec)
