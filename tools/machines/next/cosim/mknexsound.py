#!/usr/bin/env python3
"""Sound test programs for the Next as NEX files (no assembler needed): mknexsound.py <out-dir>
  ay3.nex     three AYs, tones 1093.75 / 729.2 / 546.9 Hz (periods 100 / 150 / 200), full stereo
  pan.nex     chip 0 left only, chip 1 right only, chip 2 both (tone periods 100 / 150 / 200)
  dac.nex     a sawtooth to DAC A (port #1F) and a slower one to DAC C (#4F): left / right
  mix.nex     AY chip 0 (period 100) with the DAC on mono port #DF square wave
Each program ends in JR $; the NEX is V1.2, bank 2 at #8000, SP #BFE0."""
import struct, sys, os

def ay(reg, val):
    return bytes([0x01, 0xFD, 0xFF, 0x3E, reg, 0xED, 0x79, 0x01, 0xFD, 0xBF, 0x3E, val, 0xED, 0x79])

def select(v):
    return bytes([0x01, 0xFD, 0xFF, 0x3E, v, 0xED, 0x79])

def nextreg(r, v):
    return bytes([0xED, 0x91, r, v])

def tone(chip_select, period, volume=15):
    return select(chip_select) + ay(0, period & 255) + ay(1, period >> 8) + ay(7, 0x3E) + ay(8, volume)

HEAD = bytes([0xF3]) + nextreg(0x08, 0x1A) + nextreg(0x84, 0xFF)   # DI, turbosound + DAC on, every DAC port decoded
END = bytes([0x18, 0xFE])

def dac_saw(port_a, port_c):
    # loop: A = counter; out (#1F),A ; out (#4F),(counter>>2) ; a short delay; inc counter
    code = bytes([0x06, 0x00,                # ld b,0  (counter)
                  # loop:
                  0x78,                       # ld a,b
                  0x0E, port_a, 0xED, 0x79, # ld c,port_a ; out (c),a   (high byte of BC = B = counter, fine)
                  0x78, 0xCB, 0x3F, 0xCB, 0x3F,   # ld a,b ; srl a ; srl a
                  0x0E, port_c, 0xED, 0x79, # ld c,port_c ; out (c),a
                  0x3E, 0x08, 0x3D, 0x20, 0xFD,   # ld a,8 ; dec a ; jr nz,-3  (delay)
                  0x04,                       # inc b
                  0x18, 0xEA])               # jr loop (back to 'ld a,b')
    return code

def beeper(ear_mic):
    # toggle port #FE between ear_mic and 0 with a delay: ~ 1 kHz at 3.5 MHz
    return bytes([0x3E, ear_mic, 0xD3, 0xFE, 0x06, 0x00, 0x10, 0xFE, 0x3E, 0x00, 0xD3, 0xFE, 0x06, 0x00, 0x10, 0xFE, 0x18, 0xEE])

PROGRAMS = {
    'ear': HEAD + beeper(0x10),
    'mic': HEAD + beeper(0x08),
    'ay3': HEAD + tone(0xFF, 100) + tone(0xFE, 150) + tone(0xFD, 200) + END,
    'pan': HEAD + tone(0xDF, 100) + tone(0xBE, 150) + tone(0xFD, 200) + END,
    'dac': HEAD + dac_saw(0x1F, 0x4F),
    'mix': HEAD + tone(0xFF, 100) + bytes([
        0x3E, 0xC0, 0x0E, 0xDF, 0xED, 0x79,            # DAC mono A/D high
        0x06, 0x40, 0x10, 0xFE,                          # delay
        0x3E, 0x40, 0x0E, 0xDF, 0xED, 0x79,            # low
        0x06, 0x40, 0x10, 0xFE,
        0x18, 0xEA]),
}

def nex(code):
    h = bytearray(512)
    h[0:4] = b'Next'; h[4:8] = b'V1.2'
    h[8] = 0; h[9] = 1
    h[10] = 0; h[11] = 0
    struct.pack_into('<H', h, 12, 0xBFE0); struct.pack_into('<H', h, 14, 0x8000)
    h[18 + 2] = 1
    h[134] = 0; h[139] = 0
    bank = bytearray(16384); bank[:len(code)] = code
    return bytes(h) + bytes(bank)

if __name__ == '__main__':
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    for name, code in PROGRAMS.items():
        open(os.path.join(out, name + '.nex'), 'wb').write(nex(code))
        print(name, len(code), 'bytes')
