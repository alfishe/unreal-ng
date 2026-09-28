# Metadata Manager — Requirements

**Created:** 2026-09-27
**Status:** draft for review
**Design:** [design.md](design.md) · [trigger-integration.md](trigger-integration.md)
**Related:** [ZX DLSS GigaScreen](../2026-09-27-zxdlss-gigascreen/requirements.md),
[Look-Ahead Manager](../2026-09-27-lookahead-manager/requirements.md),
roadmap [03 §5 knowledge base](../2026-09-21-roadmap/) and
[04 §2.1 game manifest](../2026-09-21-roadmap/04-zxdlss-semantic-layer-and-multiplayer.md)

## Goal

One universal place to store *what we know about what is happening on screen
and in the machine, and when* — produced by analyzers, filters and enhancers at
runtime, saved to disk, shared, and loaded back automatically when the same
software runs again.

The first user is GigaScreen: record where and when color mixing happens, so
that a known demo can be played with pre-recorded rules ("from this moment,
average this mask with period 2; stop at that moment") and runtime analysis can
be reduced or switched off.

## Terms

| Term | Meaning | Example |
|---|---|---|
| Record | one fact with a time span, optional screen/memory area, typed fields, tags | "cells in mask M flicker with period 2, frames 1200–1850" |
| Layer | a named collection of records from one producer, with a versioned schema | `gigascreen`, `sprites`, `bookmarks` |
| Tag | a label on a record for search and filtering | `period=2`, `part=tunnel`, `verified` |
| Blob | large binary payload stored once and referenced by hash | a 32×192 segment mask |
| Anchor | a recognizable moment in the software, used to align time between runs; defined by a **trigger** | "code with hash H at `ram3:0000` starts executing" |
| Trigger | event + condition + actions (the planned trigger engine, generalizing conditional breakpoints) | exec at `ram3:0000` if `physhash(ram3:0000,256)==H` |
| Pack | serialized layers for one piece of software, with its signatures | `across_the_edge.umeta` |
| Library | installed packs, indexed by signature | built-in + user folder |
| Signature | a content fingerprint identifying software | disk image hash, per-file hash, code-block hash |

## Requirements

| ID | Requirement |
|---|---|
| MD-1 | Generic: records carry a time span (point or interval), optional area (screen rect, segment/cell mask, memory range), typed fields defined by the layer schema, tags, confidence, origin. The manager knows nothing specific about GigaScreen. |
| MD-2 | Layers are registered by producers with a name, schema version and field definitions. Several producers can coexist; consumers query any layer. |
| MD-3 | Tags: free labels with optional values, hierarchical names (`giga/period=2`), indexed for queries. |
| MD-4 | Runtime collection is cheap: producers append per-frame observations; the manager **coalesces** repeated identical observations into intervals and **deduplicates** blobs by content hash. |
| MD-5 | Time is the emulated timeline (frame, T-state within frame — same as TTD time points). Records in packs never use absolute frames: they are placed **relative to anchors** (anchor + offset), **switched by triggers** (on/off), or **active while a frame-level condition holds**. Anchors are defined by triggers. |
| MD-6 | Record origin and lifecycle: `runtime`, `speculative` (from look-ahead; confirmed or dropped when the canonical run arrives), `loaded` (from a pack), `manual` (user-edited). |
| MD-7 | Serialization: compact binary pack format (chunked, versioned, zstd, with index) **and** human-readable JSON/YAML export/import of the same content; CSV export for statistics. Round-trip binary → JSON/YAML → binary is lossless. |
| MD-8 | Identification at start: signatures computed from inserted media (whole image, per-file within TRD/SCL/TAP/TZX), snapshots, and runtime code blocks; the library proposes matching packs with a confidence score. |
| MD-9 | Consumption modes per layer: **runtime only**, **hybrid** (pack rules applied immediately, runtime analysis verifies and can override), **pack only** (runtime analysis off, rules applied at anchored times). |
| MD-10 | Rule playback: a consumer can subscribe to "records active now" on a layer and receive enter/leave events as the timeline crosses record boundaries. |
| MD-11 | Access from WebAPI, MCP, Lua and Python: query, add, edit, export, import, load pack, save pack. |
| MD-12 | GUI: timeline view with one track per layer, record inspection, tag filter, manual editing; overlay of record areas on the screen. |
| MD-13 | Interplay with TTD: metadata is not machine state and survives seeks. Records collected on a branch that is abandoned by rewind-and-diverge are marked and can be dropped. A `.ttd` file can embed metadata layers (existing TTD bookmarks become the first such layer). |
| MD-14 | Storage locations: built-in packs in `data/metadata/`, user packs under the writable path (`FileHelper::GetWritablePath()`/metadata). |
| MD-15 | Test suite: schema/versioning, coalescing, blob dedup, anchor resolution, all serialization round-trips, signature matching, playback timing, fuzzed file loading. |
| MD-16 | Pack triggers use the shared trigger engine (conditional breakpoints generalized); no separate trigger mechanism inside the metadata manager. |
| MD-17 | Pack triggers work in normal play, without the debugger, at a measured and bounded cost (play-mode arming). |
| MD-18 | Entry points are recognized by content (hash of code bytes at a physical page address), not by address alone. |
| MD-19 | Pack triggers fire in the look-ahead shadow run too (records tagged speculative), so pack rules for upcoming frames are known in advance. |
| MD-20 | Pack authoring tool: proposes triggers automatically from a TTD recording and validates timing independence by replaying with different load timing. |
