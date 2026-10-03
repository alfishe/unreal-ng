"""Every XT make code through the PROFI-XT firmware on the 8035 simulator: the Profi matrix it gives.

Usage: python3 keymap.py [rom]   (default: data/rom/profixt/profi-xt-v1.27.rom of this repository)
Prints, per set-1 code 01..58, the matrix positions held while the key is down and what is left after its break.
"""
import os
import sys

from sim import Board

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "..", ".."))
ROM = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "data", "rom", "profixt", "profi-xt-v1.27.rom")
ROWS = [0xFE, 0xFD, 0xFB, 0xF7, 0xEF, 0xDF, 0xBF, 0x7F]
NAMES = [["CS", "Z", "X", "C", "V"], ["A", "S", "D", "F", "G"], ["Q", "W", "E", "R", "T"],
         ["1", "2", "3", "4", "5"], ["0", "9", "8", "7", "6"], ["P", "O", "I", "U", "Y"],
         ["ENT", "L", "K", "J", "H"], ["SP", "SS", "M", "N", "B"]]


def fresh(rom):
    board = Board(rom)
    board.run(3000)   # the firmware's power-on
    return board


def matrix(board):
    out = []
    for row, high in enumerate(ROWS):
        value, _ = board.readFE(high)
        if value is None:
            return "HANG"
        out += [NAMES[row][bit] for bit in range(5) if not value & (1 << bit)]
        if not value & 0x20:
            out.append("EXT" if row == 6 else "D5@row%d" % row)
    everything, _ = board.readFE(0x00)
    return " ".join(out) + "  [all=%02X]" % everything


def main():
    rom = open(ROM, "rb").read()
    for code in range(1, 0x59):
        board = fresh(rom)
        board.sendByte(code)
        board.run(500)
        held = matrix(board)
        board.sendByte(code | 0x80)
        board.run(500)
        print("%02X: %-40s | after break: %s" % (code, held, matrix(board)))


if __name__ == "__main__":
    main()
