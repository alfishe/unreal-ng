#!/usr/bin/env python3
"""Checks the G_X_OFFS rules written in README.md against the captured RTL lines.

Rebuilds every captured line in results/zx-gxoffs.txt and results/txt-gxoffs.txt from
the memory image the harness uses (same patterns as harness.cpp) and the rules, and
reports any pixel that differs. Exit code 0 = the rules reproduce every capture.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
BORDER_DOTS = 8
WINDOW_LINE = 9  # 0-based line inside the window: ZX pixel line 9, TXT text row 1 glyph line 1


def load(name):
    rows = []
    with open(os.path.join(HERE, "results", name)) as f:
        for line in f:
            if line.startswith("#"):
                continue
            t = line.split()
            rows.append((int(t[0], 16), int(t[1]), int(t[2]), [int(x, 16) for x in t[3:]]))
    return rows


# --- memory image patterns (must match harness.cpp) -------------------------------

def zx_pixel_byte(col, y):
    return 0x81 | ((col & 0x1F) << 1) | ((y & 1) << 6)


def zx_attr_byte(col, row):
    ink = col & 7
    paper = (ink + 1 + (col >> 3)) & 7
    return ((row & 1) << 6) | (paper << 3) | ink


def txt_code(col):
    return 0x80 | (col & 0x7F)


def txt_attr(col):
    ink = col & 15
    paper = (ink + 1 + ((col >> 4) & 7)) & 15
    return (paper << 4) | ink


def txt_glyph(ch, line):
    return (ch * 0x1D + 0x35 + line * 0x40) & 0xFF


# --- ZX rule ----------------------------------------------------------------------

def zx_line(gx, y):
    c = (gx >> 2) & 31   # column counter start (cnt_col[4:0]); one step = one 16-bit fetch
    f = gx & 3           # fine shift in dots
    stream = []
    for m in range(17):  # 16 groups of 16 pixels + the group that the fine shift pulls in
        n_pix = c + 2 * m            # fetch that lands in the pixel slot
        n_atr = c + 2 * min(m, 15) + 1  # fetch that lands in the attribute slot (none for group 16)
        for h in range(2):
            def byte_of(n):
                col = 2 * ((n >> 1) & 15) + h
                return zx_pixel_byte(col, y) if n % 2 == 0 else zx_attr_byte(col, y >> 3)
            pb, ab = byte_of(n_pix), byte_of(n_atr)
            for b in range(8):
                ink = (pb >> (7 - b)) & 1  # FLASH phase is 0 in the simulated frame
                stream.append((((ab >> 6) & 1) << 3) | ((ab & 7) if ink else ((ab >> 3) & 7)))
    return stream[f:f + 256]


# --- TXT rule ---------------------------------------------------------------------

def txt_line(gx, width_dots, y):
    c = (gx >> 2) & 0x7F  # column counter start; cnt_col[1:0] = fetch type, cnt_col[7:2] = character pair
    f = gx & 3            # fine shift in dots (= 2 hi-res pixels each)
    p = c & 3
    q0 = (c + 3) >> 2     # first character pair shown
    line = y & 7
    pixels = []
    for m in range(width_dots // 8 + 1):
        q = (q0 + m) & 63
        k0, k1 = txt_code(2 * q), txt_code(2 * q + 1)
        qa = (q - 1) & 63 if p == 1 else q
        a0, a1 = txt_attr(2 * qa), txt_attr(2 * qa + 1)
        g0 = txt_glyph(k0, line) if p in (0, 3) else k0
        g1 = txt_glyph(k1, line) if p == 0 else k1
        for pb, ab in ((g0, a0), (g1, a1)):
            for b in range(8):
                pixels.append((ab & 15) if (pb >> (7 - b)) & 1 else (ab >> 4))
    return pixels[2 * f:2 * f + 2 * width_dots]


def main():
    bad = 0
    total = 0
    for vc, gx, ppd, s in load("zx-gxoffs.txt"):
        total += 1
        b = BORDER_DOTS
        if s[b:b + 256] != zx_line(gx, WINDOW_LINE) or s[:b] != [0xEE] * b or s[b + 256:] != [0xEE] * b:
            bad += 1
            print(f"ZX  V_CONFIG={vc:02X} G_X_OFFS={gx}: rule does not match")
    widths = {0x03: 256, 0x83: 320}
    for vc, gx, ppd, s in load("txt-gxoffs.txt"):
        total += 1
        w = widths[vc]
        b = 2 * BORDER_DOTS
        if s[b:b + 2 * w] != txt_line(gx, w, WINDOW_LINE) or s[:b] != [0x0E] * b or s[b + 2 * w:] != [0x0E] * b:
            bad += 1
            print(f"TXT V_CONFIG={vc:02X} G_X_OFFS={gx}: rule does not match")
    print(f"{total - bad} of {total} captured lines match the rules")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
