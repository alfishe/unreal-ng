# 04 — ZXDLSS, Semantic Game Layer, Mods and Multiplayer

| | |
|---|---|
| **Status** | Proposal / draft for review (research-heavy) |
| **Date** | 2026-09-21 |
| **Baseline** | master `ae4d40b` |
| **Related** | `2026-09-07-hud-layer/` (native-resolution HUD), `2026-09-19-unrealengine-integration/`, `core/src/emulator/memory/memoryaccesstracker.h`, `core/src/debugger/ttd/ttdinputjournal.h`, docs 01–03 |

---

## 1. Summary

"ZXDLSS" is a staged program to go from the original ZX screen to a reconstructed, enhanceable presentation of a game, in which the Z80 ultimately serves only the game logic:

1. **GigaScreen de-flicker** only for static objects (by mask), leaving dynamics untouched.
2. **Frame-analysis sprite extraction** and attribute-clash removal.
3. **Runtime tracking**: variables, sprites and masks read directly; intervention in rendering.
4. **Reconstructed parallel rendering**: the ZX screen stays as-is, and a re-created picture is drawn alongside it. This enables mods, hi-res packs and entirely new visualizations, analogous to retrofitting General Sound speech into old games.

Beyond rendering, the same semantic layer enables mods, audio remastering, UX features and multiplayer for almost any game, up to rewriting game logic in Lua.

The key architectural point: all of these consume **one semantic model per title**.

---

## 2. The semantic game layer

### 2.1 Game manifest

A per-title manifest, content-addressed by code hash (knowledge base, doc 03 §5):

```yaml
title: "Example Game"
code_hash: "sha256:…"
machine: { model: SPECTRUM128 }
sample_points:
  frame_logic_done: { event: exec, at: "0x8F20" }          # after entity update
variables:
  lives:   { addr: "0x5DC0", type: u8, events: [death] }
  room:    { addr: "0x5DC4", type: u8 }
  score:   { addr: "0x5DB0", type: bcd6 }
entities:
  table:   { base: "0x6000", stride: 8, count: 16 }
  fields:  { x: 0, y: 1, type: 2, frame: 3, flags: 4 }
routines:
  sprite_draw: { addr: "0x9A00", args: { sprite: "hl", x: "b", y: "c" }, masked: true }
  text_print:  { addr: "0x9C10", args: { str: "hl" } }
  input_read:  { addr: "0x8800" }
graphics:
  sprite_bank: { addr: "0xB000", format: "zx_mono_masked", w: 16, h: 16, count: 64 }
events:
  death:  { on: { var: lives, change: decrement } }
  pickup: { on: { exec: "0x91B4" } }
status: verified            # draft | verified | curated
```

- **Declarative first, with a Lua escape hatch** for irregular cases.
- **Producers:** analyzers, batch runs, LLM recipes, human curation (doc 03).
- **Consumers:** reconstructed renderer, asset packs, netplay, mods, achievements, accessibility, agents.

### 2.2 Bootstrap

- POKE databases identify variables (lives, energy, time) for thousands of titles.
- Fingerprints identify common routines (sprite blitters, text printers, music players).
- The discrepancy loop (§3.6) drives completion.

---

## 3. Rendering program (ZXDLSS stages)

### 3.1 Stage 1 — GigaScreen de-flicker by mask

GigaScreen alternates two screens per frame to fake extra colors. Naive blending also smears moving objects.

- **Z-1** Temporal per-pixel/per-cell analysis over a window of frames. Cells with a stable 2-frame period and no spatial motion are classified static-GigaScreen; others are left as-is.
- **Z-2** Blend only the static mask; present dynamic regions from the latest frame.
- **Z-3** Hardware GigaScreen (Pentagon 1024 `#EFF7` bit 4, already decoded in `portdecoder_pentagon1024.cpp`) is handled by the video path directly. Software GigaScreen (screen flipping via paging) is handled by the analyzer.
- **Z-4** Same machinery for sprite multiplexing flicker (objects drawn on alternate frames due to time budget), merged when the manifest or analysis identifies them.

### 3.2 Stage 2 — Frame-analysis sprite extraction and clash removal

- **Z-5** Background model per room/screen; foreground = difference; connected components → sprite candidates tracked across frames.
- **Z-6** Clash removal: foreground pixels take the sprite's dominant ink; background cells keep original attributes.
- **Limitation:** purely image-based methods cannot resolve overlapping sprites or masked XOR drawing reliably. Stage 2.5 fixes this.

### 3.3 Stage 2.5 — Draw-call recovery via write provenance

The emulator knows **which instruction wrote each screen byte** and **where it read the data from**. Pure image methods lack this.

- **Z-7 Screen write provenance.** For each screen-memory write, record (PC, source address of the data read in the same routine iteration, logical op AND/OR/XOR/LD, T-state). Existing `MemoryAccessTracker` tracks caller addresses per region in aggregate only; this needs a per-frame, per-write provenance ring for screen regions, gated by feature flag.
- **Z-8 Blitter recognition.** Cluster writes by PC and loop structure; recognize masked/unmasked blit routines (fingerprints + analysis).
- **Z-9 Draw calls.** Hook recognized blitters (trigger on entry/exit) to obtain `draw(sprite_src_addr, x, y, mask_src_addr, op)` per frame.
- **Z-10 Correct clash handling.** Color is assigned per draw call, not per attribute cell.

### 3.4 Stage 3 — Runtime tracking and render intervention

- **Z-11** With a manifest: read entity tables at the sample point (doc 02 §9) for exact positions, types and animation frames.
- **Z-12** Intervene in rendering: suppress or replace specific draw calls in the presented image. The ZX framebuffer itself is untouched, and the intervention is in the presentation layer.

### 3.5 Stage 4 — Reconstructed parallel rendering

- **Z-13** Two outputs: original ZX screen (source of truth, always available side-by-side or as an overlay) and a reconstructed scene rendered from the semantic state.
- **Z-14** Renderer backends:
  - native 2D renderer at output resolution (reusing the HUD layer approach: native resolution, not framebuffer resolution);
  - external engines through the embedding API, e.g. Unreal Engine per the UE integration design.
- **Z-15** The Z80 runs unmodified logic; the new visuals are driven by sampled state.

### 3.6 Discrepancy loop

- **Z-16** Continuously compare "what the ZX screen shows" vs. "what the manifest explains". Unexplained screen writes (provenance from a routine not in the manifest, or pixels no draw call accounts for) are logged.
- **Z-17** Discrepancies feed LLM/analyzer tasks (doc 03 §7.2) to extend the manifest.

---

## 4. Asset packs

- **A-1 Keying by content hash of source graphics bytes** (the data a draw call reads), not by title or screen position. This is the Dolphin/DuckStation texture-pack model: a pack works on any dump of the same data.
- **A-2 Dump pipeline:** automatically dump every distinct sprite/tile/font glyph encountered, with hash, dimensions, mask, palette context and usage count.
- **A-3 Replacement:** artist-provided images at any resolution, with optional normal/emission maps for 3D backends.
- **A-4 Pack format:** manifest + images; versioned; references the title manifest version it was built against.
- **A-5 Fallback:** missing replacements render the original sprite scaled.

---

## 5. Further visual features

| Feature | Description | Depends on |
|---|---|---|
| Motion interpolation | Many games update at 12.5–25 Hz. Exact per-entity motion vectors from sampled state allow true interpolation to 50/60 Hz and higher on VRR displays, which also addresses the VRR weakness of the low-latency "game mode" preset | Stage 3, sample points |
| Widescreen | Scroll detection + accumulated tile map extend the view beyond 256 px in scrollers | Stage 2.5/3 |
| Automap | Room variable + room screenshots stitched into a world map (adventures, Dizzy-style games) | Manifest (`room`) |
| True 3D for isometric games | Filmation-style games keep real 3D coordinates in variables; render them as a 3D scene (UE backend is a natural showcase) | Manifest, embedding API |
| Native-resolution text | Hook the text printer, render strings with a native-resolution font through the HUD layer | Manifest (`text_print`) |
| On-the-fly localization | Replace strings at the print hook (continuing the ZX translation tradition) | Same |

---

## 6. Audio remastering

| Feature | Description |
|---|---|
| Beeper engine recognition | Fingerprint known beeper music engines; extract score data; re-render with high-quality synthesis |
| Semantic sound effects | Beeper/AY effects mapped to events (jump, shot, death) and replaceable with samples |
| AY player extraction | Detect PT2/PT3/STC/SQT etc., extract patterns and instruments; re-render with remastered instruments while keeping the arrangement |
| General Sound retrofits | Generalize the "GS speech in old games" idea: event-driven sample playback added to titles that never had it |

Audio replacement is a presentation-layer feature. The emulated AY/beeper output stays available for comparison and for determinism.

---

## 7. Gameplay and UX features

| Feature | Description |
|---|---|
| Achievements | RetroAchievements-style conditions over memory = trigger engine conditions (doc 02) with manifest symbols |
| Assist mode | Infinite lives via variable, slowdown only in hard segments (trigger-driven speed control), highlighting interactive objects |
| Accessibility | Colorblind palettes (per-draw-call recoloring), spoken game-state summaries from manifest variables, remappable controls |
| Level editor | Reconstructed map + tile data → edit → write back (mutate actions, journaled) |
| Playing agents | Semantic state as observation for automated testing, speedrun routing, or an LLM actually playing the game rather than reading pixels |

---

## 8. Multiplayer

### 8.1 Tier 1 — Rollback netplay for unmodified games

For titles with native two-player modes (hotseat, split keyboard, two joysticks):

- **M-1** Deterministic core + input journal (`ttdinputjournal.h`) + TTD v2 checkpoints provide the GGPO-style ingredients: predict remote input, roll back on mismatch, re-simulate.
- **M-2** Rollback depth budget: restore (~110–160 µs per TTD v2 POC) + re-simulation of k frames within the frame budget. Needs a benchmark with k = 4–8 frames at 3.5 MHz and turbo models.
- **M-3** Desync detection: periodic state-hash exchange; the desync report includes the first diverging frame (TTD) for debugging.
- **M-4** Spectator mode: broadcast the input journal, not video.

### 8.2 Tier 2 — Asynchronous multiplayer (ghosts)

- **M-5** A recorded input journal is replayed in a hidden instance. Player position from the manifest is drawn as a ghost overlay on the live game (HUD layer).
- **M-6** Race-the-ghost, speedrun comparison, shared runs. No modification of game logic.

### 8.3 Tier 3 — Grafted multiplayer via game logic

- **M-7** A second player as an entity injected into the game's entity table by Lua `logic` scripts (doc 02 §8), synchronized across instances.
- **M-8** Collision/interaction usually requires Lua reimplementation of parts of the game logic. The manifest declares which routines are overridden.
- **M-9** Authority model: one host instance authoritative; clients predict; state exchanged at sample points.
- **M-10** Lua logic state must be in checkpoints (doc 02 L-1) or rollback breaks.

---

## 9. Mods and logic rewriting

- **MOD-1** Mods are packages of manifest extensions + Lua `logic`/`view` scripts + asset packs, keyed to the title's code hash.
- **MOD-2** Hook points: routine entry/exit (replace, prepend, append), variable writes, events, sample points.
- **MOD-3** Full rewrite: the Z80 keeps running as a "logic server" where useful, and Lua replaces routines incrementally.
- **MOD-4** Determinism: `logic` scripts obey doc 02 L-2. Mods therefore remain compatible with TTD, netplay and recipes.
- **MOD-5** Distribution: content-addressed packs; loading refuses on hash mismatch, with a clear explanation.

---

## 10. Architectural requirements (summary)

| # | Requirement | Owner doc |
|---|---|---|
| AR-1 | Sample points as named triggers publishing consistent state | 02 §9 |
| AR-2 | Screen-write provenance ring (PC, source address, op, T-state), feature-gated | this §3.3 |
| AR-3 | Lua state in checkpoints; logic/view roles | 02 §8 |
| AR-4 | Presentation-layer pipeline separate from the ZX framebuffer, native resolution | HUD layer design + this §3.5 |
| AR-5 | Content addressing for manifests, packs, mods | 00 principles |
| AR-6 | Embedding API for external renderers (UE) exposes sampled semantic state, not just the framebuffer | UE integration design |
| AR-7 | Rollback-grade restore + re-simulation benchmarks | 01 conformance, this §8.1 |
| AR-8 | Discrepancy detection between original screen and reconstruction | this §3.6 |

---

## 11. Phasing

| Phase | Deliverables |
|---|---|
| S0 | Manifest schema; manifest loader; sample points (depends on doc 02 T4) |
| S1 | GigaScreen static-mask de-flicker (Z-1..Z-4) — independent of manifests |
| S2 | Frame-analysis sprite extraction + clash removal (Z-5, Z-6) |
| S3 | Screen-write provenance + blitter recognition + draw calls (Z-7..Z-10); sprite dump |
| S4 | Asset packs (A-1..A-5); native-resolution text; interpolation |
| S5 | Rollback netplay (Tier 1); ghosts (Tier 2) |
| S6 | Reconstructed rendering (Stage 4) with native 2D backend; discrepancy loop |
| S7 | UE backend showcase (isometric 3D); audio remastering; mods framework; Tier 3 multiplayer |

---

## 12. Open questions

1. Provenance granularity: per byte for screen memory only, or also for attribute memory and off-screen buffers (games that draw to a back buffer and copy)? Back-buffer games need provenance to follow the copy.
2. Manifest authoring UX: LLM-drafted with human review in the debugger, or a dedicated editor view?
3. Netplay transport: own UDP protocol, or an existing rollback library behind the input/checkpoint interfaces?
4. Pack and mod distribution: a community registry, or plain files with hashes only?
