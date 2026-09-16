# UMT v2.3x — universal memory tester — reverse-engineering dossier

> **The hook.** 9.4 KiB of Z80 that can pronounce a hand-soldered Pentagon
> 1024 healthy or dead. This folder documents `UMT23X.tap` — a real-tape
> memory tester for fifteen ZX Spectrum clones, from Pentagon 128K to the
> 4 MiB Sprinter — unpacked, disassembled and analysed down to which port
> bit drives which address line on each machine. The authors published no
> source and no spec; every claim here is derived from the binary and can
> be re-derived the same way.

## What's in this folder

| artifact | what it is |
|---|---|
| [`memory-addressing.md`](memory-addressing.md) | **the analysis — read this**: per-model port/bit map, the Pentagon 1024 deep dive, the four test methodologies, how to test memory yourself, emulator parity |
| [`umt23x-z80dasm.asm`](umt23x-z80dasm.asm) | machine-generated z80dasm reference with hand-curated symbols and annotations |
| [`umt23x-z80dasm.asm.sym`](umt23x-z80dasm.asm.sym) | z80dasm symbol dictionary |
| [`umt-unpacked-6000.bin`](umt-unpacked-6000.bin) | the 24 KiB unpacked image for Z80 `0x6000..0xBFFF` (program + resident depacker) |
| [`umt-payload-c000.bin`](umt-payload-c000.bin) | the raw 5,208-byte MegaLZ-packed payload as loaded at `0xC000` |
| [`megalz-unpack.py`](megalz-unpack.py) | standalone MegaLZ unpacker — the depacker's grammar as commented Python |
| [`disasm_umt23x.py`](disasm_umt23x.py) | rebuilds everything from `testdata/memory/UMT23X.tap` alone |

Regenerate: `python3 disasm_umt23x.py` (needs `z80dasm` on PATH). The
script embeds a literal Z80-subset interpreter that executes the on-tape
MegaLZ depacker instruction by instruction — no emulator required — and its
output was validated byte-identical against an emulator capture halted at
the `JP 0x6000` entry point.

## Three ways to unpack, one answer

The 4,937-byte MegaLZ stream can be decoded three independent ways, and
all three agree byte for byte:

1. **on real hardware semantics** — the Z80-subset interpreter inside
   [`disasm_umt23x.py`](disasm_umt23x.py) executes the on-tape stage-1
   relocator and the resident depacker at `0xBF00` opcode by opcode;
2. **as a grammar** — [`megalz-unpack.py`](megalz-unpack.py) transcribes
   the same decode as plain Python, one function per Z80 construct, with
   every flag-lifetime quirk annotated (run it with `--verify
   umt-unpacked-6000.bin` to re-check);
3. **from a running machine** — the mid-run snapshot
   `testdata/memory/UMT23X.sna` (captured at `PC=6798h`, program busy
   testing RAM) holds the unpacked program in live memory. `disasm_umt23x.py`
   cross-checks its output against the snapshot on every run, expecting
   differences only in four known runtime-mutated regions (259 bytes: a
   test-state variable, text-VM bytes, one runtime-generated 256-byte
   block, stack scratch under the depacker).

The MegaLZ dialect here is the DEC40 family (compare `unmegalz_small.asm`,
`unmegalz_fast.asm`, `megalz_dec40.asm` in the `z80depacker` collection),
with the UMT depacker's own quirks — a shared SP cursor for bits and raw
bytes, a rep-offset token that rotates the offset bias `D'`, a 16-bit
length code (`code:rawbyte`, used twice in this stream for lengths 189
and 748), and the END marker's six trailer bytes.

## The program in one screen

```
0x6000  ENTRY           init, main loop, exit (restores #7FFD=10h, JP 0)
0x6037.. test drivers   RESET_TEST_STATE / PAGE_TESTABLE / TEST_PAGE_PATTERNS
0x60A6  PAGE_NUM        the page being mapped (byte) + SCREEN_PTR/SWEEP vars
0x60AD  TABLE_WORD      HL -> MODEL_TABLE[model].slot_word
0x60BF  CALL_W0..CALL_W4  jp (hl) into the model's five routine slots
0x60C6..0x61D2  MAP_*   per-clone paging (the ports/bits — see the article)
0x629E  FILL_BY_PUSH    SP-trick 16K page filler
0x62F8..0x6593  MARK_/PAT_/SUM_/ROT_  page-count ladders for the 4 tests
0x66D3..0x6B87  UI      checksum seed, text VM, SELECT_MODEL, screen table
0x6BD1..0x70E2  data    text VM vars/descriptors, MODEL_TABLE (15 x 27)
0x70E3..0x84F9  data    RU texts, 220-glyph font, EN texts
0xBF00  DEPACK_ENTRY    the MegaLZ depacker, still resident after unpacking
```

Key facts, each verifiable in the listing:

- **No clone auto-detection** — the only port read in the whole program is
  `IN A,(0xFEh)` (keyboard). You pick the model; UMT trusts you.
- **Pentagon 1024 pages through `#7FFD` alone**: bits 0-2 = page bits 0-2,
  bit 6 = pb3, bit 7 = pb4, and — the twist — **bit 5 (the 48K lock) as
  pb5/A18**, legal because the lock only latches while the extension is
  disabled (`#EFF7` bit 2). Confirmed by Born Dead #10 (see article).
- **ATM 7.1 pages through firmware**: a `#FD77`/`0ABh` + `JP 3D2Fh` ProfROM
  round-trip per page switch, with the page number written **complemented**
  to `#FFF7`.
- The four test layers (page-number uniqueness, 00/55/FF/AA patterns,
  ROM-seeded chained checksums, RR-carry rotate chains) are close to
  orthogonal — selection faults, cell faults, coupling faults, stuck bits.

## Companion material

- Source tape: [`testdata/memory/UMT23X.tap`](../../../../testdata/memory/UMT23X.tap)
- Mid-run emulator snapshot (unpack verification):
  [`testdata/memory/UMT23X.sna`](../../../../testdata/memory/UMT23X.sna)
- Reference Z80 depackers: the `z80depacker` collection (DEC40 family)
- Tape origin investigation and the dynamic/static depacker validation
  trail: `scratch/umt23x/` (session artifacts, git-ignored)
- unreal-ng model support: `core/src/emulator/ports/` — parity notes in
  [memory-addressing.md](memory-addressing.md#emulator-parity-unreal-ng)
- Package conventions follow
  [`docs/disasm/software/tfmplayer/`](../tfmplayer/README.md)
