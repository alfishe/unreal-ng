# Software metadata profile and memory markup

Status: design, 2026-11-05. Owner decisions:

- everything known about a program goes into one software metadata profile;
- it is the existing game profile (one `id`, one `match`) with new sections, not a second file; the profiles move
  from `data/controllers/profiles/games/` to a shared `data/software/`, and the controllers read their part.

First consumer: the disassembler's virtual window ([../2026-11-05-disasm-virtual-scroll/README.md](../2026-11-05-disasm-virtual-scroll/README.md)),
which needs to know where code and data are and which labels to show.

## 1. What exists, and what is missing

| Piece | Where | State |
|:--|:--|:--|
| Identifying the program | game profile `match` (machines, `signatures`, `file_sha256`, `file_set`); `GameMatch` (`core/controllers/src/profiles/profilestore.h`): manual, signature, file hash, fuzzy TR-DOS file set; keys from `swsignature` when media loads | works, used by controllers only |
| Per-byte runtime facts | L0 `MemoryAccessFacts` (opcode / operand fetch, data read / write, stack, SMC), tracker level `Facts`; coverage analyzer (executed instruction starts, 64K bits) | works, not kept, not shown as markup |
| Regions | L2 `RegionStore` (`core/src/debugger/analyzers/analysis/l2model.h`): page-local `PhysKey` ranges, `RegionKind` (Code, Data, Text, Font, Sprite, Tile, Screen, MusicData, Packed, LevelMap, Variable, PointerTable, ...), confidence, observed / inferred, detector, label, title | in memory per analysis instance; out only as ctl / skool / map / symbols exports |
| Code signatures | L2 catalog: 48K ROM routines, one PT3 player | names routines, not programs |
| Labels | `LabelManager`: prioritized sets (`user` 1000000, files from 100, `bundle:` 50, `live:`); ~20 file formats; page-aware (RAM / ROM page + offset, or any page) | nothing from a profile or the analysis pushes labels |
| Profile sections | `profile.cpp`: schema, kind, id, label, match, applies_to, extends, connectors, players, joystick_keys, options, layers, layer_key, haptics, events | no regions, labels, comments, entries |

Missing: a place where memory markup from all sources meets, with priorities, and a profile section that stores it.

## 2. The profile

`data/software/<machine>/<id>.yaml` (the moved game profiles, with `software/index.yaml`). The present sections
stay as they are; a `memory:` section is new (`core/controllers/src/profiles/memoryprofile.h`, a play profile's
only):

```yaml
schema: 1
kind: software
id: skool-daze
label: Skool Daze
match:
  machines: [48k, 128k]
  file_sha256: [...]
connectors: ...          # controllers, unchanged
events: ...              # unchanged
haptics: ...             # unchanged
memory:
  regions:
    - {at: "0x6000-0x7FFF", kind: code, title: "game engine"}
    - {at: "page:1:0x0000-0x1AFF", kind: sprite, title: "characters", geometry: {w: 3, h: 32}}
    - {at: "0xB000-0xB3FF", kind: data, sha256: "..."}   # only while the bytes match
  labels:
    - {name: main_loop, at: "0x6A00"}
    - {name: draw_sprite, at: "page:1:0x2000", comment: "IX = sprite"}
  symbols: [skool-daze.usym.json]   # bulk labels next to the profile, any LabelManager format
  entries: [{name: start, at: "0x5DC0"}]
  comments:
    - {at: "0x6A10", text: "waits for the frame interrupt"}
```

- **Addresses** use the syntax the haptic probes already use: a CPU address (`0x6A00`, `#6A00`, `$6A00`, decimal;
  any page) or a page place (`page:<ram n>:<addr>`, `rom:<n>:<addr>`, the offset taken inside the page whichever
  window it was written for); ranges `lo-hi` inclusive or `at` + `size`; a page range stays in its page.
- **Kinds** are the L2 `RegionKind` names (lower case), so analysis output and profiles speak one vocabulary.
- **Guard**: a region may carry the `sha256` of its bytes; when the bytes at that place differ (another
  level loaded, the program not yet unpacked) it is ignored until they match again. Programs that load parts at run
  time stay right.

## 3. Core: `MemoryMarkup`, a layered store

`core/src/debugger/markup/memorymarkup.h`, owned by the `DebugManager`, one per CPU. Layers, the higher wins where
claims overlap (as the label sets):

| Layer | Priority | Filled by |
|:--|:--|:--|
| `user` | highest | the debugger UI and the planes (mark as code / data, rename) |
| `profile:<id>` | high | the matched profile's `memory:` section when media loads and the profile matches; dropped on another match |
| `analysis` | middle | an L2 run (`RegionStore` claims, confidence kept) on request |
| `runtime` | low | derived on query from L0 facts / coverage when tracking is on: opcode fetch = code start, operand fetch = code, read but never executed = data; no new hot-path work |
| `bundle:<rom>` | lowest | ROM symbol bundles (`data/symbols/manifest.json`) gain regions next to their labels |

Queries (resolve a Z80 address through the current paging, then look up by physical key):

| Call | Gives |
|:--|:--|
| `At(address)` | the winning claim: kind, layer, range, title, confidence |
| `Ranges(from, to)` | the claims in a Z80 range, flattened by priority (the view's colouring and the disassembler's rows) |
| `IsInstructionStart(address)` | an anchor for the disassembler (code region starts, entries, labels on code, runtime opcode fetches) |
| `Mark(range, kind, title)` / `Unmark(range)` | the `user` layer |

Labels from a profile go into `LabelManager` as a set `profile:<id>` (between the bundles and loaded files), so the
existing label machinery (page awareness, priority, enable / disable) serves them unchanged.

## 4. Saving back

Explicit only (nothing is written on its own): "save to profile" writes the `user` layer, chosen `analysis` claims
and the `user` labels into the profile's `memory:` section (or a new profile with the current `match` keys).

## 5. Planes

| Plane | Form |
|:--|:--|
| REST | `GET /markup?from=&to=`, `PUT /markup` (user layer), `GET /markup/layers`, `POST /markup/save-profile` |
| WebSocket | the same as calls; `markup` topic on changes |
| MCP | `debug_code` actions `markup`, `mark`, `save_profile` |
| CLI / Python / Lua | `markup`, `mark`, `unmark`, `markup-save` |

## 6. Steps

| Step | What |
|:--|:--|
| 1 | Move game profiles to `data/software/` (loader, index, packaging, docs); `memory:` section parsed, validated, kept and saved back (`MemoryProfile_Test`) |
| 2 | `MemoryMarkup` with layers, physical keys, queries; profile layer + profile label set on a match |
| 3 | Runtime layer from L0 facts / coverage; analysis layer from L2 |
| 4 | Planes; save to profile |
| 5 | Consumers: disassembler window (anchors, data rows), memory viewer colouring |

## 7. Open points

- The user's own game profiles stay in `<user config>/unreal-ng/controllers/profiles/games/` (step 1 moved the
  shipped ones only); whether they follow to `<user config>/unreal-ng/software/`.

- Physical keys of other machines (Sprinter, ATM, Next pages) go through the same absolute page index; card CPUs get
  their own store.
- Whether the analysis layer refreshes on its own after an L2 run or only on request.
</content>
