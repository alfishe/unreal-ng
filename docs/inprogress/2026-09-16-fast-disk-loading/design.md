# Fast Disk Loading (TR-DOS Service Trap) — Design

| | |
|---|---|
| **Status** | r0 2026-09-16 initial draft — ROM bytes and entry table verified in-tree; per-service exit contracts and the private-stack return mechanics flagged for Phase 0 extraction |
| **Date** | 2026-09-16 |
| **Feature** | Switchable fast disk loading via a TR-DOS `$3D13` service-API hook |
| **Affects** | `core/src/emulator/cpu/{z80,core}.{h,cpp}`, `core/src/emulator/io/fdc/diskfastload.h/.cpp` (new), `core/src/base/featuremanager.h/.cpp`, `core/automation/cli/src/commands/cli-processor-settings.cpp`, `core/automation/webapi/src/api/settings_api.cpp`, `unreal-qt/src/{menumanager,mainwindow}.{h,cpp}`, `unreal-videowall/`, `core/tests/emulator/io/fdc/` |
| **Related** | [Fast tape loading](../2026-08-30-fast-tape-loading/design.md) (the sibling ROM-hook design this one mirrors), [TR-DOS ROM forensics](../2026-01-21-trdos-analyzer/trdos-rom-forensics.md), [TR-DOS / 48K ROM interaction analysis](../../../docs/rom/analysis/trdos-rom-interaction-analysis.md), [Universal track model](../2026-09-02-universal-track-model/track-model-design.md) |

## 1. Goals

1. **Instant disk loading.** When enabled, software that loads through the standard TR-DOS machine-code service API at `$3D13` (`LD C,service / CALL $3D13`) — loaders, menu systems, and the internals of BASIC `LOAD`/`RUN` — receives its sectors at host speed: file payloads are copied from the mounted disk image directly into Z80 memory, skipping motor spin-up, head steps and rotational latency entirely.
2. **Switchable.** One runtime switch — the FeatureManager feature **`fastdisk`** (alias `fdisk`, category performance, persisted in `features.ini`, default **on**), exposed as `fast_disk` in the CLI/WebAPI settings surfaces and as Machine → Fast Disk Loading in Qt. Same control plane as `fasttape`/`sound`/`screenhq`.
3. **Safe fallback.** Whenever the trap cannot faithfully serve a service — non-emulated service codes, no disk inserted, file not found, catalog unreadable, non-standard ROM — it declines completely inert: the CPU proceeds into the real TR-DOS ROM and the full WD1793 signal pipeline takes over. No restart, no position loss, authentic error behavior.
4. **TTD-transparent.** Fast loads are deterministic host-side actions (CPU entry state + disk image content + FDC register state in, memory writes out). No replay barriers, checkpoint deltas stay complete.

## 2. Non-goals (v1)

- **Write-path services.** WRITE_SECTORS (`$06`), WRITE_DESCRIPTOR (`$09`), SAVE_FILE (`$0B`), SAVE_BASIC (`$0C`), FORMAT_TRACK (`$15`) run through the real ROM — the disk-image mutation path is not duplicated host-side (same reasoning as fast SAVE on tape).
- **Custom loaders that bypass `$3D13`.** Software banging the WD1793 ports directly (`$1F`/`$3F`/`$5F`/`$7F`/`$FF`) — copy protections, non-standard DOS variants — keeps full FDC emulation. A future `turbodisk` feature (turbo mode while the FDC is busy, the `turbotape` analog) is the natural complement; see §14.1.
- **Fast VERIFY.** Rarely used; declined.
- **Non-5.03/5.04T service ABIs.** The signature gate (§6.1) automatically declines TR-DOS 5.04TM and 6.x layouts (verified byte differences, §4.1) and machines without the Beta 128 interface. Authentic speed for those.
- **Load-duration emulation.** Software that measures load wall-time sees near-zero duration. Same accepted trade-off as fast tape.
- **CAT printing.** `CAT` (`$07`) prints through the SOS-ROM trampoline; declined (authentic output, watchable).

## 3. Background — current disk pipeline

| Component | Role |
|---|---|
| `core/src/emulator/io/fdc/wd1793.h/.cpp` | `WD1793` (= `EmulatorContext::pBetaDisk`): the Beta 128 interface + WD1793 controller. Full command timing: DRQ/INTRQ handshake, `_drive` = currently selected drive index, `_selectedDrive` = `FDD*` |
| `core/src/emulator/io/fdc/fdd.h/.cpp` | `FDD`: mechanics — `MOTOR_STOP_TIMEOUT_MS = 200`, `HEAD_LOAD_TIME_MS = 50`, `DISK_REVOLUTIONS_PER_SECOND = 5` (200 ms/revolution index period) |
| `core/src/emulator/io/fdc/diskimage.h` | Universal track model: `getTrackForCylinderAndSide(cyl, side)` → `Track`; `getDataForSector(idx)` (0-based; ID field number = idx+1). Format-agnostic — TRD, SCL and flux-derived images all present the same surface |
| `core/src/loaders/disk/loader_trd.*`, `loader_scl.*` | Build formatted track models from container images; `LoaderTRD::validateTRDOSImage` is the in-tree precedent for host-side catalog walking |
| `core/src/emulator/io/fdc/trdos.h` | `TRDFile` catalog descriptor, `TRDVolumeInfo`, geometry constants, logical-track numbering comment |
| `core/src/debugger/analyzers/trdos/` | `TRDOSAnalyzer`: breakpoints at `$3D00`/`$3D13`/`$3D1A`/`$030A`, service-code identification (C register), filename extraction from `$5CDD` |
| `Z80::Z80Step()` top (`z80.cpp:194-203`) | Hardware ROM-switch model: on `CF_SETDOSROM` machines an opcode fetch from `$3D00-$3DFF` sets `CF_TRDOS` and banks the DOS ROM in — **this region runs before the trap**, so by trap time the mapped bank 0 *is* the TR-DOS ROM |

Why this is slow today: a real-ROM `LOAD` walks motor spin-up (≤200 ms), per-track head steps, one sector per rotational pass worst case, per-sector DRQ polling — seconds of emulated time per file, spent at whatever wall-clock speed the machine runs. The trap removes exactly this mechanical latency, exactly like the tape trap removes pilot tones.

In-tree evidence of the `$3D13` calling convention being *the* standard load path (90 % coverage claim):

- `docs/disasm/software/tfmplayer/tsfm-sna-guide.md:212` — `LD A,1 / LD HL,addr / LD DE,0 / LD C,0Eh / CALL 3D13h` (service `$0E` LOAD_FILE)
- `docs/disasm/software/wildplayer/wildplayer_body.asm:172,7601` — `call 03d13h` (sector-level loading via `$05`)
- `docs/inprogress/2026-01-21-trdos-analyzer/trdos-rom-forensics.md` §1.3, Appendix B — "1 user command → many low-level `$3D13` service calls"; a game's loader reads its data with `C=$05` loops
- TR-DOS ROM forensics §3.4 — even the injected `RUN "boot"` (bare `RUN` at the `A>` prompt) resolves through the LOAD handler, which issues internal `$3D13` calls — **BASIC-command loads are covered transitively, with no `$3D1A`-level trap needed**

## 4. TR-DOS analysis — the `$3D13` contract

### 4.1 Verified ROM bytes (`data/rom/`, offset `$3D00`)

```asm
      ; TR-DOS entry table — identical in trdos.rom, trdos503.rom, trdos504t.rom
3D00  00 18 2E    NOP;  JR  $3D31     ; main entry (sys vars init + command processor)
3D03  00 18 14    NOP;  JR  $3D1A     ; command execution from BASIC
3D06  00 18 E7    NOP;  JR  $3CF0     ; file input
3D09  00 18 E7    NOP;  JR  $3CF3     ; (companion entry)
3D0C  00 18 E7    NOP;  JR  $3CF6     ; file output companion
3D0F  00 18 E7    NOP;  JR  $3CF9     ; memory change companion
3D13  00 C3 69 2F NOP;  JP  $2F69     ; ← machine-code service API (this trap's anchor)
3D17  CD 21 3D    CALL $3D21          ; (error/companion path)
3D1A  E5 C3 .. .. PUSH HL; JP ...     ; command entry (token at CH_ADD)
```

Version drift (verified): `trd504tm.rom` carries `00 18 E7` at `$3D13` (the `JP $2F69` moved to `$3D17`); `dos6_10e.rom` dispatches via `JP $3449`. The 4-byte signature `00 C3 69 2F` therefore *is* the version gate: it matches exactly the 5.03/5.04T family (the de-facto standard ROMs shipped as `trdos.rom`/`trdos503.rom`/`trdos504t.rom`) and declines everything else inertly — the fast-tape signature philosophy applied in reverse (bank 0 must *be* the DOS ROM, not the SOS ROM).

### 4.2 Service ABI (C register at `$3D13`)

Full table with register parameters: TR-DOS ROM forensics §1.2 / `trdosevent.h` `TRDOSService`. The load-relevant subset:

| C | Service | Entry registers | Authentic cost | v1 |
|---|---|---|---|---|
| `$0E` | LOAD_FILE | A = mode (`0`: original address, `3`: HL), filename at `$5CDD` | motor + catalog scan + seeks + reads | **emulated** |
| `$05` | READ_SECTORS | D = track, E = sector, B = count, HL = destination | motor + seek + per-sector rotational latency | **emulated** |
| `$0A` | FIND_FILE | name at `$5CDD`; returns C = catalog entry, Z = found | catalog read (track 0) | **emulated** |
| `$08` | READ_DESCRIPTOR | A = entry number (0-127) | catalog read | **emulated** |
| `$00` | RESTORE | — | head-step to track 0 | **emulated** (positioning only) |
| `$02` | SEEK_TRACK | A = track | head-step | **emulated** (positioning only) |
| `$16`/`$17` | SELECT_SIDE_0/1 | — | side switch | **emulated** (positioning only) |
| `$01`,`$03`,`$04` | SELECT_DRIVE / SET_SECTOR / SET_DMA | A / A / HL | microseconds — already instant in ROM | decline |
| `$06`,`$09`,`$0B`,`$0C`,`$12`,`$15` | writes / format / internal delete | — | — | decline (§2) |
| `$07` | CATALOG | A = stream | printing via SOS trampoline | decline (§2) |
| `$0D` | EXIT | — | control flow | decline |
| `$13`,`$14` | MOVE_DESC in/out | HL | memory copy — instant | decline |
| `$18` | READ_SYS_SECTOR | — | one sector + var updates | decline (rare; §14.4) |

Emulation criterion: only services whose authentic duration is dominated by mechanics (motor, head, rotation) or whole-track reads are served host-side. Everything already instant, everything mutating the image, and everything printing declines.

### 4.3 Dispatcher internals — the private-stack return mechanics (Phase 0)

The dispatcher the entry jumps to is **not** a plain RET-style routine:

```asm
2F69  2A 1C 5D    LD   HL,($5D1C)    ; DOS-private stack pointer (sysvar Saved SP)
2F6C  2B           DEC  HL
2F6D  2B           DEC  HL
2F6E  F9           LD   SP,HL        ; switch off the caller stack
2F6F  C3 2F 1D     JP   $1D2F        ; service processor
```

Two consequences:

1. **The trap must not emulate the service as a naive `RET`.** Callers may `CALL $3D13` *or* `JP $3D13` (verified: `docs/disasm/demo/across-the-edge/timing-fix-analysis.md` — `JP 3D13h` tail-jumps at `$6803-$6806`); the ROM's own return path runs through the sysvar pair `$5D1A` (return address) / `$5D1C` (saved SP), not the hardware stack. The caller-observable exit (registers, flags, SP, PC) is **extracted in Phase 0** from the `$2F69 → $1D2F` path and one representative service handler, then locked by the §12.2-2 differential test — the same empirically-anchored contract flow the tape trap used (r0 → r1 correction there).
2. Error reporting flows through the error byte at `$5D0F` (0 = success) and the error entry `$3D16`; the trap emulates **success paths only** and declines otherwise so the real ROM produces authentic diagnostics (§6.2 row 5).

### 4.4 System variables the services touch (TR-DOS area `$5CB6-$5D32`)

| Address | Meaning | Trap behavior |
|---|---|---|
| `$5CDD` (8) | current filename, space-padded | read (FIND_FILE/LOAD_FILE input); written by emulated FIND_FILE/READ_DESCRIPTOR |
| `$5CE5` | file type (`B`/`C`/`D`/`#`) | written from catalog |
| `$5CE6`/`$5CE8` | file start / length | written from catalog |
| `$5D0F` | error code (0 = OK) | set 0 on emulated success |
| `$5D16` | mirror of last port `$FF` write | untouched (no port write happens) |
| `$5D19` | default drive | read-only context |
| `$5D1A`/`$5D1C` | return address / saved SP | honored per Phase 0 contract (§4.3) |

SOS-side sysvars for BASIC file loads (`PROG`, `VARS`, `E_LINE`, `CHANS` relocations, autostart line) are part of the LOAD_FILE postcondition for type `B` files — extracted from ROM handlers `$1836` (load parameter setup) / `$18AB` (after file loaded) in Phase 0, byte-locked by the differential test.

### 4.5 On-disk layout (host-side read path)

- Catalog: track 0, sector indices 0-7 (`getDataForSector(0..7)`) — 128 × 16-byte `TRDFile` entries; end marker = name byte `$00`, deleted = `$01`. Volume info: `TRDVolumeInfo` at sector index 8.
- File data walk: from `TRDFile.startTrack` (logical) / `startSector` (1-16), `sizeInSectors` sectors contiguous in *logical* order; logical track `L` → cylinder `L >> 1`, side `L & 1` (per `trdos.h` numbering: 0 = h0t0, 1 = h1t0, 2 = h0t1, … 79 = h1t39); sector 16 → next logical track, sector 1.
- Format-agnostic by construction: any image the loaders shaped into TR-DOS geometry (TRD, SCL, flux-derived) walks identically through the universal track model — no `.trd`-specific byte math in the trap.

## 5. Design overview

```text
                       Z80::Z80Step()  (pre-fetch, emulator thread)
                              │
                              ▼
              [ROM-switch region ran: pch==0x3D ⇒ CF_TRDOS set,
               DOS ROM banked at $0000-$3FFF]
                              │
                              ▼
              pc == $3D13 && diskFastLoad armed? ── no ───► normal fetch/execute
                              │ yes                              │
                              ▼                                  ▼
                 DiskFastLoad::HandleServiceTrap(cpu)      real TR-DOS ROM runs
                       │                        │                 │
              service in emulated set?     decline (no mutation)  │
              disk/catalog/file resolvable?       │               ▼
                       │                          └────► WD1793 signal path at
              copy sectors → memory (wd())              authentic speed, same
              update FDC/FDD registers, sysvars         disk state
              emulate service exit state
                       │
                       ▼
              return true (Z80Step returns; trap replaced the service)
```

Core idea, same as the tape trap: **replace the whole service invocation** at the entry-table fetch, performing the service's memory side effects, system-variable postconditions, FDC register updates and the documented exit state directly. A decline is completely inert.

## 6. Engagement rules and decline matrix

### 6.1 Arm state (cheap per-step predicate)

Hot-path cost when off: one `uint16_t` compare + one null check (the tape-trap idiom; arm evaluation happens only inside a candidate invocation):

```
IsArmed() = 'fastdisk' feature enabled          (FeatureManager live lookup;
                                               null manager = never armed)
      && CF_TRDOS set                          (bank 0 IS the DOS ROM — the exact
                                               inverse of the tape trap's gate,
                                               making the two traps mutually
                                               exclusive by construction)
      && pBetaDisk != nullptr                  (Beta 128 interface fitted)
      && mapped bank 0 signature at $3D13 == 00 C3 69 2F   (NOP; JP $2F69 — the
                                               5.03/5.04T family; 5.04TM / 6.x /
                                               SOS font bytes all differ → decline)
```

The signature check doubles as the ROM-version gate (§4.1) and as the no-Beta-interface gate (the SOS ROM carries font data at `$3D13`). A RAM bank spoofing the signature while `CF_TRDOS` is set is theoretically possible but pathological; if it ever matters, a `BANK_ROM` page-mode check via `MapZ80AddressToPhysicalPage` hardens it (risk table §13).

### 6.2 Per-invocation decline matrix

Checked in order; **any failure = decline** (return `false`, zero side effects):

| # | Condition | Outcome |
|---|---|---|
| 1 | `C` not in the emulated set (§4.2) | Decline → real ROM |
| 2 | No disk in the FDC-selected drive (`pBetaDisk` → `_selectedDrive` → `isDiskInserted()` false) | Decline → authentic "no disk" error path |
| 3 | Catalog sectors unreadable through the track model (unformatted/foreign geometry) | Decline |
| 4 | LOAD_FILE/FIND_FILE: name at `$5CDD` absent from catalog | Decline → authentic "file not found" report (the ROM's own error path) |
| 5 | LOAD_FILE: type/mode combination outside the Phase 0-verified contract | Decline |
| 6 | READ_SECTORS: track/sector/count outside image geometry | Decline → authentic behavior for out-of-range reads |
| 7 | Emulation would need the write path (never in v1 — defensive) | Decline |

Rows 4-6 implement the **faithful-success-only** rule: the trap serves exactly the cases it can reproduce byte-exactly, and every failure mode falls back to authentic ROM behavior — including authentic error messages.

## 7. Service postcondition emulation (success paths)

Common skeleton (per service, after its payload work):

```cpp
// Pseudocode — final C++ in DiskFastLoad::HandleServiceTrap()
WritePayloadToMemory(dest, bytes);            // through Z80::wd() (§9.3), 64K wrap
UpdateSysvarsFromDescriptor(...);             // $5CDD..$5CE8 region, $5D0F = 0
UpdateFdcState(track, sector, side);          // existing WD1793/FDD setters (§9.4)
cpu.pc = <Phase-0 return contract>;           // $5D1A-based return; NOT a naive POP
cpu.m1_pc = 0x3D13;                           // TTD journal + traces attribute the
                                              // synthetic invocation to $3D13
AdvanceTStates(kFastDiskTStates);             // §8
```

Per service:

- **`$0E` LOAD_FILE** — resolve `$5CDD` name → catalog entry → walk `sizeInSectors` sectors → per type: `B` BASIC program+vars to `PROG` with sysvar relocations and autostart capture; `C`/`#` CODE payload to the descriptor start address, or HL when A-mode selects it; `D` DATA arrays per ROM layout (Phase 0). Update `$5CE6`/`$5CE8` from the descriptor.
- **`$05` READ_SECTORS** — copy `B` sectors from logical `(D, E)` walking §4.5's interleave (crossing track/side boundaries) to HL through the write path; leave FDC track/sector registers at the last sector served (what the real seek-then-read loop observably leaves).
- **`$0A` FIND_FILE** — host-side catalog scan; on hit, copy the 16-byte descriptor into `$5CDD..`, set C = entry index, set Z; the miss case **declines** (row 4) so the ROM reports authentically.
- **`$08` READ_DESCRIPTOR** — copy catalog entry A into the descriptor area.
- **`$00`/`$02`/`$16`/`$17`** — positioning primitives: set WD1793 track register + FDD head/side through the existing setters, no timing. (Folded into the read services implicitly; standalone calls get the same treatment.)

## 8. Timing accounting

`kFastDiskTStates = 256` charged for the invocation itself (CALL/RET balance plus the dispatcher's stack switch); payload stores self-account ~3T/byte through the CPU write path. Consequences:

- A 30-40K CODE file (typical game blob) completes in ≈ 1 emulated frame; the 64K worst case in ≈ 2.5 frames — "almost instant".
- No zero-time side effects: the `while (t < frameLimit)` loop always observes forward progress.
- `R` advanced by 1 (single synthetic M1 attributed to `$3D13`) — same documented deviation class as the tape trap.

## 9. Integration points

### 9.1 New component — `core/src/emulator/io/fdc/diskfastload.h/.cpp`

```cpp
class DiskFastLoad
{
public:
    DiskFastLoad(EmulatorContext* context, WD1793& fdc);

    bool IsArmed() const;                    // §6.1 — lazy, per-invocation
    // Returns true when the trap consumed the $3D13 invocation (Z80Step must
    // return immediately). Returns false = decline (no state touched).
    bool HandleServiceTrap(Z80& cpu);

private:
    bool CheckROMSignature() const;          // bank 0 at $3D13 == 00 C3 69 2F
    const TRDFile* FindCatalogEntry(/*name*/); // host-side catalog walk (§4.5)
    bool ReadLogicalSector(/*L, sector, out*/);
    void ApplyServiceEffects(Z80& cpu, /*service, data*/);
};
```

Owned by `Core` next to the FDC (creation precedent: `core.cpp:185` creates `TapeFastLoad` next to `Tape`); nullable pointer `EmulatorContext::pDiskFastLoad` (pattern of `pTapeFastLoad`/`pBetaDisk`). `DiskFastLoadCUT` `_CODE_UNDER_TEST` wrapper for unit tests.

### 9.2 Hook site — `Z80::Z80Step()` (`core/src/emulator/cpu/z80.cpp`)

Immediately **after** the fast-tape trap block (`z80.cpp:311-318`), which already guarantees the two required orderings: after the TR-DOS ROM-switch region (so the mapped bank is the DOS ROM when the trap inspects it) and after breakpoint dispatch (so the `TRDOSAnalyzer`'s `$3D13` breakpoint and user breakpoints keep firing first):

```cpp
// Fast disk loading trap — design: docs/inprogress/2026-09-16-fast-disk-loading
if (pc == TRDOS::ROMSwitch::ENTRY_MCODE && _context->pDiskFastLoad != nullptr)
{
    if (_context->pDiskFastLoad->HandleServiceTrap(*this))
        return;  // trap replaced the whole service invocation
}
```

`TRDOS::ROMSwitch::ENTRY_MCODE = 0x3D13` already exists in `spectrumconstants.h` — no new constant.

### 9.3 Memory writes must be observable

Payload and descriptor stores go through `Z80::wd()` — the same dispatch every CPU store uses (not `DirectWriteToZ80Memory`): TTD dirty-tracking records them, watchpoints and memory analyzers fire, debugger views refresh. Identical rationale and mechanism to fast-tape §9.3.

### 9.4 FDC observable-state coherence

The real service leaves a *coherent controller*: track register at the last-read track, sector register at the last sector, status ready, motor spinning. The trap reproduces this host-side by calling the **existing** `WD1793`/`FDD` setters (`setTrack`, side selection, `setMotor(true)`) — the same code paths the port-decode write path drives — so:

- subsequent direct FDC port polls by the program read consistent registers;
- the FDD/WD1793 TTD blobs stay valid (fields already serialized; no format change);
- the HUD motor indicator still signals that a disk operation happened.

Documented deviation: `IWD1793Observer` subscribers (e.g. FDC-level analyzer events) do **not** see synthetic READ_SECTOR commands — the `TRDOSAnalyzer`'s `$3D13` breakpoint event is the visible trace of the operation instead (mirrors the tape trap's coverage deviation, fast-tape §11 row 6).

### 9.5 Disk-image access

Read-only, through the mounted `DiskImage` of the FDC-selected drive: `pBetaDisk → _selectedDrive → getDiskImage() → getTrackForCylinderAndSide() → getDataForSector()`. No new image state, no loader involvement at trap time (images are already shaped at mount), no cache to invalidate — disk-change/eject/insert flows are untouched (they already reset what they must).

## 10. Configuration and control surfaces

| Surface | Change |
|---|---|
| `fastdisk` feature (`featuremanager.*`) | Registered id `fastdisk`, alias `fdisk`, category performance, **default on**; `IsArmed()` live-lookup — toggles take effect on the next `$3D13` invocation |
| CLI `fast_disk` | `setting fast_disk on\|off` → `FeatureManager::setFeature` (same idiom as `fast_tape`, `cli-processor-settings.cpp`) |
| WebAPI `fast_disk` | Member of the `io_acceleration` settings group alongside `fast_tape` (`settings_api.cpp`) — `GET/PUT /settings[/fast_disk]` |
| Qt GUI | Checkable action **Machine → Fast Disk Loading** (`menumanager.cpp`, fast-tape pattern; state synced in `updateMenuStates()`) |
| unreal-videowall | **View → Toggle Fast Disk Loading** — the "for all tiles" pattern; new tiles inherit the wall-wide state |

## 11. TTD (Time-Travel Debugging) interactions

| Aspect | Handling |
|---|---|
| Determinism | Trap outcome depends only on (CPU entry state, disk image content, FDC register state) — all checkpointed or session-invariant. **No `RecordExternalEvent` barrier** (unlike tape playback, no wall-clock input anywhere in the path) |
| Memory deltas | Stores via the hooked write path → dirty-tracker records them → deltas complete |
| Seek/replay | Seek before the trap restores pre-trap state; re-execution re-fires deterministically → identical writes |
| Peripheral blobs | WD1793/FDD already serialize track/drive/side/motor state; the trap only ever *sets existing fields* — no serialization change, restore-safe |
| Replay suppression | Not required — deterministic and side-effect-safe during `ttdReplayActive` |
| Coverage | `$3D13` fetch is bypassed; `m1_pc` attribution keeps the write journal anchored there. Reverse PC-search misses the entry (accepted deviation, tape-trap precedent) |

## 12. Test plan

Artifacts under `scratch/` via `TestPathHelper`; fixtures: real TRD/SCL images already in-tree (`testdata/sound/tsfm/sonic3d.trd`, `testdata/sound/The_Viewer1.0.trd`, …); new files follow `*_test.cpp` / `ClassName_Test`.

### 12.1 Unit tests (`core/tests/emulator/io/fdc/diskfastload_test.cpp`)

Synthesized images via `TrackFormatSpec::trdos()` + hand-built catalogs (the `diskimage_test.cpp` toolkit).

1. **Decline matrix** — one test per row of §6.2 (non-emulated C, no disk, unreadable catalog, missing file, out-of-range geometry): assert return `false`, zero mutation of RAM/FDC/registers.
2. **Arm state** — signature corrupted at `$3D13` → not armed; `fastdisk` off → not armed; `CF_TRDOS` clear → not armed; the `trd504tm.rom`/`dos6_10e.rom` byte patterns at `$3D13` → not armed.
3. **READ_SECTORS walk** — multi-sector read crossing the sector-16 → next-logical-track boundary and the side flip (logical odd → side 1); byte-exact expected data, FDC track/sector registers left at the last sector.
4. **FIND_FILE hit/miss** — descriptor bytes at `$5CDD`, C = entry, Z set on hit; miss declines.
5. **LOAD_FILE CODE** — payload at descriptor address; A-mode override; `$5CE6/$5CE8/$5D0F` postconditions.
6. **Positioning services** — RESTORE/SEEK/side select update FDC/FDD registers only, no T-state spike beyond the constant.

### 12.2 Integration / differential tests

1. **Differential trap-ON vs trap-OFF** — the core contract lock (fast-tape §12.2-2 methodology): same image and caller stub both ways; traps-OFF runs the real ROM service loop to completion (bounded frame budget). Compare full RAM `$4000..SP-1` excluding timing-seeded sysvars (`FRAMES`, `LAST_K`, `KSTATE`), plus final registers, SP and `$5D0F`. Any postcondition gap in §7 shows up here as divergence.
2. **Loader end-to-end** — boot TR-DOS on a 48K+Beta machine with a real fixture mounted, trigger the tfmplayer-shape stub (`LD C,$0E / CALL $3D13`), assert payload in RAM and total load < a few frames, motor event observed.
3. **BASIC transitive path** — `RUN` at the `A>` prompt (boot injection path, forensics §3.4) with a `boot` file: assert the game boots, proving the internal-service-call coverage claim.
4. **Fast/tape interplay** — machine in BASIC with tape and disk features both on: tape trap serves `$0556` (CF_TRDOS clear), disk trap serves `$3D13` (CF_TRDOS set); a session crossing both (disk boot → tape-based title) loads both ways.
5. **TTD round-trip** — record across a fast load; double-crossing seek with `ttd::CaptureSnapshot` hashes; zero external-event markers; FDC blob restores.

### 12.3 Manual verification

- Qt: mount a large multi-file TRD, `RUN`, confirm near-instant boot; toggle Machine → Fast Disk Loading off mid-session → next load runs at authentic speed with motor/step sounds.
- Pentagon (SOS + Beta) → trap engages; machine without disk interface → trap never arms.

## 13. Risks and mitigations

| Risk | Impact | Mitigation |
|---|---|---|
| Private-stack return contract misread (§4.3) — trap returns wrongly for `JP` callers | Loader derails immediately after first service | Phase 0 mandatory extraction from `$2F69/$1D2F` + a service handler; §12.2-1 differential compares PC/SP/registers; Across-the-Edge `JP` shape included in tests |
| BASIC LOAD postcondition gaps (`PROG`-area sysvars, autostart) | ROM/BASIC flow diverges after load | Per-type semantics extracted from `$1836/$18AB` (Phase 0); full-RAM differential §12.2-1 |
| FDC register incoherence for direct-poll programs | Protection schemes or polled loaders misbehave | Register updates via existing setters; §12.1-3 asserts observable state; decline-first on any doubt |
| ROM version drift serving wrong ABI | Silent corruption on 5.04TM/6.x | Signature gate — verified declining bytes for both families (§4.1) |
| RAM bank spoofing the signature with `CF_TRDOS` set | Theoretical only | Optional `BANK_ROM` page-mode check (§6.1); tape trap accepts the equivalent risk class |
| Analyzer consumers expecting FDC command events | Missing READ_SECTOR events during fast loads | Documented deviation (§9.4); `TRDOSAnalyzer` `$3D13` event remains the operation trace |
| Zero-warnings / hot-path regression | Build/CI failure, perf regression | One compare + null check on the hot path; `core-benchmarks` Z80 loop before/after |

## 14. Open questions

1. **`turbodisk` sibling** — turbo mode engaged while the WD1793 is busy (command pending / motor on), the `turbotape` analog: covers direct-port custom loaders with authentic machine timing but warp wall-clock. Natural follow-up; the FDC already exposes observable busy state for the gate.
2. **VERIFY (`$0D` path, disk-side)** — same shape as tape VERIFY v2: host-side compare + flag result.
3. **`$3D1A` command-level trap** — probably unnecessary given transitive coverage (§3); revisit only if a command path bypasses internal `$3D13` calls.
4. **5.04TM / 6.x service ABIs** — support = additional signature + ABI table entries; decline until someone asks.
5. **Load-duration pacing mode** — if software ever measures disk load time; same v2 idea as tape §14.3.

## 15. Implementation phases

| Phase | Content | Exit criteria |
|---|---|---|
| 0 | ROM contract extraction: `$2F69 → $1D2F` dispatcher return mechanics (both CALL and JP callers), `$1836/$18AB` per-type file layouts, error paths; findings fold back into this doc as r1 | Contract tables filled from `data/rom/trdos503.rom`; design updated |
| 1 | `DiskFastLoad` component + `Z80Step` hook + arm state + positioning services + `Core`/context wiring | §12.1-2/6 green; zero-warning build |
| 2 | Host-side catalog access + FIND_FILE / READ_DESCRIPTOR / READ_SECTORS | §12.1-1/3/4 green |
| 3 | LOAD_FILE per-type semantics + differential suite §12.2-1 | All §12.2 green in `core-tests` |
| 4 | Surfaces: feature registration, CLI `fast_disk`, WebAPI `io_acceleration`, Qt menu, videowall | §12.3 manual checklist |
| 5 | Docs migration (folder → permanent `docs/` location once finalized) | Per `docs/inprogress/README.md` lifecycle |

## 16. References

- *TR-DOS for professionals and amateurs*, Alessandro Grussu — service API semantics
- In-tree: [TR-DOS ROM forensics](../2026-01-21-trdos-analyzer/trdos-rom-forensics.md) (service table, `$5CDD` filename, boot injection, hazards #137-#140), [TR-DOS / 48K ROM interaction analysis](../../../docs/rom/analysis/trdos-rom-interaction-analysis.md) (ROM-switch latch, `$3D2F` trampoline, sysvar detection tiers)
- Sibling design: [Fast tape loading](../2026-08-30-fast-tape-loading/design.md) — trap architecture, decline-matrix discipline, differential-test methodology, feature-surface migration history this design reuses
- `core/src/emulator/io/fdc/trdos.h` (`TRDFile`, logical track numbering), `core/src/emulator/spectrumconstants.h` (`TRDOS::ROMSwitch`, `TRDOS` sysvars)
- Calling-convention evidence in-tree: `docs/disasm/software/tfmplayer/tsfm-sna-guide.md` (`LD C,0Eh / CALL 3D13h`), `docs/disasm/software/wildplayer/wildplayer_body.asm`, `docs/disasm/demo/across-the-edge/timing-fix-analysis.md` (`JP 3D13h`)
- Universal track model: `core/src/emulator/io/fdc/diskimage.h` + [track-model design](../2026-09-02-universal-track-model/track-model-design.md)
