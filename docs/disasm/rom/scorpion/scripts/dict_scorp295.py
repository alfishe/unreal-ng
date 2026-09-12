"""Symbol dictionary and educational comments for scorp295.rom page 2.

Service monitor v2.95.  Derived from the base monitor dictionary
(dict_scorpion.py): ROM symbols are relocated through the byte-run
alignment map (map_mon_scorp295_p2.json) plus the hand-resolved entries
below; RAM equates keep their addresses.  Comment prose keeps the base
wording except where v2.95 genuinely differs (error table addresses,
inlined TR-DOS boot, boot checksum walk).
"""
import importlib.util
import json
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))


def load(name):
    spec = importlib.util.spec_from_file_location(name.replace('.', '_'),
                                                  os.path.join(HERE, name))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


base = load('dict_scorpion.py')
AUTO = json.load(open(os.path.join(HERE, 'map_mon_scorp295_p2.json')))

# Hand-resolved relocations: logic confirmed identical in both digests, but
# relocated call/jr targets broke the raw byte runs used by align.py.
MANUAL = {
    'Rst10Vector': 0x0010,      # vector address fixed, target (PrintChar) moved
    'XorDecodeKeys': 0x00FC,
    'ParseCommand': 0x0E54,
    'EnterTrdos': 0x13E1,       # inlined into the boot continuation
    'SetErrorTables': 0x0DAC,
    'AutoRepeatKey': 0x1B0D,
    'PrintBit7Chars': 0x1C96,
    'PickNumberBase': 0x1CDD,
    'PrintHexByte': 0x1D9A,
    'ToggleAltDisplay': 0x1E97,
    'BuildStepStub': 0x1EAA,
    'GlyphAddr': 0x09F8,
    'SaveCursorCell': 0x0A87,
    'RestartCursorBlink': 0x0AB7,
    'BlinkCursor': 0x0AC5,
    'WindowAddr': 0x0AEE,
    'AttrAddr': 0x0B3C,
    'BitmapAddr': 0x0B4F,
    'ScrollUp': 0x0B61,
    'CopyCharRow': 0x0BA3,
    'CopyAttrRow': 0x0BCE,
    'ScrollDown': 0x0BEE,
    'FillWindow': 0x0C2E,
    'FillWholeRow': 0x0C4D,
    'FillRowToEnd': 0x0C58,
    'PrinterPutChar': 0x0C80,
    'PopAllRet': 0x0C89,
    'PrintCharBit7': 0x0C8D,
    'PrintSpace': 0x0C92,
    'PrintChar': 0x0C94,
    'DispatchOutput': 0x0C9A,
    'InstallPrintHook': 0x0CE5,
    'WindowNewline': 0x0D03,
    'WindowHome': 0x0CF4,
    'KeyClick': 0x0D27,
    'CalcPagingPorts': 0x2EBF,
    'AddBreakpoint': 0x3053,
    'ListBreakpoints': 0x3083,
}

# 295-specific comment replacements (keyed by base comment address).
REPLACE_COMMENTS = {
    0x101E: [
        'EnterTrdos - boot into TR-DOS.  In v2.95 this lives inline in the',
        'boot continuation rather than as a separate routine: the paging',
        'state is cleared ($FFCA=0), a RAM-extension call (RST 18h) hands',
        'control to the DOS page of this bundle, then SetErrorTables',
        'installs the v2.95 message addresses below.',
    ],
}


def addr295(name):
    if name in MANUAL:
        return MANUAL[name]
    return AUTO.get(name)


# ---- symbols -------------------------------------------------------------
SYMBOLS = []
for a, n in base.SYMBOLS:
    if a >= 0x4000:
        SYMBOLS.append((a, n))              # RAM equates unchanged
        continue
    v = addr295(n)
    if v is not None:
        SYMBOLS.append((v, n))

nameOf = {a: n for a, n in base.SYMBOLS}
addrOf = {n: a for a, n in base.SYMBOLS}
# $XXXX prose references that name a base ROM symbol: rewrite to the 295
# address so the comments stay truthful after relocation.
HEXREF = re.compile(r'\$([0-9A-F]{4})\b')


def rewriteRefs(line):
    def sub(m):
        a = int(m.group(1), 16)
        n = nameOf.get(a)
        if n is None or a >= 0x4000:
            return m.group(0)
        v = addr295(n)
        return '$%04X' % v if v is not None else m.group(0)
    return HEXREF.sub(sub, line)


def fixLines(a, lines):
    if a in REPLACE_COMMENTS:
        return REPLACE_COMMENTS[a]
    out = []
    for s in lines:
        if a == 0x116A:                     # SetErrorTables values moved
            s = s.replace('$3B7F', '$3C48').replace('$3818', '$39E1')
        if a == 0x0326:                     # SaveContext beep wording
            s = s.replace('v2.95/prof additionally beep here',
                          'v2.95 (this ROM) additionally beeps here')
        if a == 0x0FC1:                     # BootContinue checksum wording
            s = s.replace('v2.95 adds a ROM checksum check here.',
                          'this version adds a ROM checksum walk.')
        out.append(rewriteRefs(s))
    return out


# ---- comments ------------------------------------------------------------
COMMENTS = []
for a, lines in base.COMMENTS:
    if a >= 0x4000:
        COMMENTS.append((a, fixLines(a, lines)))
        continue
    if a in base.DATA_ADDRS:      # base-only data blocks: documented locally below
        continue
    n = nameOf.get(a)
    v = addr295(n) if n else None
    if v is None:
        print('WARNING: comment at $%04X (%s) dropped' % (a, n))
        continue
    COMMENTS.append((v, fixLines(a, lines)))

# ---- header --------------------------------------------------------------
HEADER = []
for line in base.HEADER:
    isOldSisterHead = 'Sisters' in line and 'scorp295' in line
    isOldSisterProf = 'scorp_prof401.rom' in line and 'v4.01' in line and 'Sisters' not in line
    if isOldSisterHead:
        HEADER += [
            ";  Sisters  : scorpion.rom       p2 = v2.x   (base monitor - the labels here",
            ";             were derived from it by byte-run alignment),",
            ";             scorp_prof401.rom p2 = v4.01 (ProfRAM layout, RAM hooks).",
            ';',
            ';  This file: v2.95 relocates most routines above $0A54; the low core',
            ';             is shared with the base monitor byte for byte (only the',
            ';             RST 10h vector target differs).  TR-DOS boot is inlined',
            ';             and the error tables sit at $3C48/$39E1.',
        ]
        continue
    if isOldSisterProf:
        continue                          # folded into the block above
    s = line
    s = s.replace('(page 2 of scorpion.rom)', '(page 2 of scorp295.rom)')
    s = s.replace('scorpion.rom is a 64 KiB', 'scorp295.rom is a 64 KiB')
    s = s.replace('data/rom/scorpion.rom, bytes', 'data/rom/scorp295.rom, bytes')
    s = s.replace('v2.x ("base")', 'v2.95')
    HEADER.append(s)

# --- data-block documentation for the 2.95 layout ---
COMMENTS += [
    (0x0013, [
        'Filler0013: unused padding between the RST',
        'vector stubs of the vector page ($0008-$0056).',
        'The bytes read nop nop jp $00B6 but no path',
        'enters them.',
    ]),
    (0x001B, [
        'Filler001B: three nop bytes of vector-page',
        'padding between the RST 18 and RST 20 stubs.',
    ]),
    (0x0023, [
        'Filler0023: vector-page padding (nop nop nop',
        'jr $0066); never executed.',
    ]),
    (0x002D, [
        'Filler002D: dead jp $00B6 left between the',
        'RST 20 and RST 30 stubs.',
    ]),
    (0x0033, [
        'Filler0033: padding after the RST 30 stub',
        '(nop nop jp $00B6), unreachable.',
    ]),
    (0x0053, [
        'Filler0053: stray instruction bytes (a',
        'ld hl,(dFF3h)) closing the vector page;',
        'nothing references them.',
    ]),
    (0x00F1, [
        'Filler00F1: unreferenced junk immediately',
        'before the XOR key table at $00FC.',
    ]),
    (0x0A19, [
        'KeyScanCode0A19: keyboard-state helper code',
        'between GlyphAddr ($09F8) and SaveCursorCell',
        '($0A87) - bit tests on the IX/IY key cells and',
        'the scan decode; an earlier sweep had mistyped',
        'it as data.',
    ]),
    (0x1257, [
        'KeyScanTableA: first key-translate table of',
        'the editor - internal key codes in scan',
        'order, consumed by the matching editor loop;',
        'continues in KeyScanTableB ($126F).',
    ]),
    (0x126F, [
        'KeyScanTableB: second key-translate table;',
        'the final $22 byte acts as a sentinel.',
    ]),
    (0x129E, [
        'RegisterNameStrings: pool of short register-name',
        'strings, the last character of each carrying',
        "bit 7 ('I','I','H','D','B','AF','A','IX','IX',",
        "'IY','IY','P','S','HL','DE','BC' plus single",
        'letters); indexed by the register display and',
        'editor.',
    ]),
    (0x17EA, [
        'RegHeaderLine: header row of the register window',
        "('IR  SZ-H-PNC  INT RAM ROM SCR  ZX' with",
        'embedded control bytes for the column gaps);',
        'printed above the register dump.',
    ]),
    (0x2041, [
        'AnalyzeDataPool: four scratch bytes followed by',
        "the 'BAD' text ($F0-terminated) shown for invalid",
        'opcodes by the disassembler path around it.',
    ]),
    (0x2818, [
        'HelpCode2818: evaluator/help code that an earlier',
        'sweep had mistyped as data - it calls',
        'FillMemoryRange, evaluates expressions and prints',
        'inline strings (Hel+$F0 prints Help) between',
        'RegisterKeyTable and the message blocks.',
    ]),
    (0x2990, [
        'MsgOff: inline RST 20h string OF+$C6 which prints',
        'OFF - the terminating byte is emitted with bit 7',
        'stripped; used by the watch/breakpoint listing',
        'routine above.',
    ]),
    (0x2996, [
        'MsgOn: ON+$A0, printing ON - companion of MsgOff',
        '($2990).',
    ]),
    (0x2B88, [
        'PopupTabStops: four tab columns (0,2,4,6) closed',
        'by the $81 sentinel; the loop just above feeds',
        'each value to the print helper to lay out the',
        'popup menu line right before OpenPopup ($2B8D).',
    ]),
    (0x2BB4, [
        'MsgBase: popup-menu item base led by a carriage',
        'return byte; the $A0 terminator prints as a',
        'trailing space.',
    ]),
    (0x2BD1, [
        'MsgOption: popup-menu item option (CR-prefixed,',
        '$A0-terminated).',
    ]),
    (0x2BE0, [
        'MsgIntMode: popup-menu item Int mode (CR-prefixed);',
        'the choice drives the im0/im1/im2 dispatcher at',
        '$369E.',
    ]),
    (0x2BFD, [
        'MsgTapeTurbo: (t+$A9, printing (t) - appended to',
        'the tape line when turbo mode is selected.',
    ]),
    (0x3044, [
        'InlineBlob3044: twelve bytes eaten as data by the',
        'breakpoint path - the conditional call at $3040',
        '(sub_1069h scans via sub_1056h and advances HL)',
        'walks across them, and the jr at $3029 bypasses',
        'the region entirely.',
    ]),
    (0x34C7, [
        'MsgBad: Bad - bad file/tape status message',
        '($A0-terminated).',
    ]),
    (0x3629, [
        'MenuBuilder: popup-menu code of the ROM tail',
        '(2.95 layout). It calls OpenPopup ($2B8D), then',
        'prints the five entries of KeywordTable ($3773)',
        'with helper strings indexed at',
        'ErrorShortStrings+3*i+2 ($3D19). The option',
        'loop ends in the interrupt-mode dispatcher: a',
        '3-bytes-per-entry jump table at $369E (im 0 /',
        'im 1 / im 2 / invalid). The final routine',
        '($36AA) copies ten (dest,word) pairs from',
        'RamInitTable ($3F96) into the workspace.',
    ]),
    (0x36BD, [
        'EncodedTable36BD: XOR-masked ROM table, unmasked',
        'at boot into $E2DB by the decode call at $05BB',
        '(keys at XorDecodeKeys, $00FC).',
    ]),
    (0x3773, [
        'KeywordTable: menu/command keyword strings',
        "('PC','BAS','BREa','BR','CAL','CATalog',...); the",
        'final character of each entry carries bit 7,',
        'and the menu loop at $3633 stops at a $00 byte.',
    ]),
    (0x3845, [
        'WordLists: packed word lists for the token',
        "recogniser - menu words ('main','menu',",
        "'previous',...), interface words ('speed',",
        "'RS232','9600',...) and the long error words",
        "('abandoned','breakpoint',...).",
    ]),
    (0x3D19, [
        'ErrorShortStrings: dense pool of 2-4 character',
        'strings (error fragments, month names, menu',
        'decorations) - the message base also used by',
        'the menu builder via $3D19+3*i+2.',
    ]),
    (0x3F96, [
        'RamInitTable: ten (dest,word) pairs written into',
        'workspace/screen RAM by the walker at $36AA -',
        'window defaults at $DFFA-$DFFE, further cells',
        'at $E0AC-$E0B4 and two tags at $C063/$C064.',
    ]),
    (0x3FBE, [
        "ZeroPad3FBE: two orphan one-character strings",
        "('c'+$80, 'd'+$80) and zero padding up to the",
        'end of the 16 KiB page.',
    ]),
    (0x14AD, [
        'Msg14AD: separator line of nine < characters',
        '(bit-7 terminator).',
    ]),
    (0x31E7, [
        'Msg31E7: disk-catalog message File.',
    ]),
    (0x31FA, [
        'Msg31FA: disk-catalog message , free.',
    ]),
    (0x3282, [
        'Msg3282: message More... (listing pager).',
    ]),
    (0x3335, [
        'Msg3335: message Formating. (disk format).',
    ]),
    (0x33B4, [
        'Msg33B4: message Checking.  (disk verify).',
    ]),
    (0x3483, [
        'Msg3483: message Complete.',
    ]),
    (0x34B7, [
        'Msg34B7: frame-counter label  Total .',
    ]),
    (0x3520, [
        'Msg3520: message Insert disk, press Y key.',
    ]),
]
