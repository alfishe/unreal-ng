#!/usr/bin/env python3
"""Extract the TFM Music Compiler 1.12 player from TSFM-EL.TAP and build a
fully annotated disassembly (educational RE companion).

Inputs:
  testdata/sound/tsfm/TSFM-EL.TAP     the source tape (repo-relative)
  - or - player_org61A8.bin           a previously extracted player block

Outputs (next to this script):
  player_org61A8.bin                  1883-byte player block, org 0x61A8
  tfmplayer-z80dasm.asm               machine disassembly (z80dasm reference)
  tfmplayer-z80dasm.asm.sym           z80dasm symbol dictionary

NOT written by this script: tfmplayer.asm is the HAND-WRITTEN annotated
source that assembles bit-identical to player_org61A8.bin (sjasmplus);
regenerating the z80dasm output never touches it.

Usage: disasm_tfmplayer.py [TAP_PATH | BIN_PATH] [OUT_DIR]
Location-aware: with no arguments the docs folder regenerates itself.
"""
import os
import re
import subprocess
import sys
from collections import OrderedDict

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(HERE))))
TAP_DEFAULT = os.path.join(REPO, "testdata/sound/tsfm/TSFM-EL.TAP")

ORG = 0x61A8            # 25000 - the player's link address (self-referential)
PLAYER_LEN = 1883
RELOC_TABLE = 0x68F7    # last 12 bytes: six defw targets (pattern ptr slots)


def parse_tap(data):
    """Yield (header, body) for every header/data pair in a .tap stream."""
    pos, pending = 0, None
    while pos + 2 <= len(data):
        length = data[pos] | data[pos + 1] << 8
        if length == 0 or pos + 2 + length > len(data):
            break
        block = data[pos + 2:pos + 2 + length]
        if block[0] == 0x00 and length == 19:
            body = block[1:]
            pending = {
                "type": body[0],
                "name": body[1:11].decode("ascii", "replace"),
                "length": body[11] | body[12] << 8,
                "start": body[13] | body[14] << 8,
            }
        elif block[0] == 0xFF and pending is not None:
            yield pending, block[1:-1]
            pending = None
        pos += 2 + length


def extract_player(tap_path):
    """The player is the type-3 block named '_tsfmplaye' loading at 25000."""
    for header, data in parse_tap(open(tap_path, "rb").read()):
        if (header["type"] == 3 and header["start"] == ORG
                and header["name"].startswith("_tsfmplaye")):
            assert len(data) == PLAYER_LEN, len(data)
            return data
    sys.exit("player block not found in " + tap_path)


# ---------------------------------------------------------------------------
# Symbol table: addr -> (label, [comment lines]) - comments transfer into
# the listing at the label definition site.
# ---------------------------------------------------------------------------
SYMS = OrderedDict([
    (0x61A8, ("MAIN_ENTRY", [
        "LD HL,08000h (tune base); CALL TRAMP_INIT; EI; then the HALT loop:",
        "one interrupt = one frame. Border flips black/white around the frame",
        "call - the classic visual heartbeat. Any key half-row bit low exits:",
        "CALL TRAMP_STOP (chip reset), HL'=02758h for the caller, RET.",
        "This is the address a .sna sets as PC to start playback (see",
        "tsfm-sna-guide.md in this folder)."])),
    (0x61AF, ("PLAY_LOOP", ["HALT loop body: border 0, FRAME, border 7, key scan"])),
    (0x61CC, ("TRAMP_INIT", ["JP INIT - trampoline so callers need no internals"])),
    (0x61CF, ("TRAMP_FRAME", ["JP FRAME - per-interrupt entry from the HALT loop"])),
    (0x61D2, ("TRAMP_STOP", ["JP CHIP_RESET - mute + rearm both chips"])),
    (0x61D5, ("INIT", [
        "HL = tune base ('TFMcom1.12'). tune+9 is the version character:",
        "'2' (032h) = current format  -> patch FRAME epilogue to plain RET",
        "anything else              -> keep LD A,6 divider (play 1 of 6 frames)",
        "Then the relocation pass: six 16-bit offsets from tune+10..tune+21",
        "are converted to absolute addresses and stored through RELOC_TABLE",
        "into the channel stubs' LD HL,nnnn slots (one per YM2203 channel).",
        "Each stub's LD A,0FDh operand (channel state slot) is reset to 0FFh",
        "('pattern not started'). FALLS THROUGH into CHIP_RESET - init also",
        "mutes and rearms both chips before the first frame."])),
    (0x61E4, ("INIT_PATCH", ["LD (FRAME_DIVIDER),A - the version-dependent SMC patch"])),
    (0x61EE, ("RELOC_LOOP", [
        "Six-iteration LDI/LDI + pointer-fix loop. HL' walks RELOC_TABLE,",
        "DE'=061FDh is the tune-base-relative delta, BC counts 0x06FD times",
        "for the LDI pair (copies the tune header area), and each offset from",
        "the tune is ADDed to the tune base before storing to the stub slot."])),
    (0x622A, ("CHIP_RESET", [
        "Select control word 0xF8 (chip 0), CALL REG_MOP (zero/silence every",
        "YM2203 register group), then SELECT_F9 for chip 1 - but note",
        "REG_MOP itself leaves the selection as it found it: the final",
        "SELECT_F9 call here switches the ACTIVE chip for the next frame.",
        "Shared by INIT fall-through and the keypress exit path."])),
    (0x6238, ("REG_MOP", [
        "Zeroing sweep, register/data pairs written through REG_WRITE:",
        "  regs 0x0D..0x00 data 0x00   (period/noise/env - AY half)",
        "  regs 0xB3..0x40 data 0x0F   (FM operators: max attenuation = silent)",
        "    (0x4F would collide with the SSG envelope; patched to 0x3F)",
        "  regs 0x8F..0x80 data 0x0F   (channel total levels = silent)",
        "  reg  0x28 data 0,1,2 then 0x2A,0x2B key-off every FM channel",
        "  regs 0x7F..0x40 data 0x0F / reg 0x2F / reg 0x2D (0x2D = mode: AY)",
        "The same sweep, with 0xF9 selected first, silences chip 1."])),
    (0x628A, ("REG_WRITE", [
        "The register/data pair helper (A = register, A' = data):",
        "  poll IN F,(C) on 0xFFFD until bit7 clear (not busy), OUT register",
        "  swap AF; poll again; OUT data to 0xBFFD (B=0xBF for the data half)",
        "B=0xFF/B=0xBF are kept in D/E by every caller. On TSFM hardware",
        "0xFFFD reads return a status byte (bit7=busy); the 32-cycle busy",
        "window after each write is what these loops wait out."])),
    (0x628B, ("WAIT_REG", ["busy-poll before the register-select OUT"])),
    (0x6293, ("WAIT_DATA", ["busy-poll before the data OUT (the P4 park site on",
                            "legacy 2xAY TurboSound: the AY mixer reads back 0xFF",
                            "with bit7 set, so the loop never exits)"])),
    (0x629D, ("SELECT_F8", ["OUT 0xFFFD,0xF8 - control word: chip 0 active"])),
    (0x62A3, ("SELECT_F9", ["OUT 0xFFFD,0xF9 - control word: chip 1 active"])),
    (0x62A9, ("OUT_7FFD", [
        "OUT (0x7FFD),A helper (Pentagon latch). Not called from inside the",
        "player - a service entry for the embedding program (e.g. to restore",
        "ROM paging after a TR-DOS load, exactly what the tsfm-elite boot",
        "stub replicates)."])),
    (0x62B1, ("FRAME", [
        "The per-interrupt play routine, called from PLAY_LOOP:",
        "  SELECT_F8; CALL CH1/CH2/CH3 (chip 0 - YM #1)",
        "  SELECT_F9; CALL CH4/CH5/CH6 (chip 1 - YM #2)",
        "Each channel stub interprets its pattern stream and emits YM2203",
        "register writes through the same busy-polled port pairs."])),
    (0x62D2, ("FRAME_DIVIDER", [
        "SMC frame-rate divider, patched by INIT: for TFMcom1.12 tunes",
        "(version '2') INIT writes a RET here, so every frame plays; older",
        "tunes keep LD A,6 / DEC A / JR NZ and only re-enter FRAME every 6th",
        "interrupt (12.5 Hz effective frame rate for pre-1.0 data)."])),
    (0x62DF, ("OUTI_PAIRS", [
        "Bulk block writer for register/data streams: poll, OUTI the register",
        "byte to 0xFFFD, poll, OUTI the data byte to 0xBFFD; A counts pairs.",
        "Used by the instrument-load op to push a whole operator table."])),
    (0x62F3, ("OP_INSTRUMENT", [
        "Pattern op: byte  -> CH1's LD A,(HL) slot at 636Eh (operator table",
        "length), then BC = 16-bit instrument data pointer from the stream,",
        "stored into the stub's LD HL,nnnn slot at 6378h; entry continues",
        "into OP_DISPATCH inline (CALL-less tail sharing)."])),
    (0x6315, ("OP_BIG_DISPATCH", [
        "Second-level dispatch for opcodes >= 0xBF reached from OP_DISPATCH:",
        "  0xBF   JR Z taken -> OP_CALL (call pattern, 0x6305)",
        "  >=0xE0 B=A (register block select), 0xFF -> OP_CALL_16 (0x6307)",
        "         else the opcode becomes the channel's next state value",
        "         (written into the stub's LD A,nn slot - see 0x6323)",
        "  0xC0..0xDF -> OP_REG_WRITE (0x6327) after the +0x30/+0xC3 SMC dance"])),
    (0x6323, ("OP_SET_STATE", ["LD (CH1+1),A - opcode becomes the channel state byte",
                               "checked by the stub prologue on the next frame"])),
    (0x6327, ("OP_REG_WRITE", [
        "General YM register write op: A' carries the data byte; the two",
        "ADDs map the opcode into a register number, SMC'd into 632Ch, then",
        "HL = 22A4h-style (reg<<8|data) pairs are pushed through the busy-",
        "polled OUT sequence (register to 0xFFFD, data to 0xBFFD, then a",
        "0xA0-class register write with A) at WRITE_REG_DATA."])),
    (0x6337, ("WRITE_REG_DATA", ["poll/OUT L (register), poll/OUT H (data) - the",
                                 "interpreter's write helper (like REG_WRITE but",
                                 "register+data travel in HL)"])),
    (0x6359, ("OP_LOOP", ["0x7E marker: remember the current stream position as the",
                          "pattern loop point (SMC slot at 0x6360)"])),
    (0x635F, ("OP_RESTART", ["0x7F marker: reload HL from the loop point and replay"])),
    (0x6365, ("CH1", ["channel stub 1 - chip 0 channel 1 (see CHANNEL STUBS below)"])),
    (0x6465, ("CH2", ["channel stub 2 - chip 0 channel 2"])),
    (0x6566, ("CH3", ["channel stub 3 - chip 0 channel 3"])),
    (0x6667, ("CH4", ["channel stub 4 - chip 1 channel 1"])),
    (0x6767, ("CH5", ["channel stub 5 - chip 1 channel 2"])),
    (0x6868, ("CH6", ["channel stub 6 - chip 1 channel 3"])),
    (0x637A, ("OP_DISPATCH", [
        "Fetch opcode from (HL):",
        "  0x7E -> OP_LOOP      0x7F -> OP_RESTART",
        "  < 0x80 (bit6 set)   -> key control path (reg 0x28 + freq pairs)",
        "  >= 0xBF              -> OP_BIG_DISPATCH",
        "everything else falls through as data for the previous op."])),
    (0x68F7, ("RELOC_TABLE", [
        "DATA - six defw: 636B 646B 656C 666D 676D 686E",
        "addresses of the six channel stubs' LD HL,nnnn pattern-pointer",
        "slots. INIT walks this table and stores the relocated (absolute)",
        "tune pattern pointers there. The block is position-dependent:",
        "the player MUST be loaded at 0x61A8."])),
])

# data ranges inside the block: (start, end-exclusive, kind, note)
DATA_RANGES = [
    (RELOC_TABLE, ORG + PLAYER_LEN, "word",
     "RELOC_TABLE - six pattern-pointer slot addresses (defw)"),
]


def autolabels(data, base, syms):
    """Add R_xxxx labels for absolute CALL/JP targets inside the block."""
    for i in range(len(data) - 2):
        if data[i] in (0xCD, 0xC3):
            t = data[i + 1] | data[i + 2] << 8
            if base <= t < base + len(data) and t not in syms:
                syms[t] = (f"R_{t:04X}", [])
    return syms


LINE_HINT = re.compile(r";([0-9a-fA-F]{4})\b")


def disassemble(binpath, org, syms, data_ranges, header, outpath):
    """Single code segment (complement of data_ranges) + data blocks."""
    symfile = outpath + ".sym"
    with open(symfile, "w") as f:
        for addr, (name, comments) in sorted(syms.items()):
            for c in comments:
                f.write(";" + c + "\n")
            f.write(f"{name}: equ 0x{addr:x}\n")

    data = open(binpath, "rb").read()
    end_all = org + len(data)
    ranges = sorted(data_ranges)

    segments, cur = [], org
    for s, e, *_ in ranges:
        if s > cur:
            segments.append((cur, s))
        cur = max(cur, e)
    if cur < end_all:
        segments.append((cur, end_all))

    annotations = {a: c for a, (n, c) in syms.items() if c}
    symname = {a: n for a, (n, c) in syms.items()}
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
            out.append(f"{n:20s}: equ 0x{a:04X}")
    out.append("")

    items = ([(s, "code", (s, e)) for s, e in segments] +
             [(s, "data", (s, e, k, n)) for s, e, k, n in ranges])
    items.sort(key=lambda t: t[0])

    tmp = 0
    for addr, kind, payload in items:
        if kind == "code":
            s, e = payload
            seg_bin = os.path.join(HERE, f".seg{tmp}.bin")
            raw = seg_bin + ".raw"
            tmp += 1
            open(seg_bin, "wb").write(data[s - org:e - org])
            subprocess.run(["z80dasm", "-l", "-a", "-t", "-g", hex(s),
                            "-S", symfile, "-o", raw, seg_bin], check=True,
                           stderr=subprocess.DEVNULL)
            lines = open(raw).read().splitlines()
            os.remove(raw)
            os.remove(seg_bin)
            k = 0
            while k < len(lines) and not lines[k].strip().lower().startswith("org"):
                k += 1
            k += 1
            while k < len(lines) and (equ_echo.match(lines[k]) or not lines[k].strip()):
                k += 1
            for line in lines[k:]:
                if equ_echo.match(line):
                    continue
                # annotations fire at the label DEFINITION site (the label
                # may share the line with an instruction or stand alone)
                hit = next((a for a, n in sorted(symname.items())
                            if line.startswith(n + ":")), None)
                if hit is not None and hit in annotations:
                    out.append("; " + "-" * 74)
                    for c in annotations[hit]:
                        out.append("; " + c)
                    out.append("; " + "-" * 74)
                m = LINE_HINT.search(line)
                if not m:
                    out.append(line)
                    continue
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
            words = [chunk[i] | chunk[i + 1] << 8 for i in range(0, len(chunk), 2)]
            out.append("\tdefw " + ",".join(f"0x{w:04X}" for w in words))
            out.append("")

    open(outpath, "w").write("\n".join(out) + "\n")
    print(f"disasm: {outpath} ({len(out)} lines, {len(segments)} code segments)")


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else TAP_DEFAULT
    outdir = sys.argv[2] if len(sys.argv) > 2 else HERE
    os.makedirs(outdir, exist_ok=True)
    binpath = os.path.join(outdir, "player_org61A8.bin")

    if src.lower().endswith(".bin"):
        data = open(src, "rb").read()
        assert len(data) == PLAYER_LEN, len(data)
        open(binpath, "wb").write(data)
        print(f"copied {src} -> {binpath}")
    else:
        data = extract_player(src)
        open(binpath, "wb").write(data)
        print(f"extracted player: {len(data)} bytes @0x{ORG:04X} -> {binpath}")

    syms = autolabels(data, ORG, dict(SYMS))
    syms = OrderedDict(sorted(syms.items()))
    disassemble(
        binpath, ORG, syms, DATA_RANGES,
        [
            "TFM Music Compiler 1.12 player ('_tsfmplaye' block of TSFM-EL.TAP)",
            f"binary: player_org61A8.bin  org 0x{ORG:04X}  len {PLAYER_LEN}",
            "",
            "Machine-generated z80dasm reference. The hand-written annotated",
            "source (same labels, full commentary, assembles bit-identical)",
            "lives in tfmplayer.asm - read that one instead.",
            "",
            "Drives the TurboSound FM board (2xYM2203, ports 0xFFFD/0xBFFD,",
            "control words 0xF8/0xF9 select the chip; #FFFD reads = status).",
            "Position-dependent: must sit at 0x61A8; tunes at 0x8000.",
            "",
            "READING ORDER",
            "  1. MAIN_ENTRY / PLAY_LOOP     the .sna hand-off point",
            "  2. INIT                       version check + pattern relocation",
            "  3. CHIP_RESET / REG_WRITE     how the chips are silenced",
            "  4. FRAME + channel stubs      the per-interrupt interpreter",
            "  5. OUT_7FFD                   free service entry for embedders",
        ],
        os.path.join(outdir, "tfmplayer-z80dasm.asm"))


if __name__ == "__main__":
    main()
