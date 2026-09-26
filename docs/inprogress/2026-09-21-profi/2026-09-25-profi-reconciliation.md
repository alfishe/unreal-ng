# ZX Profi 1024 — Reconciliation Report

**Date**: 2026-09-25  
**Scope**: Cross-reference the `profi` branch implementation against all reviewed emulators (UnrealSpeccy, ZXMAK2, Xpeccy, Karabas-Pro FPGA RTL) to identify gaps, unfinished tasks, and TTD registry completeness.

**Sources reviewed**:
- [existing-emulators-review.md](existing-emulators-review.md) — our own survey of 4 emulators + FPGA
- [technical-design.md](technical-design.md) — the design document (§14 = status)
- [TODO.md](TODO.md) — self-reported status
- [portdecoder_profi.cpp](../../../core/src/emulator/ports/models/portdecoder_profi.cpp) — current implementation
- [screenprofi.cpp](../../../core/src/emulator/video/profi/screenprofi.cpp) — hi-res renderer
- [ttdprofipaging.cpp](../../../core/src/debugger/ttd/profi/ttdprofipaging.cpp) — TTD serializer
- [proficmos.h](../../../core/src/emulator/memory/profi/proficmos.h) — RTC/CMOS device
- Reference emulators: external corpus (`{unreal-speccy,ZXMAK2,Xpeccy,karabas-pro}`, not in this repo)

---

## 1. Renderer Architecture Rule & Dead Code Cleanup

**Rule (applies repo-wide, not just Profi): every hi-res / extended video mode lives in
its own class, owned and lazily allocated by `ScreenZX`** when the detected video mode
actually needs it - never as inline logic inside `ScreenZX` itself.

| Mode(s) | Class | Files | Introduced |
|---|---|---|---|
| M_ATM16 / M_ATMHR / M_ATMTX / M_ATMTL | `ScreenAtm` | `core/src/emulator/video/atm/screenatm.{h,cpp}` (+ `atmfont.h`) | `a9114f17` (master) |
| M_PROFIHR (512×240) | `ScreenProfi` | `core/src/emulator/video/profi/screenprofi.{h,cpp}` | `f773998d` (profi branch) |
| M_P16 / M_PMC (Pentagon AlCo) | stays in `ScreenZX::DrawAlcoMode` | `video/zx/screenzx.cpp` | deliberate: Pentagon shares the plain-ZX raster, not an extended mode |

`ScreenZX::Draw()` dispatches: ATM modes → `_atmScreen` (lazy), `M_PROFIHR` →
`_profiScreen` (lazy, `Draw(tstate, rd, framebuffer, _borderColor)`), P16/PMC →
`DrawAlcoMode`, everything else → the standard ULA LUT path. Plain machines never
allocate an extended renderer. Within one class, per-mode branches (`ScreenAtm`'s four
modes; `ScreenProfi`'s monochrome-vs-colour branch) are fine - the rule is "own class
per *family*, not per Screen::Draw*", not "one class per mode".

**Confirmed already satisfied for Profi**: `ScreenProfi` follows the pattern exactly;
there is no leftover Profi hi-res logic inside `screenzx.{h,cpp}` (an earlier design-doc
reference to `ScreenZX::DrawProfiHiRes` was stale documentation, not real code - see §7
Documentation Drift).

**Dead code cleanup, done**: `Screen::DrawProfi(uint32_t n)` (`screen.cpp:1808`) was a
no-op stub (`(void)n;`) kept only as the `M_PROFI` row in `Screen`'s per-mode dispatch
table (`screen.h`) - a table that lives entirely inside the `<Obsolete>` region and has
no call site (`_drawCallback` is assigned but never invoked). Standard 256×192 Profi
mode renders through the ordinary ZX path (bit-identical to a stock Spectrum screen), so
the stub was unreachable. Removed the method (`screen.cpp`) and its declaration
(`screen.h`); the obsolete table's `M_PROFI` slot now points at `&Screen::DrawNull`
(kept, not deleted outright, since the array is positionally indexed by `VideoModeEnum`
and removing a slot would silently misalign every entry after it) with a comment
pointing at the real renderers (`ScreenZX` for `M_PROFI`, `ScreenProfi` for
`M_PROFIHR`). Full `core-tests` rerun clean (3363 passed, 1 pre-existing unrelated skip,
zero warnings).

---

## 2. Feature Matrix: Implementation vs. Reference Emulators

> [!NOTE]
> ✅ = Done  ⚠️ = Partial / caveats  ❌ = Not implemented  N/A = Not applicable

### 2.1 Core Paging & Memory

| Feature | UnrealSpeccy | ZXMAK2 | Xpeccy | Karabas FPGA | **unreal-ng** | Notes |
|---|---|---|---|---|---|---|
| `#7FFD` decode (A15=0, A1=0) | ✅ | ✅ | ✅ | ✅ | ✅ | Was B3 (A2 gate); fixed |
| `#DFFD` decode (A15=1, A13=0, A1=0) | ✅ | ✅ | ✅ | ✅ | ✅ | Was B4 (overlap); fixed |
| Lock semantics (7FFD.5 + DFFD.4 override) | ✅ | ✅ | ✅ | ✅ | ✅ | Was B6/B7; fixed |
| RAM page = `(DFFD[2:0]<<3)\|7FFD[2:0]` | ✅ | ✅ | ✅ | ✅ | ✅ | 64 pages, 1024K |
| SCO window swap (DFFD.3) | ✅ | ✅ | ✅ | ✅ | ✅ | `UpdateModelMemoryBanks` |
| SCR #8000→page 6 (DFFD.6) | ✅ | ✅ | ⚠️ (7FFD.3 gate) | ✅ | ✅ | Follows RTL, not Xpeccy |
| WOROM RAM@#0000 (DFFD.4) | ✅ | ✅ | ✅ | ✅ | ✅ | |
| CPM / CF_DOSPORTS (DFFD.5) | ✅ | ✅ | ✅ | ✅ | ✅ | |
| `p7FFD`/`pDFFD` state written | ✅ | ✅ | ✅ | N/A | ✅ | Was B2/B5; fixed |

### 2.2 ROM & DOS Latch

| Feature | UnrealSpeccy | ZXMAK2 | Xpeccy | Karabas | **unreal-ng** | Notes |
|---|---|---|---|---|---|---|
| ROM order SYS=0, DOS=1, 128=2, 48=3 | ✅ | ✅ | ✅ | ✅ | ✅ | Verified by string inspect |
| Reset into SYS ROM (DOS latch=on) | ✅ | ✅ | ✅ | ✅ | ✅ | Was B8; fixed |
| DOS latch set on M1 at $3Dxx with rom14=1 | ✅ | ✅ | ✅ | ✅ | ✅ | Via `CF_TRDOS` |
| DOS latch clear on PC≥#4000 | ✅ | ✅ | ✅ | ✅ | ✅ | Via `CF_LEAVEDOSADR` |
| DFFD.4 clears DOS latch / blocks entry | partial | partial | partial | ✅ | ✅ | Follows emulator consensus |

### 2.3 Video Modes

| Feature | UnrealSpeccy | ZXMAK2 | Xpeccy | Karabas | **unreal-ng** | Notes |
|---|---|---|---|---|---|---|
| Standard 256×192 (DS80=0) | ✅ | ✅ | ✅ | ✅ | ✅ | Existing ZX renderer |
| Hi-res 512×240 (DS80=1, DFFD.7) | ✅ | ✅ | ✅ | ✅ | ✅ | [screenprofi.cpp](../../../core/src/emulator/video/profi/screenprofi.cpp) |
| Hi-res bitmap pages 4/6 (screen select) | ✅ | ✅ | ✅ | ✅ | ✅ | |
| Hi-res attr pages 0x38/0x3A | ✅ | ✅ | ✅ | ✅ | ✅ | |
| Attr format: b7 papBR, b6 inkBR, 5:3 paper, 2:0 ink, no flash | ✅ | ✅ | ✅ | ✅ | ✅ | |
| Byte pair: first at +0x2000, second at +0x0000 | ✅ | ✅ | ✅ | ✅ | ✅ | |
| Monochrome mode (ProfiMonochrome / Profi 3.xx) | ✅ (flag) | ✅ (UlaProfi3XX) | ❌ | ✅ | ✅ | `config.profi_monochrome` |
| Border inversion in hi-res | ✅ | ✅ | ✅ | ✅ | ✅ | `~borderColor & 7` |
| Geometry row (608×288 storage) | N/A | N/A | N/A | N/A | ✅ | `M_PROFIHR`, `R_512_240` |

### 2.4 Palette

| Feature | UnrealSpeccy | ZXMAK2 | Xpeccy | Karabas | **unreal-ng** | Notes |
|---|---|---|---|---|---|---|
| Palette port `OUT #xx7E` (A7=0, A0=0, DS80) | ✅ | ✅ | ✅ | ✅ | ✅ | |
| Colour from ~A15..A8 | ✅ | ✅ | ✅ | ✅ | ✅ | |
| Index from previous #FE write, XOR 0xF | ✅ | ✅ | ⚠️ (only on #7E) | ✅ | ✅ | Follows Unreal/ZXMAK2/Karabas |
| Format: `Gg0Rr0Bb` (2-bit) | ✅ | ✅ | ❌ (3-bit) | ❌ (3-3-3) | ✅ (3-3-3) | Now stores full 9-bit `GGGRRRBBB` (`uint16_t[16]`); extra blue LSB from `#FE.D7` |
| Reset palette to 16 standard colours | ✅ | ✅ | ✅ | N/A | ✅ | Levels now match Karabas defaults (4/7, 6/7) on all 3 channels |
| `#FE` read bit 7 (UniCopy / GX0) | ❌ | ✅ (5XX) | ❌ | ✅ | ✅ | Closed: `Port_FE_In_GX0`, DS80-gated |

### 2.5 Peripherals

| Feature | UnrealSpeccy | ZXMAK2 | Xpeccy | Karabas | **unreal-ng** | Notes |
|---|---|---|---|---|---|---|
| AY-3-8910 (#FFFD/#BFFD, A15/A14/A1) | ✅ | ✅ | ✅ | ✅ | ✅ | |
| WD1793 FDC normal (#1F..#7F, #FF) | ✅ | ✅ | ✅ | ✅ | ✅ | `DecodeFDCPort` |
| WD1793 CP/M normal (#1F..#7F, #BF) | ✅ | ✅ | ✅ | ✅ | ✅ | |
| WD1793 extended (#83/#A3/#C3/#E3, #3F) | ✅ | ✅ | ✅ | ✅ | ✅ | |
| Covox/SoundRive #5F (L), #3F (R) | ✅ | .bak (disabled) | stubs | DAC | ✅ | NORMAL mode only; bus-silencing fix `0a98d677` (below) |
| Covox extended (#87/#A7/#C7/#E7) | ✅ | ❌ | ❌ | ❌ | ✅ | Closed: #C7 Left, #A7 Right; #87/#E7 correctly left unimplemented (8255 control addresses, no reference maps them either) |
| RTC/CMOS DS12885 (#BF/#FF addr, #DF/#9F data) | ✅ | ✅ | ✅ | ✅ | ✅ | [proficmos.h](../../../core/src/emulator/memory/profi/proficmos.h) |
| IDE Profi (#8B/#AB/#CB/#EB, high-byte latch) | ✅ | ✅ | ✅ | ✅ | ❌ | **Gap** — design: [2026-09-25-ide-hdd-design.md](2026-09-25-ide-hdd-design.md), plan §9 |
| Kempston joystick (#1F, NORMAL) | ✅ | ✅ | ✅ | ✅ | ❌ | **Gap**, but not really Profi-specific — see the Kempston resolution note below |
| Kempston mouse (#FBDF/#FFDF/#FADF) | ✅ | ✅ | ✅ | ✅ | ⚠️ | Gating present but untested |
| Extended keyboard (Profi extra keys) | ❌ | ✅ | ✅ | ✅ (PS/2) | ❌ | Low priority; no emulator agrees |
| NMI → DOS latch | partial | ✅ | ❌ | ✅ | ✅ | **Closed**: `Emulator::RequestMNI()`, DS80-gated, not gated on DFFD.4 (Q7 consensus) |
| Printer #F7 | ❌ | ❌ | stubs | ❌ | ❌ | Ignored by all emulators |

**Covox mono-leak, fixed (`0a98d677`)**: Covox has no idle timeout - once
`PortDecoder_Profi` hands Covox's ports away to the FDC/RTC with no alias left to reach
it, the DAC's last-written level stayed latched forever. Real hardware wouldn't notice
(the AC-coupled output stage swallows a static DC level after the initial transient),
but through this emulator's digital audio pipeline it came out as a permanent stuck
tone on **both** channels - confirmed live via port-trace (zero Covox writes for 2+
seconds) alongside an audio capture showing a constant level (RMS == peak,
zero-crossing rate 0) on both channels, with the UI's "Covox playing" indicator stale.
Fix: `PortDecoder_Profi` tracks whether Covox currently has a live port set
(`_covoxWasReachable`) and writes silence (`0x80`) to `Covox::PORT_LEFT_A`/`PORT_RIGHT_A`
exactly once on the transition into a session where it has none. Isolated to the Profi
decoder; nothing shared touched.

**Covox extended-mode aliases, closed**: real Profi hardware moves the DAC to `#C7`
(Left) / `#A7` (Right) in CP/M-extended mode (`IsExtMode()`, `cpm && rom14`), because
the FDC takes `#1F..#7F` (where the NORMAL-mode `#5F`/`#3F` live) away from it while
that mode is active - confirmed real, not a UnrealSpeccy-only fiction, from its
`io.cpp` decode: `(port & 0x9F) == 0x87` then `(port & 0x60)` picks Left (`0x40`, i.e.
`#C7`) or Right (`0x20`, i.e. `#A7`); `#87`/`#E7` (the mask's other two addresses) never
match either bit pattern and are unused 8255 control-register addresses, correctly left
unimplemented (no reference emulator maps them to a DAC channel). Reachability tracking
was fixed alongside it: entering CP/M-extended mode from NORMAL no longer triggers the
mono-leak silencing above (the DAC just moved to a different alias, it never actually
lost the bus) - only a session with no alias at all (a plain TR-DOS/Beta128 FDC session,
`dosPorts && !IsExtMode()`) does. 8 tests in `profi_covox_test.cpp` cover both the
routing and the reachability edges.

**Kempston joystick, resolution proposal**: the Scorpion decoder's "Kempston joystick"
(`PortDecoder_Scorpion256::IsPort_KempstonJoystick`, `portdecoder_scorpion256.cpp:678`)
is **not** a real joystick peripheral - there is no host-input joystick device anywhere
in the codebase. It is a narrowly-gated hardcode that returns `0x00` (idle: no
direction, no fire) for the exact address `#FF1F` under specific `#1FFD`/TR-DOS-latch
conditions, added because of an *observed* bug: the Scorpion Service Monitor polls
`#FF1F`, saw the floating-bus default `0xFF` (all directions + fire held), and
phantom-clicked its menu highlight every 5 frames. Two separable asks hide under
"Kempston joystick":
1. **Real host-driven joystick input** - genuinely project-wide (no model has it), a
   real gap, correctly *not* tracked as a Profi-specific TODO item.
2. **An idle-stub that returns `0x00` instead of the floating-bus `0xFF` on `#1F`
   outside `CF_DOSPORTS`** - cheap, self-contained per decoder (no shared
   infrastructure needed, same shape as the Scorpion fix). Profi currently falls
   through to `0xFF` here. **Proposal**: add the stub to `PortDecoder_Profi` *only if*
   a concrete Profi firmware misbehavior surfaces (as it did for Scorpion's Service
   Monitor) - there is no such report in this corpus today, and copying the stub
   speculatively wouldn't bring Profi to parity with the reference emulators anyway
   (they all implement *real* Kempston reads, not an idle placeholder). Until then,
   leave `#1F` on the floating bus and keep this out of the Profi TODO.

### 2.6 Timing & Contention

| Feature | UnrealSpeccy | ZXMAK2 | Xpeccy | Karabas | **unreal-ng** | Notes |
|---|---|---|---|---|---|---|
| Frame: 224T × 312 = 69888T | ✅ | ✅ | ✅ | ✅ | ✅ | Consensus |
| INT length | 28T | 39T ("TODO") | 32T | RTL | 28T | Unverified vs hardware |
| INT→first paper offset | 12580T | 12583T | disputed | RTL | 12580T | Unverified |

---

## 3. TTD Registry Integration — Detailed Analysis

> [!IMPORTANT]
> This is the key area the user asked about: does TTD capture/restore **all** Profi state bit-to-bit?

### 3.1 What IS Captured

| State | Transport | Serializer | Restore Path | Verified |
|---|---|---|---|---|
| `p7FFD` (8 bits) | `TTDChipsetState` | built-in | `Memory::UpdateZ80Banks` | ✅ |
| `pDFFD` (8 bits) | `TTDProfiPaging` | [ttdprofipaging.cpp](../../../core/src/debugger/ttd/profi/ttdprofipaging.cpp) | re-run `UpdateZ80Banks` + `InitRaster` | ✅ |
| `profiPalette[16]` (16 bytes) | `TTDProfiPaging` | same | memcpy into `EmulatorState` | ✅ |
| `pFE` (8 bits) | `TTDChipsetState` | built-in | border + palette index source | ✅ |
| `pBFFD` / `pFFFD` (AY latches) | `TTDChipsetState` | built-in | AY state | ✅ |
| DOS latch (`CF_TRDOS`) | `TTDChipsetState.flags` | built-in | via flags restore | ✅ |
| RAM pages 0–63 (1024K) | Bulk RAM capture | `ResolveModelRamPages` = 64 | byte-for-byte | ✅ |
| ROM pages 0–3 (64K) | Bulk ROM capture | (read-only, not saved per checkpoint) | N/A | ✅ |
| WD1793 FDC state | `PeripheralId::BetaDisk` | existing serializer | | ✅ |
| TurboSound / AY chip registers | `PeripheralId::TurboSound` | existing serializer | | ✅ |
| Covox DAC latches | `PeripheralId::Covox` | existing serializer | | ✅ |
| Tape state | `PeripheralId::Tape` | existing serializer | | ✅ |
| Kempston mouse | `PeripheralId::KempstonMouse` | existing serializer | | ✅ |
| `MachineStateHash` includes pDFFD + palette | Via `TTDProfiPaging::TTDHashState` | FNV-1a over the 34-byte `ProfiPagingState` blob (pDFFD, 16×9-bit palette) | | ✅ |
| Peripheral contract test | `ttdmodelstatecontract_test` includes PROFI | blocks recording if id missing | | ✅ |

### 3.2 What is NOT Captured (Gaps)

| State | Risk Level | Notes |
|---|---|---|
| **RTC/CMOS register file** (256 bytes + address latch) | ⚠️ **Medium** | [`proficmos.h`](../../../core/src/emulator/memory/profi/proficmos.h) has `_cmos[256]` and `_cmos_addr`. Currently uses live host time. A TTD replay will see the **host clock at replay time**, not at capture time. The tech design (§6.2) explicitly notes this as low priority (Q10). For bit-perfect restore, needs: (a) deterministic clock mode, (b) 257 bytes added to `ProfiPagingState` |
| **IDE state** (high-byte latch + ATA registers) | ⚠️ **Medium** | IDE not implemented yet. When it is, the `m_ide_write`/`m_ide_read` 8-bit latches and the full ATA drive state must be serialized. ZXMAK2 has a 1-byte `m_ide_write` + `m_ide_read` pair. **Direction in design (rollout 2, requires further investigation)**: a shared `PeripheralId::AtaChannel` blob (both drives' registers + transfer buffer + adapter latches), not nested under `ProfiPaging`; disk contents via a copy-on-write layer + write journal ([ide-hdd-design §10](2026-09-25-ide-hdd-design.md)) |
| ~~**Covox extended-mode port aliases**~~ | **Closed** | `#C7`/`#A7` now wired (§2.5); no new TTD state - the existing Covox serializer already covers the DAC latches regardless of which port address wrote them |
| **Kempston joystick latch** | 🟢 **Low** | Joystick state is read-only from the host; no emulated state to save |
| **`_covoxWasReachable` flag** | 🟢 **Negligible** | Internal decoder tracking bit; has no observable effect on the guest machine state |
| ~~`flags` (incl. `CF_TRDOS` DOS latch) absent from the divergence hash~~ | **Closed (T4)** | `MachineStateSnapshot` (`machinestatehash.h:70`) still does not hash `flags` at all - only `peripheral_hash` and the other typed fields - so this was always a `TTDProfiPaging`-local fix, not something to wait on a project-wide `MachineStateSnapshot.flags` field for. `TTDProfiPaging::TTDHashState` now folds `_state->flags & (CF_TRDOS \| CF_DOSPORTS)` into the FNV-1a chain *after* hashing the `ProfiPagingState` blob - deliberately not added to `Snapshot()`/the persisted blob itself (`TTDStateSize()` is still 34 bytes; `TTDSaveState`/`TTDLoadState` are untouched), since `TTDLoadState` already restores `flags` correctly via `TTDChipsetState` and this only needed to widen what the *hash* covers. 2 tests in `ttdprofipaging_test.cpp` (`HashSensitiveToDosLatchFlagsButNotToOthers`, `DosLatchFlagsAreNotPartOfTheSavedBlob`). Not Profi-specific - Scorpion already works around the same class of problem by hashing its own `dosTrigger` state. |

### 3.3 Restore Correctness

The `TTDLoadState` path writes `pDFFD` and `profiPalette` back into `EmulatorState`. The comment says:
> "The caller re-runs the paging decode (`Memory::UpdateZ80Banks`) after every serializer has loaded"

This means **bank mapping**, **video mode** (via `InitRaster`→`DetectModeProfi`), and **CF_DOSPORTS** (via `UpdateModelMemoryBanks`) are all rebuilt. However:

> [!WARNING]
> **The `Screen::InitRaster` call happens "on the next frame"** — not immediately in `TTDLoadState`. If a TTD seek lands mid-frame, there is a 1-frame window where the renderer could use stale mode information. This is consistent with how ATM handles it, but worth noting for bit-perfect replay fidelity.

---

## 4. Gap Summary — Prioritized

### 4.1 Functional Gaps (affects what software can run)

| # | Gap | Effort | Priority | Reference |
|---|---|---|---|---|
| **G1** | IDE Profi ports (#8B/#AB/#CB/#EB, EXT mode) | M | **High** — CP/M-era software and HDD-bootable ROMs need it (`profi_mainrom_standart.rom` SYS ROM has an HDD boot loader at `#28CE`). Design + plan: [2026-09-25-ide-hdd-design.md](2026-09-25-ide-hdd-design.md), §9 below | UnrealSpeccy `io.cpp:265-300`, `ZXMAK2 IdeProfi.cs` (external corpus, not in this repo), Xpeccy `hdd.c:790` |
| ~~**G2**~~ | ~~Kempston joystick (#1F, NORMAL mode only)~~ | - | Not tracked — real joystick input is project-wide, not Profi-specific (see §2.5 resolution note); no idle-stub added absent a concrete misbehavior report | All emulators implement *real* reads, not a stub |
| ~~**G3**~~ | ~~`#FE` read bit 7 (UniCopy / GX0 flag in DS80)~~ | - | **Closed** — `PortDecoder_Profi::Port_FE_In_GX0`, DS80-gated | ZXMAK2 `UlaProfi5XX.cs:34-53`, Karabas `video.vhd:219` |
| ~~**G4**~~ | ~~Covox extended-mode aliases (#87/#A7/#C7/#E7)~~ | - | **Closed** — `#C7` Left, `#A7` Right; real hardware behavior (moves the DAC off the FDC-claimed `#1F..#7F`), not a UnrealSpeccy-only quirk | UnrealSpeccy `io.cpp:320-345` |
| ~~**G5**~~ | ~~NMI → DOS latch (magic button)~~ | - | **Closed** — `Emulator::RequestMNI()` raises `CF_TRDOS` (same effect as the #3Dxx M1 trap) when DS80=0; 8 tests in `profimni_test.cpp` | ZXMAK2 NmiRq; Karabas `cpu_nmi_n` |
| **G6** | Extended keyboard (extra keys through #FE) | M | Low — Xpeccy and ZXMAK2 disagree on mapping; no standard | ZXMAK2 `KeyboardProfi.cs`, Xpeccy `kbdScanProfi` |

### 4.2 TTD / Restore Gaps (affects time-travel fidelity)

| # | Gap | Effort | Priority |
|---|---|---|---|
| **T1** | RTC CMOS state not in `ProfiPagingState` — replay sees host clock, not capture clock | S-M | **Deferred** — no IDE/RTC-reading Profi software in the test corpus yet; revisit alongside G1 (IDE) |
| **T2** | RTC `_fixedTime` mode exists in `ProfiCMOS` but is never armed by TTD; needs wiring | S | **Deferred** (depends on T1) |
| **T3** | IDE state (when implemented) needs its own serializer entry | M | **Deferred** — nothing to serialize until G1 (IDE) lands. Direction: shared `PeripheralId::AtaChannel` blob + COW media journal - rollout 2, **requires further investigation** (§9); rollout 1 invalidates a recording on first IDE use |
| ~~**T4**~~ | ~~`flags` (`CF_TRDOS`/`CF_DOSPORTS`) absent from the divergence hash~~ | - | **Closed** — folded into `TTDProfiPaging::TTDHashState` (not the persisted blob); 2 tests in `ttdprofipaging_test.cpp` |

### 4.3 Verification / Test Gaps

| # | Gap | Effort | Priority |
|---|---|---|---|
| **V1** | BIOS menu entries not verified (CP/M, TR-DOS 48K/128K, Sinclair 48/128) | M | High — boot test only reaches the main menu |
| **V2** | Hi-res real-hardware timing evidence absent (design §12 Q3) | L (external) | Medium — affects TTD v2 benchmark |
| **V3** | No hi-res demo captures for visual smoke testing | M (external) | Medium |
| **V4** | Profi 512K variant not tested (mask with `GetRamMask` should work, but untested) | S | Low |

---

## 5. Feature-by-Feature Cross-Check Against Emulator Sources

### 5.1 Features Present in All Reference Emulators — Status in unreal-ng

| Feature | Status | Evidence |
|---|---|---|
| 7FFD paging (all bits) | ✅ | [portdecoder_profi.cpp:400-416](../../../core/src/emulator/ports/models/portdecoder_profi.cpp#L400-L416) |
| DFFD paging (all 8 bits) | ✅ | [portdecoder_profi.cpp:419-429](../../../core/src/emulator/ports/models/portdecoder_profi.cpp#L419-L429) |
| Full bank translation (64 pages) | ✅ | [portdecoder_profi.cpp:273-306](../../../core/src/emulator/ports/models/portdecoder_profi.cpp#L273-L306) |
| 512×240 renderer | ✅ | [screenprofi.cpp](../../../core/src/emulator/video/profi/screenprofi.cpp) (128 lines) |
| Palette write via OUT #xx7E | ✅ | [portdecoder_profi.cpp:377-394](../../../core/src/emulator/ports/models/portdecoder_profi.cpp#L377-L394) |
| Three FDC port sets (normal/CPM/extended) | ✅ | [portdecoder_profi.cpp:352-375](../../../core/src/emulator/ports/models/portdecoder_profi.cpp#L352-L375) |
| RTC/CMOS (DS12885 at #BF/#FF, #DF/#9F) | ✅ | [proficmos.h](../../../core/src/emulator/memory/profi/proficmos.h) |
| Covox DAC stereo (#5F L, #3F R) | ✅ | [portdecoder_profi.cpp:211-228](../../../core/src/emulator/ports/models/portdecoder_profi.cpp#L211-L228) |
| AY (#FFFD/#BFFD) | ✅ | [portdecoder_profi.cpp:78-88](../../../core/src/emulator/ports/models/portdecoder_profi.cpp#L78-L88) |
| Boot to BIOS splash / main menu | ✅ | Tests pass; FDC BUSY fix in tech-design §14 |
| TTD serializer (pDFFD + 9-bit palette) | ✅ | [ttdprofipaging.cpp](../../../core/src/debugger/ttd/profi/ttdprofipaging.cpp) |
| `#FE` read bit 7 (GX0/UniCopy) | ✅ | `PortDecoder_Profi::Port_FE_In_GX0`, DS80-gated |
| Automation surfaces (WebAPI, CLI, Lua, Python, MCP) | ✅ | TODO.md "done" checklist |

### 5.2 Features Present in Some but Not All Emulators

| Feature | Who has it | **unreal-ng** | Assessment |
|---|---|---|---|
| IDE Profi | UnrealSpeccy ✅, ZXMAK2 ✅, Xpeccy ✅, Karabas ✅ | ❌ | **All four have it**; must-implement |
| Kempston joystick (real host input) | All four | ❌ | All four implement *real* reads; project-wide gap, not Profi-specific (§2.5) - not tracked in the Profi TODO |
| #FE bit 7 read | ZXMAK2-5XX, Karabas | ✅ | **Closed** |
| Extended keyboard | ZXMAK2, Xpeccy | ❌ | Two of four, disagreeing; defer |
| NMI magic button | ZXMAK2, Karabas | ✅ | **Closed** |
| Covox EXT mode | UnrealSpeccy only | ✅ | **Closed** - real hardware behavior, not UnrealSpeccy-only fiction |
| Profi 3.xx vs 5.xx ULA distinction | ZXMAK2 (two classes) | ⚠️ | unreal-ng has `ProfiMonochrome` flag; no separate 3.xx ULA class. Sufficient for now |
| EXT mode from SYS ROM (Karabas variant) | Karabas only | ❌ | Clone extension; explicitly rejected per tech design |

---

## 6. TTD Extended Memory Capture — Bit-Perfect Analysis

### 6.1 RAM Coverage

The 1024K (64 × 16K pages) RAM set is captured by the bulk RAM serializer. `ResolveModelRamPages` for Profi returns `ramsize/16 = 64`. **All 64 pages are captured and restored byte-for-byte.**

### 6.2 Latch Coverage

```
ProfiPagingState (34 bytes, as of the 9-bit palette widening in §2.4):
  profiPalette:   32 bytes  ← 16 entries × uint16_t, 9-bit GGGRRRBBB each (leads the struct so the
                               trailing uint8_t needs no alignment padding)
  pDFFD:           1 byte   ← captures RAM-high, SCO, WOROM, CPM, SCR, DS80
```

Plus from `TTDChipsetState` (already covers all models):
```
  p7FFD, pFE, pBFFD, pFFFD, border_attr, flags (incl. CF_TRDOS - now also in the
  TTDProfiPaging divergence hash, see §3.2/T4), freq multipliers, t_states
```

T4 is done: `TTDProfiPaging::TTDHashState()` folds `flags & (CF_TRDOS | CF_DOSPORTS)`
into its FNV-1a chain (after hashing the blob below), with no change to `ProfiPagingState`
itself, `TTDStateSize()`, or what `TTDSaveState`/`TTDLoadState` round-trip.

### 6.3 What Would Still Be Needed for RTC/IDE Bit-Perfect Restore (T1/T3, deferred)

```diff
 struct ProfiPagingState
 {
     uint16_t profiPalette[16];
     uint8_t pDFFD;
+    // --- T1: RTC/CMOS (if we want deterministic replay) - deferred until IDE (G1) lands ---
+    uint8_t cmosAddr;           // current CMOS register address
+    uint8_t cmosData[256];      // full CMOS register file
+    // --- T3: IDE - NOT here: the latches live in the shared AtaChannel blob (§9) ---
 };
-static_assert(sizeof(ProfiPagingState) == 34, ...);
+static_assert(sizeof(ProfiPagingState) == 291, ...);
```

> [!TIP]
> The current 34-byte blob is **correct for the current implementation scope**. T4 (flags in
> the hash) is done and needed no bytes at all - just the `TTDHashState()` change above. The
> remaining 257 bytes (T1) only matter once RTC deterministic replay is implemented; it is
> deferred alongside G1 (IDE). The IDE latches originally sketched here moved to the shared
> `AtaChannel` blob (§9), so T3 adds nothing to `ProfiPagingState`. The size bump is well under the 4 KB
> blob limit whenever that work happens.

---

## 7. Documentation Drift & Project Hygiene

Items found while cross-checking the design docs against what's actually in the tree,
not functional gaps in the emulator itself:

- **Fixed**: `technical-design.md` §7.4's `ProfiHiresRaster=pico` claim was stale - it
  read "is available as an experiment and is covered by tests" when no such config
  option, code path, or test exists anywhere in the tree (`grep -rn "ProfiHiresRaster"`
  across `core/src`/`core/tests` returned nothing outside this doc). Reworded to state
  plainly that it's an unimplemented proposal pending Q3 real-hardware evidence.
- **`profi_video_test.cpp` does not exist.** The design's own test plan (§10.2) and the
  Qoder gap list both call for a dedicated `profi_video_test.cpp` (pixel/attr/thirds/
  border/palette encodings, mode-flip mid-frame); today only the boot-splash mode
  assertion (`ProfiBoot_Test.BiosSplashRendersInHiRes`) and the port-decoder-level
  `PaletteWrite`/`FEReadBit7ReportsGX0InDS80` tests exercise this area. The renderer
  itself (`ScreenProfi::Draw`) has no unit test of its own - worth adding once the
  hi-res timing question (Q3) settles enough that pixel-level goldens won't need
  re-baselining.
- **`testdata/machines/profi/` fixtures are staged in a separate worktree, not
  committed here.** `git worktree list` shows a second checkout at
  `/Users/dev/.qoder/worktree/unreal-ng/nrcTHG` (detached at `0a98d677`) holding the
  `testdata/machines/profi/{rom,software}` tree that shows up as untracked in this
  worktree. TODO.md already tracks "decide `testdata/machines/profi/` fixtures +
  `testdata/NOTICE.md` row" as an open commit decision (only on explicit request per
  AGENTS.md) - this note just records *where* the staged files currently live so they
  aren't lost or duplicated.
- **`.agents/AGENTS.md`'s "Creatable on master" line got ahead of the actual merge.**
  It originally listed `PROFI` directly under the "on master" heading while qualifying
  it inline as "(branch `profi`; ...)" - documenting a branch fact under a master-only
  heading. A later pass (2026-09-25) went further and folded `PROFI` into the master
  list outright with an "IDE not yet implemented" note, and dropped the same
  branch-qualifier wording from `.recipe/` (`machines/profi.md`, `_common/machines.md`,
  `README.md`, `peripherals/covox-sounddrive.md`) - but **`profi` has not actually
  merged to master yet** (`git merge-base --is-ancestor HEAD origin/master` fails as of
  this writing). That wording is therefore premature, not wrong-then-fixed: it will
  become accurate the moment the merge lands, and the plan is to merge shortly, so it's
  being left as-is rather than reverted-then-refixed. Two things it caused ARE fixed
  regardless of merge timing: `mcp-resources.cpp`'s `unreal://machine/profi` resource
  had drifted the other way, still listing the Covox CP/M-extended-mode aliases as a
  "known limitation" after they were implemented (§2.5) - corrected to describe them
  and drop them from the limitations list.

---

## 8. Conclusions

### What's Done Well
- The **core paging/memory** implementation is complete and matches all four reference emulators
- The **512×240 hi-res renderer** is fully implemented with correct page mapping, attribute format, byte-pair ordering, and border inversion, and lives in its own `ScreenProfi` class per the repo-wide renderer rule (§1)
- The **palette** is a full 9-bit `GGGRRRBBB` (§2.4), and `#FE` bit 7 (GX0) is implemented
- **TTD integration** covers the critical state (pDFFD + 9-bit palette) and the contract test enforces it
- All 10 original defects (B1–B10) from the integration audit are fixed
- **Automation surfaces** are complete (WebAPI, CLI, Lua, Python, MCP)
- The **FDC three-port-set** decode matches UnrealSpeccy exactly
- The **Covox mono-leak** (stuck DC tone when the bus is handed to FDC/CMOS) is fixed, and the **CP/M-extended-mode aliases** (`#C7`/`#A7`) are wired (§2.5)
- The boot path works (BIOS splash → main menu)

### Must-Fix Gaps (consensus features missing)

| Priority | Gap | Impact |
|---|---|---|
| 🔴 High | **G1: IDE** (#8B/#AB/#CB/#EB) | CP/M, HDD boot, Profi IDE software |
| 🟡 Medium | **V1: BIOS menu boot verification** | Not all menu entries tested |

Kempston joystick (real host input) dropped from this list - it's a project-wide gap
(no model has it), not a Profi-specific one; see §2.5 for the resolution note. `#FE` bit
7 (GX0) is closed.

### Should-Fix Gaps (TTD completeness)

T4 closed. T1/T2 (RTC CMOS) and T3 (IDE) deferred - all three sit behind IDE (G1)
actually landing, since there's no IDE/RTC-reading Profi software in the corpus today
to make replay-fidelity for either observable.

| Priority | Gap | Impact |
|---|---|---|
| 🟡 Medium (deferred) | **T1: RTC CMOS in TTD blob** | Replay fidelity for RTC-reading software |
| 🟡 Medium (deferred) | **T3: IDE TTD serializer** (when G1 lands) | Replay fidelity for IDE-using software |

### Nice-to-Have (low consensus, niche)

| Priority | Gap |
|---|---|
| 🟢 Low | G6: Extended keyboard |
| 🟢 Low | V2/V3: Real-hardware timing evidence / hi-res demo captures |
| 🟢 Low | Missing `profi_video_test.cpp` (§7) - defer until Q3 hi-res timing settles |

---

## 9. Implementation Plan — G1 (Profi IDE) and T3 (IDE in TTD)

Full design: [2026-09-25-ide-hdd-design.md](2026-09-25-ide-hdd-design.md). It covers the Profi controller in
detail, a side-by-side comparison with the SMUC / ATM / Nemo / ZX-Evo / DivIDE controllers, the split between
shared and per-board code, image-file and host-folder media, ATAPI CD-ROM, TTD, and the test method.

Delivery is in **two rollouts**: rollout 1 brings IDE on par with other emulators (closes **G1**); rollout 2
adds TTD and disk change layers (closes **T3**) and **requires further investigation** before it is planned
in detail.

**Key decisions**

| Topic | Decision | Why |
|---|---|---|
| Code split | One shared ATA disk core (`AtaDevice` ×2 in an `AtaChannel`; each slot a hard disk or an ATAPI CD-ROM) + a small adapter per board (`IdeAdapterProfi`, ~80 lines) | Every Spectrum IDE board differs only in port decode, gating, and how the 16-bit word is split into two Z80 bytes |
| Profi gate | Reuse `IsExtMode()` (CP/M ∧ ROM14) | UnrealSpeccy + ZXMAK2 agree; the real SYS ROM sets `#7FFD=#19`, `#DFFD=#B8` before touching IDE. Widening only IDE to the Karabas gate would collide `#EB` with the Beta128 `#FF` alias; the Karabas variant must be a whole-decoder switch (design Q2) |
| Latch roles | Read: `#00CB` = low byte + transfer, `#xxEB` = latch. Write: `#xxCB` = latch, `#00EB` = low byte + transfer. `#06AB` write = device control; `#06AB` read = `#FF` | UnrealSpeccy, ZXMAK2 and the Karabas RTL agree |
| Byte order | Image = drive order (low byte first); the emulator never swaps | The Profi BIOS stores the high byte first in RAM, so Profi disks look swapped on a PC - correct and expected |
| Timing | Instant completion (BSY never visible); optional T-state busy model for robustness tests | Matches all reference emulators; keeps TTD deterministic |
| Media (rollout 1) | Raw, HDF, HDI, fixed VHD, ISO (CD-ROM), host folder as synthesized FAT16/32 | Parity with UnrealSpeccy, pico-spec, xpeccy-plus, DOSBox-X. Profi's own CP/M HDD format cannot be synthesized from a folder, so raw images are Profi's path |
| ATAPI CD-ROM (rollout 1) | Same channel, second device personality; pico-spec's SCSI command list | UnrealSpeccy, pico-spec and MAME implement it; ZXMAK2/Xpeccy/zxsp have stubs only |
| Writes (rollout 1) | Images: write-through + write-protect option. Folders: in-memory session write map, export to `.img`, optional read-only | Same as UnrealSpeccy / ZXMAK2 / Xpeccy (images) and DOSBox-X (folders) |
| TTD (rollout 1) | Interim rule: the first IDE command while recording invalidates the session (same as floppy writes today); explicit allow-list entry in the contract test | Never a silently wrong recording |
| TTD + change layers (rollout 2) | ⚠️ **Requires further investigation.** Direction: `PeripheralId::AtaChannel = 10`, a copy-on-write layer with write journal, commit / discard; `ProfiPagingState` does not grow | See design §7.3, §10, Q10 |

**Rollout 1 phases** (each ends with a green `core-tests`; nothing is committed without an explicit request)

| Phase | Deliverable | Proved by | Closes |
|---|---|---|---|
| R1-1 | Shared disk core + `RawImage` (write-through) / `MemoryDisk`; delete the `io/hdd` skeleton; `Core` owns the `AtaChannel` | Disk-core conformance tests: reset signature, IDENTIFY, CHS/LBA, multi-sector, errors, master/slave/empty channel, INTRQ, state round-trip | — |
| R1-2 | `IdeAdapterProfi` + decoder arms; `[HDD]` ini parsing; `Scheme=PROFI` in `data/configs/profi/unreal.ini` (CRLF file); TTD interim rule | Profi port truth table in every mode combination; all-ports × all-modes collision sweep; SYS ROM loader at `#28CE` boots a synthetic image to `#0100`; no drive → loader error exit; recording invalidated on first IDE command | **G1** |
| R1-3 | `hdd` / `cd` verbs on CLI / WebAPI / Lua / Python / MCP; `NC_HDD_STATE_CHANGED` | Automation smoke tests | automation parity |
| R1-4 … R1-8 | Nemo / SMUC (replaces the Scorpion stub) / ATM adapters; HDF/HDI/VHD; host-folder FAT volumes; ATAPI CD-ROM + ISO; differential harness, fuzzing, Qt UI | Design §12, §14.1 | parity with other emulators |

**Rollout 2** (⚠️ requires further investigation): R2-0 investigation (COW granularity vs. the TTD v2 page
store, memory ceiling / spill, journal reuse, commit during recording, folder-read determinism, snapshot
format, floppy ST-4 unification) → revised plan; provisionally COW layer + write journal, `AtaChannel` TTD
serializer (closes **T3**), snapshot media references, folder commit-back. Design §14.2.

**Open questions that affect Profi** (design §13): Q1 the `#06AB` read value; Q2 the Karabas EXT variant; Q4 the
disk geometry convention (16/16 vs 16/63) and the `ProfiHiDD` header; Q5 which SYS menu path reaches `#28CE`;
Q6 a real Profi CP/M HDD image for an end-to-end boot test.
