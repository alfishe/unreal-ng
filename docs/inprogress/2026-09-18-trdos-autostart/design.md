# TR-DOS Disk Autostart — Design

| | |
|---|---|
| **Status** | Implemented 2026-09-18 (see §0). Owner decisions folded in, see §2 |
| **Feature** | Opening a disk image (menu, drag'n'drop, automation) starts the software on it, like inserting a disk into a real Beta 128 machine and pressing reset |
| **Related** | [fast disk loading](../2026-09-16-fast-disk-loading/design-r2-fdc-integrated-trap.md) (makes the autostarted load quick) |

## 0. Implementation status (2026-09-18)

Implemented and tested with the real TR-DOS ROMs. Uncommitted.

| Piece | Where |
|---|---|
| Catalog parser | `core/src/emulator/io/fdc/trdoscatalog.{h,cpp}` |
| Boot injector (generated one-line boot, bundled commander, virtual/`markClean`) | `trdosbootinjector.{h,cpp}` |
| Autostart service: capability, plan, prepare, one-shot `$02EF` name hook | `diskautostart.{h,cpp}`, hook in `Z80::Z80Step`, owned by `Core`, `EmulatorContext::pDiskAutostart` |
| Reset into TR-DOS: `Core::Reset(ROMModeEnum)` (existing RESET=DOS machinery) | `cpu/core.cpp` |
| Emulator API: `Emulator::AutostartDisk(path)` (LoadDisk + plan + inject + quick reset to DOS + arm hook), posts `NC_DISK_AUTOSTART` | `emulator.cpp/.h`, `platform.h`, `notifications.h` |
| HUD toast for the outcome / errors | `unreal-qt/src/hud/core/hudmodel.cpp` |
| Bundled commander (Unreal Speccy `boot.$b`) | `data/boot/boot.$b`, copied by every dev/package rule (unreal-qt, videowall, suite macOS/Windows/Linux, tests); runtime lookup: executable dir, then `FileHelper::GetResourcesPath()` |
| Qt: menu *Machine -> Autostart Disks* (persisted, default on), Shift+drop = mount only, no emulator -> Pentagon 128K, paused/stopped emulator -> mount only | `mainwindow.cpp/.h`, `menumanager.cpp/.h` |
| CLI `disk insert A <file> autostart`, WebAPI `{"path":..., "autostart":true}` (drive A only, default off) | `cli-processor-disk.cpp`, `tape_disk_api.cpp` |
| Tests | `core/tests/emulator/io/fdc/diskautostart_test.cpp` (18 tests) |

What the tests prove (real ROM, real disk images, frames run through the actual TR-DOS cold start):

- **Disk with `boot.B`** (`atarin.trd`): reset into DOS -> TR-DOS runs `RUN "boot"` by itself -> the boot program is loaded at `PROG`.
- **Single BASIC file, no boot** (`across_the_edge_by_demarche.trd`): the one-shot hook rewrites the cold-start line to `RUN "ACROSS"`; the program is loaded and the disk is **untouched** (no injection, not dirty, hook disarmed). A negative control (hook disarmed) does *not* load it, so the hook is what makes it work.
- **Several BASIC files, no boot** (`zx-format8.trd` with `boot` deleted): the bundled commander is injected as `boot.B` (27 sectors, valid CRC), image stays **not dirty**, TR-DOS runs it.
- Works on **Pentagon, Scorpion, ATM710, ProfScorp**.
- **Machines without a TR-DOS ROM (48K, 128K in the stock configs)**: mounted only, HUD/log "This machine has no TR-DOS".

Not done / limits:

- **ATM3 (ZX-Evo BaseConf)**: tried, not possible with the direct call. It runs EVO-DOS (page 29 of the 512K ROM, different reset code, no `RUN "boot"` builder at `$027B`), and the BaseConf reset goes through the service-ROM menu. Reset-to-DOS with window 0 on page 1/9/13/29 never reached a booted disk. It is reported as unsupported ("ZX-Evo boots disks through the BaseConf menu"). **Workaround proposal**: drive the BaseConf boot menu (service ROM, "TRDOS" item) by key injection for this one model, or implement the classic-TR-DOS page selection once the BaseConf memory manager is fully mapped; either is a separate task.
- **Profi, KAY, Quorum, Phoenix, LSY256**: the models cannot be created in the current build/test harness (the known non-creatable-model behaviour), so the entry could not be verified. The ROM hook signature is present in their ROMs; expected to behave like Pentagon.
- **Qt UI** (drag'n'drop, Shift modifier, menu) is compiled and wired but was not exercised by hand; only the core paths are covered by automated tests.
- The `IsNameHookSupported()` fallback (generated one-line boot when the `$027B` signature does not match the DOS ROM) is unit-tested at the injector level (`Injector_GeneratedBootIsVirtual`), but no bundled ROM triggers it.
- Unsaved-changes prompts (exit / replacing a dirty disk) are **not** added; no such prompts exist in the app today. Injected boots are never dirty, so they would not trigger them anyway.
- `Emulator::LoadDisk` is unchanged (pure mount).

Full `core-tests`: 3220 passed, 4 failed. The 4 failures are pre-existing and unrelated (three `ATM710CpmBoot_Test.*` and `EmulatorManager_Test.CreateEmulatorWithNonCreatableModelReportsReason`).

## 1. Goals

1. **Autostart.** When a TR-DOS disk is opened while an emulator is running, check whether the current machine can run TR-DOS. If yes, quick-reset and enter TR-DOS by a direct call; TR-DOS runs the disk's boot program itself. If not, report the problem (log + HUD).
2. **Virtual boot file.** If the disk is TR-DOS formatted and has no `boot` file:
   - no BASIC (`B`) files: only mount;
   - exactly one `B` file: start it automatically with `RUN "<name>"` through a one-shot ROM hook (§9.3), no disk change; fallback: a tiny generated boot;
   - several `B` files: inject the bundled lightweight commander as `boot.B` into the in-memory image and run it, while keeping the disk marked **unmodified** so no "unsaved changes" prompt appears because of it.
3. Never surprise the user: it must be switchable off, and scripts/automation must not change behaviour unless they ask for it.

## 2. Owner decisions (r1)

| # | Decision |
|---|---|
| D1 | **No emulator running at all** -> create Pentagon 128K, mount, autostart. **An incompatible emulator is already running** -> no switch, no reset: error in the log and on the HUD ("This machine has no TR-DOS"), disk is still mounted if the machine can hold it |
| D2 | Disk with **no BASIC files**: just mount (no reset, no injection) |
| D3 | **Several BASIC files, no `boot`**: inject a boot file that is a **lightweight disk commander** and run it. Source: the boot file that ships with **Unreal Speccy** (`cfg/boot.$b`, see §8.5). It is copied into the resources of every platform package and resolved from there at runtime |
| D4 | Entry into TR-DOS by a **direct machine-code call** (no key injection). Recipe in §9.3 |
| D5 | **Quick reset when autorunning**. If no emulator is running, just mount |
| D6 | **Single BASIC file**: prefer `RUN "<BASICNAME>"` if it can be triggered directly (it can, §9.3). Otherwise a minimal injected boot that only starts that BASIC |

## 3. Glossary

- **TR-DOS**: the disk operating system of the Beta 128 disk interface. Its ROM lives at `$3D00-$3DFF` (entry) and is paged in over the normal ROM.
- **Catalog**: the list of files on a TR-DOS disk (track 0, sectors 1-8, 16 bytes per file).
- **`B` file**: a BASIC program. **`boot.B`**: the BASIC program TR-DOS runs when you type `RUN` with no name.
- **Autostart line**: the line number a BASIC program starts at, stored after the program's end marker.
- **Virtual boot file**: a `boot.B` added to the in-memory disk only (never written to the user's file unless they save).
- **Dirty**: "the image was changed since it was loaded/saved". Drives the "Save Disk *" menu item and (to be added) unsaved-change prompts.

Worked example: the user drags `game.trd` (one BASIC loader `GAME` and some CODE files, no `boot`) onto a running Pentagon 128K. The machine quick-resets, the in-memory disk gets a tiny virtual `boot.B` (`RANDOMIZE USR 15619: REM : RUN "GAME"`), TR-DOS starts by itself, reads `boot`, and the game loads. Closing the window does not ask to save the disk. The same drop onto a 48K machine without a TR-DOS ROM only mounts the disk and shows "This machine has no TR-DOS" on the HUD.

## 4. Research summary

Full reference reading was done on ZXMAK2, xpeccy-plus/Xpeccy, unreal-speccy, UnrealSpeccyP, plus ZX-M8XXX, Spectral and others, and on the web.

| Emulator | Autostart | Method | Boot file handling |
|---|---|---|---|
| **xpeccy-plus** | Yes (`autorun` option, default on; `--autostart` / `--no-autostart`; Shift+drop = ask Run/Mount) | Drive A only. Switch machine (asks *Switch / Keep / Cancel*), **reset to 48K BASIC**, type `RANDOMIZE USR 15619: REM: RUN` with named ZX keys. Waits for the ROM keyboard scan, then 150 settle frames; gives up after 1500 frames. Per-machine action table (ZX48/128/Pentagon/Scorpion/Profi: 48K + keys; TSLAB: 128 menu; Pentevo: service ROM key `s`; +2A: none) | `loadBoot` (option `addboot`, default on) scans the catalog for `"boot    B"`; if absent, loads a Hobeta boot file from a configured path and sets `changed = 0` (**not dirty**) |
| **UnrealSpeccyP** | Yes (`auto play image`, default on) | Resets. 48K: pages TR-DOS ROM in directly (no keys). 128K + bootable: 48 BASIC ROM + frame-timed key macro. 128K + no boot: reset into the service ROM and press Enter twice (Max Petrov's menu selects the first file) | `Bootable()` = `"boot    B"` entry exists (unknown format counts as bootable) |
| **unreal-speccy** | No | Only manual `Reset to TR-DOS` and NMI-to-DOS | `[BETA128] BOOT=` option: `FDD::addboot()` validates TR-DOS disk (sector-9 signature, disk type, no existing boot) and injects a boot file into the in-memory disk |
| **ZXMAK2** | No | User must reset into TR-DOS | none |
| **ZX-M8XXX** | Yes, best match to this design | Mode *none*: reset, page TR-DOS ROM in at `$0000`, `PC=0`; TR-DOS runs `boot` itself. Modes *run first/last*: picks first/last `B` file (skipping `boot` unless it is the only one), boots TR-DOS, types `RUN "name"`. Requires drive A, Beta available and a TR-DOS ROM, otherwise only mounts | `boot-manager.js`: add / replace / none / run first / run last; works on the TRD bytes at load time; temporarily hides an existing boot entry (first byte `0x01`) on a working copy |
| **Fuse** | Feature request #90 | "Supplant boot.b": inject a minimal BASIC loader `1 RANDOMIZE USR 15619: REM : RUN "program"` running the first BASIC program; `--no-auto-load` to disable | Caveats noted: injection needs a real TR-DOS parser, some games already contain renamed boot files, must not corrupt layouts |
| Spectral | No (documented TODO) | "autoboot = page TR-DOS ROM in and jump to 0" | |

Key facts confirmed:

- **`RANDOMIZE USR 15619: REM: <command>`** enters TR-DOS at `$3D03` and runs the TR-DOS command after the `REM` (`RUN` with no name runs `boot`). This is the standard, machine-independent trick (Fuse, xpeccy-plus, TR-DOS documentation). `RANDOMIZE USR 15616` (`$3D00`) only enters TR-DOS to its prompt.
- **Direct entry** (page TR-DOS ROM in, `PC=0`) needs no keyboard timing and is used by ZX-M8XXX and Xpeccy's reset-to-DOS. ROM analysis in §9.2 shows why it works and that TR-DOS then runs `RUN "boot"` unconditionally.
- Nobody stores the *autostart line* in the catalog: it is appended to the BASIC file body (`0x80`, `0xAA`, line low, line high).

Web sources:
- [Fuse feature request #90: supplant boot.b on Beta 128](https://sourceforge.net/p/fuse-emulator/feature-requests/90/)
- [Sinclair Wiki: TR-DOS filesystem](https://sinclair.wiki.zxnet.co.uk/wiki/TR-DOS_filesystem)
- [TR-DOS 5.03 disassembly (programandala-net)](https://github.com/programandala-net/tr-dos)
- [Kaitai TR-DOS image format](https://formats.kaitai.io/tr_dos_image/)

## 5. What exists in unreal-ng today

- **One sink for opening disks**: `Emulator::LoadDisk` (`emulator.cpp:1454`), used by Qt `MainWindow::loadFile` (menu and drag'n'drop), CLI, WebAPI and GDB. It mounts into drive A only and does **no reset, no autostart, no model switch, no key input**. It replaces the old image without any prompt.
- **Models**: TR-DOS availability is decided by ROM set, not by a flag: `Memory::dosAvailable = base_dos_rom && base_sys_rom` (`memory.cpp:814`). The WD1793 is always created. Real TR-DOS paths exist for Pentagon, Profi, Scorpion/ProfScorp (own DOS trigger), ATM710/ATM3, KAY and others; 48K / plain 128K depend on ROM files (to verify).
- **Forcing TR-DOS**: the ROM-mode switch (`RM_DOS`, `memory.cpp` ~750) pages the DOS ROM in programmatically and refuses if `!(trdos_present && dosAvailable)`. `Memory::UpdateZ80Banks()` re-derives `CF_TRDOS` etc.
- **No runtime model switch**: switching = destroy and recreate the emulator (`MainWindow::handleMachineModelChangeRequested`, `EmulatorManager::CreateEmulatorWithModel[AndRAM]`). The mounted disk is lost, so the caller must `LoadDisk` again on the new instance.
- **No catalog parser** for TR-DOS. No typing/macro API (`Keyboard::TypeSymbol` is an empty stub); tests press keys with `PressKey` + `RunNFrames` and must wait until the ROM keyboard loop is live.
- **Dirty tracking** is per image (`DiskImage::isDirty`, `markClean`). Guest writes mark tracks dirty (WD1793). `Track::getDataForSector()` returns a raw pointer: **writing through it does not set dirty**. Loaders call `markClean()` after load.
- **No unsaved-changes prompt exists anywhere** (exit, model change, or replacing a disk). The model-switch dialog only says "unsaved state will be lost". Menu shows "Save Disk *" when dirty.
- `Emulator::Reset()` keeps disks mounted.

## 6. Architecture

```
 drop / File>Open / CLI --autostart
              |
        DiskAutostart::Plan(emulator, image, options)   <- pure decision, unit-testable
              |
   +----------+-----------+----------------------+
   | capability check     | catalog analysis     |
   | (TR-DOS ROM, model)  | (TrdosCatalog)       |
   +----------+-----------+----------+-----------+
              |                      |
     Plan { action, createPentagon?, bootInjection?, entryRecipe }
              |
     executor (UI thread + emulator thread)
       1. no emulator -> create Pentagon 128K; incompatible emulator -> log + HUD error, stop
       2. LoadDisk
       3. TrdosBootInjector::Ensure(image)   (in-memory, markClean)
       4. quick reset + direct TR-DOS entry (§9.3)
```

New components (all under `core/src/emulator/io/fdc/` or `core/src/loaders/disk/`):

- **`TrdosCatalog`**: read-only parser of a `DiskImage` (§7).
- **`TrdosBootInjector`**: adds the virtual `boot.B` (§8.4).
- **`DiskAutostart`**: planning + execution (§9).
- **`TrdosCapability`**: answers "can this emulator run TR-DOS?" (§9.1).

Policy lives outside `Emulator::LoadDisk`: `LoadDisk` stays a pure "mount" (automation and tests depend on that). Autostart is a separate call made by the UI layer, and by automation only when requested.

## 7. TR-DOS catalog parser (`TrdosCatalog`)

Layout (from the Sinclair wiki / Kaitai / TR-DOS docs; **verify offsets against sample images in tests**):

- Track 0 sectors 1-8 (0-based 0-7): up to 128 entries of 16 bytes:

  | Offset | Size | Field |
  |---|---|---|
  | 0-7 | 8 | name, space padded. First byte `0x00` = end of catalog, `0x01` = deleted |
  | 8 | 1 | type: `B` BASIC, `C` code, `D` data, `#` stream, other |
  | 9-10 | 2 | start address / program length incl. variables (BASIC) |
  | 11-12 | 2 | length (BASIC: program length without variables) |
  | 13 | 1 | length in sectors |
  | 14 | 1 | first sector |
  | 15 | 1 | first track |

- Sector 9 (0-based 8) info: `0xE1` first free sector, `0xE2` first free track, `0xE3` disk type (`0x16`-`0x19`), `0xE4` file count, `0xE5-E6` free sectors, `0xE7` TR-DOS id `0x10`, `0xF4` deleted count, `0xF5-FC` label.
- **BASIC autostart line**: at the end of the file body, after the `0x80` end marker: `0xAA`, line low, line high. Sizes in the catalog do not include these 4 bytes; the sector count does.

API sketch:

```cpp
struct TrdosFile { std::string name; char type; uint16_t start, length; uint8_t sectors, firstSector, firstTrack; bool deleted; int autostartLine /* -1 = none */; };
class TrdosCatalog {
 public:
  static bool IsTrdos(const DiskImage&);               // geometry + id 0x10 at 0xE7 + sane type
  static std::optional<TrdosCatalog> Parse(const DiskImage&);
  const std::vector<TrdosFile>& files() const;
  const TrdosFile* FindBoot() const;                   // name "boot    ", type 'B', not deleted
  std::vector<const TrdosFile*> BasicFiles() const;    // non-deleted type 'B'
  uint16_t freeSectors() const;
};
```

Non-TR-DOS images (CP/M, +3 DSK, MGT) fail `IsTrdos` and are left alone (out of scope, see §12).

## 8. Autostart policy by disk content

### 8.1 Decision table

Non-deleted entries only; `boot` means the exact 8-byte name `"boot    "` with type `B`.

| Disk | Action |
|---|---|
| Not TR-DOS format | Mount only |
| Has `boot.B` | Boot it as is |
| No `B` files | Mount only (D2) |
| Exactly one `B` file, no `boot` | Quick reset + direct entry, the one-shot hook turns the cold-start `RUN "boot"` into `RUN "<name>"` (§9.3); disk untouched. Fallback if the hook cannot be applied: tiny generated `boot.B` (§8.2) |
| Several `B` files, no `boot` | Inject the bundled Unreal commander as `boot.B`, boot it (§8.3) (D3) |

"Boot it" always means the same thing: quick reset and direct entry into TR-DOS (§9.3). The direct entry (plus the one-shot name hook, or the injected boot file) is what makes TR-DOS run the right thing, so **no key typing is needed anywhere**.

### 8.2 Single BASIC file

**Preferred (D6): no injection.** Named run through the one-shot hook (§9.3): TR-DOS cold-starts, builds `RUN "boot"`, our hook rewrites it to `RUN "<NAME>"`, TR-DOS loads and starts that BASIC program. The disk is never touched, so nothing can become dirty.

**Fallback: generated minimal boot**, used only if the hook conditions are not met (ROM signature mismatch). One BASIC line:

```
1 RANDOMIZE USR 15619: REM : RUN "NAME"
```

Tokenised (`RANDOMIZE` `$F9`, `USR` `$C0` with the hidden 5-byte number, `REM` `$EA`, `:`, `RUN` `$F7`), followed by `0x80`, `0xAA`, line 1; a normal `B` entry named `boot    `, 1 sector, injected like §8.4 and marked clean. Names containing `"` are skipped (mount only).

### 8.3 Several BASIC files: bundled commander

Injected as `boot.B` exactly as shipped: a Hobeta file (17-byte header + 27 sectors). The injector strips the header and uses the header's name/type/start/length/sector count for the catalog entry.

### 8.4 Injection mechanics (`TrdosBootInjector::Inject(DiskImage&, BootSource)`)

Modelled on Unreal Speccy's `FDD::addboot()` + `addfile()` (`unreal-speccy/wldr_trd.cpp:169-215`, cp1251 source; same code in the ZX-Evo fork). Its checks, which we keep:

1. Disk is TR-DOS: sector 9 readable, sector size 256, byte 0 of the info sector is 0, TR-DOS id `0x10` at `0xE7`, the 9 reserved bytes at `0xE9-0xF1` all spaces or all zero, disk type one of `0x16-0x19` (80/40 tracks, 1/2 sides).
2. None of the 128 directory entries starts with `"boot    B"` (9 bytes). (Deleted `boot` entries: their first byte is `0x01`, so they do not match and injection proceeds.)
3. Free sectors (`0xE5-E6`) >= the boot file's sector count.
4. Add the 16-byte entry at slot `file count` (14 bytes copied from the header, then first sector/track from `0xE1/0xE2`).
5. Write the file sectors starting at the first free sector/track, moving to the next track when a track is full. Update in the info sector: first free sector/track, file count + 1, free sectors - count.
6. Write sectors **with valid data-field CRC** through the existing sector-write helper (raw-pointer writes skip the CRC).
7. `DiskImage::markClean()` (image, track and per-sector flags) and remember `virtualBootInjected` on the image.

Result: `isDirty()` is false, no "Save Disk *", no prompts. If the user modifies the disk later, the injected boot is saved together with those changes ("user can still save with boot injected"); `Save As` includes it too. The guest sees `boot` in `CAT`; it uses 27 sectors (documented, switchable).

Notes versus Unreal: Unreal only appends at the end of the directory (slot = file count) and does not reuse deleted slots; we do the same for simplicity and to keep the info-sector accounting identical.

### 8.5 The bundled boot file (Unreal Speccy's)

- **Source**: the `plain unreal-speccy` tree contains the injector but not the file (its `x32/unreal.ini` sets `BOOT=boot6.$b`, shipped only in the binary distribution). The ZX-Evo Unreal fork in this workspace **does** ship it: `zx-evo/pentevo/unreal/Unreal/cfg/boot.$b`, configured as `[BETA128] BOOT=boot.$b` and also used as `diskA=boot.$b` autoload in `Unreal.ini`; docs (`unreal_e.txt`): "if disk image does not contain boot.B, it may be appended".
- **Format**: Hobeta, 6929 bytes = 17-byte header + 27 sectors (6912 bytes). Header name `boot    `, type `B`. It is a tiny BASIC loader (`CLEAR ... RANDOMIZE USR ...`, ends with `jp $3D13` into TR-DOS service calls) followed by machine code.
- **What it is**: a disk manager/commander (strings seen: Rename, Change disk, New title, Move, Insert, SvCat, "NO DISK", "from A ..."). This matches the "lightweight commander" requirement; to be confirmed by running it on a multi-file disk (Phase 1).
- **Alternative seen, not used**: xpeccy-plus `config/boot.$B` (Dimon boot 2024, 7 sectors). Keep as fallback if the Unreal one misbehaves.
- **Location in the repo**: `data/boot/boot.$b` (lower-case like Unreal's; the Hobeta header holds the TR-DOS name, so the host file name is irrelevant).

## 9. Autostart execution

### 9.1 Capability check (`TrdosCapability`)

An emulator "can run TR-DOS" when the model has a TR-DOS ROM set (`Memory::dosAvailable`), `config.trdos_present` is on, and the model's entry recipe (§9.3) is implemented. Result: `Supported` or `Unsupported(reason)`.

### 9.2 Why a direct call works (ROM analysis)

Verified by disassembling the bundled ROMs (`trdos503.rom` 5.03, `trdos504t.rom` 5.04T, `trdos.rom` 5.05D, `trd504tm.rom` 5.04TM, `dos.rom` 5.04F). The entry code at `$0000`, `$0114`, `$0239`, `$3D00-$3D40` is byte-identical in all of them (I re-checked `$0114 = C3 31 3D`, `$3D30 = C9`, `$3D31 = CD 21 3D E5 C3 39 02` and the `AA` test at `$0239`). The 5.04TM differs only in the "initialised" magic byte address.

- **DOS ROM paging**: an opcode fetch (M1) at `$3D00-$3DFF` while the 48K BASIC ROM is selected pages the TR-DOS ROM in; a fetch at `PC >= $4000` pages it out.
- **`$3D2F` = `NOP`, `$3D30` = `RET`**: the classic gate. Jumping there pages TR-DOS in, and `RET` returns to whatever is on the stack.
- **Reset in DOS mode (PC = 0)**: TR-DOS re-initialises RAM/system variables itself (`$0000-$0113`), then `$0114: JP $3D31` -> `$3D21` (probe/init) -> `$0239` (cold/boot entry).
- **`$0239`**: if the flag byte at `$5D17` is not `$AA` it sets it, prints the banner, and if the magic byte (`$5B00`, `$5CB0` in 5.04TM) equals `$AA` it builds the BASIC line `RUN "boot"` (bytes `F7 22 62 6F 6F 74 22 0D 80`) in `E_LINE` and falls into the command dispatcher `$030A`. RUN's handler (`$1D4D`) loads the file and, for a `B` file, jumps to `$012A` (start BASIC). A missing `boot` gives the normal TR-DOS error and prompt.
- **Bare `RUN` = boot**: the code at `$027B` (reached from cold start, and from the RUN handler when no argument is given) is what builds `RUN "boot"` in `E_LINE`: it writes `F7 22 62 6F 6F 74 22 0D 80` at `(E_LINE)`, sets `K_CUR` (`$5C5B`) to the `0D`, and `WORKSP/STKBOT/STKEND` (`$5C61/$5C63/$5C65`) to the byte after the `80`, sets bit 3 of `FLAGS`, then `JR $02EF` into the command loop. This 51-byte sequence (`$027B-$02AE`) is **byte-identical in every TR-DOS ROM I checked**: 5.03, 5.04T, 5.04TM, 5.04F, 5.05D, 6.10e and the clone ROMs (Pentagon, Profi, Scorpion, ATM1/2, KAY, LSY, Quorum, GMX, TS-BIOS, ZX-Evo). Only one ZX-Evo bank variant differs.
- The RAM fill at `$002B` leaves `$5D17` != `$AA` after a fresh `PC = 0` entry, so **the boot branch is always taken after a cold entry** (a warm `USR 15616` skips it because `$5D17` is already `$AA`).
- **Scorpion ROM's trick**, the one you remembered: its 128 menu item "TR-DOS" does `LD HL,0 / PUSH HL / LD HL,$3D30 / JP $1B4A` (the `$1B4A` stub selects the ROM and `RET`s to `$3D30`). So the CPU executes `RET` at `$3D30` (DOS pages in) which pops `0000`: **PC = 0 with the DOS ROM active** -> cold start -> `RUN "boot"`. No keys, no sysvars. The Scorpion LW ROM even cancels the autoboot when Symbol Shift is held (a possible future modifier: "hold Shift while dropping = mount only", which we already plan).
- Other 64K clone ROMs (Profi, Pentagon, ATM2) end in the same `JP $3D31`.

### 9.3 Entry recipe (the direct call)

**Recipe A - reset into TR-DOS (primary; what ZX-M8XXX and Xpeccy's reset-to-DOS do):**

1. Disk already mounted in drive A (autostart is drive A only).
2. `Reset()` (quick reset, RAM cleared/pattern-filled by the TR-DOS init itself).
3. Select the **48K BASIC ROM** as the background ROM (128K-family: `7FFD` bit 4 = 1; ATM/Scorpion per their own mapping).
4. Set the DOS-ROM-active state through the existing ROM-mode switch (`RM_DOS`-style; sets `CF_TRDOS` and updates banks), so we do not depend on a fetch at `$3Dxx`.
5. `PC = 0x0000`, interrupts as after reset. TR-DOS does the rest.

**Recipe B - hardware-faithful equivalent (no state forcing):** run one instruction pair from a stub: stack `0000`, then `JP $3D30` (or `$3D2F`). The `$3Dxx` fetch pages DOS in, `RET` lands on `$0000`. Useful for models whose ROM-mode switch is awkward (ATM710, Scorpion): set `SP` to a safe RAM address, write `00 00` there, `PC = $3D30`, ROM 48K selected. Must be validated per model in tests.

**Named file without `boot` (D6): one-shot hook.** The cold start hard-codes `RUN "boot"`, but it builds that line in `E_LINE` right before entering the command loop at `$02EF`. The emulator arms a one-shot hook for the autostart and fires it at the first arrival at `PC = $02EF` with the DOS ROM active:

1. Check the line is what we expect: bytes at `(E_LINE)` are `F7 22 62 6F 6F 74 22 0D 80`. If not, do nothing and disarm (fallback to the generated boot).
2. Rewrite it as `F7 22 <NAME> 22 0D 80` (name up to 8 chars, trailing spaces trimmed, no `"`), and set `K_CUR` (`$5C5B`) to the new `0D`, and `$5C61/$5C63/$5C65` to the byte after the `80`, mirroring what the ROM code at `$0298-$02A7` does.
3. Disarm. TR-DOS continues: `$02EF` -> `$030A` dispatcher -> RUN handler -> load `NAME` -> start BASIC (`$012A`).

Why this is safe: the workspace after `E_LINE` is empty at that moment (13 bytes instead of 9), the stack is far away, the signature check makes it a no-op on anything unexpected, and it is armed only during an autostart (a normal user `RUN` is never touched). It gives exactly "RUN \"<BASICNAME>\"" without keys and without touching the disk. The same hook point can also serve future features (for example running a chosen file of another type, if a loader line is built).

Per-model application (verify each experimentally, Phase 1):

| Model | Recipe |
|---|---|
| Pentagon 128/512/1024 | A |
| Scorpion / ProfScorp | B (matches the ROM menu code) or A |
| Profi, KAY | A |
| ATM710 / ATM3 | classic TR-DOS must be selected, not the ATM sys-BIOS launcher (see the ATM710 TR-DOS boot test); A or B, to verify |
| 48K / 128K + Beta ROM (if a DOS ROM is present) | A |
| Models without a DOS ROM (`dosAvailable == false`), +2A/+3 | unsupported (§9.4) |

Caveats: the 5.04TM magic byte lives at `$5CB0` (its own init writes it, so Recipe A is fine, but do not pre-patch RAM by hand); in 128K mode with ROM 0 selected a fetch at `$3Dxx` does not page DOS in (Recipe A sets state directly; Recipe B needs ROM 1 selected).

### 9.4 Flow (D1, D5)

```
Open disk (menu / drop / CLI autostart flag)
  |
  +- no emulator running ---------------> create Pentagon 128K, LoadDisk, autostart
  |
  +- emulator running
       +- not TR-DOS capable ----------> LoadDisk (mount) only; log error + HUD "This machine has no TR-DOS"
       +- TR-DOS capable
            +- autostart off / mount-only gesture -> LoadDisk only
            +- autostart on ------------> LoadDisk, decision table (§8.1),
                                          inject boot if needed, quick reset, Recipe A/B
```

- "Quick reset": the normal `Emulator::Reset()` path (keeps mounted disks), TTD session invalidated as `LoadDisk` already does; no dialogs.
- "Not running": no emulator instance exists (the app just started or it was closed). "Just mount" from D5 applies when the disk is opened without autostart (setting off or mount-only gesture); with nothing running, D1 creates the Pentagon.
- There is **no model switching** for an already running incompatible machine (decision D1), so the destroy/recreate path and disk carry-over problem only apply to the "no emulator" case, where nothing is lost.

### 9.5 Autostart controller

Because both recipes are direct, the controller is tiny: it runs on the emulator thread, performs reset, sets state (Recipe A) or the stub (Recipe B) and finishes. No frame-timed state machine, no key scripting, no turbo. It still guards: drive A only, aborts if the emulator is being destroyed, and posts a notification (`NC_FILE_LOADED` follow-up / HUD toast) with the outcome.

## 10. Interfaces and settings

- **Qt**: `loadFile` (menu + drag'n'drop) calls the autostart service after `LoadDisk` when enabled. Shift+drop = mount only. Menu: *Machine -> Autostart disks* and *Inject boot file when missing* (checkable). HUD messages: "Autostart: GAME", "This machine has no TR-DOS", "Mounted".
- **Settings/feature keys**: `autostart_disk` (default on), `disk_virtual_boot` (default on); CLI `--autostart` / `--no-autostart`.
- **Automation (CLI/WebAPI/MCP/GDB)**: `LoadDisk` stays a pure mount. New optional `autostart=true` parameter and an explicit `disk autostart` command; default off, so scripts and tests do not change.
- **Unsaved-change prompts** (new; today none exist): ask on exit and when a disk replaces a dirty one. Uses `DiskImage::isDirty()`; a virtual boot injection never triggers it. (Model-change prompts matter less now that a running machine is never switched.)
- **Packaging** (bundled boot file): `data/boot/boot.$b` must be copied next to every existing `data/rom` copy rule:
  - `unreal-qt/CMakeLists.txt` ~478-487 (dev output), ~536-540 (macOS DMG staging), ~568-575 (macOS bundle `Resources`), ~598-601 (Windows zip), ~620-627 (Linux tar, directory list is explicit);
  - top-level `CMakeLists.txt`: `DATA_PATH` at line 444; `package_suite_macos` rules ~482-494 (UnrealNG, Screen Viewer and Video Wall bundles), `package_suite_windows` ~575-583, `package_suite_linux` ~685-695;
  - `unreal-videowall/CMakeLists.txt` ~329-341, ~419-422, ~445-446;
  - `core/tests/CMakeLists.txt` ~262-278 (test data copy).
  CI (`.github/workflows/release.yml`) only calls the suite targets, so no workflow change is needed.
- **Runtime lookup**: new helper (e.g. `FileHelper::FindResource("boot/boot.$b")`) following `ROM::LoadROM`: try the executable directory, then `FileHelper::GetResourcesPath()` (macOS `<app>/Contents/Resources`), first hit wins. If not found: log a warning and skip commander injection (single-file generated boot still works).

## 11. Other aspects to consider (things not yet covered)

1. **Drive A only.** `LoadDisk` mounts only into A today (`FIXME`). Autostart must only fire for A; opening into B/C/D just mounts.
2. **Running software is lost by the quick reset** (decision D5). Mitigations already in the design: autostart setting, Shift+drop = mount only, automation defaults to mount only. No confirmation dialog.
3. **No model switch for a running incompatible machine** (D1): only an error/HUD message. Consequence: a user on a 48K machine must switch model manually before autostart works; the HUD text should say which models support it.
4. **TTD (time travel)**: autostart resets, so TTD recording must stop/invalidate exactly like `LoadDisk` does. Fast disk loading is auto-disabled during TTD recording.
5. **Fast disk loading interplay**: autostart benefits from the `fastdisk` trap; both work in TR-DOS mode only. With direct entry there is no keyboard wait, so turbo is optional.
6. **TR-DOS ROM variants**: 5.03 vs 5.04T vs 5.05 handle reset-into-DOS and `boot` differently (xpeccy-plus deliberately avoids reset-into-TR-DOS, claiming it is a 5.04T feature; our disassembly shows the entry code is identical in all five bundled ROMs, only the 5.04TM magic-byte address differs). Validate per bundled ROM anyway.
7. **ATM710 / Scorpion / Pentevo special cases** (BIOS menu, DOS trigger, service ROM); the classic TR-DOS must be selected, not the machine's own launcher.
8. **Multiple `B` files / heuristics**: prefer file with autostart line; consider a menu-loader variant later. Exclude files that cannot be expressed in the loader line.
9. **Existing but unusable boot**: a `boot` entry of type other than `B`, a deleted `boot`, different case or trailing characters. TR-DOS `RUN` only runs a `B` named exactly `boot`.
10. **Catalog full / disk full** (no free slot or sector): silently skip injection, log at info level. Very short disks (40-track) still fine, injection needs 1 sector.
11. **Write-protected drive / read-only files**: injection is in memory only; write protect does not matter until the user saves. Save behaviour for read-only paths follows existing `SaveDisk` retarget logic (writes UDI beside the original when the format cannot hold the change).
12. **Formats**: TRD, SCL, FDI, UDI, TD0 all become TR-DOS geometry in memory; the injector works on the in-memory image, so all of them are covered. SCL round-trips only if the saver rewrites the file list; check `SaveDisk` to make sure a virtual boot is written properly.
13. **Non-TR-DOS disks** (CP/M, +3 DSK, MGT): out of scope now; +3 has its own disk boot (xpeccy-plus `as_menu`). Design should leave a hook for other autostart kinds (tape, snapshot).
14. **Guest visibility**: the injected `boot.B` shows up in `CAT` and consumes a sector. Acceptable (same as Fuse/xpeccy-plus/Unreal) but should be documented and switchable.
15. **Auto-run identity of games that already have a differently named loader**: because we inject only when `boot` is missing, disks with `Boot`/`BOOT` variants that the ROM does not treat as boot may get an injected one. Keep exact-match rule.
16. **User feedback**: toasts, and a log line explaining the decision (which file, which method, why nothing happened).
17. **Determinism for tests**: the decision logic is pure (`Plan()` takes emulator capabilities + catalog) and easy to unit test; the executor is tested with real boots.
18. **Drag'n'drop of several files / disk + snapshot together**: today only the first URL is handled; define behaviour (first wins, or ignore autostart when a snapshot is also present).
19. **Localization / accessibility of prompts** (English strings only for now).
20. **Other drives / multi-disk games**: e.g. games that need disk B. Out of scope; still mount.

## 12. Verification plan (before implementation)

1. **Recipe A/B experiments** in this emulator: Pentagon (5.04T), Scorpion, ATM710, 5.03 ROM. Reset -> DOS ROM active -> `PC=0` (A) and stack `0000` + `PC=$3D30` (B): confirm `RUN "boot"` executes without keys, and what happens with no boot.
2. **Commander check**: mount a multi-file TRD with the injected `boot.$B`; confirm it lists the `B` files and launches one, on 5.03 and 5.04T ROMs.
3. **Catalog offsets**: parse sample `.trd` images from `core/tests/data` and cross-check with an independent tool.
4. **Dirty invariants**: inject, assert `isDirty()==false` (also after `computeDirtyState()` and for the Save menu state); write a sector in the guest and assert dirty becomes true and the injected file survives save/reload.
5. **Sector CRC**: injected sectors pass `isDataCRCValid` and are read back byte-exact by the FDC (with and without `fastdisk`).
6. **Packaging**: build each package target and confirm `boot/boot.$B` is present and found at runtime (dev, macOS bundle, Windows, Linux).

## 13. Phases

1. `TrdosCatalog` + tests (no behaviour change).
2. `TrdosBootInjector` (generated and bundled) + dirty/CRC tests; `data/boot/boot.$B` + packaging rules + resource lookup helper.
3. Recipe experiments and `TrdosCapability`; direct entry for Pentagon; autostart service; Qt hook, HUD messages, no-emulator -> Pentagon path.
4. Scorpion, ATM710/ATM3, Profi/KAY entry recipes; unsaved-change prompts.
5. Automation parameter, settings/menu items, docs.

## 14. Open questions

- **Q1.** Hook robustness: if a model's TR-DOS is not byte-identical at `$027B` (only one ZX-Evo bank variant differed), the design falls back to the generated one-line boot.

Resolved: commander = Unreal's `boot.$b`; single BASIC = named `RUN "<name>"` via one-shot hook (fallback generated boot); disk on command line = "no emulator running" case (D1).
