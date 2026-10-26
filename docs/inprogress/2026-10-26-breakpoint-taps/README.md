# Breakpoint taps: one non-stopping observation path for analyzers, haptics and scripts

**Status:** design, 2026-10-26. Nothing implemented beyond what exists today (§1).

The emulator gets more and more watchers that never stop the CPU: analyzers (TR-DOS, NedoOS, DSS, ROM print, the
BASIC editor monitor), the haptic feed's game probes, coverage, the bus and font analyzers ahead. Each needs the same
thing: "tell me when this address runs / this byte is written / this port is touched, under this condition, cheaply".
Today three half-mechanisms do it. This design folds them into one: a **tap** is a breakpoint whose action is "notify
a sink" instead of "stop", built on the conditional-breakpoint model
([conditional-breakpoints/design.md §4](../2026-08-17-conditional-breakpoints/design.md)) and its hot-path matching
([hotpath-matching-design.md](../2026-08-17-conditional-breakpoints/hotpath-matching-design.md)).

The haptic feed then is one more profile-driven analyzer: a game profile names its events (§7), an analyzer turns the
taps into named events per frame, and haptics is one consumer of them.

## 1. What exists

| Mechanism | Where | Universal | Not universal |
|:--|:--|:--|:--|
| Probe breakpoints | `BreakpointDescriptor::probe`, `BreakpointManager::WalkCandidates`, `IBreakpointProbeSink` | the matching: addresses, ranges, pages, port masks, hidden + owner, the 64K inline filter, hit counts | **one** sink per manager (`SetProbeSink`), an 8-bit index into that sink's table: a second client would steal the first one's hits. Comments and naming say "haptics" |
| Debug hold | `FeatureManager::setProbeDebugHold`, `Z80::execProbeHold` | | set by `HapticProbes::Arm` only. Read / write taps switch the whole debug memory path on (memory tracking, TTD hooks, video flag): +38 % CPU on a 48K |
| Analyzer breakpoints | `AnalyzerManager::requestExecutionBreakpoint*`, `IAnalyzer::onBreakpointHit` | owned and auto-released per analyzer | exec only in practice. Each hit looks the owner up in two hash maps in `Z80::RunInstructionStartHooks` (`ownsBreakpointAtAddress`, `dispatchBreakpointHit`). Needs full debug mode |
| `requestMemoryBreakpoint` | `analyzermanager.cpp` | | **defect**: adds the breakpoint with the interactive owner, not hidden: it shows in the user's list and stops the CPU. Only a unit test calls it |
| `subscribeMemoryRead` / `subscribeMemoryWrite` | `AnalyzerManager` | | **dead**: `dispatchMemoryRead` / `dispatchMemoryWrite` are never called; a subscriber gets nothing |
| Frame-end byte watches | `ControllerManager::_hapticWatches` | free on the CPU: one read per watch per frame | private to the haptic feed |

The condition engine (F1) is not in `core/src` yet ([conditional-breakpoints/TODO.md](../2026-08-17-conditional-breakpoints/TODO.md));
the hit-count policies (F5) are.

## 2. Goals

- One way for any client (analyzer, haptics, a script, DeZog logpoints later) to watch exec / read / write / port
  without stopping, any number of clients at once.
- Nothing armed: zero cost, the code paths of today.
- Armed: cost proportional to what is watched. An exec tap costs one bit test per instruction; a write tap costs one
  byte test per write to its 16K bank, not the debug memory path.
- Conditions and hit policies of the conditional-breakpoint model apply to taps unchanged.
- TTD replay mutes every tap in one place.

Non-goals: per-access callbacks for every byte of memory (coverage keeps its own path), changing what a stopping
breakpoint does.

## 3. The model: action = stop or tap

```text
BreakpointDescriptor
  trigger + facets (address / range / page / slot / port mask / condition / hit policy)   — unchanged
  action:  Stop (today's breakpoints)  |  Tap { sink: uint8_t, cookie: uint32_t }
```

- **Sinks are registered**: `BreakpointManager::RegisterTapSink(ITapSink*) -> sinkId` (up to 255; id 0 is none),
  `UnregisterTapSink(sinkId)` removes the sink's taps too. `ITapSink::OnTap(const TapHit&)` with
  `{cookie, kind, address, value, before, pc, tState}`. The cookie is the client's own index or pointer tag.
- `WalkCandidates` already has the branch: a tap bumps `hitCount`, runs the hit policy (F5; the condition F1 once it
  exists), calls `_tapSinks[sink]->OnTap(hit)` and never sets the stop result. One array index replaces today's one
  pointer.
- Taps are `hidden` with an owner, so they never show in the user's list, and `ChangedSince` leaves them out (as
  today).
- **Write `before`**: a write tap gets the byte it replaced. The light write path (§5) reads it before the store, so
  the client keeps no shadow copy (today `HapticProbes::_last`).

`IBreakpointProbeSink` / `descriptor->probe` become `ITapSink` / `descriptor->tapSink + tapCookie`. HapticProbes is
the first sink; nothing else uses the old names.

## 4. Gates: demand from the armed set, not from one client

Today a client flips global switches (`setProbeDebugHold`, `execProbeHold`). Instead the breakpoint manager derives
the demand from its own armed set every time the set changes, as it already derives `_hotState.hasExec / hasRead /
hasWrite / hasPortIn / hasPortOut`:

| Demand | Raised while | What it switches |
|:--|:--|:--|
| `tapExec` | any armed tap of kind exec | `Z80::execTapHold` (today `execProbeHold`): the instruction start's check without debug mode; `IdleStepsInert` off |
| `tapWrite` | any armed write tap | the per-bank write hook (§5) for the banks its addresses map to; re-derived on a bank switch |
| `tapRead` | any armed read tap | the debug memory path (rare; §5) |
| `tapPort` | any armed port tap | nothing new: the port path already checks `hasPortIn / hasPortOut` |

The manager pushes the demand into Core / Memory through one call (`Core::OnTapDemandChanged`) on the emulation
thread (or with the CPU held, as every breakpoint change). No client touches FeatureManager. A stopping breakpoint
still needs debug mode, as today.

## 5. The light write path

`MemoryWriteFast` already has one rare branch per write: `if (_videoWatch && (_bank_ram_page_cache[bank] | 2) == 7)
[[unlikely]]` (the event-rule screen's catch-up). Fold it into one per-bank byte:

```cpp
// Memory: set on every bank switch and on a tap or video watch change
uint8_t _bankWriteHook[4];   // bit 0: the video watch's page (5 or 7); bit 1: a write tap in this bank

void Memory::MemoryWriteFast(uint16_t addr, uint8_t value)
{
    const uint8_t bank = addr >> 14;
    if (_bankWriteHook[bank]) [[unlikely]]
        return MemoryWriteHooked(addr, value);          // catch-up, the store, the tap bit test
    *(_bank_write[bank] + (addr & 0x3FFF)) = value;
}
```

- Nothing armed: one byte load and a test instead of today's pointer test plus page compare. Same or cheaper; an A/B
  benchmark proves it ([performance-guidelines.md](../../guidelines/performance-guidelines.md)).
- A write tap: only writes into its 16K bank leave the fast line. There one bit test (`_hotState.MemoryBit(WRITE)`);
  a miss stores and returns. A Dizzy-style counter in page 2 taxes page 2's writes only, not the screen's.
- The contended and overlay variants wrap `MemoryWriteFast`, so all eight interfaces get it without new ones.
- **Reads** stay on the debug path while a read tap is armed: a read check on the fast path taxes every read for
  everyone, and read taps are rare (profiles prefer exec, haptics.md §5). Revisit with a measurement if an analyzer
  needs them.

Expected: `BM_HapticProbeFrame/1` (one each-write tap on a 48K) from +38 % to the noise of `BM_HapticProbeFrame/0`.

## 6. Frame watches as a service

The frame-end byte watches (`value`, `changed`, `increased` / `decreased`) cost nothing on the CPU and serve more
than haptics: a lives counter for a Stream Deck key, a score for the OSD, a script's `on_change`. Move them out of
ControllerManager into a `FrameWatchSet` owned by the AnalyzerManager: `Watch({page, address, word}) -> id`, read at
`dispatchFrameEnd`, a client reads the frame's values and changes. TTD replay suppression is already there.

## 7. Game events: the profile names events, consumers subscribe

A play profile today declares `haptics.sources` (probes) and `haptics.rules` (effects). The sources are game facts,
not haptic ones. Split:

```yaml
events:                                  # game facts, any consumer
  jump:   { probe: exec, address: "0x795A" }
  died:   { probe: exec, address: "0x8B80" }
  lives:  { watch: value, address: "0x767F" }
haptics:
  rules:
    - { on: jump, play: tick, target: handle.high }
    - { on: died, play: life-lost }
```

- `GameEventAnalyzer` (an `IAnalyzer`, activated when the resolved profile has events) arms the taps and watches,
  and at each frame end publishes `{frame, fired: [names], values: {name: v}}`.
- Consumers: the haptic feed (rules), the Stream Deck (`show:` a value), the OSD, scripts (Lua / Python
  `on_game_event`), TTD markers ("died" as a bookmark), the activity feed. Each on all automation planes
  ([automation-planes-guidelines.md](../../guidelines/automation-planes-guidelines.md)): a WebSocket topic
  `game.events`, REST `GET /game/events`.
- `haptics.sources` stays accepted as an alias of `events` (schema 1 profiles keep working); the index generator
  writes `events`.

Finding the events stays the recipe [.recipe/input/game-haptics.md](../../../.recipe/input/game-haptics.md); it
becomes "finding a game's events".

## 8. Analyzers move onto taps

- `requestExecutionBreakpoint*` / `requestMemoryBreakpoint` create taps to the AnalyzerManager's own sink with the
  analyzer index as the cookie; `OnTap` calls `onBreakpointHit` (gains the kind and value). The two hash lookups per
  hit in `RunInstructionStartHooks` go; the Z80 no longer asks "analyzer or user?" (the action says it).
- Analyzers run without full debug mode (the demand gates of §4). TR-DOS and the OS analyzers then cost what their
  taps cost.
- `requestMemoryBreakpoint` gets an owner, hidden and the tap action: fixes the defect of §1.
- `subscribeMemoryRead` / `subscribeMemoryWrite`: removed, or reimplemented as a full-range tap if a client appears.
  A dead API that silently does nothing is worse than none.

## 9. Cost targets

| Case | Today (µs/frame, median) | Target |
|:--|:--|:--|
| nothing armed (48K, feed on) | 224 | 224 (A/B: the per-bank byte vs today's check) |
| one exec tap | 228 | 228 |
| one each-write tap | 310 (+38 %) | ≤ 232 (+4 %) |
| Dizzy X, 4 exec taps | 566-595 (no feed 625) | the same |
| TR-DOS analyzer active (exec taps), debug mode off | n/a (needs debug mode) | ≤ +2 % |

Benchmarks: `core/benchmarks/debugger/hapticprobe_frame_benchmark.cpp` (exists), plus one for the analyzer case and
an A/B of `MemoryWriteFast` with nothing armed.

## 10. Phases

1. **Taps in the breakpoint manager**: the sink registry, `tapSink + tapCookie`, `TapHit` with `before`; HapticProbes
   moves onto it. The demand gates (§4) replace `setProbeDebugHold` / `execProbeHold`.
2. **Light write path** (§5) with the A/B benchmark. Write taps leave the debug path.
3. **Analyzers on taps** (§8): the sink, the cookie, the defect fix, the dead API decision.
4. **Frame watches** (§6) and **game events** (§7): `GameEventAnalyzer`, the profile key, the automation planes; the
   haptic feed becomes a consumer.
5. When F1 lands: conditions on taps (`when: "A==3"` in a profile event) with no extra work in the tap path.

Each phase lands on its own with its tests; phases 1-2 hold the performance claims.

## 11. Tests

- Two sinks at once on the same address: both get the hit, neither stops, the user's list stays empty.
- A tap and a stopping breakpoint on one address: the stop happens, the tap is told.
- Demand: arming / disarming each kind sets and clears exactly its gate; with nothing armed the memory interface is
  the fast one and `IdleStepsInert` holds.
- Light write path: a tap in bank 2 does not route bank 1 writes through the hook; a bank switch moves the hook;
  `before` is the replaced byte; the video watch's catch-up still runs (the event-rule screen tests).
- TTD replay: no sink is told.
- Analyzers: TR-DOS analyzer tests pass with debug mode off; `requestMemoryBreakpoint` does not stop and is hidden.
- Game events: Dizzy X's play test publishes `jump`, `land` as events; the haptic feed plays them from the event
  list.

## 12. Open points

- Read taps on the fast path (§5): only with a client and a measurement.
- The cookie width: 32 bits covers an index or a tag; a pointer would need 64 (prefer the index).
- Whether DeZog logpoints map onto taps (a log sink) in the same phase as F1.
