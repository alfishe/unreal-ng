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

`core/src/debugger/markup/memorymarkup.h`, owned by the `DebugManager` (`GetMemoryMarkup()`; the main CPU, card
CPUs later). Layers, the higher wins where claims overlap (as the label sets), inside a layer the claim made later:

| Layer | Priority | Filled by |
|:--|:--|:--|
| `user` | highest | the debugger UI and the planes (mark as code / data, rename) |
| `profile:<id>` | high | the matched profile's `memory:` section when media loads and the profile matches; dropped on another match |
| `analysis` | middle | the instance's last L2 run (`RegionStore` claims on their pages, confidence kept; the `untouched` filler left out; decoded instructions as entries), taken on its own when a new build is there; `SetLayer(analysis)` puts other content in its place until cleared |
| `runtime` | low | derived at each query from the L0 facts (level `facts`; Z80 mode: CPU places, physical mode: page places) or, without them, the coverage: fetched = code, opcode fetch = instruction start, data / stack access only = data; coverage starts at most 4 bytes apart join into one stretch. No new hot-path work (the window's kinds read under one lock), no revision step while the program runs |
| `bundle:<rom>` | lowest | ROM symbol bundles (`data/symbols/manifest.json`) gain regions next to their labels |

Queries (resolve a Z80 address through the current paging, then look up by physical key):

| Call | Gives |
|:--|:--|
| `At(address)` | the winning claim: kind, layer, source, title, confidence |
| `Ranges(from, to)` | the CPU range as spans of one winning claim each, flattened by priority (the view's colouring and the disassembler's rows); `claimStart` when a span begins at its claim's first byte |
| `Anchors(from, to)`, `IsInstructionStart(address)` | instruction starts known for certain: entries of every layer (runtime opcode fetches and L2 instructions included) unless a higher layer claims the byte as no code, and the first byte of a winning code claim |
| `Comments(from, to)` | one comment per address, the highest layer's |
| `Mark(range, kind, title)` / `Unmark(range)` / `SetComment` | the `user` layer. A mark is made on the pages the CPU shows now (one claim per window it crosses; the part after a window boundary or a cut starts no instruction); `cpuView` marks the CPU addresses instead |
| `Revision()` | grows on every change, for redraws |

A claim is a page place (RAM / ROM / cache page, offsets inside the 16K page) or a CPU place; a query maps each
CPU slot through `MapZ80AddressToPhysicalPage` (8K slots on the Next) and keeps the claims of the page it shows. A
`sha256` guard is checked against the bytes at the place on every query that reaches the claim.

Labels from a profile go into `LabelManager` as a set `profile:<id>` (priority 75: between the bundles, 50, and the
loaded files, 100 on), so the existing label machinery (page awareness, priority, enable / disable) serves them
unchanged. Entries go there too (kind `entry`), and the `symbols` files next to the profile merge into the set.

**Where a match applies it.** `core/controllers/src/profiles/memoryprofilemarkup.h` (header only: the controllers
library uses no core symbols) turns a parsed `memory` section into the profile layer and the label set
(`ApplyMemoryProfile`, `ClearMemoryProfile`). unreal-qt's `HostControllers` calls it after each profile resolve:
the strongest game match (`gameMatches`, the controllers' own choice) lies over the attached machine; another match,
or none, takes the previous one back. One profile per emulator; an instance the GUI is not bound to keeps what it
had. A WebAPI / MCP agent drives an unreal-qt instance (offscreen included), so it sees the same markup; a process
without the controllers (none today) would need the match in its own glue.

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
| 2 | `MemoryMarkup` with layers, physical keys, queries; profile layer + profile label set on a match (done: `MemoryMarkup_Test`, `MemoryProfile_Test.LaidOverAMachineAndTakenBack`) |
| 3 | Runtime layer from L0 facts / coverage; analysis layer from L2 (done: `MemoryMarkup_Test.Runtime*`, `AnalysisLayerFollowsTheLastBuild`) |
| 4 | Planes; save to profile |
| 5 | Consumers: disassembler window (anchors, data rows), memory viewer colouring |

## 7. Open points

- The user's own game profiles stay in `<user config>/unreal-ng/controllers/profiles/games/` (step 1 moved the
  shipped ones only); whether they follow to `<user config>/unreal-ng/software/`.
- Physical keys of other machines (Sprinter, ATM, Next pages) go through the same absolute page index; card CPUs get
  their own store.
</content>
