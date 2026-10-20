#!/usr/bin/env python3
"""A 6912-byte ZX Spectrum screen as a 352 x 288 frame with the border, in the emulator's Pentagon palette: a
gallery frame from a .scr file.

usage: scr2png.py <out.png> <file.scr> [border 0-7]"""
import sys
import numpy as np
from PIL import Image
N = [(0,0,0),(0,34,199),(214,40,22),(212,51,199),(0,197,37),(0,199,201),(204,200,42),(202,202,202)]
B = [(0,0,0),(0,43,251),(255,51,28),(255,64,252),(0,249,47),(0,251,254),(255,252,54),(255,255,255)]
def render(data, border=0):
    img = np.zeros((288, 352, 3), np.uint8); img[:] = N[border]
    for y in range(192):
        line = ((y & 0xC0) << 5) | ((y & 7) << 8) | ((y & 0x38) << 2)
        for cx in range(32):
            bits = data[line + cx]; attr = data[6144 + (y >> 3) * 32 + cx]
            pal = B if attr & 0x40 else N
            ink, paper = pal[attr & 7], pal[(attr >> 3) & 7]
            for b in range(8):
                img[48 + y, 48 + cx * 8 + b] = ink if bits & (0x80 >> b) else paper
    return img
if __name__ == '__main__':
    Image.fromarray(render(open(sys.argv[2], 'rb').read(), int(sys.argv[3]) if len(sys.argv) > 3 else 0)).save(sys.argv[1])
