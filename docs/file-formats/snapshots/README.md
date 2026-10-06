# Snapshot Formats

A snapshot is the state of a machine at one instant: RAM, the CPU registers, the paging latches, the sound chip and,
in the richer formats, the disk drives, the tape and other devices. Loading one skips the tape or the disk and starts
the program where it was.

Unreal-NG reads and writes the common Spectrum formats and reads two formats that belong to specific machines.

## Supported Formats

| Format | Extensions | Made on | Load | Save | What it holds |
|--------|------------|---------|:----:|:----:|---------------|
| [SNA](sna-snapshot.md) | `.sna` | 48K, 128K family | ✔ | ✔ | Registers, RAM, #7FFD. Fixed layout, no compression. No AY, no #1FFD |
| [Z80](z80-snapshot.md) | `.z80` | 48K to Scorpion 256K, Pentagon | ✔ | ✔ (v3) | Registers, RAM in 16 KB blocks, #7FFD, #1FFD, AY registers, frame position |
| [SZX](szx-snapshot.md) | `.szx` | 16K to Pentagon 1024, Scorpion | ✔ | ✔ | Exact CPU state, RAM, paging, AY, Beta 128, +3 floppies, tape, General Sound, mouse, keyboard |
| [SPG](spg-snapshot.md) | `.spg` | TS-Conf | ✔ | — | A TS-Conf SDK program: blocks of RAM at physical addresses, packed with MegaLZ or Hrust |
| [ZXP](zxp-snapshot.md) | `.zxp` | ZX-Poly | ✔ | — | All four CPU modules of the quad-Z80 ZX-Poly |

Also read, but not described here:

- **RZX** input recordings (`.rzx`) carry a start snapshot (and optional snapshots inside the recording) in SNA, Z80 or
  SZX form. The snapshot goes through the same loaders; see the [RZX recipe](../../../.recipe/media/play-rzx.md).
- **Time-travel sessions** and the in-memory state transfer between instances are not file formats of this family;
  see [automation](../../features/automation.md#machine-state-transfer).

Not read: `.sp`, `.snp`, `.sit`, `.zx`, `.sem`, `.snx`, `.slt`, `.ach`, `.prg`. (A `.zxs` buffer given to the
in-memory loader is read as SZX.)

## Choosing a format

| You want | Use |
|----------|-----|
| The widest tool support, a 48K or 128K program | SNA or Z80 |
| A 48K program with the frame position, AY registers and the machine named | Z80 v3 |
| Everything that matters for exact resumption: HALT, the interrupt shadow after EI, MEMPTR, Q, floppies, the tape, General Sound | SZX |
| A Pentagon 512 / 1024 or Scorpion 256 snapshot | Z80 (Scorpion) or SZX |
| A TS-Conf program started the way the SDK's shell starts it | SPG |

A 48K SNA keeps the PC on the stack, so a stack in the ROM cannot be saved (the save is refused; use Z80 or SZX); a 128K SNA,
Z80 and SZX store the PC in the header. A Pentagon 512 / 1024 can only be saved as SZX.

## How a snapshot is loaded

Every loader first reads the file into one format-neutral record (the *snapshot image*: RAM as logical 128K banks, the
paging latches, the CPU, AY, the border, and a list of the other blocks the file carries). A planning step then decides who writes the
machine, and only after that is anything written:

1. The **caller's choice**, if one was given (`commit`): `legacy` is the loader's own commit, a policy name selects a machine
   policy. An unknown name is refused and the answer lists the known ones.
2. The **machine's own policy**, if it has one. The Sprinter does: a snapshot goes into its running Spectrum mode through
   the PLD cell table, and is refused outside one.
3. The **fit check**: the memory the snapshot carries must exist on the machine. A 128K snapshot on a 48K machine, or
   banks beyond the machine's RAM, are refused with the reason and what would work.
4. Otherwise the loader's own commit.

Files no machine can take are refused at the same step, before anything is written: a Z80 snapshot from a SamRam or SAM Coupe,
a Z80 with a ROM block.

A refusal changes nothing: the machine is as it was. The report of the last load (which commit ran, who was asked, what each
block of the file did, warnings, the reason of a refusal) is available on every automation surface and in the Qt
window; `inspect` shows what a load would do without loading. Details and examples: the
[snapshot recipe](../../../.recipe/media/load-snapshot.md).

## How a snapshot is saved

A snapshot holds what a Spectrum 128K program sees: RAM as banks 0-7 (a 48K machine: 5, 2 and 0), #7FFD, the CPU, the AY, the
border. A save takes that **view** of the running machine, then writes the file from it; nothing is converted from one file
format to another (a separate tool does that), and the running machine is never touched.

1. **The machine stops first.** The save pauses the emulator and waits until it has actually parked; if it does not stop within a
   second nothing is saved and the answer says so.
2. **The view.** The Spectrum models (48K, 128K, +2, +2A, +3, Pentagon, Scorpion) are the view as they are. Machines with their
   own memory give a view only while they run a Spectrum 128K layout, and are refused with the reason otherwise:

   | Machine | The view |
   |---------|----------|
   | **Sprinter** | In a ZX mode, banks read through the PLD cells #F0-#F7 in the Spectrum's order, so the file restores on any other 128K machine and back on a Sprinter. The file names the machine the launcher mode made: `SP.ZX`, `SPRINTER.ZX`, `ORIGIN.ZX` a 128K, `P128.ZX`, `PENT128.ZX` a Pentagon 128, `SC256.ZX`, `SCORPION.ZX` a Scorpion (a mode without #7FFD paging a 48K). At the DSS prompt or in the BIOS there is no view (`needs: zx_mode`); the 512 KB modes have none yet |
   | **TS-Conf**, **ATM Turbo 2+ / 3 / 4.50** | While the window map is a Spectrum 128K (ROM at #0000, RAM 5 and 2, the page #7FFD names at #C000): a snapshot load puts the machine there. In the machine's own mode there is none (`needs: mode:128k`). The file says a 128K |
   | Profi, Kay and the other models | No view yet (`needs: capture_unsupported`) |

3. **Which formats.** Asked of the machine, right now: `snapshot formats` (CLI), `GET /snapshot/formats` (WebAPI), `snapshot_formats()`
   (Lua, Python), the Save Snapshot menu of the Qt window (items that cannot be used are disabled with the reason as their tip, and
   the save dialog offers only the formats that work). Each format answers *yes* or *no, because ... (needs ...)*:

   | Format | Refused when |
   |--------|--------------|
   | SNA | the machine has more than 128 KB in use (Pentagon 512 / 1024, Scorpion: use .szx, or .z80 for a Scorpion); #1FFD special paging; a 48K layout whose SP is in the ROM |
   | Z80 | Pentagon 512 / 1024 (the format has no such model: use .szx) |
   | SZX | the snapshot's model has no SZX machine id |

   A save that no machine could read back is never written; a format that cannot hold part of the state (an SNA has no AY and
   no #1FFD) says so in the answer's warnings.

### What a load resets

A SNA or Z80 load resets the machine first (peripherals, AY, FDC, tape), then puts the file's state in. For a 48K snapshot
the 48K BASIC ROM is selected with its latches (on a +2A / +3 the ROM number's high bit lives in #1FFD). On a 128K-family
machine a 48K Z80 leaves paging locked (#7FFD = #30), as a real 128K running a 48K program is; a 48K SNA leaves it unlocked
(#10): the two formats differ here. Machines whose reset leaves an extended paging on are put back
into the plain Spectrum 128K form before the snapshot's own #7FFD is written: the Pentagon 1024 (#EFF7 bit 2), the ATM
family (memory manager on, ROM pairs by #7FFD bit 4), TS-Conf (MEM_CONFIG in mapped mode, LCK128 = 128K).
SZX writes the paging through each model's decoder.

### Which machine takes which file

- **SNA and Z80:** any machine that has every bank the file carries. A 48K file loads on every machine; a 128K file needs at
  least 128 KB of RAM, so a 48K machine refuses it; a Scorpion 256K Z80 file (banks up to 15) needs 256 KB. Nothing is
  converted or dropped silently. The Sprinter is the exception that proves the rule: it takes a snapshot only into a running
  Spectrum mode (its own RAM pages 0-7 are not Spectrum banks) and refuses it at the DSS prompt.
- **SZX:** the model it was saved on (the file's machine id names it; 16K and NTSC 48K load as a 48K, 128Ke as a 128K, +3e as a
  +3). A file for another model is refused through the automation interfaces, naming both models; the Qt window offers to
  switch to the file's model first, as it does for SPG and RZX. Saving needs a machine id (the 48K, 128K, +2, +2A, +3,
  Pentagon 128 / 512 / 1024 and Scorpion have one): a TS-Conf, ATM or Sprinter in a Spectrum 128K layout is saved as the 128K (or the
  Pentagon / Scorpion its Sprinter mode names); other states are refused with the reason ([above](#how-a-snapshot-is-saved)).
- **SPG:** TS-Conf only (the Qt window and the automation launchers switch to it first).
- **ZXP:** a ZX-Poly machine only (four modules).

## Test material

The fixtures are under `testdata/loaders/`: `sna/`, `z80/` (with `libspectrum/` synthetic files for every machine id),
`szx/` (libspectrum-written and other emulators' files, each with a dump of what libspectrum reads from it), and
`golden/` (the state every fixture leaves on every machine; see its README). The ZX-Poly and TS-Conf corpora are under
`testdata/machines/`.

## References

Original specifications and the implementations they were checked against. The text of each format page in this folder
follows these; where a page differs, it says so.

**SNA and Z80**

- [World of Spectrum FAQ: file formats](https://worldofspectrum.org/faq/reference/formats.htm) (SNA, SLT, the others)
- [World of Spectrum FAQ: Z80 file format](https://worldofspectrum.org/faq/reference/z80format.htm)
- [Sinclair Wiki: SNA format](https://sinclair.wiki.zxnet.co.uk/wiki/SNA_format)
- [Sinclair Wiki: Z80 format](https://sinclair.wiki.zxnet.co.uk/wiki/Z80_format)
- [libspectrum: sna.c](https://github.com/speccytools/libspectrum/blob/master/sna.c) and
  [z80.c](https://github.com/speccytools/libspectrum/blob/master/z80.c) (the Fuse emulator's readers and writers)

**SZX (ZX-State)**

- [The zx-state file format 1.5](https://www.spectaculator.com/docs/zx-state/index.html), the specification by the Spectaculator author
- [Sinclair Wiki: ZX-State format](https://sinclair.wiki.zxnet.co.uk/wiki/ZX-State_format)
- [libspectrum: szx.c](https://github.com/speccytools/libspectrum/blob/master/szx.c)

**SPG**

- [SPG v1.0](https://github.com/tslabs/zx-evo-docs/blob/main/Formats/SPGv1_0.txt) and
  [v1.1](https://github.com/tslabs/zx-evo-docs/blob/main/Formats/SPGv1_1.txt) (zx-evo-docs, TS-Labs); the
  [Formats folder](https://github.com/tslabs/zx-evo-docs/tree/main/Formats) also holds the SNA and Z80 notes of that project
- [lvd's mhmt](https://github.com/lvd2/mhmt), the reference MegaLZ / Hrust packer used to check the depackers

**ZXP**

- [raydac/zxpoly](https://github.com/raydac/zxpoly) (the ZX-Poly emulator and Sprite Corrector) and
  [the format's grammar](https://github.com/raydac/zxpoly/blob/master/zxpoly-emul/src/jbbp/snapshots/zxp/com.igormaznitsa.zxpoly.formats.ZXPParser.jbbp)

**RZX**

- [World of Spectrum FAQ: RZX file format](https://worldofspectrum.org/faq/reference/rzxformat.htm)
