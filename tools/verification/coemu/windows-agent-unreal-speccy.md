# Task for a Windows AI agent: run the ctprobe compatibility test on the Unreal Speccy family

You are on a Windows machine. Build four versions of the Unreal Speccy emulator from source, run the unreal-ng
contention probe (**ctprobe**) on each of them, on every machine they emulate, and report the results in the
format described at the end. Do not change any emulator source code. Do not publish anything.

The probe is a ZX Spectrum program. It times short pieces of code against the frame interrupt, compares every
value with tables derived from real hardware, prints the result on the screen and keeps it in memory. Its
documentation: <https://github.com/alfishe/unreal-ng/blob/master/tools/verification/contention/ctprobe/README.md>.
Read that README first; it explains every check (M1-01, D-03, P-02, ...) and what "class" and "onset" mean.

## 1. What to build

| Id (use it in folder names) | What | Source |
|:--|:--|:--|
| `us-0.39.0` | classic Unreal Speccy 0.39.0 (SMT) | `svn://svn.nedopc.com/pentevo/tools/unreal_fix/0.39.0/original` at revision **805** (the untouched archive is `.../0.39.0/orig_arch/us0.39.0.src.7z`, r800) |
| `us-nedopc` | the latest Unreal Speccy (the nedopc line, lvd's fixes up to 2024-12) | `svn://svn.nedopc.com/pentevo/tools/unreal_fix/0.39.0/nedopc` at **HEAD** (1162 when this was written) |
| `ns-classic` | Unreal NS, its first revision in the repository | `svn://svn.nedopc.com/pentevo/tools/unreal_fix/0.39.0/Unreal_NS` at revision **1088** (2023-07-16) |
| `ns-latest` | Unreal NS, latest | the same path at **HEAD** (1321 when this was written) |

Optional, if time allows: `tslabs` = TS-Labs Unreal, `https://github.com/tslabs/zx-evo`, folder `pentevo/unreal`
(`Unreal_2022.sln`), latest commit.

The nedopc repository allows anonymous read access with `svn export`. Write down the exact revision you built
for each id (`svn info --show-item last-changed-revision <url>`).

## 2. Tools to install

- Git for Windows, a command-line Subversion client (SlikSVN, or TortoiseSVN with "command line client tools").
- Visual Studio 2022 Build Tools with the "Desktop development with C++" workload (MSVC v143, a Windows 10/11
  SDK).
- Python 3 (for the probe's compare script).
- 7-Zip only if you use the `orig_arch` archive.

## 3. Get the probe from GitHub

Download these four files from `https://raw.githubusercontent.com/alfishe/unreal-ng/master/tools/verification/contention/ctprobe/`
into `C:\coemu\probe\` and check their SHA-256 (`certutil -hashfile <file> SHA256`):

| File | SHA-256 |
|:--|:--|
| `ctprobe.trd` | `6fcb0f870a5d6312efd73fae40fecbca8f833999f696e98fbd7c03de15176e1e` |
| `ctprobe.tap` | `110ceff202c32db1b5be96d554cbcb35ebb0dba2aabe213b0a58969e2641b7b7` |
| `ctprobe.sym` | `63a62e3ed1b6148ad2da923b3fcfbe5c8970de4de7bbc06a3a788fbbf08e629e` |
| `ctprobe-compare.py` | `dcea0d8d03666bbb630418dc8b152afba3eb8196cc8a5ff2f7164090573e951e` |

If a hash differs, the published probe has changed: stop and report the new hashes instead of guessing.

Addresses you need (from `ctprobe.sym`): `START` = `#8CA0`, `DONE` = `#8CB0`, `FAILS` = `#8CB1`,
`PROBEEND` = `#B1C4`. The memory dump is `#8CA0` .. `#B1C3` inclusive (9508 bytes).

`ctprobe.trd` holds `boot` (a BASIC loader) and `ctprobe` (the code). On a TR-DOS machine, `RUN` at the `A>`
prompt starts it; the program runs by itself and needs no keys.

## 4. Build each version

For each id, `svn export -r <rev> <url> C:\coemu\src\<id>`, then:

- `us-0.39.0`, `us-nedopc`: open `msvc\unreal.sln` (projects `unreal`, `z80`, `snd`), build **Release | Win32**:
  `msbuild msvc\unreal.sln /m:4 /p:Configuration=Release /p:Platform=Win32`. If the projects name an old
  platform toolset or SDK, retarget without changing any source: add `/p:PlatformToolset=v143
  /p:WindowsTargetPlatformVersion=10.0` to the command line.
- `ns-classic`, `ns-latest`: the solution is `SRC\msvc\unreal.sln`; build it the same way. The tree also has
  MinGW batch files (`_MAKE_NOW.bat`) for the toolchains named in the `_FOR_*` folders; use them only if the MSVC
  build fails, and say so in the report.

The runnable folder is the one with `unreal.ini`, the ROMs and `bass.dll`: `x32\` for the classic and nedopc trees,
`release\` for NS. Copy the whole folder to `C:\coemu\run\<id>\` and put your freshly built `unreal.exe` in it
(replace any prebuilt one; write down the file size and SHA-256 of the exe you used). Keep a pristine copy of
`unreal.ini` as `unreal.ini.orig`.

If a version does not build, record the compiler errors (first 30 lines) and go on with the others.

## 5. Machines

Unreal Speccy emulates the Pentagon and the Soviet clones; it has no Sinclair ULA contention. Run these machines,
each from its own ini file (`C:\coemu\run\<id>\<machine>.ini`, a copy of `unreal.ini.orig` with only these keys
changed; keep every other setting stock):

| Harness name | `[MISC] HIMEM` | `[MISC] RAMSize` | `[ULA] Preset` | `[ROM] ROMSET` |
|:--|:--|:--|:--|:--|
| `pentagon` | `PENTAGON` | `128` | `PENTAGON` | `pentagon` |
| `scorpion` | `SCORPION` | `256` | `SCORPION` | `scorpion` |
| `profscorp` | `PROFSCORP` | `256` | `SCORPION` | the ProfROM set if the ini has one (`[ROM] PROFROM=`) |
| `atm710` | `ATM710` | `1024` | `ATM1_2_3.5MHz` | the ATM2 set |
| `atm3` | `ATM3` (NS: `ZX_EVO` if that is its name) | `4096` | `ATM1_2_3.5MHz` | the ATM3 / ZX-Evo set |
| `profi` | `PROFI` | `1024` | `PROFI` | the Profi set |

Also in each machine ini: `[MISC] RESET=DOS` (boot straight into TR-DOS) and `[AUTOLOAD] diskA=C:\coemu\probe\ctprobe.trd`.
Read the `[ROM.*]` sections of that version's ini to find the ROM set names; a machine whose ROM file is missing
or which the version does not know is `skipped` with the reason. Key names differ slightly between versions (NS
writes `KEY = value ; comment`); keep each file's own style.

The probe must run at 3.5 MHz. If it stops with "The CPU ran faster than 3.5 MHz", switch the machine's turbo off
(for the ATM: the 3.5 MHz preset, or the BIOS setting) and run again; if that is not possible, the result is
`error` with that message.

## 6. Run one machine

1. `cd C:\coemu\run\<id>` and start `unreal.exe -i <machine>.ini` (the `-i` option selects the ini file; a file
   name on the command line is loaded as a disk). Start one emulator at a time.
2. At the TR-DOS prompt `A>`, type `RUN` and Enter. Either send the keys to the emulator window (PowerShell
   `WScript.Shell` `SendKeys`, the window focused), or put `RUN` + newline on the clipboard and press
   `ALT SHIFT INS` (Unreal's "paste text" key, `main.pastetext` in the ini).
3. Press `NUMLOCK` (`main.maxspeed`) to run at maximum speed. The probe takes a few thousand frames.
4. Wait until the screen shows `ALL VALUES AS EXPECTED` or `VALUES WRONG` (take screenshots with `ALT F8`,
   `main.screenshot`; they go to `ScrShotDir`, PNG). Give up after 20 minutes of real time: result `error`
   ("did not finish").
5. Dump the results: press `ESC` (the monitor, `main.monitor`), then `ALT W` (`mon.saveblock`), choose "to
   binary file", file name `<machine>.bin` (the field is short: keep the name short, it is saved in the current
   folder), start `8CA0`, end `B1C3`. Press `ESC` to leave the monitor, then close the emulator.
6. Check and compare: `python C:\coemu\probe\ctprobe-compare.py <machine>.bin C:\coemu\probe\ctprobe.sym >
   <machine>.compare.txt`. Exit code 0 = all as expected, 1 = values differ, 2 = bad dump (DONE not 1, too short,
   or too fast), 3 = the probe could not measure on this machine.
7. Keep the last screenshot as `<machine>.png`.

## 7. What to report

Put everything under `C:\coemu\out\<id>\` in exactly this layout (it is the unreal-ng co-emulation harness's
layout, so the results can be merged into its compatibility matrix):

| File | Content |
|:--|:--|
| `<machine>.bin` | the 9508-byte dump |
| `<machine>.compare.txt` | the compare script's output |
| `<machine>.result` | one line: `ok ALL VALUES AS EXPECTED`, `wrong <the compare script's last line>`, `error <why>` or `skipped <why>` |
| `<machine>.png` | the final screen |
| `<machine>.log` | what you did: ini keys changed, how `RUN` was typed, how long it ran, anything unusual |
| `build.txt` | source URL, revision, build command, compiler version, exe SHA-256, build warnings/errors summary |

Then write `C:\coemu\out\summary.md`:

1. A table, one row per id, one column per machine (`pentagon scorpion profscorp atm710 atm3 profi`), each cell
   `ok`, `wrong: <n> values wrong in <m> checks`, `error: <why>` or `skipped: <why>`.
2. For every `wrong`: the first lines of `compare.txt` (the machine line with class and onset, and the first
   three differing checks), and one sentence on what the difference looks like (the compare script says when a
   whole row is shifted by some ticks).
3. For every `error` and build failure: the reason in one sentence.

Zip `C:\coemu\out` as `unreal-speccy-ctprobe-<date>.zip` and hand it back. Do not interpret the differences
further than that; the unreal-ng side will explain them against the hardware.

## Notes

- Expected outcome for orientation (not a target to reach): a plain Pentagon should report the class "no
  contention" and pass; the Scorpion presets turn on `EvenM1`, which the probe detects and measures.
- Never edit the emulator sources or its stock ini beyond the keys listed in section 5; every key you had to
  change must appear in `<machine>.log`.
- Use at most half of the machine's CPU cores for builds (`/m:4` or fewer).
