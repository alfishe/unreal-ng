# DIZZY X (KID__DR release) - loader and its ROM check

**Fixture**: `testdata/loaders/tap/DIZZY_X_KID__DR.tap`
**Listing**: [`loader.asm`](loader.asm) - annotated, for study.

A cracked "48/128K" release of Dizzy X. Its loader is a good small example of three
classic tricks: code encrypted with the Z80 refresh register, a hand-written tape routine,
and a **ROM identity check** - the protection that makes the game refuse to run on a
machine whose 48K ROM differs from the original Sinclair one.

## Tape layout

| Block | Bytes | What it is |
|:--|--:|:--|
| 0 | 17 | Header: program `DIZZY-X`, length 15340 |
| 1 | 15340 | BASIC program, line 0. Its body is machine code, encrypted |
| 2 | 6912 | Screen-sized block (a picture, judging by its size) |
| 3 | 41216 | Main game data (looks encrypted) |
| 4 | 16384 | 128K extras, not read on a 48K machine |
| 5 | 6912 | Screen-sized again; also not read on a 48K machine |

The ROM loads block 0 and block 1 normally. From then on the program's own loader
(stage 2, `#5DAE`) reads every following block with its own pulse-timing routine.

## Three stages

1. **Stage 1** (`#9802`, called from `#989E`): runs from the BASIC line, clears the screen, then decrypts the
   rest of itself in two passes. The key of each pass is the **R register**: `LD A,R` returns
   a counter that increments on every instruction fetch, so the decrypted result is right only
   if the exact number of fetches matches. A debugger that single-steps differently, or an
   emulator that counts R wrongly, produces garbage. The DD prefixes inside the first loop do
   nothing but add to R.
2. **Stage 2** (`#5D00-#5E60`): the tape loader plus the decrypt helpers.
3. **Stage 3** (`#5E61`): relocates the game, installs an interrupt handler, asks
   "infinite lives? Y/N", starts the game.

## The ROM check

The loader reads the byte at ROM address `#006D` and requires `#20`:

```asm
ld   a,(#006D)
cp   #20
jr   nz,wipe          ; not the original ROM
```

In the Sinclair 48K ROM, `#0066` is the NMI handler `PUSH AF / PUSH HL / LD HL,(#5CB0) /
LD A,H / OR L / JR NZ,... / JP (HL)`, and `#006D` is the opcode of that `JR NZ` (`#20`).
Some patched ROMs have `JR Z` (`#28`) there.

The check runs **three times**: once at `#986B` (stage 1), and again on **every interrupt**
(`#5ECA`, 50 times per second). It never mentions the ROM by name, which is the point:
a cracker who patches the ROM for his own tricks trips it.

Failure reaction: stage 1 fills 15000 bytes of memory from `#4000` with `#15` (`DEC D`), which
also erases stage 2; the CPU then runs through the filler and ends in a red or solid screen.
The interrupt handler pushes `#0000` and jumps to ROM `#3D30`, which in the 48K ROM is the character
set: the CPU executes the bitmap of an "&" as code and runs wild.

## Verdict

This is a **copy and clone protection working as designed**, not an emulator defect. The project
keeps the Pentagon ROM set as it is; the tape does not run on machines whose 48K ROM is not the
original one, as on those real clones. Use a model with the original 48K ROM (the 48K model, verified) to play it.

## Emulator consequences

- **48K, 128K, ZX-Spectrum+ models with the original 48K ROM** pass the check.
- **unreal-ng Pentagon** uses the ROM set `[ROM.pentagon]` whose 48K page is
  `rom/48for128.rom` (patched, `#28` at `#006D`) - the game derails right after the first
  decrypt; the cursor stays at block 2 of 6. The same tape loads on MAME's Pentagon, whose ROM
  has `#20`. It is **not** a tape-timing or emulation bug.
- Other patched ROMs (some clones, TR-DOS-aware ROMs) behave the same.

See also the investigation notes in
[`docs/inprogress/2026-08-30-fast-tape-loading/nonstandard-loader-investigation.md`](../../../inprogress/2026-08-30-fast-tape-loading/nonstandard-loader-investigation.md).

## What is verified and what is not

Verified by running the tape in unreal-ng 48K and Pentagon (execution breakpoints, register and
memory reads) and cross-checked in MAME (`spectrum`, original ROM): stage 2 and 3 code, the `#006D` check, the filler
reaction. Also verified: the helper at `#6009` is a lone `RET`; the relocation routine at `#7022`
finds its own address by `CALL #0052` (a `RET` in the 48K ROM), a further implicit ROM dependence;
the Y/N answer Y sets the cheat flag `#5D80` to 0; the two `#C9` patches at `#5FFD`/`#5FE3` hit the
routine at `#5FD2`. **Inferred** only: the `#FFFF` write/read-back looks like a test for RAM at the
top of memory (16K vs 48K). **Not examined**: the content of the encrypted game blocks and the
on-screen purpose of the routine at `#5FD2`. Those places are marked `INFERRED` or `NOT TRACED` in
the listing.
