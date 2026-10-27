# Research: the Z80N instruction set

**Date:** 2026-10-07 · part of [README.md](README.md) · used by [design-cpu.md](design-cpu.md)

Sources read: the table at https://wiki.specnext.dev/Extended_Z80_instruction_set (fetched 2026-10-07);
MAME's disassembler table
[`z80ndasm.cpp`](https://github.com/mamedev/mame/blob/master/src/devices/cpu/z80/z80ndasm.cpp) (the `ED` page,
decoded by index); the comments of jnext's
[`z80n_ext.cpp`](https://github.com/jorgegv/jnext/blob/main/src/cpu/z80n_ext.cpp) that cite the VHDL
`t80n_mcode.vhd`. All instructions are `ED xx` (xx below). Timings are the wiki's T-states.

**Verification status:** the opcode numbers, mnemonics and T-states come from the wiki table and agree with
MAME's decoded table. The words in the Notes column are shorthand from general knowledge of the instruction set
plus the jnext comments quoted below; they are **not** a specification. Phase N1 reads the wiki pages and the
T80N microcode for each instruction and writes the exact semantics into the test table before any code.

| Opcode | Mnemonic | T | Notes |
|:--|:--|:--|:--|
| `ED 23` | SWAPNIB | 8 | swap A nibbles |
| `ED 24` | MIRROR A | 8 | reverse A bits |
| `ED 27 n` | TEST n | 11 | flags as AND A,n without storing (jnext also computes the undocumented X / Y flags for it; exact rule to be read from the VHDL) |
| `ED 28` | BSLA DE,B | 8 | barrel shift DE left by B[4:0]; jnext notes a shift of 16 or more gives 0 (VHDL `shift_left` on 16 bits) |
| `ED 29` | BSRA DE,B | 8 | arithmetic right |
| `ED 2A` | BSRL DE,B | 8 | logical right |
| `ED 2B` | BSRF DE,B | 8 | right, filling with ones |
| `ED 2C` | BRLC DE,B | 8 | rotate left |
| `ED 30` | MUL D,E | 8 | DE = D x E |
| `ED 31` / `32` / `33` | ADD HL/DE/BC,A | 8 | 16-bit plus unsigned A; flags: the wiki marks carry "?" |
| `ED 34 nn` / `35` / `36` | ADD HL/DE/BC,nn | 16 | 16-bit immediate, flags untouched per the wiki |
| `ED 8A hh ll` | PUSH nn | 23 | the operand is big-endian |
| `ED 90` | OUTINB | 16 | out (C),(HL); HL++ ; no B decrement; flags "?" in the wiki |
| `ED 91 r n` | NEXTREG r,n | 20 | writes the register, not through the port select (jnext, citing the VHDL) |
| `ED 92 r` | NEXTREG r,A | 17 | |
| `ED 93` | PIXELDN | 8 | HL to the pixel line below in the screen layout |
| `ED 94` | PIXELAD | 8 | HL = screen address of the pixel (D,E) |
| `ED 95` | SETAE | 8 | A = bit mask from E[2:0] |
| `ED 98` | JP (C) | 13 | jump to the 16K-aligned address from port C input |
| `ED A4` | LDIX | 16 | like LDI but skip when (HL) = A |
| `ED A5` | LDWS | 14 | copy (HL) to (DE), increments L and D; flags as INC |
| `ED AC` | LDDX | 16 | like LDD with the skip |
| `ED B4` | LDIRX | 21/16 | repeat form |
| `ED B7` | LDPIRX | 21/16 | pattern fill: source address taken from HL high bits and E low bits |
| `ED BC` | LDDRX | 21/16 | |

That is 29 entries counting the three `ADD rr,A`, three `ADD rr,nn`, the two `NEXTREG` forms and the five
barrel shifts separately. The one `ED` entry in the decoded MAME table without a wiki row is none: both lists
match. **jnext additionally has `LDIRSCALE`** (`ED B6`) which neither the wiki table nor MAME's table has. The FPGA
microcode decodes it, but the register effects that would make it a scaling copy are commented out in `t80n.vhd`, so
it acts like `LDIRX` (Q7 closed, [research-fpga-vhdl.md](research-fpga-vhdl.md) section 6): implement it as `LDIRX` and pin that with a test.

## Facts that matter for the engine

| Fact | Source |
|:--|:--|
| Block-transfer flags of the `LDIX` family: S, Z, C preserved; H = 0, N = 0; P/V = (BC != 0 after the decrement); X, Y from A + the byte | jnext comment citing `t80n.vhd` |
| `NEXTREG` bypasses the I/O bus and does not change the `#243B` select latch; the six internal T-states stand for the register-fabric write | jnext comment citing `t80n_mcode.vhd` |
| The internal idle cycles of the `LDIX` family expose DE on the address bus without MREQ, so contention applies to them | jnext comment citing the VHDL |
| MAME's `z80n_device` has a "stackless NMI" mode (NMI return without pushing, `RETN` seen callback) and an `in_nextreg` / `out_nextreg` callback pair | MAME `z80n.cpp` |
| Both emulators run the Z80N on a normal Z80 core plus the extension; the real core is the T80N VHDL translation | jnext `INTERNAL-Z80N-CORE-PLAN.md` |
| The three-clock model: the Z80N takes the 28 MHz clock divided by 8, 4, 2 or 1 | MAME (`28_MHz_XTAL / 8`, `set_clock_scale(1 << speed)`) |
