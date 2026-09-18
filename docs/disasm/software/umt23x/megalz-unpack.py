#!/usr/bin/env python3
"""MegaLZ unpacker (DEC40 family, UMT v2.3x dialect) - standalone Python
transcription of the resident Z80 depacker at 0xBF00 in UMT23X.tap.

Why this exists
---------------
`UMT23X.tap` carries one MegaLZ-packed code block (4,937 stream bytes that
expand to 9,466 bytes at 0x6000). The block ships its own depacker which it
leaves resident at 0xBF00. `disasm_umt23x.py` in this folder already unpacks
the tape with a literal Z80-subset interpreter; this script is the *grammar-
level* companion: the same decode expressed as plain Python, one function
per Z80 construct, with every quirk that makes MegaLZ interesting spelled
out in comments. Cross-reference the addresses cited below against
`umt23x-z80dasm.asm` (DEPACK_ENTRY listing) and the reference Z80
implementations in the `z80depacker` collection (unmegalz_small.asm,
unmegalz_fast.asm, megalz_dec40.asm).

Validation: byte-identical output vs the interpreter in disasm_umt23x.py
(which is itself byte-identical vs an emulator capture halted at JP 0x6000)
and vs the runtime state in testdata/memory/UMT23X.sna (see README.md).

The stream, the way Z80 sees it
-------------------------------
The depacker keeps its input cursor in SP and mixes two fetch styles over
ONE forward cursor - the heart of the format's compactness:

  * bit windows - `POP HL` grabs a 16-bit little-endian window; bits are
    consumed MSB-first by `ADD HL,HL` (bit15 -> carry) exactly 16 times,
    counted by `B` (reset from `C`=10h by `LD B,C` after each refill)
  * raw bytes   - `DEC SP` + `POP AF` nets +1: F=mem[sp-1] (discarded),
    A=mem[sp] - i.e. the value byte sits AT the pre-decrement cursor

Because a window fetch advances SP by 2 and a raw byte by 1, bit reads and
byte reads interleave on the same tape position; a refill can even land in
the middle of a token. `unpack()` mirrors this with a single `sp` index.

Register banks (EXX)
--------------------
All bit twiddling runs in the shadow file: HL' = window, B' = bit counter,
C' = 10h, D' = "bias" byte (see rep tokens). The output file holds DE =
write pointer, HL = match source, BC = match length. A and the flags are
shared (EXX swaps BC/DE/HL only), which the depacker exploits ruthlessly:
RLA/ADD HL,HL touch ONLY carry, so Z often survives half a dozen innocent-
looking instructions from a CP far back. Every such flag lifeline is
annotated where it matters below.

Grammar (tokens selected by the first dispatch bit)
---------------------------------------------------
  1        literal: one raw byte
  0 11..   gamma length V (pairs "11" continue, V = P+1+3k; forced exit
           at V=16), then an offset code and an LDIR match:
             V=1      3-bit short distance 1..8
             V=2      special 2-bit-offset path (Z-flag quirk, see below)
             V=3..15  2-bit offset prefix + low byte
             V=4      escape into the extended codes (below)
  extended (V=4), one/two more bits then:
             1...     SHORT composite: 4-bit code, copy+literal+copy (3 bytes)
             01..     raw block: 2*(6+4bit) bytes copied verbatim through
                      the epilogue POP loop (SP stays on the stream)
             00..     7-bit code: 15 = END marker; <15 = 16-bit length
                      (code:rawbyte); >15 = length code
Usage:
  megalz-unpack.py [INPUT] [-o OUT.bin] [--offset N] [--org N] [--verify REF]

  INPUT     packed payload (default: umt-payload-c000.bin next to script)
  --offset  stream start inside INPUT (default 0x10F: past the stage-1
            bootstrap + resident depacker copy in the payload block)
  --org     Z80 address the unpacked data is written to (default 0x6000)
  --verify  compare output against a reference image prefix; exit 1 on diff
"""
import argparse
import os
import sys

# Six trailer bytes the END marker materialises from. On the Z80 they sit
# at IX=BFE0 and the epilogue POP loop copies them as three DE pairs after
# `LD SP,IX` - the last thing the depacker writes before `JP 0x6000`.
# For UMT the value is ASCII "...)" + two zeros.
END_TABLE = bytes([0x2E, 0x2E, 0x2E, 0x29, 0x00, 0x00])


def unpack(stream, org=0x6000):
    """Decode a UMT-dialect MegaLZ stream.

    Returns (image, consumed): the unpacked bytes as written from `org`
    (END trailer included), and the number of stream bytes consumed.
    """
    mem = bytearray(65536)          # flat 64K so match sources can wrap
    sp = 0                          # the shared byte cursor (Z80 SP)
    hl = 0                          # shadow HL: the 16-bit bit window
    b = 0                           # shadow B: bits left in the window
    dbias = 0xBF                    # shadow D: offset-code bias (bf09)
    ob = 0                          # output-file B: high byte of BC
    out = org                       # output-file DE: write pointer

    # ---- fetch primitives -------------------------------------------------

    def pop16():
        """bf18/bf44/bf5a/bf95 `POP HL` refill: little-endian 16-bit window."""
        nonlocal sp
        w = stream[sp] | stream[sp + 1] << 8
        sp += 2
        return w

    def getbit():
        """bf15/bf20/bf41/bf57/bf92/bfb6 `ADD HL,HL` (+DJNZ refill):
        shift the window left, return the bit that fell out of bit15.
        DJNZ counts B down from C'=10h; on zero the window is refilled."""
        nonlocal hl, b
        bit = hl >> 15
        hl = (hl << 1) & 0xFFFF
        b -= 1
        if b == 0:
            hl = pop16()
            b = 0x10
        return bit

    def raw():
        """`DEC SP` + `POP AF`: the value byte sits at the current cursor
        (A=mem[sp] after the net +1) - literals, offset low bytes, raw
        block bytes and 16-bit length low bytes all come through here."""
        nonlocal sp
        v = stream[sp]
        sp += 1
        return v

    def rl_bits(seed):
        """The `RLA / JR C,loop` unary reader (bf41/bf57/bfb6): shift stream
        bits into A while the seed's leading 1-bits keep falling out of
        bit7. Seed 0xBF -> 2 bits, 0xDF -> 3, 0xEF -> 4, 0xF7 -> 5, 0xFC
        -> 7. Result keeps the seed's low 1-bits set in the top, so e.g.
        seed 0xBF yields 0xFC|v2. NOTE: RLA sets only carry - Z survives."""
        a = seed
        while True:
            bit = getbit()
            cout = a >> 7
            a = ((a << 1) | bit) & 0xFF
            if not cout:                 # JR C not taken: last leading 1 out
                break
        return a

    # ---- output primitives ------------------------------------------------

    def copy_match(dist, count):
        """bf6a..bf6c `LD L,A / ADD HL,DE / LDIR`: copy `count` bytes from
        `dist` bytes back. ADD HL,DE computes src = out - dist via 16-bit
        wrap. LDIR runs BC down to ZERO - which is how the output-file B
        register gets cleared for free after every match (see `ob`)."""
        nonlocal out, ob
        for _ in range(count):
            mem[out] = mem[(out - dist) & 0xFFFF]
            out += 1
        ob = 0

    def short_composite(code):
        """bf81..bf8e: a 3-byte token - match byte, literal byte, match
        byte. `LD L,A` with A=0xF0|v4 (NOT masked to 4 bits: H is forced
        to FF so dist = 256-code = 16-v4 for the extended entry, and
        code=0x10|(x-10h) for the special-byte entry). LDI copies ONE
        byte and decrements BC; `LD (DE),A` stores the POP AF literal;
        `INC HL / LD A,(HL)` fetches from src+2 (LDI already advanced HL
        by 1, INC HL makes 2 - NOT src+1)."""
        nonlocal out, ob
        src = (out - (256 - code)) & 0xFFFF
        mem[out] = mem[src]              # LDI: one byte, HL advances
        out += 1
        # bf82 LD C,A then LDI decrements BC as a 16-bit value (may borrow
        # from B - faithful even for pathological stream states)
        ob = ((((ob << 8) | code) - 1) & 0xFFFF) >> 8
        mem[out] = raw()                 # POP AF / LD (DE),A
        out += 1
        mem[out] = mem[(src + 2) & 0xFFFF]   # INC HL / LD A,(HL)
        out += 1

    def special_ff(clen):
        """bf75..: the H=FF offset-byte escape. One raw byte X:
          X < E0h     plain match, dist = 256-X, length clen
          X >= E0h    RLCA; XOR C (C = low byte of the match length!);
                      INC A; if the result is 0 -> REP token (bf70-bf73:
                      EXX, RRC D' rotates the bias byte, NO output);
                      else SUB 10h -> SHORT composite with that code."""
        nonlocal dbias
        x = raw()
        if x < 0xE0:
            copy_match(256 - x, clen)
            return
        x = ((x << 1) | (x >> 7)) & 0xFF      # RLCA
        x ^= clen & 0xFF                      # XOR C
        x = (x + 1) & 0xFF                    # INC A
        if x == 0:                            # JR Z -> rep-offset token
            dbias = (dbias >> 1) | (dbias & 1) << 7   # RRC D
            return
        short_composite((x - 0x10) & 0xFF)    # SUB 10h, fall into bf81

    def offset_and_copy(length, z_is_set):
        """bf3d..bf6e: read the match offset, then LDIR `length` bytes.

        `length` is the full 16-bit BC (see the bfa0 16-bit length path).
        `z_is_set` re-creates the one flag the Z80 smuggles in: Z from
        `CP 002h` (bf38) survives EXX/LD/LD/EXX/LD and the whole RLA
        reader, so bf49 `JR Z` branches on (V==2) - the "2-bit special"
        quirk that reuses INC A's result differently per path."""
        a = rl_bits(0xBF)                      # bf41: 2-bit prefix, a=FC|v2
        if z_is_set:                           # bf49 JR Z taken (V==2):
            a = (a + 1) & 0xFF                 # bf50 INC A
            if a == 0:                         # bf51 JR NZ not taken:
                # v2==3: EF-seed reader after RRCA (0xEF -> 0xF7, 5 bits),
                # `CP A` forces Z=1 so bf62 JR Z fires: H=FF, dist=32-v5
                copy_match(256 - rl_bits(0xF7), length)
                return
        else:
            a = (a + 1) & 0xFF                 # bf4b INC A
            s = a + dbias                      # bf4c ADD A,D'
            carry = s > 0xFF
            a = s & 0xFF                       # Z here = (a+d'==0), lives
            if not carry:                      # bf4d JR NC -> bf57:
                # extra unary read seeded with A itself (variable width!),
                # then bf62 JR Z tests the Z from ADD A,D' above
                a = rl_bits(a)
                if a == 0:                     # Z path: H stays FF
                    copy_match(256 - a, length)
                else:
                    h = a                      # bf64 LD H,A
                    if (a + 1) & 0xFF == 0:    # bf66 INC A / bf67 JR Z
                        special_ff(length & 0xFF)
                    else:
                        copy_match(0x10000 - (h << 8) - raw(), length)
                return
            a = (a - dbias) & 0xFF             # bf4f SUB D (undoes ADD
            a = (a + 1) & 0xFF                 # on the summed value!)
            if a == 0:                         # bf51 JR NZ not taken:
                copy_match(256 - rl_bits(0xF7), length)   # F7 5-bit path
                return
        # common tail, Z==0 at bf51: bf5f EXX / LD H,FFh / JR Z (no) /
        h = a                                  # bf64 LD H,A
        if (a + 1) & 0xFF == 0:                # bf66 INC A / bf67 JR Z
            special_ff(length & 0xFF)
            return
        copy_match(0x10000 - (h << 8) - raw(), length)   # bf69 POP AF lo

    # ---- bootstrap (bf00..bf14) -------------------------------------------
    # The stage-1 stub has already LDDR'd the resident copy to 0xBF00 and
    # left SP on the stream; the depacker then: POP HL (first window),
    # DEC SP / POP AF (el0 - the very first output byte is a raw literal,
    # guaranteeing a defined start), EXX, LD (DE),A, INC DE, EXX -> loop.
    hl = pop16()
    b = 0x10
    mem[out] = raw()                           # el0 first literal
    out += 1

    # ---- token loop (bf15 dispatch) ---------------------------------------
    while True:
        if getbit():                           # 1 -> literal (jr c,bf0f)
            mem[out] = raw()
            out += 1
            continue

        # gamma length (bf1c..bf31). A=080h, E=001h; each iteration reads
        # exactly two bits (first RLA always carries out of 0x80, second
        # never does), giving the pair value P in {0,1,2,3}. P<3 exits with
        # V=P+E; P==3 continues after A+=E, E=A and A^=10h - when that XOR
        # zeroes A the loop is FORCED out with A=0, so V=E=16: length 15.
        # (V = P+1+3k, the classic MegaLZ Elias-gamma cousin.)
        e, a = 1, 0x80
        while True:
            bit = getbit()
            cout = a >> 7
            a = ((a << 1) | bit) & 0xFF
            if not cout:                       # second bit of the pair
                if a < 3:                      # bf28 CP 3 / JR C exit
                    break
                a = (a + e) & 0xFF             # bf2c ADD A,E / LD E,A
                e = a
                a ^= 0x10                      # bf2e XOR C (C'=10h)
                if a == 0:                     # bf2f JR NZ not taken:
                    break                      #   forced exit, V = e = 16
                a = 0x80                       # bf1e: next pair
        V = (a + e) & 0xFF                     # bf31 ADD A,E

        if V == 4:
            # ---- extended codes (bf90..) ----------------------------------
            # bf92 ADC-loop: 1st bit 1 -> SHORT composite via bfbe/bfbf
            # (SBC A,A clears Z, JR NZ fires); 1st 0, 2nd 1 -> bfc3 BIT 7
            # fails on the 4-bit value -> SUB 0EAh = 6+v4 -> raw block;
            # both 0 -> FC-seed 7-bit code (bf9c).
            if getbit():
                short_composite(rl_bits(0xEF))     # A=F0|v4, dist 16-v4
                continue
            if getbit():
                # raw block: SP is NOT rewired (no LD SP,IX), so the
                # epilogue POP loop at bfc7 just streams 2*(6+v4) bytes
                # verbatim into the output; DEC A / JR NZ counts pairs.
                n = 2 * (6 + (rl_bits(0xEF) & 0x0F))
                for _ in range(n):
                    mem[out] = raw()
                    out += 1
                continue
            code = rl_bits(0xFC) & 0x7F
            if code == 0x0F:
                # END marker: ADD A,0F4h turns 15 into 3 with carry SET,
                # LD SP,IX rewires SP to the 6-byte END_TABLE, the pop
                # loop writes it, and JR NC at bfd1 (carry still set - POP
                # touches no flags!) falls into the final `JP 0x6000`.
                for i in range(6):
                    mem[out] = END_TABLE[i]
                    out += 1
                break
            if code < 0x0F:
                # bfa0: DEC SP / POP BC / LD C,B / LD B,A - a SIXTEEN-BIT
                # length: BC = code:rawbyte (two such tokens exist in the
                # UMT stream: 0x00BD=189 and 0x02EC=748). CCF clears the
                # CP carry so bf3f takes the standard reader; Z stays 0.
                length = (code << 8) | raw()
                ob = code                      # LD B,A parks code in B
            else:
                # bfab JR NZ -> bf3b entered directly (no EXX first):
                # LD C,A sets only the LOW byte - B keeps whatever LDIR
                # or a previous bfa0 left there (LDIR zeroes it, so in
                # practice lengths here are just `code`).
                length = (ob << 8) | code
            offset_and_copy(length, False)     # Z=(code==15)=0
            continue

        # bf36 ADC A,0FFh: the length adjust. CP 004h carry-in is 0 for
        # V>4 and 1 for V<4, so ADC adds FFh or 100h: V<=3 stays V,
        # V>=5 becomes V-1 (V=16 -> 15). The CP 002h right after sets the
        # Z that bf49 will test much later AND the carry that bf3f uses:
        # V<=1 -> carry -> bf55 3-bit short distance; V>=2 -> reader.
        length = V if V <= 3 else V - 1
        if V == 1:
            # bf55: LD A,0BFh was already loaded, RRCA makes 0xDF (3-bit
            # seed), CP A forces Z so H=FF: dist = 8-v3, one-byte match.
            copy_match(8 - (rl_bits(0xDF) & 7), (ob << 8) | 1)
        else:
            offset_and_copy((ob << 8) | length, V == 2)

    return bytes(mem[org:out]), sp


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(
        description="Unpack the MegaLZ (DEC40 family, UMT dialect) stream "
                    "carried by the UMT23X payload block.")
    ap.add_argument("input", nargs="?",
                    default=os.path.join(here, "umt-payload-c000.bin"),
                    help="packed payload file (default: %(default)s)")
    ap.add_argument("-o", "--output",
                    help="write the unpacked binary to this path")
    ap.add_argument("--offset", type=lambda s: int(s, 0), default=0x10F,
                    help="stream start offset inside INPUT "
                         "(default: %(default)d)")
    ap.add_argument("--org", type=lambda s: int(s, 0), default=0x6000,
                    help="Z80 write address of the unpacked data "
                         "(default: %#x)" % 0x6000)
    ap.add_argument("--verify",
                    help="reference image; output must be a byte-exact "
                         "prefix of it")
    args = ap.parse_args()

    payload = open(args.input, "rb").read()
    stream = payload[args.offset:]
    image, consumed = unpack(stream, args.org)

    print(f"input:        {args.input} ({len(payload)} bytes)")
    print(f"stream:       offset {args.offset:#x}, {consumed} bytes consumed "
          f"({len(stream) - consumed} trailing)")
    print(f"unpacked:     {len(image)} bytes at "
          f"{args.org:#06x}..{args.org + len(image):#06x}")

    if args.output:
        open(args.output, "wb").write(image)
        print(f"written:      {args.output}")

    if args.verify:
        ref = open(args.verify, "rb").read()
        if len(image) <= len(ref) and image == ref[:len(image)]:
            print(f"verify:       OK - byte-identical prefix of {args.verify}")
        else:
            for i, (x, y) in enumerate(zip(image, ref)):
                if x != y:
                    print(f"verify:       MISMATCH at +{i:#x}: "
                          f"{x:#04x} != {y:#04x}")
                    break
            else:
                print("verify:       MISMATCH (length)")
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
