# Building NedoOS from source: the kernel and the distribution

NedoOS is a ZX Spectrum clone OS (ATM Turbo 2, ZX-Evo, Pentagon 2.666, ...). The project builds on Windows with the
`.bat` files of its tree. This recipe builds the kernel on macOS or Linux, byte for byte the same as the tree's
release, and puts together a distribution (an SD card folder) around it. Use it when a test needs a kernel with a
fix, another build preset or another revision.

## Sources

- The tree: the GitHub mirror `https://github.com/alfishe/NedoOS` of the project's Subversion repository. Clone it
  with history (`git clone`, not `--depth 1`): the testdata names revisions (for example
  `testdata/machines/p2666/software/README.md`), and `git worktree add --detach <dir> <rev>` gives one tree per revision.
- `release/` in the tree is the distribution as the maintainers built it: the kernels (`*.$C`, `*.trd`) and the
  programs (`bin/`, `doc/`, `ini/`, `nedodemo/`, `nedogame/`). A kernel built at a revision is checked against that
  revision's `release/`.
- The tree's sources are CP866 text with CRLF line ends. Edit them byte-preserving (Python on bytes, or an editor set
  to CP866 + CRLF). A patch made with `git diff` keeps the CRs.

## Tools, built from the tree

Use the versions in the tree's `tools/src/`: they made the release, and the byte check below depends on them.

| Tool | Source | Build |
|:--|:--|:--|
| sjasmplus (1.20.1 at `d1d011f8`) | `tools/src/sjasmplus` | `cmake -S tools/src/sjasmplus -B <build>/sjasmplus -DCMAKE_BUILD_TYPE=Release -DENABLE_LUA=OFF && cmake --build <build>/sjasmplus` |
| mhmt (the MegaLZ packer of the kernel) | `tools/src/mhmt` | copy `tools/src/mhmt` out of the tree, then `make -C <copy>/linux CC=cc 'CFLAGS=-std=c99 -O2 -D_POSIX_C_SOURCE=1 -U__STRICT_ANSI__ -w' mhmt` (its own CFLAGS have `-Werror`, which fails on clang) |

Build the tools outside the source tree: the kernel build writes into it, the tools do not need to. The
`tools/*.exe` and the ELF `tools/mhmt` in the tree are prebuilt binaries for Windows and i386 Linux. Do not run them.

## The kernel

`tools/nedoos/kernel.sh` (unreal-ng repository) does what `src/mk<preset>.bat` and `src/kernel/build.bat` do:

1. It writes `src/_sdk/syssets.asm` from the preset's `echo ... _sdk\syssets.asm` lines.
2. It assembles `ngsinst.asm` and `main.asm`.
3. It packs `syscode.c` (`mhmt -mlz`), joins `initcode.c` + `syscode.c.mlz` into `code.c`, and wraps it as the
   hobeta `nedoos.$C` (`hobeta.asm`).

```bash
export SJASMPLUS=<build>/sjasmplus/sjasmplus MHMT=<build>/mhmt
tools/nedoos/kernel.sh <nedoos-tree> pe26sd <out>/osp26sd.\$C
```

| Preset (`src/mk*.bat`) | Release file | Machine |
|:--|:--|:--|
| `evo` | `sd_boot.$C` | ZX-Evo BaseConf, Z-Controller SD (also has the NeoGS SD driver, `NGSSD`) |
| `atm3sd` / `atm3hd` | `osatm3sd.$C` / `osatm3hd.$C` | ATM3 (ZX-Evo), SD / Nemo IDE |
| `pe26sd` | `osp26sd.$C` | Pentagon 2.666 LE, Z-Controller SD, NeoGS SD (`KOE`, `NGSSD`) |
| `atm2hd`, `atm2hd_maxmem` | `osatm2hd.$C`, `osatm2hm.$C` | ATM Turbo 2, IDE |
| `atm2`, `atm3`, `pe26` | `*.trd` | the TR-DOS boot disks: the script builds their kernel (`src/kernel/code.c`), not the disk |

Notes:

- `SVNREVISION`: the `.bat` files take it from `svnversion`, which a git mirror does not have. The releases were
  built without it, so leave it unset for the byte check. Set `SVNREVISION=<n>` to stamp a build.
- FatFs (`src/fatfs4os`) needs the IAR Z80 compiler. Without IAR, `build.bat` skips it and uses the prebuilt
  `fatfs.raw` in the tree, and so does the script.
- The script writes `src/_sdk/syssets.asm` and the build outputs in `src/kernel/` (all untracked). Use one tree per
  build, or run `git clean` between builds.

### Byte check against the release

Build the unchanged revision and compare it with the same revision's `release/` file:

```bash
cmp -l <nedoos-tree>/src/kernel/nedoos.\$C <nedoos-tree>/release/osp26sd.\$C
```

The expected result is one line: the byte right after the code (the hobeta header's length, offsets 11-12, plus the
17-byte header). The hobeta file is padded to whole sectors with whatever was in the assembler's buffer, and every
code byte is equal. At `d1d011f8` this holds for `pe26sd`, `evo`, `atm3sd` and `atm3hd`. Anything more means another
tool version or a wrong preset: fix that before you patch.

### A patched kernel

1. Make a worktree at the revision and edit the source (keep CP866 + CRLF).
2. Build it with the script, then `git diff > <name>.patch` in the tree.
3. Keep the kernel and the patch together, and write down the revision, the preset and the check above.

Example: `testdata/machines/p2666/software/nedoos/kernel-ngsfix/` (the NGS SD driver's `OUTI` fix for the Pentagon
2.666), used by `GSNedoOsP2666_Test.V5KernelDrivesTheNeoGsSdCard`.

## The distribution

The distribution is the tree's `release/` at the same revision, with your kernel in place of the release one:

| Boot medium | Layout |
|:--|:--|
| SD card (Z-Controller, a folder or FAT image in `sd.zc`) | `SD_BOOT.$C` (the kernel) at the root, `bin/` (`term.com` and `cmd.com` at least; the shell runs `bin/autoexec.bat`), plus `doc/`, `ini/`, `nedodemo/`, `nedogame/` as needed |
| TR-DOS disk (`atm2`, `atm3`, `pe26`) | `make.bat`'s `nedotrd` steps: `boot6000.$b`, the kernel body `kernel/code.c` at 24576, then the programs from `release/bin` |

Mount the folder with [media/use-media-slots.md](../media/use-media-slots.md) (`sd.zc` takes a host folder).
NedoOS on a machine: [machines/pentagon-2666.md](../machines/pentagon-2666.md),
[machines/atm/atm3-zxevo-baseconf.md](../machines/atm/atm3-zxevo-baseconf.md).

Rebuilding the programs themselves is a Windows job at this point. `src/make.bat` runs some 130 per-program
`build.bat` files, which use `for` loops, MinGW `make`, `nedores`, `convega` and the self-hosted NedoLang tools.
`src/Makefile` (GNU make, Linux) covers only part of them, and at `d1d011f8` it is out of date:

- several of its `SUBDIRS` no longer exist (`nmisvc`, `ngsdec`, `dmapps/*`);
- the NedoLang tools do not bootstrap;
- its host tools are built with `-m32` and need GNU `realpath`;
- its `pe26sd` settings are not `mkpe26sd.bat`'s.

To rebuild one program, run the `sjasmplus` line from its `build.bat` in its directory and copy the `.com` into
`bin/`.

## Pitfalls

- Shells expand `$C` in `nedoos.$C`: quote it (`'nedoos.$C'` or `nedoos.\$C`).
- Never use `/tmp` for the outputs: put them in the repository's `scratch/<topic>/`.
- A newer NedoOS release is not always a better test kernel. The Pentagon 2.666 builds after `d1d011f8` stop after
  "loading term.com" (`testdata/machines/p2666/software/README.md`, "Why not the latest NedoOS release").
