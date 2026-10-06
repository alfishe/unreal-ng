# SZX (ZX-State) Snapshot Format

`.szx` (also `.zx-state`) is a block-structured snapshot format designed by the author of the Spectaculator emulator. Unlike SNA
and Z80 it names the machine it was saved on, stores the exact CPU state (the interrupt shadow after `EI`, `HALT`, MEMPTR,
Q, the position inside the frame) and carries the state of peripherals: Beta 128, +3 floppy drives, the tape, General Sound,
mice and more. Fuse, Spectaculator, ZXMAK2 and several other emulators read and write it.

All values are little-endian and structures are byte-packed; strings are null-terminated.

## File layout

An 8-byte header, then blocks:

```
ZXSTHEADER   dwMagic 'Z','X','S','T'   BYTE chMajorVersion   BYTE chMinorVersion   BYTE chMachineId   BYTE chFlags
ZXSTBLOCK    dwId (4 characters)       dwSize (the data that follows, without this 8-byte header)
data ...
ZXSTBLOCK ...
```

- An unknown block id is skipped using `dwSize`, with no error. A parser must read files of any minor version of its major
  version (1), and must not choose how to parse by version number: it follows the blocks.
- The order of blocks is generally free. `Z80R` and `SPCR` come before the hardware blocks; an optional `CRTR` creator block comes
  first.
- `chFlags` bit 0: alternate (late) timings were in use.

### Machine ids (`chMachineId`)

| Id | Machine | Id | Machine |
|---:|---------|---:|---------|
| 0 | 16K ZX Spectrum | 9 | Timex TC2068 |
| 1 | 48K ZX Spectrum or Spectrum+ | 10 | ZS Scorpion 256 |
| 2 | 128K | 11 | ZX Spectrum SE |
| 3 | +2 | 12 | Timex TS2068 |
| 4 | +2A | 13 | Pentagon 512 |
| 5 | +3 | 14 | Pentagon 1024 |
| 6 | +3e | 15 | NTSC 48K |
| 7 | Pentagon 128 | 16 | 128Ke |
| 8 | Timex TC2048 | | |

## Blocks

Blocks Unreal-NG reads or writes (ids as the four characters in file order; `\0` is a zero byte):

| Id | Block | Contents | Unreal-NG |
|----|-------|----------|-----------|
| `CRTR` | Creator | Program name, version, free data | Read (reported); written with `unreal-ng` and the build's commit |
| `Z80R` | Z80 registers | AF, BC, DE, HL and the alternates, IX, IY, SP, PC, I, R, IFF1, IFF2, IM; `dwCyclesStart` (T-states since the interrupt); `chHoldIntReqCycles`; flags; `wMemPtr` | Read and written. See below |
| `SPCR` | ULA / paging | Border, #7FFD, #1FFD (#EFF7 on the Pentagon 1024 uses the same byte), #FE (since 1.1) | Read and written |
| `RAMP` | RAM page | `wFlags` (bit 0: zlib-compressed), page number, 16 KB | Read and written. Pages per machine below |
| `AY\0\0` | AY chip | Flags (Fuller Box, Melodik), the selected register, the 16 registers | Read and written. On TurboSound FM machines the block goes into the SSG half of YM2203 chip 1 |
| `B128` | Beta 128 | Flags (connected, custom ROM, paged, autoboot), drive count, the WD1793 registers, the #FF system register | Read and written (registers and paging; a command in flight is not in the format) |
| `BDSK` | Beta disk | One drive's disk: linked file name or an embedded image (TRD, SCL, FDI, UDI), zlib-compressed when embedded | Read and written |
| `+3\0\0` | +3 disk interface | Drive count, motor state | Read and written |
| `DSK\0` | +3 disk file | A linked disk image per drive | Read and written (always a link) |
| `TAPE` | Tape | The recorder's image (linked or embedded) and the current block | Read and written |
| `GS\0\0`, `GSRP` | General Sound | The GS CPU and latches; its RAM pages | The classic GS card (`GSType=Z80`): read and written. A NeoGS keeps its own RAM and applies the block approximately; a machine without a GS card reports it ignored |
| `COVX` | Covox | The level | Read and written |
| `AMXM` | Mouse | Kempston or AMX mouse | Kempston read and written; AMX is reported ignored |
| `KEYB` | Keyboard | Issue 2 flag, keyboard joystick | Read; an Issue 2 keyboard and keyboard joysticks are reported ignored |
| `JOY\0` | Joysticks | The joystick types | Read; reported ignored |
| `DRUM` | SpecDrum | The sample level | Read; reported ignored (not emulated) |

Other blocks the specification defines (Interface 1, Multiface, Opus, +D, Microdrives, ZXCF, ATA, Timex SCLD / Dock, custom ROMs,
Spectranet, uSpeech, LEC, printer, and others) are listed in the load report as *ignored: hardware this machine does not have*,
and skipped. The Timex, SE and other models without a machine here are refused.

### `Z80R` details

- `chFlags`: bit 0 `SUPPRESS_INTS` (the last instruction was `EI` or a stray `DD` / `FD` prefix: interrupts are not yet
  accepted), bit 1 `HALTED` (the CPU is executing NOPs until an interrupt; mutually exclusive with `SUPPRESS_INTS`), bit 2
  `FSET` (the last instruction set the flags, which gives Q).
- `dwCyclesStart` counts T-states from the frame's interrupt; `chHoldIntReqCycles` is how many T-states of the interrupt
  request are left when the snapshot resumes. Together they restore the exact position, including an interrupt that has
  just been accepted.
- `wMemPtr` exists since version 1.4. In files from 1.1 to 1.3 MEMPTR's high byte is taken from the byte that held `chBitReg`.
- A HALT snapshot whose PC points past the `HALT` (written so by some emulators) is moved back onto it.
- libspectrum 0.5.0 and older wrote A and F swapped in this block; the creator block tells, and Unreal-NG swaps them back.

### RAM pages per machine

| Machine | Pages written |
|---------|---------------|
| 16K | 5 |
| 48K, NTSC 48K (and the Timex models, which are not emulated) | 5, 2, 0 |
| 128K, +2, +2A, +3, Pentagon 128 | 0 to 7 |
| Scorpion 256 | 0 to 15 |
| Pentagon 512 | 0 to 31 |
| Pentagon 1024 | 0 to 63 |

A 48K machine keeps its three pages under their 128K numbers. A page that does not exist on the running machine is skipped and
reported.

## In Unreal-NG

Snapshot overview and how loading works: [README](README.md).

**Loading.** Version 1 files of any minor version are read (a different major version is refused). The snapshot's machine
must be the running one: 16K and NTSC 48K are read as a 48K, 128Ke as a 128K, +3e as a +3, and a Scorpion on a ProfScorp or a smaller
Pentagon on a bigger one is allowed with a warning. Any other model is refused with both named
(*"the snapshot was saved on a Pentagon 512K, the running machine is a ZX-Spectrum 128k: create a Pentagon 512K to load it"*).
The Qt window instead switches to the file's model first. The CPU position inside the frame is converted from "T-states since the
interrupt" to the emulator's own frame counting. The load report lists every block with its outcome: *applied*,
*approximated* (applied as far as this machine can hold it), *ignored* or *unknown*.

**Media.** A linked disk or tape image is looked for next to the snapshot first, then at the stored path, and inserted with
Session access: the guest's writes never reach the linked file. An embedded image is staged in a temporary file that goes with
the medium.

**Saving.** Version 1.5. RAM pages are zlib-compressed when that makes them smaller. File-backed disks and the tape are saved as links
(relative to the snapshot's folder when inside it). Only machines that have a machine id above can be saved: the 48K, 128K, +2, +2A, +3,
Pentagon (128 / 512 / 1024) and Scorpion. An ATM, ZX-Evo, Profi, TS-Conf or Sprinter state is refused with *"this model has no SZX
machine id; save it as .z80 or .sna"*.

**Test material.** `testdata/loaders/szx/`: synthetic files written by libspectrum for every machine id we emulate, converted
copies of the SNA / Z80 fixtures, and files from Spectaculator, ZXMAK2 and ZX-M8XXX. Each has a text dump of what libspectrum
reads from it, which the reader test compares field by field and page by page; the README of that folder has the details.

## References

- [The zx-state file format 1.5](https://www.spectaculator.com/docs/zx-state/index.html), the specification (this page follows it):
  [introduction](https://www.spectaculator.com/docs/zx-state/intro.html),
  [basic information](https://www.spectaculator.com/docs/zx-state/basic_info.html),
  [header](https://www.spectaculator.com/docs/zx-state/header.html),
  [block header](https://www.spectaculator.com/docs/zx-state/block.html),
  [block types](https://www.spectaculator.com/docs/zx-state/block_types.html),
  [Z80R](https://www.spectaculator.com/docs/zx-state/z80regs.html),
  [SPCR](https://www.spectaculator.com/docs/zx-state/specregs.html),
  [RAMP](https://www.spectaculator.com/docs/zx-state/rampage.html),
  [AY](https://www.spectaculator.com/docs/zx-state/ay.html),
  [B128](https://www.spectaculator.com/docs/zx-state/beta128.html),
  [BDSK](https://www.spectaculator.com/docs/zx-state/betadisk.html),
  [TAPE](https://www.spectaculator.com/docs/zx-state/cassette_recorder.html),
  [GS](https://www.spectaculator.com/docs/zx-state/gs.html)
- [Sinclair Wiki: ZX-State format](https://sinclair.wiki.zxnet.co.uk/wiki/ZX-State_format)
- [libspectrum: szx.c](https://github.com/speccytools/libspectrum/blob/master/szx.c), the reference implementation in the Fuse
  emulator's library, used to cross-check the reader and to write the test files
