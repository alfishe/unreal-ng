#!/usr/bin/env python3
"""Rebuild the three listings of this folder from prosiak.exe (needs z80dasm 1.2.0 on PATH).

    make_listings.py prosiak.exe

Names and comment blocks come from the tables below; z80dasm does the rest. A comment block is
written above the line that carries its address (a label or an instruction)."""
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from unpack_prosiak import unpack  # noqa: E402

# ---------------------------------------------------------------- stage 1: the DSS primary loader
LOADER = {
    0x4327: ("LoaderStart", "Load address of the primary loader (file offset #24347, #6AC bytes)."),
    0x432A: ("AllocBlock", "DSS GetMem (#3D): A = pages, C = handle tag. Keeps up to #40 blocks: the DSS handle\n"
                           "at #4266+n, the tag at #42E6+n; BIOS GetMemBlkPages (#C5) writes the block's\n"
                           "physical pages. Carry = no memory."),
    0x4375: ("AllocPage", "One page: AllocBlock with A = 1; returns the physical page in A."),
    0x4384: ("FreeAll", "DSS FreeMem (#3E) for every block AllocBlock got."),
    0x43A2: ("PageOfTag", "C = tag: A = the block's first physical page, Carry = unknown tag."),
    0x43C4: ("RunStage2", "Call stage 2 at #0100 (window 0 = the page AllocPage gave), then restore window 0,\n"
                          "close the file (DSS #12), print \"Done ...\", FreeAll, DSS Exit (#41)."),
    0x43E2: ("ErrorExit", "Print \"Error Execute ...\" and exit."),
    0x4409: ("Entry", "The DSS start address. (IX-3) = CLP_FM, the handle of this EXE file that DSS keeps\n"
                      "open for a program with a primary loader; kept at #4105 for every later read.\n"
                      "Saves the window-0 page (port #82) at #4100, prints the banner, takes three pages\n"
                      "(window 0 for stage 2, windows 2 and 3), copies Depack to #3D4F and unpacks the\n"
                      "LZSA2 stream at #44EA to #0000 of the new window-0 page. Then the 33-byte RST\n"
                      "trampolines (stage 2 #05E6) go to #3DDF of both window-0 pages, so DSS calls work\n"
                      "from stage 2."),
}
LOADER_DATA = [(0x43EA, 0x4409), (0x44AF, 0x44EA)]

# The LZSA2 depacker as it runs: copied from #4943 to #3D4F
DEPACK = {
    0x3D4F: ("DepackBackward", "Entry for a backward stream (not used here)."),
    0x3D56: ("Depack", "LZSA2 raw block, forward: HL = packed data, DE = output. The format is LZSA2 of\n"
                       "github.com/emmanuel-marty/lzsa; `lzsa -d -r -f 2` gives the same bytes."),
    0x3DB7: ("DepackLength", "Extended length: nibble, then byte, then 16-bit word."),
    0x3DCE: ("DepackNibble", "Next nibble (high first) in A, the other half kept in A'."),
}

# ---------------------------------------------------------------- stage 2: the loader VM (window 0)
STAGE2 = {
    0x0008: ("Rst08", "BIOS calls: through the trampoline at #3DDF (window 0 back to the DSS page)."),
    0x0010: ("Rst10", "DSS calls: through the trampoline at #3DE5."),
    0x0100: ("Stage2Start", "Prints two lines, keeps SP for VmExit, sets the key word #05C4 = #FFFF and runs\n"
                            "the VM script from the file's second sector (file offset #100)."),
    0x011C: ("VmExit", "Back to the loader (#43C7): SP as at the start, DI, RET. Script op `24 1C 01`."),
    0x0121: ("PrintZ", "Print the zero-terminated string at HL (DSS PutChar #5B)."),
    0x012C: ("PutChar", ""),
    0x013E: ("PrintHex16", ""),
    0x0145: ("PrintHex8", ""),
    0x0158: ("PrintDec", ""),
    0x0190: ("HexDump", "Debug dump: BC bytes from HL (not called by this file's script)."),
    0x01EE: ("OpCompare", "Ops #E8 (if ... gosub) / #EC (if ... goto): two operand words, each the low\n"
                          "byte itself when the high byte is 0, else the byte at that address (C = first,\n"
                          "B = second); the next script byte is the condition: #C0 C < B, #C6 C <= B,\n"
                          "#CE ==, #D4 C >= B, #DC C > B, #E2 !=; then the 2-byte target, skipped when the\n"
                          "condition is false. Read from the code; this file's script has none."),
    0x0244: ("VmSeek", "Read #40 script bytes from the VM position into #4106."),
    0x0247: ("VmLoop", "Re-read when the pointer reaches #4136 (16 bytes before the buffer's end)."),
    0x024D: ("VmFetch", "Opcode = the displacement of the JR at #0252 (self-modifying dispatch):\n"
                        "target = #0254 + op. Traced with TTD: #024D runs once per op."),
    0x0254: ("OpLoad", "Op #00: load a record (5 bytes: A, DE, HL, BC) without unpacking."),
    0x0259: ("OpLoadUnpack", "Op #05 `05 A DE HL BC`: read BC bytes from file offset #100*(DE+1)+A to HL,\n"
                             "then UnpackInPlace."),
    0x026E: ("OpPut", "Op #1A: write a record to the #8000 window (read from the code; unused here)."),
    0x0273: ("OpPutC", "Op #1F: as #1A with Carry set (unused here)."),
    0x0278: ("OpCall", "Op #24 `24 lo hi`: CALL lo hi with HL / BC = the last block's address / length."),
    0x0294: ("OpGosub", "Op #40 `40 lo hi`: push the return position (Z80 stack), continue at lo hi.\n"
                        "Op #47 enters at #029B: goto. Op #63 (#02B7): return. Unused here."),
    0x02AB: ("OpPages", "Op #57 `57 fn arg`: page service (#0375)."),
    0x02BA: ("OpKeyCheck", "Op #66: word XOR (#05C4) == 0: the XOR key at #05C6 becomes active, else jump.\n"
                           "Unused here: #05C4 stays #FFFF and the key is empty."),
    0x02EB: ("UnpackInPlace", "HL = record start: a word = the unpacked length, then the LZSA2 stream; BC =\n"
                              "record length. The output ends where the record ends; the 32 bytes behind\n"
                              "the record are saved at #05A4 during the unpacking and put back after it."),
    0x0352: ("Nop", "DI; RET (around every call out of the VM)."),
    0x0375: ("PageService", "A = 2: map the remembered tag's page into window 2 (#C2); 3: into window 3\n"
                            "(#E2); 4: restore both; 5: remember tag C; 6 (C != 0): AllocPage for it."),
    0x03C1: ("ReadRecord", "A, DE, HL, BC from the script."),
    0x0469: ("LoadRecord", "ReadRecord, then ReadFile."),
    0x0473: ("ReadFile", "Read BC bytes from the record's file position to HL. A part that would land on\n"
                         "#3DFF-#3E1F (the depacker and trampolines) goes through the copy at #4246."),
    0x04EB: ("ReadChunk", "Bit 7 of D = this chunk is XORed with the key at #05C6 (cycled to its NUL)\n"
                          "after the read. No record of this file sets it."),
    0x052B: ("ReadSectors", "A = byte offset in the first sector, DE = sector - 1; partial first and last\n"
                            "sectors go through the 256-byte buffer #4146."),
    0x0576: ("DssReadSectors", "DSS Move_FP (#15) to (DE+1)*256, then DSS Read (#13) of B sectors."),
    0x05A4: ("SaveArea", "32 bytes kept by UnpackInPlace."),
    0x05C4: ("KeyWord", "#FFFF: OpKeyCheck compares against it."),
    0x05C6: ("XorKey", "The XOR key (zero-terminated; empty in this file)."),
    0x05E6: ("Trampolines", "Copied to #3DDF: map the DSS page into window 0, run its RST, map stage 2 back."),
}
STAGE2_DATA = [(0x0013, 0x0038), (0x003D, 0x0064), (0x006B, 0x0100), (0x01CD, 0x01EE), (0x05A4, 0x05E6)]

# ---------------------------------------------------------------- stage 3: block 0, the Wild Sound player
PLAYER = {
    0x4500: ("PlayerInit", "Script op `24 00 45`. Prints \"XM Init ... OK\", probes ISA slot 0, then slot 1.\n"
                           "Neither: AEN on (BusIdle) and VmExit, so the loader prints \"Done ...\"."),
    0x4519: ("ProbeSlot", "A = slot: prints \"Wild Sound ... ISA #n \", selects the slot, Identify; prints the\n"
                          "card's string or \"Not Found\". Z = found."),
    0x455D: ("WriteWord", "DE to ISA port 1 (data)."),
    0x458C: ("ReadData", "B bytes from ISA port 1 to HL."),
    0x459A: ("WriteData", "B bytes from HL to ISA port 1."),
    0x45B5: ("CmdF1", "Command #F1 to port 0, then the word A (D = 0) to port 1: select a function."),
    0x45C5: ("CmdF2", "Command #F2, count B, then B data bytes: one data packet."),
    0x45DD: ("Checksum", "Rolling add / xor / sub over the status frame."),
    0x4601: ("ReadStatus", "Command #F3, then 32 bytes from port 1 to #4DD5; valid when bytes 29 / 30 are\n"
                           "#FA / #3D and the checksum matches. The player waits here for the card."),
    0x465C: ("WaitReady", "Loops on ReadStatus, flashing the border."),
    0x4686: ("UploadBlock", "Sends DE bytes as #F2 packets, each no longer than the card's buffer size from\n"
                            "the status frame (#4DDF)."),
    0x4755: ("LoadTrack", "Script op `24 55 47` after block 1: first call #F1 #00, #01, #02, then #F1 #10\n"
                          "and the data; \"XM TRK Data\"."),
    0x4796: ("LoadInsHeaders", "Script op `24 96 47` after block 2: #F1 #20 and the data; \"XM INS HDR\"."),
    0x47AB: ("LoadSamples", "Script op `24 AB 47` after blocks 3-7: #F1 #21 and the data; \"XM INS\"."),
    0x47C0: ("Play", "Script op `24 C0 47`: #F1 #30, \"XM PLAY\", wait for a key, #F1 #00 and #01,\n"
                     "AEN on. The card plays; the Z80 only waits."),
    0x463B: ("SetBorder", "Border colour (port #FE): LoadInsHeaders 5, LoadSamples 4, Play 0 (6 at #4784, not called)."),
    0x4990: ("SetIsaLatch", "#1FFD = #11 (extended pages), #9FBD = A (A19-A14 = bits 5-0, AEN = bit 6,\n"
                            "RESET = bit 7; core/src/emulator/io/sprinter/isa/sprinterisabus.h), #1FFD = #01."),
    0x49DC: ("SetSlot", "A = slot: patches the operand of SelectIsaIo to 4 | slot*2."),
    0x49F2: ("SelectIsaIo", "ISA I/O page #D0 | (A & 6): #D4 = slot 0, #D6 = slot 1, patched into every\n"
                            "access routine below; #9FBD = 0."),
    0x4A1E: ("BusIdle", "#9FBD = #40: AEN on, no cycles reach the cards."),
    0x4A26: ("IsaOut", "Write A to ISA I/O port DE: window 3 = the slot's I/O page, (#C000 | DE) = A."),
    0x4A46: ("IsaOutWord", ""),
    0x4A6B: ("IsaOutBlock", "BC bytes from HL to the one port DE."),
    0x4B3A: ("IsaIn", "Read ISA I/O port DE into A."),
    0x4B7D: ("IsaInBlock", "BC bytes from the one port HL to DE."),
    0x4D47: ("Identify", "SendEnable with A = 0; found when the card answers #DF; then up to 32 bytes of\n"
                         "its identification string go to #4D55."),
    0x4D82: ("SendEnable", "Port 0 <- #F0, port 1 <- \"Wild Sound Enable \", #00 (A), #00; A = port 1."),
    0x4DBA: ("InData", "A = ISA port 1."),
    0x4DC3: ("OutCmd", "ISA port 0 <- A."),
    0x4DCC: ("OutData", "ISA port 1 <- A."),
}
PLAYER_DATA = [(0x454D, 0x455D), (0x47FA, 0x4858), (0x48B6, 0x48FF), (0x4C47, 0x4D47), (0x4D55, 0x4D76),
               (0x4DA7, 0x4DBA)]


def dasm(data, org, names, blocks, title):
    with tempfile.TemporaryDirectory() as tmp:
        b, s, k = (os.path.join(tmp, n) for n in ("in.bin", "in.sym", "in.blk"))
        open(b, "wb").write(data)
        open(s, "w").write("".join(f"{n}: equ 0x{a:04x}\n" for a, (n, _) in names.items()
                                   if org <= a < org + len(data)))
        open(k, "w").write("".join(f"d{a:04x}: start 0x{a:04x} end 0x{e:04x} type bytedata\n"
                                   for a, e in blocks))
        out = subprocess.run(["z80dasm", "-a", "-l", "-t", "-g", hex(org), "-S", s, "-b", k, b],
                             capture_output=True, text=True, check=True).stdout
    lines, done = [], set()
    for line in out.splitlines():
        if line.startswith("; command line:") or re.match(r"^d[0-9a-f]{4}_end:", line):
            continue
        m = re.match(r"^(\w+):", line)
        addr = next((a for a, (n, _) in names.items() if m and n == m.group(1)), None)
        if addr is None:
            m = re.search(r";([0-9a-f]{4})\t", line)
            addr = int(m.group(1), 16) if m else None
        if addr in names and addr not in done and names[addr][1]:
            done.add(addr)
            lines += [""] + ["; " + t for t in names[addr][1].split("\n")]
        lines.append(line)
    return [f"; {title}"] + lines


def main(exe_path):
    exe = open(exe_path, "rb").read()
    with tempfile.TemporaryDirectory() as tmp:
        unpack(exe, tmp, verbose=False)
        get = lambda n: open(os.path.join(tmp, n), "rb").read()  # noqa: E731
        loader, stage2, player = get("loader.bin"), get("stage2.bin"), get("block0.bin")
    head = ("; prosiak.exe (Wild Sound XM player, Sprinter). Rebuilt by tools/make_listings.py from the\n"
            "; file of the MAME pack's system disk (DEMOS/WILDSND); z80dasm 1.2.0 with -a -l -t.\n")
    code = loader[:0x44EA - 0x4327]
    text = dasm(code, 0x4327, LOADER, LOADER_DATA, "Stage 1: the primary loader, #4327-#44E9") + [
        "", "; #44EA-#4942: the LZSA2 stream of stage 2 (stage2.asm); #4943-#49D2: Depack, below at its run",
        "; address #3D4F (the loader copies it there before the call)", ""]
    text += dasm(loader[0x4943 - 0x4327:0x4943 - 0x4327 + 0x90], 0x3D4F, DEPACK, [], "Depack at #3D4F")
    open(os.path.join(HERE, "..", "loader.asm"), "w").write(head + "\n".join(text) + "\n")
    text = dasm(stage2, 0, STAGE2, STAGE2_DATA, "Stage 2: the loader VM, #0000-#0606 of its own page (window 0)")
    open(os.path.join(HERE, "..", "stage2-vm.asm"), "w").write(head + "\n".join(text) + "\n")
    text = dasm(player, 0x4500, PLAYER, PLAYER_DATA, "Stage 3: block 0, the Wild Sound player, #4500-#4DD4")
    open(os.path.join(HERE, "..", "player.asm"), "w").write(head + "\n".join(text) + "\n")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    main(sys.argv[1])
