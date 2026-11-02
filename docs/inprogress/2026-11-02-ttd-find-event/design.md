# TTD find event: design

Requirements: [README.md](README.md). Plan: [plan.md](plan.md).

## 1. What already exists

| Need | Already there | Gap |
|------|---------------|-----|
| A byte's value per frame | The engine's per-checkpoint piece changes: `ChangesAt(i, region)` lists the 4 KB pieces that got a new version, `PieceStore().Decode` reads one, `RestoreRegion` decodes a region once | No forward search, and a value is matched only for equality (`TTDSearchQuery::value`) |
| Every CPU write with the old value | `FindAccesses` with `changes_only` (write journal, or the coverage-pruned replay) | Backward only, newest first |
| The screen's bytes per frame | The session's screen layout per checkpoint (`ScreenLayoutOf`, a ULA model falls back to page 5 / 7 from `p7FFD`), the piece changes | Only a yes/no per frame (`WroteScreen` in the timeline); no measure of how much of the picture changed |
| First run of an address | Coverage index, `QueryCoverageScan` with `order=oldest`: the frame | The time inside the frame and the instruction: none forward (`reverse-continue` finds the last run before a moment) |
| Port accesses, border | The IN / OUT port journals, `SearchPortEvents` (`TTDPortQuery`: port mask and value, value matching, trigger, direction, named events `border`, `key`...) | Nothing: it is wrapped |
| Key presses | `TTDInputJournal` (always on): time, kind, key, pressed | No verb returns its events |

## 2. The query

```text
TTDFindEventQuery {
  kind: mem | screen | exec | port | border | key
  order: first | last              (first: the oldest hit in the window; last: the newest)
  fromFrame, toFrame               (the window; the whole history by default)
  limit                            (1..100, default 1: the hits after the first in the same order)
  mem:    address, physPage?, size 1 | 2 (little endian), cond changed | increased | decreased | equals V |
          becomes V | differs V | bit_set B | bit_clear B, exact (follow every write, not frame boundaries)
  screen: minChanged % (default 50), cond change | cleared
  exec:   address [, addressTo], physPage?
  port:   dir in | out, port, portMask (default #FFFF), value?, valueMask?
  border: value? (a colour 0-7)
  key:    key? (a ZX key name: "SPACE", "Q", "CAPS", ...; any key by default), pressed (default true)
}
```

`becomes V` is `equals V` that was not equal at the frame before. `equals V` hits the first frame where the value is V,
which may be the window's first frame. `changed` / `increased` / `decreased` compare with the frame before. Without
`physPage`, a Z80 address uses the page mapped there at the current position, the same rule as `find-last` and the
cause chain.

## 3. The search per kind

**mem (frame boundaries, the default).** Walk the checkpoints of the window in order (backwards for `last`). The byte or
word is read once at the window's start with `RestoreRegion`. After that it is updated only when `ChangesAt(i, ram)`
lists its piece: one `Decode` of 4 KB, with no replay. The condition compares the value at checkpoint i with the value
at i-1. A hit frame is replayed once to find the write: `FindAccesses` with `changes_only` limited to that frame. The
hit is the first write, in time order, after which the condition holds against the value at the frame's start. Then:

- the hit gives frame, tinframe, PC and the old and new values;
- if no CPU write explains the change (DMA, a ROM trap, a device), the hit keeps the frame and says `device`, as
  CC-5 does.

**mem exact.** `FindAccesses` with `changes_only` over the window, in chunks (each hit list newest first, at most 10000,
`next_before` continues). It is reversed for `first` and every write is checked against the condition. This finds a
change that comes back within a frame. It costs the write journal (if on) or the coverage-pruned replay of every frame
that wrote the address.

**screen.** The layout ranges of each checkpoint (`ScreenLayoutOf`, else the ULA page 5 / 7 fallback) name the
picture's bytes. A running copy of those bytes is kept, at most 2 x 6912 bytes for ULA models and the size of the
layout otherwise. For each frame:

- the pieces in `ChangesAt` that overlap the layout are decoded;
- the changed bytes inside the layout are counted against the running copy, and the copy is updated;
- the percentage is changed / layout size;
- a layout switch (page 5 to 7, a shadow screen) compares the new layout's bytes with the copy of the old one, which
  is what the viewer saw change.

Conditions:

- `change`: a hit when the percentage is at least `minChanged`.
- `cleared`: the pixel bytes are all one value, and they were not at the frame before.

A hit's `why` reads like "72 % of the screen changed (4,980 of 6,912 bytes; attributes 90 %)". Frame precision: the
time is the frame's start. The writer with the most screen writes in the frame (coverage) is named when known.

**exec.** With the coverage index: `QueryCoverageScan(executed, address range, page, order)` gives the frame. That frame
is replayed once with the M1 hook, which stops at the first (`first`) or last (`last`) start inside the range. The hit
gives tinframe, PC and the registers' A / HL. Without coverage (a session loaded from a file): the frames are replayed
forward with the hook, at most `max_frames` (default 1000), and the reply says the coverage was missing.

**port / border.** `SearchPortEvents` with `TTDPortQuery`. For `port`: direction, mask, value. For `border`: the named
event `border` with the value matched on bits 0-2 and trigger `Change`, so a repeated OUT of the same colour is not a
hit. Search forward for `first`, `newestFirst` for `last`. Exact time and PC come from the journal.

**key.** The input journal's events of kind Key in the window, matched by key and pressed. Exact time. No PC (the
press is the host's). The `why` adds the first ULA read after it that saw the key down (port journal, the `key`
event), so the agent sees when the program noticed.

**Barriers.** A marker that cuts the history (a snapshot load, a reset with a state jump) inside the window ends a
boundary comparison across it. The frame after it is compared with nothing, and the reply lists the barrier.

## 4. The reply

```text
{ event: {kind, cond, order, address?, phys_page?, ...},
  hits: [ { frame, tinframe, pc, code_page, text, before, after, why, device: bool } ],
  window: {from_frame, to_frame}, next_from_frame | next_to_frame, barriers: [...],
  frames_scanned, frames_replayed, ms, missing: [{source, why}], text (text=true) }
```

MCP / CLI text: one line per hit, then a summary line:

```text
f1834+21980 #8A14 ld (hl),a  (#A130 RAM 2) #03 -> #02  decreased
f2210+0  screen 78 % changed (5,391 of 6,912 bytes; attributes 96 %)  most writes #6A00 ldir
1 hit in frames 10..4810, 4800 scanned, 1 replayed, 6.2 ms
```

## 5. Costs

- No recording cost (FE-10). The hooks stay null outside the query.
- mem (boundaries) and screen: one region read at the window's start, then a 4 KB decode per changed piece that
  matters, plus one frame replay per hit. A 10,000-frame window costs milliseconds when the byte changes rarely, and
  a few tens of milliseconds for a screen redrawn every frame.
- mem exact: the write journal (fast), else one replay per frame that wrote the address (2-6 ms each).
- exec: an index lookup plus one replay per hit. Without coverage, one replay per frame scanned.
- port / border / key: journal scans, no replay.

## 6. The planes

| Plane | Form |
|-------|------|
| CLI | `ttd event <kind> [args] [--last] [--from F] [--to F] [--limit N] [--exact]`: `ttd event mem #A130 decreased`, `ttd event screen 50`, `ttd event exec #8A14`, `ttd event port out #FE`, `ttd event border 2`, `ttd event key SPACE` |
| WebAPI | `GET /api/v1/emulator/{id}/ttd/find-event?kind=&cond=&addr=&addr_to=&phys_page=&size=&value=&bit=&exact=&min_changed=&dir=&port=&port_mask=&value_mask=&key=&pressed=&order=&from_frame=&to_frame=&limit=&text=`, also as a socket `call` of the same route |
| MCP | `time_travel {"action":"find_event", "kind", "cond", "addr", ...}`: the text of §4 |
| Lua | `emu:ttd_find_event(kind, opts)` |
| Python | `emu.ttd_find_event(kind, **opts)` |

The verb goes in `TTDControl` (`Verbs`, `OptionsFor`, the dispatch, a backend member). The backend is
`TimeTravelController::QueryFindEvent` in `ttdcontrollerfindevent.cpp`. Docs: command-interface.md, then the plane
docs (cli, webapi, lua, python, MCP README), then the recipe `.recipe/analysis/ttd-reverse-debugging.md`.
