#!/usr/bin/env python3
"""Extract The Link (invdemo) from its TR-DOS image.

- ALASM sources (TR-DOS type 'H') are detokenized into UTF-8 text: src/<name>.asm
- BASIC programs (type 'B') are listed as text: basic/<name>.bas
- every file, sources included, is also written raw: raw/<name>.<type>
  (with --raw; the raw files are for analysis and are not kept in the repo)
- CATALOG.md-style table on stdout

ALASM source format (read from the assembler on the same disk, alasm_64.C):
a 64-byte header (name "NAME    H", source length at +#21), then lines of
[length byte including itself][body]. In the body:
- bytes #01-#1F are runs of spaces (the count);
- the first byte >= #80 outside a comment/string is a mnemonic or directive
  (table MNEMONICS, #80 = INCLUDE ... #E6 = RUN);
- later bytes >= #80 in the operand field are registers / operands
  (OPERANDS: (BC)..(IY) from #9F, (C) (IX (IY AF' from #D0, BC..I from #E0);
- in comments and strings bytes >= #80 are text in code page 866 (Russian);
- #FF is a line flag of the editor (seen at the start and the end of lines),
  never text.
A token passed as a macro argument (MTEXTURER INC) is written as the mnemonic.
"""

import argparse
import os
import sys

MNEMONICS = (
    "INCLUDE INCBIN MACRO LOCAL RLCA RRCA HALT CALL PUSH RETN RETI DJNZ OUTI OUTD LDIR CPIR INIR OTIR LDDR CPDR "
    "INDR OTDR DD DEFB DEFW DEFS DISP ENDM EDUP ENDL MAIN ELSE DISPLAY EXA DB DW DS NOP INC DEC RLA RRA DAA CPL "
    "SCF CCF ADD ADC SUB SBC AND XOR RET POP RST EXX RLC RRC SLA SRA SLI SRL BIT RES SET OUT NEG RRD RLD LDI CPI "
    "INI LDD CPD IND ORG EQU ENT INF DUP IFN REPEAT UNTIL0 IF0 LD JR JP OR CP EX DI EI IN RL RR IM ENDIF EXD JNZ "
    "JZ JNC JC RUN"
).split()

OPERANDS = {}
for base, names in (
    (0x9F, ["(BC)", "(DE)", "(HL)", "(SP)", "(IX)", "(IY)"]),
    (0xD0, ["(C)", "(IX", "(IY", "AF'"]),
    (0xE0, "BC DE HL AF IX IY SP NZ NC PO PE HX LX HY LY B C D E H L A P M Z R I".split()),
):
    for i, n in enumerate(names):
        OPERANDS[base + i] = n

# ZX Spectrum BASIC tokens #A3-#FF (48K set; #A3/#A4 are the 128K SPECTRUM/PLAY)
BASIC_TOKENS = (
    "SPECTRUM PLAY RND INKEY$ PI FN POINT SCREEN$ ATTR AT TAB VAL$ CODE VAL LEN SIN COS TAN ASN ACS ATN LN EXP "
    "INT SQR SGN ABS PEEK IN USR STR$ CHR$ NOT BIN OR AND <= >= <> LINE THEN TO STEP DEF_FN CAT FORMAT MOVE ERASE "
    "OPEN_# CLOSE_# MERGE VERIFY BEEP CIRCLE INK PAPER FLASH BRIGHT INVERSE OVER OUT LPRINT LLIST STOP READ DATA "
    "RESTORE NEW BORDER CONTINUE DIM REM FOR GO_TO GO_SUB INPUT LOAD LIST LET PAUSE NEXT POKE PRINT PLOT RUN SAVE "
    "RANDOMIZE IF CLS DRAW CLEAR RETURN COPY"
).split()


def cp866(b):
    return bytes([b]).decode("cp866")


def detokenize_alasm(data):
    """ALASM source (header included) -> list of text lines"""
    length = data[0x21] | data[0x22] << 8
    body = data[64:64 + length]
    lines = []
    p = 0
    while p < len(body):
        n = body[p]
        if n == 0:
            break
        raw = body[p + 1:p + n]
        p += n
        out = []
        comment = False
        string = False
        mnemonic_seen = False
        for i, c in enumerate(raw):
            if c == 0xFF:
                continue  # line flag set by the editor (at the start or the end of a line): no text
            if comment or string:
                if c == 0x22 and string:
                    string = False
                out.append(cp866(c) if c >= 0x80 else (" " * c if c < 0x20 else chr(c)))
                continue
            if c < 0x20:
                out.append(" " * c)
            elif c == ord(";"):
                comment = True
                out.append(";")
            elif c == 0x22:
                string = True
                out.append('"')
            elif c >= 0x80:
                if not mnemonic_seen:
                    mnemonic_seen = True
                    idx = c - 0x80
                    word = MNEMONICS[idx] if idx < len(MNEMONICS) else "<%02X>" % c
                    if not out:
                        out.append("        ")  # no label: mnemonic column
                    out.append(word)
                    nxt = raw[i + 1] if i + 1 < len(raw) else None
                    if nxt is not None and nxt >= 0x20:
                        out.append(" ")
                elif c in OPERANDS:
                    out.append(OPERANDS[c])
                elif c - 0x80 < len(MNEMONICS):
                    out.append(MNEMONICS[c - 0x80])  # a mnemonic as a macro argument
                else:
                    out.append("<%02X>" % c)
            else:
                # A word that starts after whitespace (not a label in column 0)
                # is the mnemonic field: a user macro call. Tokens after it
                # are operands
                if not mnemonic_seen and i > 0 and (raw[i - 1] < 0x20 or raw[i - 1] == 0x20):
                    mnemonic_seen = True
                out.append(chr(c))
        lines.append("".join(out).rstrip())
    return lines


def list_basic(data, length):
    """Tokenized BASIC -> listing text"""
    out = []
    p = 0
    while p + 4 <= length:
        num = data[p] << 8 | data[p + 1]
        ln = data[p + 2] | data[p + 3] << 8
        if num > 9999:
            break
        line = data[p + 4:p + 4 + ln]
        p += 4 + ln
        text = []
        i = 0
        while i < len(line):
            c = line[i]
            if c == 0x0E:  # hidden 5-byte number
                i += 6
                continue
            if c == 0x0D:
                break
            if c >= 0xA3:
                text.append(" " + BASIC_TOKENS[c - 0xA3].replace("_", " ") + " ")
            elif c >= 0x20 and c < 0x80:
                text.append(chr(c))
            else:
                text.append("<%02X>" % c)
            i += 1
        out.append("%d %s" % (num, " ".join("".join(text).split())))
    return out


def catalog(image):
    files = []
    for i in range(128):
        e = image[i * 16:(i + 1) * 16]
        if e[0] == 0:
            break
        if e[0] == 1:
            continue  # deleted
        name = e[0:8].decode("latin1").rstrip()
        ext = chr(e[8])
        start = e[9] | e[10] << 8
        length = e[11] | e[12] << 8
        sectors = e[13]
        offset = (e[15] * 16 + e[14]) * 256
        files.append((name, ext, start, length, sectors, image[offset:offset + sectors * 256]))
    return files


def safe(name):
    return "".join(ch if ch.isalnum() or ch in "+-_$" else "_" for ch in name)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("trd")
    ap.add_argument("out")
    ap.add_argument("--raw", action="store_true", help="also write every file raw into <out>/raw")
    args = ap.parse_args()
    image = open(args.trd, "rb").read()
    for sub in ("src", "basic") + (("raw",) if args.raw else ()):
        os.makedirs(os.path.join(args.out, sub), exist_ok=True)

    print("| File | Type | Start | Length | Sectors | Output |")
    print("|---|---|---|---|---|---|")
    for name, ext, start, length, sectors, data in catalog(image):
        stem = safe(name)
        written = ""
        if ext == "H":
            path = os.path.join("src", stem + ".asm")
            with open(os.path.join(args.out, path), "w", encoding="utf-8", newline="\n") as f:
                f.write("\n".join(detokenize_alasm(data)) + "\n")
            written = path
        elif ext == "B":
            path = os.path.join("basic", stem + ".bas")
            with open(os.path.join(args.out, path), "w", encoding="utf-8", newline="\n") as f:
                f.write("\n".join(list_basic(data, length)) + "\n")
            written = path
        if args.raw:
            with open(os.path.join(args.out, "raw", "%s.%s" % (stem, ext)), "wb") as f:
                f.write(data[:length] if ext not in "H" else data)
        print("| `%s` | %s | #%04X | %d | %d | %s |" % (name, ext, start, length, sectors, written))
    return 0


if __name__ == "__main__":
    sys.exit(main())
