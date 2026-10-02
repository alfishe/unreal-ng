# TTD v2: a new engine next to v1, and its names

Decided 2026-10-02. Part of the [TTD v1 → v2 migration](README.md).

## Decision

TTD v2 is written as a **new engine, `ttd::TimeTravelEngine`**, next to today's `ttd::TimeTravelManager` (v1). v1 keeps running the emulator until v2 matches it on every check. Then the emulator switches to the engine, and `TimeTravelManager` is deprecated and deleted.

The other option was to change v1 in place, step by step. It was not chosen.

## Why a new engine

- **The changes are large.** Phases 1–5 touch memory storage, device state, the journals, the memory budget and the file format. Each one in place would rework a running engine while it must stay correct.
- **v1 stays a reference.** A frame or a point inside a frame restored by the engine must match the same position restored by v1, byte for byte. That check needs v1 intact.
- **Recorded files are a ready-made input.** A v1 `.ttd` file holds both kinds of data TTD records: the snapshots at every frame boundary, and the events with their exact timing (memory writes, port reads and writes, input). The engine can be fed from these files in tests and benchmarks, frame by frame, in the same form the live capture will hand it.
- **The expected result is known.** [POC 011, experiment E6](../../../tools/poc/011-ttd-v2-capture-analysis/experiments/e6-v1-v2-model/README.md) models the engine's bytes, in memory and in the file, on real sessions. The C++ engine has to reproduce them.

What TTD records does not depend on this choice. The engine records the same two kinds of data as v1:
- **events with their exact timing, as they happen:** input, port reads and writes, memory writes, each at its T-state inside the frame;
- **snapshots at frame boundaries:** CPU, memory, device state.

## Names

Rule: **no "V2", "New" or "Next" in any class, file or folder name.** The version lives only in the `.ttd` format number. When `TimeTravelManager` is deleted, every name of the engine is already final, and nothing is renamed.

| What | v1 (to be deleted) | Engine |
|---|---|---|
| Main class | `ttd::TimeTravelManager` | `ttd::TimeTravelEngine` |
| Files | `timetravelmanager.{h,cpp}` | `timetravelengine.{h,cpp}` |
| Emulator context pointer | `pTimeTravelManager` | `pTimeTravelEngine` |
| Parts that only the engine has | — | folder `core/src/debugger/ttd/engine/`, named by what they do, e.g. `TTDPieceStore` (replaces `TTDCodecPageStore`), `TTDDeviceHistory` (device state versions) |
| Parts both use | `ttdcompression.h`, the peripheral registry, the serializers, the input and port journals | stay where they are, shared |
| Automation and UI | WebAPI `/ttd`, MCP `time_travel`, CLI, Lua, Python, Qt | names unchanged; they switch from the manager to the engine inside, and users see no difference |

**Why "Engine".** The class records, moves along the timeline, restores, and answers queries (who wrote this address, which port was read). "Recorder" would name only the recording, "History" only the storage.

## What this changes in the roadmap

The phases keep their order and their checks. A phase now adds its feature to the engine instead of changing v1. The switch from v1 to the engine and the deletion of v1 become a step of their own, at the point where the engine matches v1 on every check. Phase 6 (cleanup) then removes what only v1 used.

The roadmap is reworded accordingly once the review of what the engine must support from the start is done.
