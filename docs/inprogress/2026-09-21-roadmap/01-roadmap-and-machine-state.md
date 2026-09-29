# 01 — Roadmap and Machine-State Consolidation

| | |
|---|---|
| **Status** | Proposal / draft for review |
| **Date** | 2026-09-21 |
| **Baseline** | master `ae4d40b`; branches `generalsound` (+8), `moonsound` (+22), `uns-snapshots` (+28, 1 non-merge commit); `tools/poc/011-ttd-v2-capture-analysis` |
| **Related** | `docs/inprogress/PLAN.md`, `2026-09-10-ttd-registry-integration/`, `2026-07-19-time-travel/`, `docs/emulator/design/snapshots/emulator_snapshots.md` |

---

## 1. Summary

The main sound devices and ATM2 video modes are done. Remaining work on machines: Profi, ZX-Evo BaseConf, then TSConf, ZX Spectrum Next and polish of the original Sinclair/Amstrad models.

This document proposes:

- **An order of work** that finishes in-flight machines first, then consolidates machine state before the heaviest machines (TSConf, Next).
- **A per-machine definition of done** that includes TTD, snapshot and conformance coverage, so that "adapting TTD to the zoo" is never a separate phase.
- **Requirements for TTD v2 integration** covering device-owned memory, storage media and co-processors.
- **The universal snapshot (UNS)** rebuilt as a container over the TTD peripheral registry, not as a second serializer.
- **A conformance matrix** parameterized over all models.

---

## 2. Current state (verified)

| Area | Finding | Evidence |
|---|---|---|
| TTD model-state registration | Model-aware and fail-safe: `RegisterModelPeripherals` asks the port decoder for `CreateTTDSerializers()` and `GetTTDModelStateIds()`, and refuses to record if a declared id has no serializer | `core/src/debugger/ttd/timetravelmanager.cpp` ~L1040–1100 |
| Model serializers present | ATM paging (`PeripheralId::AtmPaging`), Scorpion ProfROM. **Profi has none** (`Port_DFFD` is a stub; corrected 2026-09-21, see `docs/inprogress/2026-09-21-profi/`) | `core/src/debugger/ttd/atm/`, `scorpion/` |
| Core peripherals registered | TurboSound/TSFM, Covox, Tape, Kempston mouse, Beta Disk | same function |
| Not registered | HDD/IDE (`core/src/emulator/io/hdd`); SD/Z-controller does not exist yet | grep |
| General Sound TTD | `SoundChip_GeneralSound::TTDSaveState` memcpy's the full `_ram` (512 KB) into the blob; GS Z80 writes are not dirty-tracked | branch `generalsound`, `soundchip_gs.cpp` ~L790 |
| Registry plan | Phase 1 bug fixes done; Phase 2.4 (random-access delta decoding) open; Phases 3–5 open | `2026-09-10-ttd-registry-integration/TODO.md` |
| TTD v2 POC | 4 KB page COW store, I-frame every 50 frames, batch = 4, XOR + zstd-1; paged peripheral model; 30× smaller than v1 on real 128K recordings, ~110–160 µs restore | `tools/poc/011-ttd-v2-capture-analysis/AUDIT.md`, `knowledge/ttd-v2-design.md` |
| TTD v2 real-data coverage | Real recordings are 128K only (4 files, 1209 frames); ATM2/BaseConf/TSConf/GS rows are synthetic | `knowledge/model-scalability-analysis.md` |
| UNS | One real commit (2025-07-12): YAML serializer (899 lines) + loader; predates `TTDSerializable`; 27 merge commits since | `git log origin/master..origin/uns-snapshots` |
| Serializer reuse intent | `ttdserializable.h` states the same implementations serve TTD checkpoints and file snapshots | header comment |
| Reverse execution | Restore checkpoint + forward replay (`ReverseStepInstructions`, `ReverseStepTStates`, `ReverseContinue`) | `2026-07-19-time-travel/phase-4-reverse-execution.md` |

---

## 3. Order of work

| Step | Work | Rationale |
|---|---|---|
| 1 | Finish Profi and ZX-Evo BaseConf; merge `generalsound` and `moonsound` | Close in-flight work; long-lived branches rot (see `uns-snapshots`) |
| 2 | Machine-state consolidation (§5–§8): TTD v2 integration incl. paged device memory, storage overlays, UNS on the registry, conformance matrix | Force multiplier: every later machine gets TTD + snapshots + tests by implementing one contract |
| 3 | Capability registry + trigger engine (doc 02) | Central mechanism for debugging, automation, LLM loop and semantic features |
| 4 | Developer toolchain P0–P2 (toolchain doc) | Depends on 01/02 API foundations |
| 5 | TSConf, then Next | They stress the state framework hardest (DMA mid-frame, sprites, copper, CPU speed switching); doing them after step 2 makes them validation, not a rewrite trigger |
| ∥ | Polish of original Sinclair/Amstrad models | Independent; can run in parallel |

---

## 4. Per-machine definition of done

A machine or peripheral is **done** only when all of the following hold:

| # | Criterion | Check |
|---|---|---|
| DoD-1 | Functional emulation against its test corpus | Existing per-machine tests |
| DoD-2 | All model state declared via `GetTTDModelStateIds()` and served by `CreateTTDSerializers()` | Registration refuses otherwise (already enforced) |
| DoD-3 | Device-owned memory > 4 KB exposed through the paged-region contract (§5.1), not as a blob | Review + conformance test |
| DoD-4 | Storage devices covered by COW overlays (§6) | Conformance test |
| DoD-5 | Conformance matrix passes (§8) | CI |
| DoD-6 | UNS save/load round-trip passes | CI (falls out of §7) |
| DoD-7 | Automation surface reports the machine honestly: `/state/screen/mode`, `/state/paging`, port-trace decode rules, capabilities | PLAN T1 #3, T2 #8, T2 #9 |
| DoD-8 | Per-machine MCP resource written | PLAN T3 #14 |
| DoD-9 | Real TTD recordings captured and run through the v2 benchmark | §5.4 |

---

## 5. TTD v2 integration requirements

### 5.1 Paged device-memory contract

**Problem.** TTD v2 solves large main RAM via the page store. The POC also models peripherals as paged (`reference/measure_paged_peripheral.cpp`), but the production code does not: GS copies 512 KB per checkpoint, and writes from the GS Z80 do not reach any dirty tracker.

**Requirement.** Introduce a second serializable kind alongside `TTDSerializable`:

```cpp
// Sketch — names indicative
struct TTDPagedRegion
{
    ttd::PeripheralId owner;
    uint16_t          regionId;        // device-local id (e.g. GS SRAM, OPL4 sample RAM)
    uint8_t*          base;
    uint32_t          pageCount;       // 4 KB pages
    const uint64_t*   dirtyBitmap;     // set by the device's own write path
};

class TTDPagedSerializable
{
public:
    virtual ~TTDPagedSerializable() = default;
    virtual size_t EnumerateRegions(TTDPagedRegion* out, size_t max) const = 0;
    virtual void   ClearDirty(uint16_t regionId) = 0;
};
```

- The registry routes paged regions into the same page store as main RAM: identical I-frame/P-frame, XOR, BACKREF and batching rules.
- Small fixed state (registers, latches, counters) stays in `TTDSerializable` blobs. At ≤ 4 KB, full capture costs ≤ 0.06 µs per the POC, so no delta scheme is needed. This also retires registry Phase 2.4 for large devices.
- The device's own memory write path (not `Memory`) must set dirty bits. For GS this is the GS Z80 memory interface.
- **Consumers:** General Sound (512 KB), NeoGS (up to 2 MB), MoonSound/OPL4 sample RAM, TSConf SFILE/CRAM if large, Next sprite pattern RAM and Layer 2 if not in main RAM.

### 5.2 Assumptions to correct in the POC

| POC assumption | Correction |
|---|---|
| GS playback = 0 dirty bytes (Z80 only reads samples) | GS firmware keeps stack, player variables and mixing state in the same SRAM; expect a few dirty pages per frame |
| Dirty-rate ceiling derived from host CPU clock only | Co-processors have their own ceilings: GS Z80 at 12 MHz, NeoGS at 24 MHz |
| ATM2/BaseConf/TSConf/GS rows | Synthetic; replace with measured values (§5.4) |

These do not threaten the frame budget, but the published numbers must be measured, not modeled (registry plan Phase 5: "label measured-vs-target numbers").

### 5.3 Co-processor and clock-domain replay

Reverse execution = checkpoint restore + forward replay. Requirements:

- Every co-processor's timebase is part of checkpoint state (GS already stores `_gsCyclesAbs`).
- Replay across CPU speed switches (ATM3/BaseConf turbo, TSConf, Next 3.5/7/14/28 MHz) must be exact.
- Conformance test: *reverse-step equivalence*. For random targets, `ReverseStepInstructions(n)` followed by comparison yields the same state hash as a direct forward run to the same T-state, with GS active and during turbo switches.

### 5.4 Real-recording validation

After Profi, BaseConf and GS land, capture real sessions: a demo, a game, an OS/shell workload, and a GS-heavy title per machine. Run `benchmarks/real_ttd_v1_v2_bench.cpp` and the scalability bench on them. Store the recordings as fixtures (v2 makes them small).

---

## 6. Storage media in TTD

**Problem.** Seeking back after the guest writes to disk restores RAM but not the medium. The file system on the medium then disagrees with in-memory state, and the machine continues from an impossible combination. This matters most for NedoOS and other ZX-Evo software that writes FAT volumes, but applies to TR-DOS writes too.

**Requirements.**

- **ST-1** Block-level COW overlay per medium (sector for FDD, 512 B sector or 4 KB cluster for HDD/SD). The overlay is a page store keyed by block number, and checkpoints reference overlay slots.
- **ST-2** The base image is never modified during a session. An explicit **commit** writes the overlay back; **discard** drops it.
- **ST-3** Register HDD/IDE in the TTD registry. SD (Z-controller) is designed with the overlay from day one.
- **ST-4** Verify whether WD1793/FDD writes through the universal track model are already captured in TTD. If only controller state is captured, extend to track data via the overlay.
- **ST-5** A host-directory-backed virtual FAT (see toolchain doc §13.2) plugs in as an alternative base; the overlay semantics stay identical.
- **ST-6** (2026-09-28) Media come from the unified media manager ([2026-09-28-storage-manager](../2026-09-28-storage-manager/technical-design.md), PLAN #58): slots, media identity (`Medium::ContentId`, source, format, access), the `SessionWriteMap` session layer (becomes the journaled overlay of ST-1 in its phase M7) and `HostFolderFat` (the ST-5 base). TTD v2 takes media identity and the overlay from there instead of building its own. **TTD v1 does not handle media**: it works with the peripherals and media present, loads and stores none of them; its sessions record at port level, so a recorded response never changes on replay.

---

## 7. Universal snapshot (UNS) on the registry

### 7.1 Decision

Do **not** continue the `uns-snapshots` YAML serializer as an independent implementation. Rebuild UNS as a **container** over the same state producers TTD uses:

```text
snapshot.uns/                    (folder or single archive)
├── manifest.yaml                human-readable: model, config, media, timing, metadata, thumbnails
├── state/
│   ├── core.bin                 CPU + chipset (the TTDCheckpoint value part)
│   ├── ram.pages                page store (I-frame only) — main RAM
│   ├── peripherals/<id>.bin     TTDSerializable blobs, one per registered device
│   └── regions/<id>-<r>.pages   TTDPagedSerializable regions
├── media/                       disk/tape images + COW overlays (or references)
└── debug/                       optional: labels, breakpoints/trigger sets, debug-info refs
```

- The YAML manifest keeps the design goals of `emulator_snapshots.md`: human-readable, self-contained, versioned. But device state inside it is produced **by the registry**, never hand-serialized.
- An optional per-device human-readable decode (e.g. AY registers as named fields) is generated from device-provided field descriptors. It is informative, and never the source of truth.
- UNS ⇔ TTD: a UNS file is an I-frame plus manifest; a TTD session is a sequence of them plus deltas and journals. Loading a UNS can start a TTD session with no conversion.

### 7.2 Requirements

- **UNS-1** Save/load via the registry only; unregistered state = refusal (same rule as TTD).
- **UNS-2** Forward compatibility: unknown peripheral ids are preserved on re-save and reported on load.
- **UNS-3** Media: embed small images; reference large ones by content hash with an optional embedded COW overlay.
- **UNS-4** Import/export for `.sna`, `.z80` (existing loaders) and optionally `.szx`, mapping to/from registry state.
- **UNS-5** UNS is the fixture format for tests, LLM recipes and bug reports.
- **UNS-6** (2026-09-28) The manifest's media section is the media set of the unified media manager ([2026-09-28-storage-manager](../2026-09-28-storage-manager/technical-design.md)): per slot the slot id, source (path, relative when possible), format, access mode, `ContentId` (including a hash of session changes) and the dirty flag. On load the manager compares it with what is inserted and **reports** mismatches ("expects EYEACHE.TRD in fdd.b, the drive is empty"); it never swaps media by itself. The GUI offers "insert the expected media" as an explicit action. Embedding (UNS-3) stays optional for small media.

---

## 8. Conformance matrix

A single parameterized suite over `N_MM_MODELS` × peripheral configurations:

| Test | Assertion |
|---|---|
| Capture/restore round-trip | Restore(checkpoint N) → state hash equals hash recorded at N |
| Serialize/deserialize session | Session dump → load → identical hashes for all checkpoints |
| UNS round-trip | Save → load → identical state hash; continue N frames → identical to uninterrupted run |
| Reverse-step equivalence | §5.3 |
| Seek storm | Random seeks across I-frame boundaries; hashes match |
| Zero-bytes for absent peripherals | Session size unaffected by peripherals not fitted (registry Phase 3.1) |
| Storage overlay | Guest writes, seek back before the write, reread → pre-write content |
| Divergence corpus | Extend `TTD_Divergence_Corpus_Test` with one title per machine and per large peripheral |

The suite runs in CI; a new model is not merged until it passes (DoD-5).

---

## 9. Machine-specific notes

| Machine | State-framework stressors |
|---|---|
| Profi | Extended paging, own video modes; serializers present — needs conformance and real recordings |
| ZX-Evo BaseConf | 4 MB RAM, 14 MHz turbo, SD card (needs ST-1..3), CMOS/RTC state |
| TSConf | DMA active mid-frame (checkpoint at frame boundary must capture in-flight DMA state), sprites/tiles, CRAM, SFILE, per-line interrupts |
| ZX Spectrum Next | Z80N instruction set, copper, Layer 2, sprites, tilemap, DMA, 3.5–28 MHz switching, DivMMC, multiple memory-mapping schemes |
| Original models | Contention and floating-bus precision, +2A/+3 paging and FDC (µPD765); low state-framework risk |

---

## 10. Open questions

1. I-frame interval for device regions: same as RAM (50 frames), or per-region tuning based on dirty rate?
2. Should COW overlays for large media spill to disk during long sessions (tiered storage per POC `tiered-storage-analysis.md`)?
3. UNS archive container: zip (ubiquitous) vs 7z (per existing design note) vs zstd-framed tar — pick one for writing, accept all for reading?
