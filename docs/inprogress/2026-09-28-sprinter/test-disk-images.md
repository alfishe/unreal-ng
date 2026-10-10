# Sprinter test disk images: booting straight into a program

**Date:** 2026-10-09 · part of [README.md](README.md). Copy-paste steps:
[.recipe/media/sprinter-test-images.md](../../../.recipe/media/sprinter-test-images.md).

A test, a benchmark, a profile and a bug report all need the same thing: the machine boots and runs one program,
the same way each time, with no hand on the keyboard. The Sprinter's system disk does not do that by itself. This
note explains why, the three ways the project uses to change it, and which one to pick.

## 1. Why the shipped disk is not enough

The disk is the MAME pack's `sp_hdd_sys.chd` (raw `sp_hdd_sys.img`, 1 GiB, DSS 1.71.57). It is not in the
repository. Its layout is in the collection's README (`zx-spectrum/sprinter/hdd/README.md`) and in
[hardware-reference.md](hardware-reference.md) §9.3.

How it boots:
- The BIOS loads the DSS loader from LBA 1-3.
- The loader reads `SYSTEM.DOS` from the FAT16 partition of MBR entry 0, which starts at LBA 63.
- The shell `SYSTEM.EXE` runs `SYSTEM.BAT`. On this disk the file is:

```
@echo off
set PATH=%BOOTDSK%\;%BOOTDSK%\BIN\;%BOOTDSK%\DSS\;%BOOTDSK%\FN\;%BOOTDSK%\FM\;%BOOTDSK%\UTILS\;%BOOTDSK%\ZX\;%BOOTDSK%\DEV\SOLID\;
ver
fn
```

`fn` is Flex Navigator, a file manager that waits for the mouse and the keys. To start a demo from there, a script
has to walk the panels (the demo runner, [demo-status.md](demo-status.md)) or type at the prompt. Both are slow,
and they depend on timing.

To boot straight into the program, `SYSTEM.BAT` itself must start it. Often the program also has to be a
different build than the one on the disk: a fixed version, or the owner's `dont_blink_test1`.

## 2. Three ways

| Way | What changes | Where it is used | Limits |
|:--|:--|:--|:--|
| **A. In-session rewrite** (`FatInPlace`, `core/src/emulator/io/storage/fat/fatinplace.h`) | The disk is inserted in session mode; the code overwrites `SYSTEM.BAT` in the session copy | `SprinterFastPathsDemo_Test`, `BM_SprinterDemo_*`, the ZX-session tests (`sprinterzxsession.h`: `fn` blanked, DSS at the prompt) | The new text cannot be longer than the file: it keeps the file's size and cluster chain, so the text is padded with a `rem` line. It can only rewrite files the disk already has |
| **B. Composite medium** (`*.ucompose.yaml`, [compose-media.md](../../../.recipe/media/compose-media.md)) | The base image at the bottom, host folders on top: an upper file shadows a lower one. The build is a graft: the MBR, the loader and `SYSTEM.DOS` stay where DSS expects them | Any slot user: the GUI, the WebAPI / MCP, `UNREAL_SPRINTER_HDD`; `SprinterBoot_Test.ComposeDssGraftedUtilFolder` | None for this use. Files can grow, new folders can be added, and nothing is copied |
| **C. A host copy** (`tools/machines/sprinter/test-image/make-test-image.sh`, mtools) | A new raw image with files copied in and `SYSTEM.BAT` replaced | Tools that take only a plain file, or an image to hand to someone. The profiles of 2026-10-09 used two such copies (`sp-test1.img`, `sp-gui.img`, now deleted) | 1 GiB per copy. On APFS the copy is a clone and takes no space until written |

**Which to pick:**
- In C++ (a test, a benchmark): A for a changed `SYSTEM.BAT`.
- For your own files on the disk: B.
- Outside the emulator, or as one file: C.

B and C can be combined with A: a test given a descriptor or a copy as `UNREAL_SPRINTER_HDD` still rewrites
`SYSTEM.BAT` itself. For that, the patch layer's file must be at least as long as the test's text.

## 3. The same boot every time

What else must hold for two runs to be identical:
- **The CMOS clock.** DSS and some programs read it. The tests and the benchmarks fix it
  (`PortDecoder_Sprinter::GetRtc().SetFixedTime`, 2026-01-01 12:00:30 UTC).
- **The fast start.** `config.sprinter.fast_start = 1` skips the ROM's PLD loader. This is the test default; the
  user default is the full start.
- **Power-on RAM.** RAM pages 5 and 7 power up random (`Memory::RandomizeMemoryContent`). A two-machine
  comparison zeroes them after the reset.
- **Turbo.** Turbo renders only the frames the host clock allows. That changes the picture, not the machine, so
  compare the CPU state in turbo and the pictures at the real speed.

The two-machine comparison tests that rely on this: `SprinterIdleCycles_Test` and `SprinterFastPathsDemo_Test`
([sprinter-cpu-and-peripherals.md §8](../../emulator/design/core/sprinter-cpu-and-peripherals.md)).

## 4. Checked 2026-10-09

- **Way B.** The pack's `sp_hdd_sys.img`, a layer with `dont_blink_test1` mounted at `/DEMOS/DNTBLINK`, and a
  patch layer with `SYSTEM.BAT`.
  - The medium is `graft-fat16`.
  - `DEMOS/DNTBLINK/DNTBLINK.EXE` reads 9 743 bytes (the test build; the disk's own is 9 341).
  - `SYSTEM.BAT` is the patch's.
- **Way C.** The same build and the same `SYSTEM.BAT`, with `make-test-image.sh`.
- **Both B and C under `SprinterFastPathsDemo_Test.DontBlink`.** DSS boots, the demo loads and runs, and the
  fast-path exactness checks pass.
  - Only the test's music check fails: 4 608 Covox ring writes against more than 100 000 expected.
  - That check is tuned to the disk's DNTBLINK. The test build streams its music differently.
- **Way A** is what the demo tests and the benchmarks have done since they were written.

## 5. Rules

- Images stay in `scratch/`. They are derived from the owner's disk, 1 GiB, and never committed. The base disk
  is never written: insert with `access: session`, or use a copy.
- `SYSTEM.BAT` lines end in CR LF, and DSS paths use backslashes. Keep the `set PATH=` line when the program calls
  DSS tools.
- A program from a local build (`testdata/machines/sprinter/demo/` is the owner's uncommitted folder) is not the
  workload of the committed tests. Numbers and checks taken on it are not comparable with theirs.
