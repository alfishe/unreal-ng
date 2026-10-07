#!/usr/bin/env python3
"""Writes the ZX-MultiSound TTD fixture program, a 128K .sna that plays every source of the card.

The TTD corpus sessions `multisound-pentagon` and `multisound-zxevo` (testdata/ttd/README.md) record this program
on a Pentagon 128 and on a ZX-Evo (BaseConf) with the card in ZX-bus slot 1; the corpus tests record them in their
own process (core/tests/_helpers/ttdmultisoundsessions.h), nothing else needs re-recording after a change here. Every frame the program writes the
SounDrive DACs; every 16 frames it changes the notes of both YM2203 (FM and SSG), of the SAA1099 and sends MIDI
(reverb / chorus sends off, a program change, note off / note on and a drum hit), bit-banged on YM U4's IOA2 at 31 250 baud; once it sends the
General Sound a command. So a 300-frame recording holds state changes of every device of the card.

Deterministic: the same file on every run. Usage:

    python3 tools/verification/multisound/ttd-fixture/make-program.py [output.sna]

Default output: testdata/sound/multisound/ttd/allsources.sna (relative to the project root).
"""

import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
DEFAULT_OUT = ROOT / "testdata" / "sound" / "multisound" / "ttd" / "allsources.sna"

ORIGIN = 0x8000          # program
STEPS_TABLE = 0xA000     # 8 x u16: the note-change routine of each step
MIDI_TABLES = 0xB000     # 8 x 256 bytes: R14 values, one per MIDI bit
FRAME = 0x7000           # frame counter (u8)
SAMPLE = 0x7001          # SounDrive phase (u8)
INT_FLAG = 0x7002        # set by the interrupt handler (CPU speed calibration)
INT_CODE = 0x7F00        # the interrupt handler
BITBANG = 0x7F20         # the MIDI bit-bang subroutine (its delay count patched by the calibration)
STACK = 0x7FF0
VECTORS = 0xFE00         # IM 2 table: 257 x #FD -> handler at #FDFD
HANDLER = 0xFDFD

CONTROL_U4 = 0xF2        # control byte: chip select 0 (U4, the MIDI chip), FM on, SAA clock on
CONTROL_U10 = 0xF3       # chip select 1 (U10)
LINE_HIGH = 0xFF         # R14 with IOA2 = 1 (MIDI line idle)
LINE_LOW = 0xFB          # IOA2 = 0
# 31 250 baud is 112 T-states a bit at 3.5 MHz. The bit loop costs 16 n + 48 T-states for a delay count n:
# n = 4 at 3.5 MHz (112), 11 at 7 MHz (224), 25 at 14 MHz (448); the calibration picks n from the CPU speed
DELAY_FOR_SPEED = {1: 4, 2: 11, 4: 25}


class Asm:
    def __init__(self, origin):
        self.origin = origin
        self.code = bytearray()

    def here(self):
        return self.origin + len(self.code)

    def b(self, *values):
        self.code.extend(values)

    def out(self, port, value):
        # LD BC,port; LD A,value; OUT (C),A
        self.b(0x01, port & 0xFF, port >> 8, 0x3E, value, 0xED, 0x79)

    def ym(self, reg, value):
        self.out(0xFFFD, reg)
        self.out(0xBFFD, value)

    def saa(self, reg, value):
        self.out(0x01FF, reg)
        self.out(0x00FF, value)

    def midi_bits(self, table, count):
        """Bit-bangs `count` R14 values from `table` on U4 (selected by the caller) through the subroutine"""
        self.out(0xFFFD, 0x0E)
        self.b(0x21, table & 0xFF, table >> 8)         # LD HL,table
        self.b(0x16, count)                            # LD D,count
        self.b(0xCD, BITBANG & 0xFF, BITBANG >> 8)     # CALL bitbang


def bitbang_routine():
    """LD BC,#BFFD; then per bit: LD A,(HL); OUT (C),A; INC HL; LD E,n; DEC E; JR NZ; LD E,0; DEC D; JP NZ = 16 n + 48
    T-states. Returns the code and the offset of n"""
    a = Asm(BITBANG)
    a.b(0x01, 0xFD, 0xBF)                              # LD BC,#BFFD
    loop = a.here()
    a.b(0x7E, 0xED, 0x79, 0x23)                        # LD A,(HL); OUT (C),A; INC HL      7 + 12 + 6
    a.b(0x1E, DELAY_FOR_SPEED[1])                      # LD E,n                             7
    n_offset = len(a.code) - 1
    a.b(0x1D, 0x20, 0xFD)                              # DEC E; JR NZ,$-1                    16 n - 5
    a.b(0x1E, 0x00)                                    # LD E,0                             7
    a.b(0x15)                                          # DEC D                              4
    a.b(0xC2, loop & 0xFF, loop >> 8)                  # JP NZ,loop                        10
    a.b(0xC9)                                          # RET
    return a.code, BITBANG + n_offset


def interrupt_handler():
    """PUSH AF; LD A,1; LD (flag),A; POP AF; EI; RETI"""
    return bytes([0xF5, 0x3E, 0x01, 0x32, INT_FLAG & 0xFF, INT_FLAG >> 8, 0xF1, 0xFB, 0xED, 0x4D])


def midi_line(data):
    """R14 values: two idle bits, then each byte as start, 8 data bits (LSB first), stop"""
    line = [LINE_HIGH, LINE_HIGH]
    for byte in data:
        line.append(LINE_LOW)
        line.extend(LINE_HIGH if (byte >> bit) & 1 else LINE_LOW for bit in range(8))
        line.append(LINE_HIGH)
    return line


FM_FNUM = [0x26A, 0x2B4, 0x309, 0x33A, 0x3A0, 0x411, 0x492, 0x4D6]    # an F-number per step (C major-ish)
SSG_PERIOD = [0x1AC, 0x17D, 0x153, 0x140, 0x11D, 0x0FE, 0x0E2, 0x0D6]
SAA_NOTE = [0x05, 0x21, 0x3C, 0x4A, 0x62, 0x78, 0x8D, 0x97]
MIDI_NOTE = [60, 62, 64, 65, 67, 69, 71, 72]


def init(a):
    a.b(0xF3)                                          # DI
    a.b(0x31, STACK & 0xFF, STACK >> 8)                # LD SP,stack
    a.b(0x3E, VECTORS >> 8, 0xED, 0x47, 0xED, 0x5E)   # LD A,#FE; LD I,A; IM 2
    # CPU speed: count 35-T-state loops from one interrupt to the next (about 2000 at 3.5 MHz, 4000 at 7 MHz,
    # 8000 at 14 MHz) and patch the bit-bang delay for 31 250 baud (the ZX-Evo starts at 7 MHz)
    a.b(0xFB, 0x76)                                     # EI; HALT
    a.b(0xAF, 0x32, INT_FLAG & 0xFF, INT_FLAG >> 8)     # XOR A; LD (flag),A
    a.b(0x21, 0x00, 0x00)                               # LD HL,0
    cal = a.here()
    a.b(0x23, 0x3A, INT_FLAG & 0xFF, INT_FLAG >> 8, 0xB7)   # INC HL; LD A,(flag); OR A
    a.b(0x28, (cal - (a.here() + 2)) & 0xFF)            # JR Z,cal
    a.b(0xF3)                                           # DI
    a.b(0x7C, 0x0E, DELAY_FOR_SPEED[1], 0xFE, 12, 0x38, 0x08)   # LD A,H; LD C,n1; CP 12; JR C,set
    a.b(0x0E, DELAY_FOR_SPEED[2], 0xFE, 24, 0x38, 0x02)         # LD C,n2; CP 24; JR C,set
    a.b(0x0E, DELAY_FOR_SPEED[4])                               # LD C,n4
    a.b(0x79, 0x32, 0x00, 0x00)                         # set: LD A,C; LD (n),A  (address patched in build)
    a.patch_n = len(a.code) - 2
    for chip, control in ((0, CONTROL_U4), (1, CONTROL_U10)):
        a.out(0xFFFD, control)
        for ch in range(3):
            for op in range(4):
                o = op * 4 + ch
                a.ym(0x30 + o, 0x01 + op)              # DT / MUL
                a.ym(0x40 + o, 0x00 if op == 3 else 0x28)   # TL: the carrier loud, modulators soft
                a.ym(0x50 + o, 0x1F)                   # AR
                a.ym(0x60 + o, 0x05)                   # DR
                a.ym(0x70 + o, 0x02)                   # SR
                a.ym(0x80 + o, 0x37)                   # SL / RR
            a.ym(0xB0 + ch, 0x32 if chip else 0x04)    # FB / algorithm
            a.ym(0x08 + ch, 0x0C - ch)                 # SSG volumes
        # U4: IOA an output (the MIDI line) with the line idle; tones A-C on. U10: tones A-C, noise on C
        a.ym(0x0E, LINE_HIGH)
        a.ym(0x07, 0x78 if chip == 0 else 0x18)
        a.ym(0x06, 0x0F)
    # SAA: reset the frequency generators, sound on, six voices, noise on voice 2 and 5
    a.saa(0x1C, 0x02)
    a.saa(0x1C, 0x01)
    for v in range(6):
        a.saa(v, 0x99 if v % 2 else 0x5C)
    a.saa(0x14, 0x3F)
    a.saa(0x15, 0x24)
    a.saa(0x16, 0x21)
    a.saa(0x18, 0x8A)                                   # envelope generator 0
    # General Sound: a command (#20, total RAM; the answer goes to its data register, the command flag clears when
    # the firmware takes it - it may still be booting)
    a.out(0x00BB, 0x20)
    a.b(0xAF, 0x32, FRAME & 0xFF, FRAME >> 8)           # XOR A; LD (frame),A
    a.b(0x32, SAMPLE & 0xFF, SAMPLE >> 8)               # LD (sample),A
    a.b(0xFB)                                           # EI


def main_loop(a):
    loop = a.here()
    a.b(0x76)                                           # HALT
    a.b(0x3A, FRAME & 0xFF, FRAME >> 8, 0x3C, 0x32, FRAME & 0xFF, FRAME >> 8)   # LD A,(frame); INC A; LD (frame),A
    # SounDrive: 48 samples per channel per frame, two saws and two squares
    a.b(0x3A, SAMPLE & 0xFF, SAMPLE >> 8, 0x5F)        # LD A,(sample); LD E,A
    a.b(0x16, 48)                                       # LD D,48
    sd = a.here()
    a.b(0x7B, 0x01, 0x0F, 0x00, 0xED, 0x79)             # LD A,E; LD BC,#000F; OUT (C),A
    a.b(0x0E, 0x4F, 0x2F, 0xED, 0x79)                   # LD C,#4F; CPL; OUT (C),A
    a.b(0x7B, 0xE6, 0x80, 0x0E, 0x1F, 0xED, 0x79)       # LD A,E; AND #80; LD C,#1F; OUT (C),A
    a.b(0x7B, 0xE6, 0x40, 0x87, 0x0E, 0x5F, 0xED, 0x79)  # LD A,E; AND #40; ADD A,A; LD C,#5F; OUT (C),A
    a.b(0x7B, 0xC6, 0x0B, 0x5F)                         # LD A,E; ADD A,11; LD E,A
    a.b(0x15, 0x20, (sd - (a.here() + 2)) & 0xFF)       # DEC D; JR NZ,sd
    a.b(0x7B, 0x32, SAMPLE & 0xFF, SAMPLE >> 8)         # LD A,E; LD (sample),A
    # Every 16 frames: the step's routine (step = frame / 16 mod 8), through the table
    a.b(0x3A, FRAME & 0xFF, FRAME >> 8)                 # LD A,(frame)
    a.b(0xE6, 0x0F)                                     # AND 15
    a.b(0x20, 0x00)                                     # JR NZ,loop (patched below)
    jr_at = len(a.code) - 1
    a.b(0x3A, FRAME & 0xFF, FRAME >> 8)                 # LD A,(frame)
    a.b(0x0F, 0x0F, 0x0F, 0xE6, 0x0E)                   # RRCA x3; AND #0E  -> step x 2
    a.b(0x6F, 0x26, STEPS_TABLE >> 8)                   # LD L,A; LD H,table
    a.b(0x7E, 0x23, 0x66, 0x6F)                         # LD A,(HL); INC HL; LD H,(HL); LD L,A
    ret = a.here() + 5
    a.b(0x01, ret & 0xFF, ret >> 8, 0xC5)               # LD BC,ret; PUSH BC
    a.b(0xE9)                                           # JP (HL)
    assert a.here() == ret
    a.b(0x18, (loop - (a.here() + 2)) & 0xFF)           # JR loop
    a.code[jr_at] = (loop - (a.origin + jr_at + 1)) & 0xFF


def step(a, k):
    entry = a.here()
    a.b(0xF3)                                           # DI: the MIDI bits need exact timing
    fnum = FM_FNUM[k]
    # U10: three FM channels (a chord), SSG tones; U4: one FM channel and its SSG tone A
    a.out(0xFFFD, CONTROL_U10)
    for ch in range(3):
        f = FM_FNUM[(k + 2 * ch) % 8]
        a.ym(0x28, ch)                                  # key off
        a.ym(0xA4 + ch, ((4 - ch % 2) << 3) | (f >> 8))
        a.ym(0xA0 + ch, f & 0xFF)
        a.ym(0x28, 0xF0 | ch)                           # key on
        p = SSG_PERIOD[(k + 3 * ch) % 8] >> ch
        a.ym(ch * 2, p & 0xFF)
        a.ym(ch * 2 + 1, p >> 8)
    a.ym(0x0D, 0x0A + (k & 1) * 4)                     # envelope shape (restarts the envelope)
    a.out(0xFFFD, CONTROL_U4)
    a.ym(0x28, 0x00)
    a.ym(0xA4, (5 << 3) | (fnum >> 8))
    a.ym(0xA0, fnum & 0xFF)
    a.ym(0x28, 0xF0)
    a.ym(0x00, SSG_PERIOD[k] & 0xFF)
    a.ym(0x01, SSG_PERIOD[k] >> 8)
    # SAA: six voices, octaves move with the step
    for v in range(6):
        a.saa(0x08 + v, (SAA_NOTE[(k + v) % 8] + 16 * v) & 0xFF)
    a.saa(0x10, ((k + 2) & 7) | (((k + 3) & 7) << 4))
    a.saa(0x11, ((k + 1) & 7) | (((k + 4) & 7) << 4))
    a.saa(0x12, ((k + 3) & 7) | (((k + 2) & 7) << 4))
    # MIDI on U4 (still selected): program change, previous note off, note on, a drum hit
    previous = MIDI_NOTE[(k - 1) % 8]
    # Reverb and chorus sends 0 on both channels (CC 91, CC 93; GM resets them to 40 and 0): the effect lines stay
    # silent, so the synthesizer's state in each checkpoint is its voices, not kilobytes of reverb tail
    data = [0xB0, 91, 0, 0xB0, 93, 0, 0xB9, 91, 0, 0xB9, 93, 0,
            0xC0, k * 3, 0x80, previous, 0x00, 0x90, MIDI_NOTE[k], 0x64, 0x99, 36 + (k & 1) * 2, 0x64]
    line = midi_line(data)
    table = MIDI_TABLES + k * 256
    assert len(line) <= 256
    a.midi_bits(table, len(line))
    a.b(0xFB, 0xC9)                                     # EI; RET
    return entry, table, line


def build():
    ram = bytearray(0x10000)
    a = Asm(ORIGIN)
    init(a)
    main_loop(a)
    entries = []
    for k in range(8):
        entry, table, line = step(a, k)
        entries.append(entry)
        ram[table:table + len(line)] = bytes(line)
    assert a.here() < STEPS_TABLE, "program overlaps the step table"
    ram[ORIGIN:ORIGIN + len(a.code)] = a.code
    for k, entry in enumerate(entries):
        struct.pack_into("<H", ram, STEPS_TABLE + 2 * k, entry)
    for i in range(257):
        ram[VECTORS + i] = HANDLER & 0xFF
    ram[HANDLER:HANDLER + 3] = bytes([0xC3, INT_CODE & 0xFF, INT_CODE >> 8])   # JP handler
    handler = interrupt_handler()
    ram[INT_CODE:INT_CODE + len(handler)] = handler
    bitbang, n_address = bitbang_routine()
    assert INT_CODE + len(handler) <= BITBANG and BITBANG + len(bitbang) < STACK - 0x40
    ram[BITBANG:BITBANG + len(bitbang)] = bitbang
    struct.pack_into("<H", ram, ORIGIN + a.patch_n, n_address)
    return ram


def sna128(ram, pc):
    """128K .sna: header, banks 5 / 2 / 0 (#4000-#FFFF), PC, #7FFD = #10 (ROM 1, bank 0), no TR-DOS, banks 1 3 4 6 7"""
    header = struct.pack(
        "<BHHHHHHHHHBBHHBB",
        VECTORS >> 8,            # I
        0, 0, 0, 0,              # HL' DE' BC' AF'
        0, 0, 0,                 # HL DE BC
        0x5C3A, 0,               # IY IX
        0x00,                    # IFF2 (interrupts disabled: the program enables them)
        0,                       # R
        0, STACK,                # AF SP
        2, 7)                    # IM, border
    assert len(header) == 27
    out = bytearray(header)
    out += ram[0x4000:0x10000]
    out += struct.pack("<HBB", pc, 0x10, 0)
    out += bytes(0x4000) * 5
    return bytes(out)


def main():
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_OUT
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(sna128(build(), ORIGIN))
    print(f"{out} ({out.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
