# Building Estex DSS from source

**Status:** verified 2026-10-04: a fresh clone of the community DSS (`master` `ae08ad9`, 2026-05-18) builds with
no errors and no warnings, and the result boots on the emulator to Flex Navigator, `ver` printing
"Estex DSS version 1.71.66. Shell version 1.2.523." The built files are kept in
`testdata/machines/sprinter/dss/1.71.66/`. Related: [atapi-cd-boot.md](atapi-cd-boot.md) (the boot loader the BIOS
starts), [../2026-10-04-estex-dss-debugging/](../2026-10-04-estex-dss-debugging/README.md) (the symbols below).

DSS (Disk Sub System) is the Sprinter's DOS. A system disk carries three parts:

| Part | Source | Output | What it is |
|---|---|---|---|
| kernel | `DSS/DSS-MAIN.ASM` | `system.dos` | the DOS itself: the `RST #10` calls, file systems, drivers |
| shell | `SHELL/SHELL.ASM` | `system.exe` | the command line (`dir`, `cd`, `ver`, ...) |
| installer | `BOOT/boot.asm` | `boot.exe` | a DSS program that writes the boot loader and the two files to a floppy or hard disk |
| boot loader | `BOOT/DSSBOOT.ASM` (included by `boot.asm`) | `build/DSSloader.bin` | the `Starting...` sector code the BIOS runs; `boot.exe` carries it |

## 1. What you need

- **The sources**, with their submodule (`Shared_Includes`: macros, constants, the Lua helpers the build uses):

  ```bash
  git clone --recurse-submodules https://zxgit.org/Tolik-Trek/Estex-DSS.git
  ```

  Branches: `master` (the one built here), `beta_cdfs` (the CD file system work, 2026-07-18, marked "not working
  yet"; it has no common history with `master`), `Vibe`, `public-beta-1.70.990`.
- **sjasmplus 1.21.1** (<https://github.com/z00m128/sjasmplus>), built with its Lua support (the default). The
  sources use `LUA` blocks to read the build number and the date, so a build without Lua fails.

## 2. Build

Run everything from the repository root: the version files open `./DSS/build.txt` and `./SHELL/build.txt` by a
path relative to the current directory.

```bash
mkdir -p Build build
sjasmplus --nologo -I. -IDSS  --exp=Build/system.exp --sym=Build/system.sym --raw=Build/system.dos DSS/DSS-MAIN.ASM
sjasmplus --nologo -I.        --raw=Build/system.exe SHELL/SHELL.ASM
sjasmplus --nologo -I. -IBOOT --raw=Build/boot.exe  BOOT/boot.asm
```

Each prints `Errors: 0, warnings: 0`. Without `--exp`, the kernel build warns once about its `EXPORT` lines.
`boot.asm` also writes `build/DSSloader.bin` itself (an `OUTPUT` line in `DSSBOOT.ASM`).

The result of `master` `ae08ad9` (built 2026-10-04):

| File | Bytes | SHA-256 |
|---|---:|---|
| `system.dos` | 16 941 | `7142eb2995bcae1a6c330d0111c2b4249ed3b5b50fbbc8e59a118f0c50a21a94` |
| `system.exe` | 7 936 | `f92fe5df8248ee061ceb0523c0dcb1d45649830bd1b7656687cc520dba461e6d` |
| `boot.exe` | 3 476 | `fab039a16f0b6ac3800580a38663a06aca3cb0f82393d7ddbd34c00d2e396ed1` |
| `DSSloader.bin` | 1 719 | `50056f50bc53b3649c07fa37340a18a44c6cd0e34db71960a54faa848efa8d50` |
| `system.exp` | 399 | `0f0958b83c04468928c48e58bd1a8c36c2d8ebc86e798b4734356bf035bc2df9` |
| `system.sym` | 144 762 | `faf3fe89600e19c2e236ec89917f4936aedafcae49a2a15c6c699694fd3627f1` |

Two builds of the same commit on the same day are byte for byte equal (checked with a second, fresh clone). On
another day they differ: the kernel stores the build date (`DSS/VERSION.INC` reads sjasmplus's `__DATE__`), and so
does the installer.

**Version numbers.** The version is 1.71 (`VERS`, `MODF` in `DSS/VERSION.INC`); the third number is the build
counter in `DSS/build.txt` (66 at `ae08ad9`); the shell's is in `SHELL/build.txt` (523). Defining
`INCREASE_BUILD` (`-DINCREASE_BUILD`) makes the build add one to the counter file first; leave it out to rebuild
a commit unchanged.

**Linux and other case-sensitive file systems.** The include paths mix cases (`Shared_Includes` and
`shared_includes`, `Build` and `build`). macOS and Windows do not care; on Linux, add a symbolic link for each
spelling the build reports as missing (for example `ln -s Shared_Includes shared_includes`) and create both `Build`
and `build`.

## 3. Symbols for debugging

- `system.exp` (`--exp`): the ten labels the kernel exports on purpose (`CORE_BUFFERS.*`: the file manager and
  sector buffers, the memory table, the current and work directories), as `NAME: EQU 0x....` lines.
- `system.sym` (`--sym`): every label of the kernel, 3 640 of them, in the same format. `--lst=Build/system.lst`
  adds the full listing (about 1 MB) when the code itself is needed next to the addresses.

The kernel is assembled from address 0 (`ORG 0` in `DSS-MAIN.ASM`), so the addresses are those of the kernel's
own memory page, not of a fixed CPU address. The shell and the installer are assembled at `org_addr` (a DSS
program's load address). The symbols match only the binary they came from: keep each `.sym` next to its
`system.dos`.

## 4. Putting the build on a disk

**On the Sprinter (or the emulator):** run `boot.exe` from DSS. It writes the boot loader and copies
`system.dos` / `system.exe` to a floppy or a hard disk partition, and it can replace a loader already installed
(`BOOT/README.TXT`).

**On the host, over an existing DSS hard disk image:** replace the two files in the FAT partition. The usual
partition starts at sector 63, byte offset 32 256; with mtools:

```bash
cp dss-hdd.img dss-built.img
mcopy -o -i dss-built.img@@32256 Build/system.dos ::SYSTEM.DOS
mcopy -o -i dss-built.img@@32256 Build/system.exe ::SYSTEM.EXE
```

The loader already on the disk loads the new files (it reads the file names, not fixed sectors). This is how the
build was checked: the hard disk of the MAME Sprinter pack with the two files replaced, BIOS 3.06 Hotfix 2, boots
to Flex Navigator and `ver` shows 1.71.66 / 1.2.523.

## 5. Notes

- `master` has the CD file system source (`DSS/FS/CDFS.ASM`) and the ATAPI driver, but the built 1.71.66 gives the
  CD drive no letter (`dir d:` and `dir e:` answer "Path not found" with a CD on the primary slave). See
  [atapi-cd-boot.md](atapi-cd-boot.md) section 5.
- The DSS repository publishes sources only; the built files in `testdata/machines/sprinter/dss/1.71.66/` are the
  ones listed above.
