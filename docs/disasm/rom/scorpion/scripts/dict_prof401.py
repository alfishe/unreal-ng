"""
Service monitor v4.01 ("ProfRAM").  Derived from the base monitor
dictionary (dict_scorpion.py): ROM symbols are relocated through the
byte-run alignment map (map_mon_prof401_p2.json) plus the hand-resolved
entries below; RAM workspace equates are remapped where v4.01 moved the
cells (window descriptors shifted +$40, stacks/pointers into $E3xx,
tables into $E5xx-$EAxx).  Comment prose keeps the base wording except
where v4.01 genuinely differs (RST 18h and RST 30h repurposed,
linked-list tokenizer at $23A3, relocated workspace).
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
AUTO = json.load(open(os.path.join(HERE, 'map_mon_prof401_p2.json')))
SIG = json.load(open(os.path.join(HERE, 'mapsig_prof401.json')))

# Hand-resolved relocations: verified against the digest (vector targets,
# call chains, table boundaries).  Logic matches the base monitor unless a
# replacement comment below says otherwise.
MANUAL = {
    'WriteAnyBankByte': 0x0000,     # scf; bit 7,h; jp - RST 00h twin of RST 28h
    'Rst08Vector': 0x0008,          # vector fixed; target ReportError moved
    'Rst10Vector': 0x0010,          # target PrintChar ($2B12)
    'Rst18Vector': 0x0018,          # repurposed: inline hex digit parser
    'Rst20Vector': 0x0020,          # target Rst20Handler ($16D0)
    'Rst18Handler': 0x0E41,         # NEW: RST 18h backend (see comments)
    'RegisterNameList': 0x0F91,     # NEW: replaces RegisterNameTable
    'TokenTable': 0x23A3,           # NEW: linked-list command tokenizer head
    'ReportError': 0x02A5,          # jp at $0008
    'XorDecodeKeys': 0x00FC,
    'AyWriteData': 0x03C2,          # align.py collided it with SaveContext
    'SaveContext': 0x03CE,          # ld bc,$1FFD; ld ($0DDA7),sp; ld sp,$DD83
    'FatalHalt': 0x066D,            # ld a,4; out ($FE),a; halt
    'BootContinue': 0x069B,
    'DecodeTables': 0x06B1,         # ld hl,$3ED2; ld de,$EAED; call $3837
    'ExitToError': 0x0B06,          # ld sp,$E336
    'ParseCommand': 0x0B40,
    'MnemonicTable': 0x1107,        # token pairs 5C 01 5C 02 ... start here
    'CharClasses': 0x14F6,
    'Disassemble': 0x1512,
    'PrintBit7Chars': 0x15C7,       # ld a,(hl); call PrintCharBit7; rlca; ret c
    'PrintJustifiedHeader': 0x15DD, # print bit-7 string, pad spaces to width
    'PickNumberBase': 0x1626,
    'PrintDecimal': 0x1658,
    'Rst20Handler': 0x16D0,         # jp at $0020
    'PrintHexWord': 0x16DE,         # calls PrintHexByte ($16E3) twice
    'ToggleAltDisplay': 0x17DD,
    'BuildStepStub': 0x17F0,        # copies 8 bytes to StepStub ($E39A)
    'CollectAsmText': 0x1A83,
    'EditorKeyLoop': 0x1AA7,        # ld hl,EditorKeyTable; ld bc,$0024
    'EditorRegKeys': 0x1BD1,
    'BlinkCursor': 0x28F3,            # flash counter at $E3B6, state $E01D
    'FillWholeRow': 0x2A7A,            # call SaveCursorCell; ld (ix+1),0; fall
    'DispatchOutput': 0x2B18,
    'WindowNewline': 0x2B6E,        # bit 2,(ix+7); call nz,$2B84
    'WindowHome': 0x2BA0,           # xor a; ld (ix+1),a; ld (ix+0),a; ret
    'ScanTokens': 0x2C7F,           # walks the TokenTable linked list
    'CommandLoop': 0x2C92,
    'ShowRegisters': 0x2D70,        # ld hl,$0F91 (RegisterNameList)
    'MemoryDumpWindow': 0x2E3D,
    'ScrollWindow': 0x2EA0,         # ld hl,(DD87)
    'HexDumpRow': 0x2EBA,           # ld (ix+1),2; call PrintHexWord
    'DisasmLine': 0x2F80,           # call PrintHexWord; call FetchPrefixBytes
    'FindWatchpoint': 0x322C,       # calls InitWatchTable ($3281)
    'RestorePagingShadow': 0x35EC,  # ($E00E) -> ($E012)
}

# Unique byte-signature matches from mapsig.py (trusted; literals above win).
for _n, _v in SIG.items():
    if isinstance(_v, int):
        MANUAL.setdefault(_n, _v)

# Symbols the v4.01 rewrite removed: no counterpart routine exists, so both
# the label and its comment are dropped (expected - no warning).
DROP = {
    'RamExtCall', 'RamExtDispatcher', 'RamExtTarget',   # RST 18h repurposed
    'AyShadowReg',                                     # reg-7 save now $E004
    'InventoryHardware', 'KeyJumpTable',               # rewritten away
    'RegisterNameTable',                               # -> RegisterNameList
    'GlyphAddr', 'EvalOperand', 'CheckRangeInclusive',
    'ScanQuotedString', 'PrintMarkerChar', 'AutoRepeatKey',
    'StepDispatch', 'SetTempBreakpoint',
    'AddBreakpoint', 'ListBreakpoints',                # breakpoint UI gone
    'FillMemoryRange', 'ChecksumLoop',
    'CalcPagingPorts', 'ReadNmiPort', 'PrintFrameCount',
    'LookupToken', 'SaveToTape',
    'EnterTrdos', 'SetErrorTables', 'InstallPrintHook',
}

# RAM workspace cells that moved (bank 0 at $C000 while the monitor runs).
# Verified from the digest: window descriptors all shifted +$40, the
# stacks/pointers moved to $E33x-$E3Bx, the tables to $E52x-$EAFx.
RAM_REMAP = {
    'ErrorTextTable': 0xDDDC,        # written/read at $34CA/$3505 cluster
    'ErrorTextTable2': 0xDDDE,
    'PagingBackup': 0xE00E,          # second shadow written at $E010
    'PagingState': 0xE012,           # 21 references - the hot mirror
    'IyWorkBase': 0xE014,            # ld iy,$E014 in SaveContext
    'StepFlags': 0xE026,             # IyWorkBase+$12, as in the base
    'PromptWindowDef': 0xE075,       # +$40 shift, cf. DisasmWindowDef
    'DisasmWindowDef': 0xE091,       # ld hl,$E091 in DisasmWindow
    'WorkBuffer': 0xE09F,            # ld hl,$E09F in SetDefaultWorkspace
    'PopupWindowDef': 0xE0AD,        # ld hl,$E0AD in OpenPopup
    'RegWindowDef': 0xE0C9,          # +$40 shift (layout arithmetic)
    'StepContext': 0xE11A,           # swapped with UserPc in PrepareStep
    'MonitorStack': 0xE336,          # ld sp,$E336 in ExitToError
    'CursorCellSave': 0xE3A4,        # ld de,$E3A4 in SaveCursorCell (8x2)
    'WorkBufferPtr': 0xE3B7,         # ld ($E3B7),hl in SetWorkspace
    'StepStub': 0xE39A,              # ldir target in BuildStepStub
    'DecodedTables': 0xE3BE,         # $3B92 -> $E3BE (and $3ED2 -> $EAED)
    'WatchTable': 0xE52D,            # ld ix,$E52D; ld b,8; ld de,11
    'ExtMenuTable': 0xE9A9,          # ld hl,$E9A9 in PrintExtMenu
    'CommandTable': 0xE9BD,          # ld de,$E9BD in LookupCommand
    'HwConfigTable': 0xEAF5,         # cold-start validation walk at $0049
    'BootConfigTable': 0xF508,       # pointer cell: ld ix,($F508)
}

NEW_SYMBOLS = [
    (0x0E41, 'Rst18Handler'),
    (0x0F91, 'RegisterNameList'),
    (0x23A3, 'TokenTable'),
]

NEW_COMMENTS = [
    (0x0E41, [
        'Rst18Handler - the v4.01 RST 18h backend: inline hexadecimal',
        'digit parser.  Pops the return address into HL, reads the',
        'character there (lower-case folded up), converts it to a nibble',
        'value and stacks the corrected resume address past the digit;',
        'carry/A >= 0Ah reports "not a digit" (the $0E45 wrapper turns',
        'that into error code $10).  $0E51 is the digit-range predicate,',
        '$0E59 the case-folding hex variant.  The base monitor spent this',
        'vector on RAM-extension calls (RamExtCall) instead.',
    ]),
    (0x0F91, [
        'RegisterNameList - the two register-name strings of the register',
        'dump (bit-7 terminated, printed by RST 20h; ShowRegisters at',
        '$2D70 loads HL here).  List 1 ($0F91) orders the main display',
        'PC,SP,IX,IY,HL,DE,BC; list 2 ($0FA0) enumerates every editable',
        'name ON,OFF,MEM,R,AF\'... down to I at $0FE9.  Four cursor-delta',
        'trampolines follow at $0FEB (ld bc,-8/+8/-1/+1; jr $1005 -',
        'LEFT/RIGHT/DOWN/UP), converging on the cursor-move core.',
    ]),
    (0x23A3, [
        'TokenTable - the v4.01 command tokenizer: a linked list of',
        'records {u16 next; u8 token; name[] NUL-terminated; handler',
        'snippet}, headed here ($23A3; cf. ld hl,$23A3 at $3AC8) and',
        'chained up to $27DA.  Named records implement the display',
        'commands (MEM/REG/DIS/SCR/SYS1/SYS2), memory configuration',
        '(FF/AM/RAM/ALL/EM/EM!), port I/O (IN/OUT) and PAUSE (RST 30h',
        'RAM hook with inline args); the many empty-name records are',
        'single-character/suffix matchers (R, D, F, C, X, Y, E, L, N, P,',
        '">", "@", "!", ND, OR, OT, IT, DDR, CF, VF, EG, YS1, YS2, WAP,',
        'VER, ROP, EY, UP, OP).  ScanTokens ($2C7F) walks the list;',
        'each Tok* block below is one record of this list.',
    ]),
    (0x27DA, [
        'regptrs: data tail of the tokenizer - 13 row/column bytes for',
        'the register display, then 11 window-routine pointers ($2BF8,',
        '$2C18, $2AAD, $2ACA, $2AED, $2BA8, $2B7D, $2A5E, $2B6E, $2BCA,',
        '$2A1B) behind the register-view dispatch; code resumes at $27FD',
        '(call $28B8).',
    ]),
]

# v4.01-specific comment replacements (keyed by BASE comment address).
REPLACE_COMMENTS = {
    0x0018: [
        'RST 18h - v4.01 repurposed this vector: JP $0E41 parses ONE',
        'inline hexadecimal digit (upper-cased) that follows the RST at',
        'the call site and stacks the corrected resume address (see',
        'Rst18Handler).  The base monitor used RST 18h for RAM-extension',
        'calls (RamExtCall $02DC) instead.',
    ],
    0x0030: [
        'RST 30h - v4.01 repurposed this vector: JP $E3D3 hands control',
        'to RAM-resident code (decoded at boot from the XOR tail).  The',
        'bytes following each RST 30h are inline arguments consumed',
        'RAM-side through the stacked return address - 22 sites, e.g.',
        '$06C6 in the boot sequence.  The base monitor used RST 30h to',
        'set the IX work buffer; SetWorkspace ($301C) keeps that role',
        'as a plain routine.',
    ],
    0x02F0: [
        'AY-3-8910 helpers: register select on #FFFD, data on #BFFD.',
        'AyReadRegister (here $0398) gates on IY+$14 bit 0, selects',
        'register 7, saves the port value at $E004 and leaves $FF in',
        'the register; AyReadData ($03B6) returns any register\'s data',
        'byte.  The wrapper at $03C2 (AyWriteData) writes the saved',
        '$E004 value back into register 7 - the read-modify-restore',
        'pair behind the mouse buttons and hardware probes.',
    ],
    0x05A2: [
        'DecodeTables: decrypt the XOR-masked ROM tail into RAM with the',
        'keys at XorDecodeKeys ($00FC), via DecodeXorTable ($3837):',
        'first $3ED2 -> $EAED here, then the helper at $0739 does',
        '$3B92 -> $E3BE.  IX is loaded through the pointer cell at',
        '$F508 and control passes to it (jp (IX)) - the RAM-side boot',
        'configuration continuation.',
    ],
    0x0A8A: [
        'ParseCommand: the command/expression reader.  Accepts a typed',
        'line into the IX work buffer, evaluates operands and reduces',
        'the input to tokens for DispatchMode.  v4.01 matches the verb',
        'against the linked-list token table at $23A3 (TokenTable)',
        'instead of the base compressed table walk.',
    ],
    0x13A3: [
        'SetDefaultWorkspace: HL <- $E09F (the v4.01 WorkBuffer), then',
        'falls into SetWorkspace below.',
    ],
    0x13A6: [
        'SetWorkspace: (WorkBufferPtr, $E3B7) <- HL and IX <- HL.  Window',
        'routines address their descriptors IX-relative, so this is how',
        'the monitor switches windows.  In v4.01 this is a plain routine',
        '- RST 30h no longer enters here (see the RAM hook at $0030).',
    ],
    0x13B5: [
        'ScanTokens: walks the v4.01 tokenizer linked list at TokenTable',
        '($23A3): each record matches one character of the input, the',
        'next-pointer is followed on a partial match, and the record',
        'handler runs (terminator action via jp (HL)) once the name is',
        'consumed.  The command tokenizer of the monitor.',
    ],
    0x19DB: [
        'PrepareStep: swap the user PC with the step-context copy at',
        'StepContext ($E11A), park the step engine on its own stack at',
        '$E38D (the v4.01 monitor itself keeps $E336), arm the trace',
        'enable (bit 5 of StepFlags at $E026), then let the class',
        'patchers below adjust the step context for the next',
        'instruction.',
    ],
    0x2B6B: [
        'TapeMenu: the tape command cluster - save/verify/leader state',
        'machine; control flow is dense here (cf. the feeder at',
        'FeedTapeOutput and the key waits in this region).',
    ],
    0x24EF: [
        'BlinkCursor: toggle cursor visibility - v4.01 counts down 12/10',
        'inversions in the $E3B6 cell (state bit in $E01D); draw/erase',
        'goes through the cell buffer.',
    ],
}

# Per-address prose fixes applied before the $XXXX reference rewrite.
FIX_STRINGS = {
    0x0326: [('v2.95/prof additionally beep here',
              'v2.95 and v4.01 (this ROM) additionally beep here')],
    0x0FC1: [('v2.95 adds a ROM checksum check here.',
              'the v2.95 sister adds a ROM checksum walk here.')],
    0x14AB: [('inline RST 20h strings at RegisterNameTable ($1531)',
              'two bit-7-terminated lists at RegisterNameList ($0F91)')],
    0x1668: [('workspace E051', 'workspace $E091')],
    0x13E2: [('the E035 window', 'the $E075 window')],
    0x19BF: [('calling the RAM hook at $EB06',
              'calling RAM hooks (zone $EB03/$EB0D)')],
    0x2677: [('enter at $267E', 'enter at $2A81')],
    0x26AA: [('buffer pointer at E2D9', 'buffer pointer at $E3BC')],
    0x26C4: [('DFE8 bits 4/5/6', '$E01D bits 4/5/6'),
              ('window; installed print hooks watch the stream as well.',
               'window.')],
    0x2D80: [('(DFDD) into the DFD9/DFDB shadows',
              '($E012) into the $E00E/$E010 shadows')],
    # 0x2FFD: BreakpointSlot is still $DD8D-based in v4.01 (verified at
    # $389F: ld de,$DD8D; add hl,hl; add hl,de) - no fix needed.
    0x3584: [('DFDB shadow -> DFDD paging mirror',
              '$E00E shadow -> $E012 paging mirror')],
}


def addr401(name):
    if name in MANUAL:
        return MANUAL[name]
    return AUTO.get(name)


# ---- symbols -------------------------------------------------------------
SYMBOLS = []
for a, n in base.SYMBOLS:
    if a >= 0x4000:
        if n in RAM_REMAP:
            SYMBOLS.append((RAM_REMAP[n], n))
        elif n not in DROP:
            SYMBOLS.append((a, n))         # RAM equates that stayed put
        continue
    v = addr401(n)
    if v is not None:
        SYMBOLS.append((v, n))
SYMBOLS += NEW_SYMBOLS

nameOf = {a: n for a, n in base.SYMBOLS}
addrOf = {n: a for a, n in base.SYMBOLS}
# $XXXX prose references that name a base ROM symbol: rewrite to the v4.01
# address; RAM references are rewritten through RAM_REMAP so the comments
# stay truthful after the workspace move.
HEXREF = re.compile(r'\$([0-9A-F]{4})\b')


def rewriteRefs(line):
    def sub(m):
        a = int(m.group(1), 16)
        n = nameOf.get(a)
        if n is None:
            return m.group(0)
        v = RAM_REMAP.get(n) if a >= 0x4000 else addr401(n)
        return '$%04X' % v if v is not None else m.group(0)
    return HEXREF.sub(sub, line)


def fixLines(a, lines):
    if a in REPLACE_COMMENTS:
        return [rewriteRefs(s) for s in REPLACE_COMMENTS[a]]
    out = []
    for s in lines:
        for old, new in FIX_STRINGS.get(a, []):
            s = s.replace(old, new)
        out.append(rewriteRefs(s))
    return out


# ---- comments ------------------------------------------------------------
COMMENTS = []
for a, lines in base.COMMENTS:
    if a in base.DATA_ADDRS:      # base-only data blocks: documented locally below
        continue
    n = nameOf.get(a)
    if n in DROP:
        continue
    v = addr401(n) if n else None
    if v is None:
        print('WARNING: comment at $%04X (%s) dropped' % (a, n))
        continue
    COMMENTS.append((v, fixLines(a, lines)))
COMMENTS += NEW_COMMENTS

# ---- header --------------------------------------------------------------
HEADER = []
i = 0
while i < len(base.HEADER):
    line = base.HEADER[i]
    if 'Sisters' in line and 'scorp295' in line:
        HEADER += [
            ";  Sisters  : scorpion.rom       p2 = v2.x   (base monitor - the labels here",
            ";             were derived from it by byte-run alignment),",
            ";             scorp295.rom      p2 = v2.95  (boot ROM checksum, code moved).",
            ';',
            ';  This file: v4.01 repurposes RST 18h (inline hex parse) and',
            ';             RST 30h (RAM hook at $E3D3 with inline args), tokenizes',
            ';             commands through a linked list at $23A3, and moves the',
            ';             workspace (window defs +$40, stacks/pointers into',
            ';             $E3xx, tables into $E5xx-$EAxx); several base routines',
            ';             (breakpoint UI, tape writer, print hook) are gone.',
        ]
        i += 2                          # skip the prof sister line too
        continue
    if 'RST 18h  RAM-extension call' in line:
        HEADER += [
            ';    RST 18h  parse one inline hex digit (handler at $0E41); the',
            ';             base monitor used this vector for RAM-extension calls',
        ]
        i += 2                          # skip the second line of the entry
        continue
    if 'RST 30h  set IX work buffer' in line:
        HEADER.append(';    RST 30h  RAM hook: JP $E3D3, inline argument bytes follow')
        i += 1
        continue
    if 'identical in all versions' in line:
        HEADER.append(';  MONITOR RST API  (stub at $0000-$0038; v2.x/v2.95 share it byte')
        HEADER.append(';  for byte, v4.01 repurposes vectors 18h and 30h)')
        HEADER.append(';')
        i += 1
        continue
    if '$DFD7/$DFD8' in line:
        HEADER.append(';    $E004        AY register-7 save (read/restore pair $0398/$03C2)')
        i += 1
        continue
    if '$DFDB' in line and '$DFDD' not in line:
        HEADER.append(';    $E00E/$E010  paging shadows (SavePagingShadow writes both)')
        i += 1
        continue
    if '$DFDD' in line:
        HEADER.append(';    $E012        paging mirror (E = last #7FFD, D = last #1FFD value)')
        i += 1
        continue
    if '$DFDF' in line:
        HEADER.append(";    $E014        IY base for the monitor's flags (IY+$00..$16)")
        i += 1
        continue
    if '$E035/$E051' in line:
        HEADER.append(';    $E075/$E091/$E0AD/$E0C9  window descriptors (prompt, disasm,')
        i += 1
        continue
    if '$E05F' in line:
        HEADER.append(';    $E09F        default IX work buffer (SetDefaultWorkspace $3019)')
        i += 1
        continue
    if '$E0AA' in line:
        HEADER.append(';    $E11A        step context copy, $E39A step trampoline buffer')
        i += 1
        continue
    if '$E2B5' in line:
        HEADER.append(';    $E336        monitor SP ($E38D step engine), $E3A4 cursor')
        HEADER.append(';                 cell save (16 bytes), $E3B6 blink counter')
        i += 1
        continue
    if '$E358/$E373' in line:
        HEADER.append(';    $E3B7        work-buffer pointer, $E3BC printer buffer ptr')
        i += 1
        continue
    if '$E394' in line:
        HEADER.append(';    $E52D        watchpoint table: 8 entries x 11 bytes')
        i += 1
        continue
    if '$E7F7' in line:
        HEADER.append(';    $E9A9        extension menu table (RAM hooks at $EB03/$EB0D)')
        i += 1
        continue
    if '$E80B' in line:
        HEADER.append(';    $E9BD        command table (2-byte entries, see RunCommand)')
        i += 1
        continue
    if '$E82D' in line:
        HEADER.append(';    $E3BE/$EAED  tables decoded at boot from the XOR tail')
        i += 1
        continue
    if '$E928' in line:
        HEADER.append(';    $EAF5        hardware configuration table (validated at boot)')
        i += 1
        continue
    if '$DDD8/$DDDA' in line:
        HEADER.append(';    $DDDC/$DDDE  error message table pointers')
        i += 1
        continue
    if 'Trailing data at the end of the page' in line:
        HEADER += [
            ';  Trailing data at the end of the page: the XOR-masked tail at',
            ';  $3B91-$3FFF (decrypted at boot into $E3BE and $EAED by',
            ';  DecodeTables $06B1) holding RAM hook code, port-name strings',
            ';  and table data; the boot configuration lives behind the',
            ';  pointer cell at $F508, the 6x8 font RAM copy at $FCA0.',
        ]
        i += 4                          # skip the four base lines
        continue
    s = line
    s = s.replace('(page 2 of scorpion.rom)', '(page 2 of scorp_prof401.rom)')
    s = s.replace('scorpion.rom is a 64 KiB', 'scorp_prof401.rom is a 64 KiB')
    s = s.replace('data/rom/scorpion.rom, bytes', 'data/rom/scorp_prof401.rom, bytes')
    s = s.replace('v2.x ("base")', 'v4.01 ("ProfRAM")')
    s = s.replace('THIS service monitor (machine-code debugger)',
                  'THIS service monitor v4.01 (machine-code debugger)')
    HEADER.append(s)
    i += 1

# --- data-block documentation for the 4.01 layout ---
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
    (0x0B23, [
        'DeadBytes0B23: unreferenced 29-byte blob - a',
        'remnant of a routine removed when the RST 30h',
        'RAM-hook dispatcher replaced the older call',
        'path; nothing enters it.',
    ]),
    (0x0DEC, [
        'EditorSyntaxChars: four (character,code) pairs',
        "for the editor syntax tokens: '%' -> 2,",
        "'@' -> 8, '.' -> $0A, '#' -> $10; matched by the",
        'walker at $0DF4 (called from $0DB7/$2065) and',
        'walked four-at-a-time by the loader at $0E02.',
    ]),
    (0x0F4E, [
        'KeyScanTableA: first key-translate table of the',
        'editor (extended to 24 entries in 4.01) -',
        'internal key codes in scan order; continues in',
        'KeyScanTableB ($0F67).',
    ]),
    (0x0F67, [
        'KeyScanTableB: second key-translate table;',
        'the final $22 byte acts as a sentinel.',
    ]),
    (0x1062, [
        'DeadBytes1062: unreferenced junk left between',
        'the register lists and MnemonicTable; nothing',
        'references it.',
    ]),
    (0x2CB8, [
        'RegHeaderLine: header row of the register window',
        "('IR  SZ-H-PNC  INT RAM ROM SCR  ZX' with",
        'embedded control bytes for the column gaps);',
        'printed above the register dump.',
    ]),
    (0x3205, [
        'MsgOff: inline RST 20h string OF+$C6 which prints',
        'OFF - the terminating byte is emitted with bit 7',
        'stripped; used by the watch/breakpoint listing',
        'routine above.',
    ]),
    (0x320B, [
        'MsgOn: ON+$A0, printing ON - companion of MsgOff',
        '($3205).',
    ]),
    (0x33B6, [
        'PopupTabStops: four tab columns (0,2,4,6) closed',
        'by the $81 sentinel; the loop at $33A3 feeds each',
        'value to the print helper to lay out the popup',
        'menu line right before OpenPopup ($33BB).',
    ]),
    (0x33D6, [
        'MsgAnalyser: menu title Analyser  printed as an',
        'inline RST 20h string right after the popup at',
        '$33CD is opened.',
    ]),
    (0x33E8, [
        'MsgBase: popup-menu item base led by a carriage',
        'return byte; the $A0 terminator prints as a',
        'trailing space.',
    ]),
    (0x3406, [
        'MsgOption: popup-menu item option (CR-prefixed,',
        '$A0-terminated).',
    ]),
    (0x3415, [
        'MsgIntMode: popup-menu item Int mode (CR-prefixed,',
        '$A0-terminated).',
    ]),
    (0x344A, [
        'ScreenModeValues: the five legal screen-',
        'configuration values for the paging port;',
        'validated with cpir at $343D (the index of the',
        'current value is kept in the workspace) - any',
        'other value returns a carriage return.',
    ]),
    (0x3B91, [
        'EncodedTail: XOR-encoded tail region. The boot',
        'decode (DecodeTables, $06B1) unmask $3B92 into',
        'DecodedTables ($E3BE) and $3ED2 into $EAED;',
        'the ROM copy itself stays masked, so these',
        'bytes never execute as code.',
    ]),
    (0x1DD1, [
        'Msg1DD1: command keyword FLOAD of the 4.01',
        'command scanner.',
    ]),
    (0x1DD6, [
        'Msg1DD6: command keyword FSAVE of the 4.01',
        'command scanner.',
    ]),
    (0x2302, [
        "Msg2302: message fragment eval*2' of the nearby",
        'expression evaluator.',
    ]),
    (0x3161, [
        'Msg3161: command keyword Help of the 4.01',
        'monitor.',
    ]),
    (0x316C, [
        'Msg316C: command keyword CMOS (NVRAM setup) of',
        'the 4.01 monitor.',
    ]),
    (0x3170, [
        'Msg3170: command keyword RESNVRAM (NVRAM reset)',
        'of the 4.01 monitor.',
    ]),
    (0x3B35, [
        'Msg3B35: short string comp. - head fragment of',
        'the message pool in front of the encoded tail.',
    ]),
]
