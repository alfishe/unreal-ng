# Attribute Clash, Automatically: TTD Analysis and Display-Side Mitigation

> Design note, 2026-09-27 — the broader question around the ZX-Poly port: using
> TTD + MCP/WebAPI to analyze attribute clashing *automatically*, and three
> ways to act on the result — (A) runtime shadow-attribute patching,
> (B) intervention in the ULA sampling algorithm, (C) per-game metadata loaded
> by content signature. Includes the coverage question: must we play through
> every screen, or is boot + discovery enough? Companions:
> [automated-colorization-pipeline.md](automated-colorization-pipeline.md) (shares
> the discovery/atlas infrastructure) · [zxpoly-platform.md](zxpoly-platform.md)
> (the machine-level alternative).

## 1. The design space

Attribute clash is a property of the *engine's placement grammar* meeting the
ULA's one-attribute-per-8×8-cell rule. Two families of fixes:

| Family | Mechanism | Examples |
|:--|:--|:--|
| **Machine-level** — give pixels more color bits | change the hardware model | ZX-Poly (4 CPUs), Spec256, TS-Conf Rainbow |
| **Display-side** — keep the single CPU and stock game, change what the renderer shows | filter between machine state and framebuffer | this document's A/B/C |

Display-side approaches are not heresy in this codebase: ULA+ support, CRT
filters, and scanline-accurate rendering are already "enhanced display" modes
over an honest machine state. The non-negotiable rule: **the machine state
(RAM, CPU, timing, TTD) must remain stock** — the filter lives only in the
rendering path. That single rule eliminates the classic failure of real-machine
attribute patches (games that *read* attributes back for logic — the Flying
Shark case — are untouched, because we never write to RAM).

## 2. Analysis: is a full playthrough needed?

No — for data-driven engines, boot + discovery gets ~everything, and static
analysis is *more complete* than observed play:

1. **Boot + TTD discovery** (the Layer-1 machinery of
   [automated-colorization-pipeline.md](automated-colorization-pipeline.md)
   §3): find the draw routine's memory reads → sprite/tile blocks; find the
   room-draw loop's index source → the map. This needs minutes of runtime, not
   a walk-through — the engine unpacks and shows one room, and its *code and
   data* reveal the whole grammar.
2. **Static potential-clash analysis**: given tile library + map + sprite
   placement rules, compute *all possible* cell co-occupancies (tile×tile,
   tile×sprite, sprite×sprite per cell) offline. This covers rooms the analyst
   never visits — observed playthroughs sample the space; the map *bounds* it.
3. **Attribute mining** needs some observed frames, but only to seed colors
   (§4 of the colorization doc); a few rooms per game usually suffice, and the
   static pass tells you which tile pairs actually need disambiguation.
4. **Playthrough remains necessary only for**: procedurally generated graphics
   (no stable data blocks), runtime-generated maps, compressed-with-branching
   assets that must be captured post-unpack, and final validation.

Verdict: *launch-and-discover, statically complete, play to validate* — not
"play everything".

## 3. Approach A — JIT shadow-attribute allocator

**Mechanism.** Maintain a shadow copy of the attribute file that only the
renderer sees. Each frame (or better, each scanline row) recompute the shadow
attributes from the current bitmap + color metadata, then let the stock ULA
rendering consume the patched values.

- **Per-scanline granularity is cheap in unreal-ng**: `ScreenZX` already
  renders beam-chased (per-T-state `Draw()`); rewriting the shadow attribute
  for row *y* at the start of row *y*'s display window is invisible to the CPU
  and introduces zero frame lag. This is the difference between a 20 ms-lagged
  per-frame hack and a correct filter.
- **Allocator**: per cell, determine the intended (ink, paper). Best source is
  metadata — the atlas (shape-hash → colors) matched against the composite
  bitmap (pixels-present voting over the tile library), falling back to the
  game's own attribute when the vote is ambiguous. Output: best-2-colors-per-cell
  rendering, i.e. clash *minimized*, not eliminated.

**Pros**: no game/code modification; no desync class of problems at all (no
ZX-Poly semantics needed); works per-game automatically after auto-mining — a
push-button "AntiClash" mode; rides existing beam-chased renderer; TTD replay
stays deterministic (filter is a pure function of machine state + metadata).

**Cons**: still 2 colors per cell — two different-colored sprites sharing a
cell compromise (allocator picks the dominant/last one; no per-pixel split
except via FLASH abuse); vote ambiguity → occasional wrong-color cells (needs
hysteresis to avoid flicker); FLASH-based effects and intentional mono cells
need policy; metadata quality directly bounds quality.

## 4. Approach B — intervene in ULA sampling

**Mechanism.** Instead of rewriting attributes, change what the renderer
fetches per pixel: attribute granularity finer than 8×8.

- **B1 — fine-grained attribute grid** (8×1 or 8×2, "hicolor-style"): the
  renderer samples a shadow attribute *per char-row*. Sprites and tiles in
  tile-engines are almost always char-row aligned in their draw logic, so
  per-row allocation kills most visible clash — this is what beam-racing games
  (Buzzsaw-class) achieve in software, done for free by the filter.
- **B2 — atlas-driven per-pixel sampling**: match each cell's bitmap (or
  subregions) against the tile library per frame and take colors from the
  matched entries at sub-cell granularity — approaching "sprite-perfect"
  coloring, per-pixel, without touching the game.

**Pros**: strictly better quality than A (B1 ≈ 8× improvement in cell
granularity; B2 near-ZX-Poly visuals on well-matched engines); same honesty
properties (display-only).

**Cons**: per-frame tile matching is the hot path (needs the hash library
indexed well — feasible: dirty-cell diffing limits work to changed cells);
mismatch → color flicker (needs stable matching + hysteresis); a real renderer
fork with its own mode plumbing (`VideoModeEnum`, `ScreenZX` composition) —
more surface than A's shadow file; risk of over-fitting to tile engines
(scraper/scaling engines match worse).

## 5. Approach C — per-game metadata by signature

Not a renderer — the enablement and persistence layer for A/B:

1. The automatic analysis of §2 + attribute mining produce a per-game bundle:
   engine profile, asset inventory (addresses/roles), atlas (colors), optional
   allocator hints (priorities, FLASH policy).
2. Bundles are keyed by **content signature** — hash chain over load-image +
   key RAM regions after boot stabilization (more stable than file hash:
   survives tape/disk re-packagings, breaks deliberately on hacked versions —
   which you want, since addresses shift).
3. On load: signature match → bundle applied → A/B filter armed. Unknown game →
   automatic cold-start analysis runs once (minutes), result cached into the
   local bundle store; user reviews optional.
4. Bundles are plain JSON → shareable "mods" library, git-diffable, and the
   same artifact feeds the ZX-Poly pipeline later.

**Pros**: reproducibility; zero-touch UX after first run; composes with A and B
and with the ZX-Poly port; sharing/community angle.

**Cons**: curation drift (game variants, trainers, translations need re-mining);
signature edge cases; storage of derived colors is per-game opinions, not truth
— keep it clearly labeled as a display enhancement.

## 6. Comparison (including the ZX-Poly column)

| | A: shadow attrs | B: ULA sampling | C: metadata DB | ZX-Poly port |
|:--|:--|:--|:--|:--|
| Visual result | best 2 colors / cell | per-row…per-pixel | (enables A/B) | true 16/pixel |
| Game/code changes | none | none | none | data-only, loader |
| Machine state | stock | stock | stock | real 4-CPU machine |
| Desync risk | none | none | none | lockstep discipline |
| Works on unknown games | after auto-analysis | worse without metadata | cold-start auto | needs adaptation |
| Fidelity story | display filter (like ULA+/CRT) | display filter | n/a | new machine model |
| Effort (below) | ~2 wk | +2–3 wk | ~1 wk | 7–9 wk (port) |

A/B/C and ZX-Poly **share** the discovery + atlas infrastructure — building
A/C first de-risks and directly subsidizes the port's Phase 3.

## 7. Effort

| Piece | Effort | Notes |
|:--|:--|:--|
| Discovery harness (shared) | 1–2 wk | already scoped in [automated-colorization-pipeline.md](automated-colorization-pipeline.md) §8 |
| Static clash-pair analysis + report (map × tiles) | 3–5 d | pure offline math over inventory; MCP/WebAPI report endpoint |
| Attribute miner (shared) | 2–3 d | as in colorization doc §4 |
| **A**: shadow-attr allocator + per-scanline integration in `ScreenZX` | ~2 wk | allocator core 3–4 d; beam-chased hook 3–5 d; glue+hysteresis 3 d |
| **B1**: per-char-row grid mode | +1 wk | renderer mode plumbing |
| **B2**: per-pixel atlas matching | +1–1.5 wk | matching engine + flicker tuning (riskiest) |
| **C**: signature store, load-time match, bundle format | ~1 wk | JSON + hash chain |
| WebAPI/MCP surface (toggle, status, bundle upload, clash report) | 2–3 d | parity rule: same info from same source |

MVP = discovery + miner + A + C-store ≈ **5–6 weeks** for a push-button
AntiClash mode over the DIZZY_X corpus already in `testdata/loaders/`.
B1/B2 are quality upgrades afterward; ZX-Poly remains the "true fix" track.

## 8. Risks and honest limits

- The allocator can *minimize* but not eliminate clash (fundamental: 2 colors
  per cell in A; B's quality is bounded by matching reliability).
- Games whose look *depends* on attributes as logic (inking effects, FLASH
  storytelling) need per-game policy knobs in the bundle — flag, don't guess.
- "Automatic" claims must stay *auto-seeded, human-tunable* — same honesty rule
  as the colorization doc.
- Renderer forks (B) risk divergence from stock rendering paths — keep the
  filter as a composable stage, tested against golden frames of stock mode.
- Metadata bundles encode subjective color choices; version them.

## 9. Recommendation

1. Build the shared discovery + mining + atlas core once (it serves this track
   *and* the ZX-Poly port — the strongest reason to do it early).
2. Ship **A + C** as the push-button AntiClash display mode (per-scanline
   shadow attributes, signature-matched bundles, honest machine state).
3. Add **B1** (per-char-row) when A's per-cell compromise proves limiting;
   treat **B2** as experimental.
4. Keep the **ZX-Poly port** as the separate, higher-fidelity track it is —
   these display modes make unadapted games look better *today*; the port makes
   them *be* better.
