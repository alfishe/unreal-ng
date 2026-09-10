#!/usr/bin/env python3
"""Extract the pure WildPlayer v0.333 body from live-memory dumps and build
fully annotated disassemblies (educational RE companion).

Inputs  (SRC dir, live-memory dumps captured from the emulator):
  wildplayer_main_org5B00.bin        main RAM 0x5B00-0xBFFF (screen+ROM omitted)
  wildplayer_bank{0,1,4,5,6,7}_orgC000.bin   RAM-bank images @0xC000

Outputs (OUT dir):
  wildplayer_body_org5D3B.bin        the PURE player body: 0x5D3B-0xA8C2
                                     (BASIC loader line + unpacked player +
                                      installed VTII engine; the loaded PT3
                                      module at 0xA8C3+ is CONTENT, excluded)
  wildplayer_body.asm                annotated disassembly of the body
  wildplayer_bank1_vtii.asm          VTII PT3 r.7 engine master (org 0xC000)
  wildplayer_bank4_unipt2.asm        UniPT2 second-format engine (org 0xC000)
  WILDDISASM_NOTES.md                memory map, bank roles, the TS story

Usage: disasm_wildplayer.py [SRC_DIR] [OUT_DIR]

Location-aware: if a `dumps/` folder sits next to this script (the
docs/disasm/software/wildplayer layout), it is used as SRC_DIR and the
script directory as OUT_DIR - the docs folder regenerates itself.
"""
import os
import re
import subprocess
import sys
from collections import OrderedDict

HERE = os.path.dirname(os.path.abspath(__file__))
DOCS_DUMPS = os.path.join(HERE, "dumps")
HAVE_DUMPS = os.path.isdir(DOCS_DUMPS)

SRC = sys.argv[1] if len(sys.argv) > 1 else (
    DOCS_DUMPS if HAVE_DUMPS
    else "/Volumes/TB4-4Tb/Projects/Test/unreal-ng/scratch")
OUT = sys.argv[2] if len(sys.argv) > 2 else (HERE if HAVE_DUMPS else "scratch")
os.makedirs(OUT, exist_ok=True)

MAIN_BASE, BODY_START, BODY_END = 0x5B00, 0x5D3B, 0xA8C3
BANK_BASE = 0xC000

main = open(os.path.join(SRC, "wildplayer_main_org5B00.bin"), "rb").read()
banks = {}
for b in (0, 1, 4, 5, 6, 7):
    p = os.path.join(SRC, f"wildplayer_bank{b}_orgC000.bin")
    if os.path.exists(p):
        banks[b] = open(p, "rb").read()

# ---------------------------------------------------------------------------
# 1. Pure body extraction
# ---------------------------------------------------------------------------
body = main[BODY_START - MAIN_BASE:BODY_END - MAIN_BASE]
body_path = os.path.join(OUT, "wildplayer_body_org5D3B.bin")
open(body_path, "wb").write(body)
print(f"body: {len(body)} bytes @ 0x{BODY_START:04X}-0x{BODY_END - 1:04X} -> {body_path}")

# ---------------------------------------------------------------------------
# 2. Symbol tables (forensic anchors + auto-discovered call targets)
# ---------------------------------------------------------------------------
# addr -> (label, [comment lines])   comments transfer into the listing
SYM_MAIN = OrderedDict([
    (0x5D3B, ("BASIC_LINE256", [
        "boot.B BASIC line 256 header:  00 01 (line 256) EC 00 (len 236) then",
        "FD(CLEAR) '0' 0E 00 00 B3 5F 00  /  F9(C0) RANDOMIZE-USR '0' 0E 00 00 53 5D 00",
        "/ 3A(:) EA(REM) <machine code to EOL>.",
        "TR-DOS hide-the-numbers trick: the LISTED digits are placeholder '0's;",
        "each is followed by CH-14 (0x0E) + a 5-byte float in ZX small-int form",
        "(exponent 00, value in mantissa bytes 2-3): 0x5FB3=24499 and 0x5D53=23891.",
        "Execution uses the binary copy, so the line really means",
        "CLEAR 24499 : RANDOMIZE USR 23891 : REM <code>  -  RAMTOP=0x5FB3 keeps",
        "the stack below the code, USR jumps to 0x5D53 right after the REM token.",
        "TR-DOS `run` loads the whole B-file at PROG=0x5D3B; the interpreter then",
        "executes this line - USR lands in the loader with the code as 'comment'."])),
    (0x5D53, ("LOADER_ENTRY", [
        "Loader entry: DI; XOR A; OUT (#FE),A (border black); JR STAGE2.",
        "The packed Wild Player image follows as REM data; this stub is the",
        "only part that runs 'as loaded' - it unpacks the rest over RAM."])),
    (0x5DB3, ("STAGE2", ["unpack/bootstrap stage 2 (JR target from loader)"])),
    (0x5FE0, ("BANK_SWITCH", [
        "PUSH BC / LD A,#? / AND 7 / OR #10 / LD BC,#7FFD / OUT (C),A / POP BC / RET",
        "Pages RAM bank (A & 7) into the 0xC000 window, keeping bit 4 (screen)",
        "and bit 3 (48K ROM) intact. Used by every bank-aware player routine."])),
    (0x6090, ("TS_PROBE_OVERLAY", [
        "TurboSound presence probe RAN HERE at intro time (code since replaced):",
        "  OUT #FFFD,#F8 / OUT #FFFD,#00 / OUT #BFFD,#BF / IN A,(#FFFD)",
        "  JP P,+4 -> bit7 of readback == 0 means 'TS present' -> (8EBF)=1",
        "On a plain 2xAY TurboSound the #BF marker survives -> readback #BF ->",
        "bit7=1 -> 'no TS' -> 8EBF=0 -> whole dual-chip machinery disabled."])),
    (0x6A3A, ("TS_GATE_A", [
        "LD A,(6B05); OR A; JR Z,TS_MARKER_CHECK - first gate of the TS setup:",
        "only runs for player types that support TurboSound at all."])),
    (0x6A5D, ("TS_MARKER_CHECK", [
        "LD A,(0xA062); CP #20; JR Z,skip; LD A,2; LD (6B04),A; LD (6B02),A",
        "Converts the probed marker state into TS_MODE_FLAG=2 (dual engine)."])),
    (0x6B02, ("TS_MODE_FLAG", [
        "TurboSound mode: 0 = single chip, 1/2 = dual chip.",
        "Gates the dual-chip init (86EF) and the per-frame switch (907D).",
        "Stays 0 in our emulator runs - see TS_PROBE_OVERLAY above."])),
    (0x6B03, ("TS_STATE", ["player-type / TS state bytes 6B03..6B05 (6B05=1 here)"])),
    (0x6B05, ("TS_STATE2", ["TS-capable player-type flag checked at TS_GATE_A"])),
    (0x728A, ("R_728A", [])),
    (0x847F, ("BANK_SWITCH_TAIL", ["JP BANK_SWITCH - bank helper tail-jump thunk"])),
    (0x87F6, ("ANALYZER_POLL_SET", ["analyzer polling preamble (register select)"])),
    (0x87FA, ("ANALYZER_POLL", [
        "LD BC,#FFFD / OUT (C),reg / IN A,(C) - AY register probe used by the",
        "setup screen to show current chip state. High-traffic in port traces",
        "because the UI polls it every frame (6054 hits/PC in the capture)."])),
    (0x86EF, ("DUAL_CHIP_INIT", [
        "CALL 86CC; LD A,(TS_MODE_FLAG); OR A; JR Z,skip;",
        "dual-chip init: OUT #FFFD,#FE / OUT #FFFD,#FF (+ register defaults)",
        "- installs and initializes BOTH chip engines when TS was detected."])),
    (0x8A61, ("MUTE_BOTH", [
        "Mute both chips: OUT #FFFD,#FE + R8/R9/R10:=0, then OUT #FFFD,#FF +",
        "R8/R9/R10:=0. In the captured run this is the ONLY code that ever",
        "touched chip 2 after the failed probe (9 writes total)."])),
    (0x8A66, ("MUTE_SEL_FE", [])),
    (0x8A6F, ("MUTE_SEL_FF", [])),
    (0x8EBF, ("TS_PRESENT_FLAG", [
        "TS-present flag written by the intro probe (1=present).",
        "0 => every TS setup path (6888 / 6D09 pattern: OR A / JP Z) is",
        "skipped and TS_MODE_FLAG stays 0 forever."])),
    (0x8EC0, ("TS_SETUP_DATA", ["TS engine install parameters filled in when 8EBF=1"])),
    (0x9060, ("INT_HANDLER", [
        "Interrupt entry. Runs the TS switch block every frame when",
        "TS_MODE_FLAG != 0: bank-switch + OUT #FFFD,#FE + CALL engine1 +",
        "OUT #FFFD,#FF + CALL engine2 (engine copies live at 0xC005 inside",
        "their RAM banks - only reachable while the right bank is paged)."])),
    (0x907D, ("PER_FRAME_PLAY", [
        "LD A,(TS_MODE_FLAG); OR A; JR Z,single - the per-frame play gate.",
        "Dual path: #FE select at 9091, #FF select at 909B (per port trace)."])),
    (0x9091, ("SEL_FE_PLAY", [])),
    (0x909B, ("SEL_FF_PLAY", [])),
    (0x9B04, ("FDC_POLL", ["player's TR-DOS disk driver: FDC status poll loop (#1F/#FF)"])),
    (0xA000, ("VTII_INSTANCE", [
        "Vortex Tracker II PT3 r.7 engine - instance INSTALLED by the player",
        "into fixed RAM (the master copy lives in RAM bank 1 @0xC000).",
        "Entry points follow the VTII standard: +0 INIT (HL=module ptr),",
        "+5 PLAY (call every interrupt). The engine's register-write loop at",
        "+0x5B4/+0x5B7 (pc C5B4/C5B7 with its bank paged) is the music data",
        "stream seen in the port trace (13 OUTs/frame)."])),
    (0xA00D, ("VTII_SIG", ['signature "=VTII PT3 Player r.7="'])),
    (0xA8C3, ("MODULE_BASE", [
        "Loaded PT3 module ('Vortex Tracker II 1.0 module: Bad Apple!! ...').",
        "CONTENT, not player code - excluded from the pure body binary."])),
])

# data ranges inside the body: (start, end-exclusive, kind, note)
DATA_MAIN = [
    (0x5D3B, 0x5D53, "byte", "BASIC line-256 header (00 01 EC 00 FD 30 ...)"),
    (0x5D59, 0x5DB3, "ascii", "WP SETUP menu text"),
    (0x6688, 0x66A6, "ascii", "'ZX Spectrum Sound C...' info string"),
    (0x6BF8, 0x6C40, "ascii", "module-format detector strings (ProTracker 3. / Vortex Tracker II 1. / TFMcom1.)"),
    (0x6EF8, 0x7110, "byte", "PSC/Pro Sound Creator detector strings + tables"),
    (0x84E8, 0x8500, "byte", "setup key table 'WwTtSsMmBCcZz-'"),
    (0x8E1A, 0x8F04, "ascii", "file browser UI text (HDD/CD, Len:, Drv:, Fls/Free, Root Dir)"),
    (0x97A0, 0x9E4A, "byte", "virtual keyboard tables / note tables"),
    (0x9E4A, 0xA000, "byte", "8x8 font bitmaps"),
    (0xA00D, 0xA023, "ascii", "engine signature '=VTII PT3 Player r.7='"),
    (0xA5A0, 0xA8C3, "byte", "VTII engine tail tables (note/volume delay tables)"),
]

def autolabels(data, base, lo, hi, syms):
    """Add R_xxxx labels for absolute CALL/JP targets and LD rr,nn pointers."""
    n = len(data)
    for i in range(n - 2):
        op = data[i]
        if op in (0xCD, 0xC3) and i + 2 < n:            # CALL nn / JP nn
            t = data[i + 1] | data[i + 2] << 8
            if lo <= t < hi and t not in syms:
                syms[t] = (f"R_{t:04X}", [])
        # LD BC/DE/HL/SP,nn  01/11/21/31
        if op in (0x01, 0x11, 0x21, 0x31) and i + 2 < n:
            t = data[i + 1] | data[i + 2] << 8
            if lo <= t < hi and t not in syms:
                syms[t] = (f"D_{t:04X}", [])
        # LD (nn),HL / LD (nn),A / LD HL,(nn) etc -> just target visibility
        if op == 0x22 or op == 0x2A or op == 0x32 or op == 0x3A:
            t = data[i + 1] | data[i + 2] << 8
            if lo <= t < hi and t not in syms:
                syms[t] = (f"M_{t:04X}", [])
    return syms

# ---------------------------------------------------------------------------
# 3. z80dasm + post-processing
# ---------------------------------------------------------------------------
LINE_RE = re.compile(r"^(\S.*?)(?:\s+;([0-9a-fA-F]{4})\s*(.*))?$")

def disassemble(binpath, org, syms, data_ranges, header, outpath):
    """Disassemble binpath with educational annotations.

    The address space is split into CODE SEGMENTS = complement of
    data_ranges; every segment gets its OWN z80dasm run restarting cleanly
    at its boundary. A single linear sweep would drift through junk (BASIC
    headers, fonts, tables) and swallow boundary addresses as operand bytes
    - e.g. `jr nc` at 0x9FFF covers 0xA000, so the VTII engine entry label
    def-site would never appear and the first instructions after every data
    block would mis-decode until the sweep happens to resync.
    """
    symfile = outpath + ".sym"
    with open(symfile, "w") as f:
        for addr, (name, comments) in sorted(syms.items()):
            for c in comments:
                f.write(";" + c + "\n")
            f.write(f"{name}: equ 0x{addr:x}\n")

    data = open(binpath, "rb").read()
    end_all = org + len(data)
    ranges = sorted(data_ranges)

    segments = []          # code = complement of the data ranges
    cur = org
    for s, e, *_ in ranges:
        if s > cur:
            segments.append((cur, s))
        cur = max(cur, e)
    if cur < end_all:
        segments.append((cur, end_all))

    annotations = {a: c for a, (n, c) in syms.items() if c}
    symname = {a: n for a, (n, c) in syms.items()}
    # z80dasm echoes the WHOLE sym table as equ lines at the top of every
    # run; emit the named-anchor table once ourselves and strip every
    # equ echo from segment output (auto M_/D_/R_ labels live inline only)
    equ_echo = re.compile(r"^[A-Za-z_][A-Za-z_0-9]*:\s*equ")
    is_auto = lambda n: re.match(r"^[MDR]_[0-9A-F]{4}$", n)

    out = ["; " + "=" * 74]
    for h in header:
        out.append("; " + h)
    out.append("; " + "=" * 74)
    out.append("")
    out.append(f"\torg 0x{org:04X}")
    out.append("")
    out.append("; " + "-" * 74)
    out.append("; symbol table - named anchors (auto labels appear inline at use sites)")
    out.append("; " + "-" * 74)
    for a, (n, c) in sorted(syms.items()):
        if not is_auto(n):
            out.append(f"{n:24s}: equ 0x{a:04X}")
    out.append("")

    items = [(s, "code", (s, e)) for s, e in segments] + \
            [(s, "data", (s, e, k, n)) for s, e, k, n in ranges]
    items.sort(key=lambda t: t[0])

    tmp = 0
    for addr, kind, payload in items:
        if kind == "code":
            s, e = payload
            seg_bin = os.path.join(OUT, f".seg{tmp}.bin")
            raw = seg_bin + ".raw"
            tmp += 1
            open(seg_bin, "wb").write(data[s - org:e - org])
            subprocess.run(["z80dasm", "-l", "-a", "-t", "-c", "-g", hex(s),
                            "-S", symfile, "-o", raw, seg_bin], check=True,
                           stderr=subprocess.DEVNULL)
            lines = open(raw).read().splitlines()
            os.remove(raw)
            os.remove(seg_bin)
            # skip banner up to and including the org line, then ALL equ
            # echoes (the named-anchor table is emitted once at the top)
            k = 0
            while k < len(lines) and not lines[k].strip().lower().startswith("org"):
                k += 1
            k += 1
            while k < len(lines) and (equ_echo.match(lines[k]) or not lines[k].strip()):
                k += 1
            if s != org:
                out.append("")
                out.append("; " + "." * 74)
                out.append(f"; code segment restart 0x{s:04X} - clean decode boundary after data above")
                out.append("; " + "." * 74)
            for line in lines[k:]:
                if equ_echo.match(line):   # z80dasm re-emits equ echoes mid-stream
                    continue
                m = re.search(r";([0-9a-fA-F]{4})\b", line)
                if not m:
                    out.append(line)
                    continue
                a = int(m.group(1), 16)
                nm = symname.get(a)
                # annotations fire only at the label DEFINITION site, so they
                # attach to the routine header, not to every reference
                if nm and a in annotations and line.startswith(nm + ":"):
                    out.append("; " + "-" * 74)
                    for c in annotations[a]:
                        out.append("; " + c)
                    out.append("; " + "-" * 74)
                out.append(line)
        else:
            s, e, kind, note = payload
            if s in annotations:
                for c in annotations[s]:
                    out.append("; " + c)
            out.append("; " + "-" * 74)
            out.append(f"; DATA 0x{s:04X}-0x{e - 1:04X}  {note}")
            out.append("; " + "-" * 74)
            out.append(f"DATA_{s:04X}:")
            chunk = data[s - org:e - org]
            for j in range(0, len(chunk), 8):
                row = chunk[j:j + 8]
                hx = " ".join(f"{b:02X}" for b in row)
                asc = "".join(chr(b) if 0x20 <= b < 0x7F else "." for b in row)
                out.append(f"\tdefb {hx:<24} ;{s + j:04X}  {asc}")
            out.append("")

    open(outpath, "w").write("\n".join(out) + "\n")
    print(f"disasm: {outpath} ({len(out)} lines, {len(segments)} code segments)")

# ---------------------------------------------------------------------------
# 4. Main body
# ---------------------------------------------------------------------------
syms = autolabels(body, BODY_START, BODY_START, BODY_END, dict(SYM_MAIN))
syms = OrderedDict(sorted(syms.items()))
disassemble(
    body_path, BODY_START, syms, DATA_MAIN,
    [
        "WildPlayer v0.333 - PURE PLAYER BODY (Pentagon live-memory dump)",
        f"binary: wildplayer_body_org5D3B.bin  org 0x{BODY_START:04X}  len 0x{len(body):X}",
        "",
        "WHAT THIS IS",
        "  Everything between the boot.B BASIC line (0x5D3B) and the loaded",
        "  PT3 module (0xA8C3): loader stub, unpacked player code, setup UI,",
        "  file browser, format detectors, TR-DOS disk driver, interrupt",
        "  handler with the TurboSound switch block, and the installed VTII",
        "  PT3 engine instance at 0xA000.",
        "",
        "READING ORDER",
        "  1. BASIC_LINE256 / LOADER_ENTRY  - how TR-DOS `run` reaches the code",
        "  2. BANK_SWITCH (0x5FE0)          - every bank touch goes through it",
        "  3. TS_PROBE_OVERLAY (0x6090)     - the TurboSound detection story",
        "  4. TS_GATE_A / TS_MARKER_CHECK   - how the probe result arms TS mode",
        "  5. DUAL_CHIP_INIT / INT_HANDLER / PER_FRAME_PLAY - the dual-chip play",
        "  6. VTII_INSTANCE (0xA000)        - the per-chip music engine",
        "",
        "TURBOSOUND SEMANTICS (classic TS rev.C / 2xAY)",
        "  OUT #FFFD,#FE  select FIRST chip   OUT #FFFD,#FF  select SECOND chip",
        "  OUT #FFFD,r    select register r   OUT #BFFD,v    write register data",
        "  IN  #FFFD      read current register of the SELECTED chip",
        "  (aliased mirrors #C000/#8000/#BEFD etc. decode to the same devices)",
    ],
    os.path.join(OUT, "wildplayer_body.asm"))

# ---------------------------------------------------------------------------
# 5. bank1: VTII engine master
# ---------------------------------------------------------------------------
if 1 in banks:
    b1 = banks[1]
    sym_b1 = OrderedDict([
        (0xC000, ("VTII_INIT", [
            "LD HL,VTII_MODULE; JR ... - engine init, HL = module address",
            "for THIS instance. Standard VTII contract: C000=init, C005=play."])),
        (0xC005, ("VTII_PLAY", [
            "JP VTII_PLAY_IMPL - per-interrupt play entry. WildPlayer calls",
            "this at 0xC005 through the paged bank (see INT_HANDLER)."])),
        (0xC4B9, ("VTII_PLAY_IMPL", ["play frame implementation"])),
        (0xC5B4, ("VTII_REG_OUT_SELECT", [
            "register-select OUT loop - pc C5B4/C5B7 in the port trace is",
            "exactly here (13 register writes per frame = one VTII frame)."])),
        (0xC5B7, ("VTII_REG_OUT_DATA", ["paired data OUT (OUT (#BFFD-mirror),A)"])),
        (0xC86E, ("VTII_MODULE", [
            "PT3 module staged for this engine ('Bad Apple!!' PT3).",
            "The player points each engine instance's INIT HL at its module."])),
    ])
    sym_b1 = autolabels(b1, BANK_BASE, 0xC00D, 0xC86E, dict(sym_b1))
    sym_b1 = OrderedDict(sorted(sym_b1.items()))
    b1_path = os.path.join(OUT, "wildplayer_bank1_orgC000.bin")
    open(b1_path, "wb").write(b1)
    disassemble(
        b1_path, BANK_BASE, sym_b1,
        [(0xC86E, BANK_BASE + len(b1), "byte",
          "staged PT3 module data + VTII work area")],
        [
            "WildPlayer RAM bank 1: VTII PT3 r.7 ENGINE MASTER + staged module",
            "This is the copy the player installs FROM: it patches a few bytes",
            "(chip select / module pointer) per instance and writes the result",
            "into the target RAM bank at 0xC000 (instance also mirrored at",
            "0xA000 in fixed RAM - see wildplayer_body.asm VTII_INSTANCE).",
            "",
            "VTII standard entry contract:",
            "  call VTII_INIT with HL=module   once per module",
            "  call VTII_PLAY                  once per interrupt",
        ],
        os.path.join(OUT, "wildplayer_bank1_vtii.asm"))

# ---------------------------------------------------------------------------
# 6. bank4: UniPT2 engine
# ---------------------------------------------------------------------------
if 4 in banks:
    b4 = banks[4]
    sym_b4 = OrderedDict([
        (0xC000, ("UNIPT2_ENGINE", [
            "Second music engine: UniPT2 player (see '=UniPT2/-layer ' string",
            "at 0xCABB). WildPlayer supports PT3 (VTII) and PT2 (UniPT2); the",
            "right engine is installed per module type."])),
        (0xCABB, ("UNIPT2_SIG", ["'=UniPT2/Player ...' version string"])),
    ])
    sym_b4 = autolabels(b4, BANK_BASE, BANK_BASE, BANK_BASE + len(b4), dict(sym_b4))
    sym_b4 = OrderedDict(sorted(sym_b4.items()))
    b4_path = os.path.join(OUT, "wildplayer_bank4_orgC000.bin")
    open(b4_path, "wb").write(b4)
    disassemble(
        b4_path, BANK_BASE, sym_b4,
        [(0xCAB0, BANK_BASE + len(b4), "byte", "engine data/tables tail")],
        ["WildPlayer RAM bank 4: UniPT2 engine (PT2 format support)"],
        os.path.join(OUT, "wildplayer_bank4_unipt2.asm"))

# ---------------------------------------------------------------------------
# 7. Engine-instance install diff (A000 instance vs bank1 master)
# ---------------------------------------------------------------------------
if 1 in banks:
    n = min(len(banks[1]), 0x700)
    diffs = []
    for i in range(n):
        a = main[0xA000 - MAIN_BASE + i]
        b = banks[1][i]
        if a != b:
            diffs.append((0xA000 + i, 0xC000 + i, a, b))
    print(f"\nengine install patches (instance@A000 vs master@bank1): {len(diffs)} bytes")
    for ia, ib, va, vb in diffs[:40]:
        print(f"  {ia:04X}: {va:02X}  (master {ib:04X}: {vb:02X})")
    open(os.path.join(OUT, "engine_install_patches.txt"), "w").write(
        "\n".join(f"{ia:04X}: {va:02X}  (master {ib:04X}: {vb:02X})"
                  for ia, ib, va, vb in diffs) + "\n")

# ---------------------------------------------------------------------------
# 8. NOTES.md
# ---------------------------------------------------------------------------
notes = f"""# WildPlayer v0.333 pure-body extraction & annotated disassembly

Source: live-memory dumps of the emulator running ts_my.trd (Pentagon),
captured mid-play; see `wildplayer_dump_README.txt` for the raw dump layout.

## The pure body

`wildplayer_body_org5D3B.bin` = main RAM **0x5D3B-0xA8C2** ({len(body)} bytes):

| range | content |
|---|---|
| 0x5D3B-0x5D52 | boot.B BASIC line-256 header (`CLEAR 24499 : RANDOMIZE USR 23891 : REM`) |
| 0x5D53-0x5D58 | loader stub: `DI; XOR A; OUT (#FE),A; JR STAGE2` |
| 0x5D59-0x5DB2 | WP SETUP menu text |
| 0x5DB3-0x5FFF | unpack/bootstrap code + workspace |
| 0x5FE0 | BANK_SWITCH helper (`AND 7 / OR #10 / OUT (#7FFD)`) |
| 0x6000-0x9FFF | the unpacked Wild Player: setup UI, file browser, format detectors (PT3/PT2/PSC/TFM/STP...), TR-DOS disk driver (0x9B04 poll loop), **TurboSound probe/gate/init/switch** |
| 0xA000-0xA8C2 | installed VTII PT3 r.7 engine instance + tables |

Excluded: 0x5B00-0x5D3A (TR-DOS workspace: catalog copy, typed-line buffer),
0xA8C3+ (the loaded *Bad Apple!!* PT3 module - content, not player).

## RAM-bank roles

| bank | role (evidence) |
|---|---|
| 0 | TR-DOS catalog cache + help screens (`WP_Help!`, `TW_MUZA`, `WP333hlp`, `WRD1`, `PlayHelp`, `txt` entries at 0xD802) |
| 1 | **VTII PT3 r.7 engine master** (identical head to the 0xA000 instance) + staged Bad Apple PT3 module at 0xC86E (second copy at 0xF131) |
| 4 | **UniPT2 engine** (`=UniPT2/-layer ` string at 0xCABB) - PT2 format support |
| 5 | data (music/aux buffers, long U/G/D runs) |
| 6 | table-driven data (repeating `ld (hl),.. / inc`-like patterns - screens/tables) |
| 7 | mostly empty + font-like bitmaps at 0xE9xx; was paged at 0xC000 at dump time |

## The TurboSound story (why chip 2 was silent - port-trace forensics)

1. **Probe** (intro overlay at 0x6090, code replaced by dump time):
   `OUT #FFFD,#F8 / OUT #FFFD,#00 / OUT #BFFD,#BF / IN A,(#FFFD); JP P,ts_ok`
   - reads back R0 of the #F8-latched chip; bit7==0 => "TS present"
2. On a classic 2xAY TurboSound the #BF marker survives => readback #BF =>
   **8EBF=0** ("no TS").
3. 8EBF gates every TS setup (`LD A,(8EBF); OR A; JP Z,skip` pattern) =>
   **TS_MODE_FLAG (6B02) stays 0** => DUAL_CHIP_INIT skipped, and the
   interrupt handler's switch block (0x9060/0x907D: #FE/#FF selects +
   CALL engine per chip) never runs.
4. Captured consequence: all 15,394 music writes went to ONE chip (pc
   C5B4/C5B7 = the VTII register-OUT loop inside a paged engine instance);
   chip 2 got exactly the 9 mute writes of MUTE_BOTH (0x8A61).

## Engine instances and install patches

The player copies the VTII master (bank 1) into place and **patches bytes per
instance** (chip-select value / module pointer / play trampoline) - see
`engine_install_patches.txt` (instance @0xA000 vs master @bank1) and the
A000-diff annotations in `wildplayer_body.asm`.

## Files

- `wildplayer_body.asm` - annotated disassembly of the pure body (READ THIS)
- `wildplayer_bank1_vtii.asm` - VTII engine master + module staging
- `wildplayer_bank4_unipt2.asm` - UniPT2 engine
- `engine_install_patches.txt` - per-instance install patches
- `wildplayer_body_org5D3B.bin` - the extracted pure body binary
"""
open(os.path.join(OUT, "WILDDISASM_NOTES.md"), "w").write(notes)
print(f"notes: {os.path.join(OUT, 'WILDDISASM_NOTES.md')}")
