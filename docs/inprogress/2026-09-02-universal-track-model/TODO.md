# TODO — Universal track model (2026-09-02)

**Status:** mostly complete — model, WD1793 integration and all twelve file-format
loaders are implemented **including the flux pipeline (FluxPll, MFM/FM codecs) and
the HFE/SCP loaders** (`a637bfa7`, 2026-09-05; verified 2026-09-16). The remaining
work is (a) wiring HFE/SCP into the load/save dispatch and (b) the flux bridge —
the **biggest pending item** in this work item.

## Progress
- Universal per-track model with variable-length raw stream, clock-mark and
  weak-bit bitmaps, `Track::reindex()` sector indexing; WD1793 read/write sector +
  track, rotational latency, FM path (`fm-support.md`).
- Loaders implemented (read + write as applicable): TRD, SCL, UDI, FDI, DSK/EDSK,
  TD0, MGT/IMG, Hobeta, **HFE v1/v3** (weak bits, roundtrip save), **SCP**
  (25 ns flux, multi-revolution majority merge, weak-bit detection) —
  `core/src/loaders/disk/`, tests in `core/tests/loaders/disk/`.
- Flux pipeline in `core/src/emulator/io/fdc/flux/`: `FluxPll`, `MfmEncoder/
  MfmDecoder`, FM encoders/decoders, with unit and round-trip tests.
- Non-standard layouts groundwork superseded the old diskimage-modernization
  Phase 1 ([`../2026-01-24-diskimage-modernization/TODO.md`](../2026-01-24-diskimage-modernization/TODO.md)).

## Remaining (value order)
1. **Flux bridge — KryoFlux / Greaseweazle direct integration** — design:
   [flux-bridge.md](flux-bridge.md). L1: KryoFlux stream import (loader);
   L2: live Greaseweazle device bridge (`IFluxAdapter` → `BridgeDrive`), modes,
   TTD materialize-then-emulate policy, write path with safety interlocks.
   Prior art: WinUAE FloppyBridge (RobSmithDev) — analysis in the design doc.
   → PLAN T2 item #12.
2. **Wire HFE/SCP into `Emulator::LoadDisk`/`SaveDisk`** — the loaders shipped in
   `a637bfa7` but the extension dispatch in `emulator.cpp` has no `hfe`/`scp`
   branches, so GUI/automation cannot reach them (Phase B0 of the design, small,
   independent win).
3. Copy-protection regression corpus — protected-disk fixtures + golden traces
   through the flux pipeline (open question 7 of diskimage modernization); grows
   naturally once real-adapter captures exist (bridge B2+).

## Pointers
- Cumulative plan: [`../PLAN.md`](../PLAN.md) — item #12 (T2); HFE/SCP wiring rides
  the same item.
