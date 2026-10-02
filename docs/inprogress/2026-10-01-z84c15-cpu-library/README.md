# Z84C15 CPU library for the Sprinter

**Created:** 2026-10-01 · **Status:** see [TODO.md](TODO.md) · PLAN row #59 (Sprinter)

The Peters Plus Sprinter runs on a Zilog Z84C15: a CMOS Z80 core (Z84C00) plus an on-chip block
that changes bus timing and behavior (wait-state generator, chip selects, watchdog, CTC / SIO /
PIO with their interrupt daisy chain). This folder designs and tracks moving the Sprinter, and
only the Sprinter, onto its own vendored CPU library, `core/src/3rdparty/z84c15/`, forked from
`unreal-z80` 0.5.0. Every other machine keeps the native interpreter in
`core/src/emulator/cpu/`, byte for byte.

## Owner decisions (2026-10-01, binding)

1. The Sprinter CPU gets its own vendored library modeled on `core/src/3rdparty/unreal-z80/`,
   although the core itself differs from the NMOS Z80 in two places only (`OUT (C),0` writes
   `#FF`; LD A,I / LD A,R keep P/V when an INT follows). Isolating odd chips beats avoiding
   duplication. The base Z80 is never patched for the Z84C15.
2. Every other machine stays on the native interpreter, unchanged: the same TTD corpus, the same
   TTD CI gate bytes, the same contention and timing tests, the same frame cost.
3. Every place where the main CPU will later move to a library too carries a
   `// CPU-LIBRARY-MIGRATION(<id>): ...` comment; the list is in [design.md](design.md) §7.

This replaces the recommendation of [research-cpu-z84c15.md](../2026-09-28-sprinter/research-cpu-z84c15.md)
§8.1 (a variant flag on the shared core); the research's facts (§1-§7) are the input.

## Documents

| File | Content |
|---|---|
| [design.md](design.md) | library layout and API, the engine seam in `Z80`, what lives where, the wait generator, the daisy chain, the tag list, tests, risks |
| [TODO.md](TODO.md) | status marker: steps, progress, open questions |

## Glossary

| Term | Meaning |
|:--|:--|
| **Native interpreter** | The Z80 core every machine uses today: `Z80::Z80Step` and the opcode tables in `core/src/emulator/cpu/op_*.cpp`. |
| **Engine** | Something that executes instructions in place of the native interpreter for one machine (`ICpuEngine`). The Sprinter's is the Z84C15 library. |
| **Seam** | The single point in `Z80` where the engine is called instead of the native interpreter. |
| **Register file** | The CPU registers as one packed block. The library executes directly on the host's `Z80Registers` (zero copy). |
| **WCR / MWBR** | The Z84C15's wait-state control and memory-wait-boundary registers (pointer `#EE`, data `#EF`). |
| **Programmed wait** | A wait state the chip's own wait generator inserts. **External wait**: one the board drives on the `/WAIT` pin (the Sprinter PLD); the two add up. |
| **Daisy chain** | Zilog's interrupt priority chain: CTC, SIO and PIO in the order of register `#F4`, each supplying its own IM2 vector and watching for `RETI`. |
