# 02 — Capability Registry and Trigger Engine

| | |
|---|---|
| **Status** | Proposal / draft for review |
| **Date** | 2026-09-21 |
| **Baseline** | master `ae4d40b` |
| **Related** | `2026-08-17-conditional-breakpoints/design.md` (+ `performance.md`, `hotpath-walkthrough.md`), `2026-08-26-expression-evaluator/`, `2026-08-26-automation-gaps/` (T4 #22, #23), `core/src/debugger/ttd/ttdexternalevents.h`, toolchain doc §8–§9 |

---

## 1. Summary

The conditional-breakpoint design is sound for what it targets: address/port triggers, RPN conditions, fast predicates, "pause + notify". The goal is broader: breakpoints that **do anything** — logging, counting, bookmarking, poking, running Lua, enqueuing work for external tools or LLMs — and a system where every new device, machine or event is picked up **automatically** by triggers, automation surfaces and the toolchain.

This document specifies:

1. A **capability registry** with three facets: observables, event sources, actions.
2. **Triggers** as the generalization of breakpoints: event + condition + actions + policy.
3. A **determinism contract** via action classes.
4. **Execution tiers** that protect the hot path.
5. **Two execution backends** for one compiled condition: live and retroactive over TTD.
6. **Lua state in checkpoints**, so scripted logic survives seek, replay and rollback.
7. The registry as the **single source** for generated CLI/WebAPI/Lua/Python/MCP surfaces and docs.

---

## 2. Current state (verified)

| Item | Finding |
|---|---|
| Breakpoint model | `BreakpointManager` is the single source of truth for Qt, CLI, Lua, Python, WebAPI; matching on the hot path via page map + hash lookups |
| Conditions | Planned: compiled at set time to RPN over `int64_t`; dominant shapes as tagged fast predicates (~2–5 ns), VM fallback (~5–30 ns); evaluated only after an address/port match |
| Symbol set | Fixed opcode set: registers, flags, memory deref, access address/value, port/value, T-states, paging, labels |
| Actions | "pause + notify" (future: log, count-only) |
| Phase 0 speed substrate | Landed: 12–18× miss-path improvement with benchmarks as regression gates |
| External events | `TTDExternalEvent` (80 B) records pokes/injections from control, Lua, WebAPI/IO threads as timeline markers |
| Model-state registration precedent | `PortDecoder::CreateTTDSerializers()` + `GetTTDModelStateIds()` with refusal on mismatch |
| Event push | WebSocket controller skeleton (Drogon, pub/sub topic); MCP SSE framing + progress notifications; PLAN T4 #23 deferred |
| Schema generation | PLAN T4 #22 deferred; manual parity replication cost already flagged as real |

---

## 3. Capability registry

### 3.1 Facets

| Facet | What it is | Examples |
|---|---|---|
| **Observable** | A named, typed, side-effect-free value readable at an instruction boundary | `cpu.hl`, `cpu.f.z`, `mem[addr]`, `paging.slot[1].page`, `beam.line`, `beam.tstate`, `ay[0].reg[7]`, `gs.cpu.pc`, `fdc.track`, `tape.block`, `video.mode`, `ttd.frame` |
| **Event source** | Something that can fire a trigger | PC execute, memory read/write (logical or physical), port in/out, frame start, INT accepted, raster position reached, paging change, video-mode change, FDC command, tape block start, device register write, module loaded (OS descriptor), host input |
| **Action** | Something a trigger can do | pause, log, count, TTD bookmark, trace span, poke, register edit, inject input, run Lua, emit event to subscribers, enqueue delegate task |

### 3.2 Registration

- Devices, port decoders (models) and subsystems register their facets at construction/model setup, mirroring the TTD registration pattern.
- Each entry carries: stable id, type, unit, description, cost class (see §6), availability predicate (e.g. only when GS fitted), and schema for parameters.
- Model change → registry rebuild → `capabilities_changed` event (toolchain doc, requirement R2).
- **Refusal rule:** a trigger referencing an unavailable facet is rejected at apply time with a precise error, never silently inert.

### 3.3 Consumers

- Expression compiler (symbols → direct-accessor opcodes, resolved once at compile time).
- Trigger engine (event wiring).
- Automation surfaces: CLI, WebAPI, Lua, Python, MCP — generated (§10).
- MCP `find_capability` / recipe index.
- Toolchain: LSP completion for `@break if …` expressions; DAP variable views.
- Documentation generator.

---

## 4. Trigger model

```text
Trigger
  id, owner (namespace), generation            // declarative sets, toolchain doc R1
  event:      <event source id> + filter        // e.g. exec @ (page 5, 0x0123), write @ range, port mask
  condition:  <expression>                      // optional; compiled to fast predicate or RPN
  policy:     hit count / every Nth / once / enabled window (T-state or frame range) / rate limit
  actions[]:  ordered list of { action id, params }
  state:      OK | ERROR(reason)                // errors never silently pass (existing F6 rule)
```

- A classic breakpoint is `event=exec, actions=[pause]`. A logpoint is `actions=[log]`. A watchpoint is `event=write, actions=[pause]`.
- Existing breakpoint APIs remain as thin constructors over triggers; nothing breaks for current frontends.

---

## 5. Determinism contract: action classes

| Class | Examples | Live execution | During TTD seek / replay / reverse |
|---|---|---|---|
| **observe** | log, count, trace span, emit event | Execute | Suppressed; results available from the recording if logged |
| **annotate** | TTD bookmark, label, marker | Execute | Idempotent (dedup by trigger id + T-state) |
| **mutate** | poke, register edit, input injection, Lua write to machine state | Execute **and** journal as `TTDExternalEvent` | **Replayed from the journal only**, trigger not re-evaluated for this action |
| **control** | pause, speed change | Execute | Suppressed |
| **delegate** | enqueue task for daemon/LLM (doc 03) | Enqueue asynchronously | Suppressed |

Rules:

- **D-1** Every action declares its class in the registry; the engine enforces the column semantics.
- **D-2** A trigger containing any mutate action is itself marked *mutating*: changing or removing it during a recorded session is recorded as a timeline event, so replay remains faithful to what actually happened.
- **D-3** Replay/seek sets an engine mode flag checked once per action dispatch (not per instruction), keeping hot-path cost unchanged.
- **D-4** Netplay rollback (doc 04) uses the same mode.

---

## 6. Execution tiers

| Tier | Where | Allowed actions | Constraints |
|---|---|---|---|
| **Inline** | Emulation thread, at the matching instruction boundary | pause, count, bookmark, native log into a lock-free ring, poke, register edit | No allocation, no locks, no exceptions; bounded cost |
| **Deferred** | Emulation thread at the next safe point (instruction boundary after the inline pass), from a queue | Lua callbacks, composite actions | Budgeted per frame; overrun → trigger enters ERROR with reason |
| **Async** | Off the emulation thread | emit event to subscribers, delegate tasks | Never blocks emulation; back-pressure drops with counters |

- **Synchronous Lua** (callback runs inline and may mutate before the next instruction) is available only via an explicit flag, and is documented as a performance hazard.
- Cost classes of observables (register read vs. banked memory deref vs. device query) are known at compile time; the compiler rejects conditions exceeding an inline budget unless the trigger is marked deferred.

---

## 7. Two backends for one condition

- **Live backend:** the hot-path engine above.
- **Retroactive backend:** evaluates the same compiled condition over a TTD recording: scan M1 records / write journals / coverage index, re-simulating where needed.
- Existing `find-last`, `ReverseContinue(pcs)`, range find-last become instances of the retroactive backend.
- **Requirement:** a trigger spec is backend-agnostic. Any trigger can be applied live, or run as a query over a recording, returning matching (frame, T-state, context) tuples.
- This makes "record once, ask many questions" the default investigation workflow for humans, recipes and LLMs.

---

## 8. Lua integration

- **L-1 Lua VM state in checkpoints.** If Lua scripts hold logic (mods, multiplayer, trainers), their state must be captured in TTD checkpoints and UNS. Approach: scripts keep persistent state in a registered, serializable table; the VM itself is re-created on restore and the table re-attached. Arbitrary closures/upvalues are not serialized and are documented as non-persistent.
- **L-2 Determinism for logic scripts.** No host time, no host randomness outside a seeded RNG exposed by the emulator, no I/O except through emulator APIs. Enforced by a restricted environment for scripts marked `logic`.
- **L-3 Two script roles:** `view` (observe-only, may be skipped on replay) and `logic` (may mutate; journaled; state in checkpoints).
- **L-4 Access** to all registry observables and actions through generated bindings.

---

## 9. Sample points

Many consumers need "state at a logically consistent moment", not "state at an arbitrary instruction":

- ZXDLSS / reconstructed rendering must read entity tables after the game's update, not halfway through it.
- Netplay must exchange state at a consistent point.
- Recipes need reproducible observation points.

**Requirement:** a *sample point* is a named trigger (usually `exec @ end-of-update` or `HALT in main loop`) whose action publishes a consistent snapshot of selected observables to subscribers. Title manifests (doc 04) declare their sample points.

---

## 10. Registry-generated automation surface

- The registry plus the command schema (PLAN T4 #22) generates CLI commands, WebAPI endpoints and the OpenAPI document, Lua/Python bindings, MCP tool definitions and the recipe index, and reference documentation.
- This removes the recurring manual parity work, and keeps docs truthful by construction (the failure class behind PLAN T1 #1).
- The generated MCP surface is consumed through the existing intent-based `find_capability` meta-tool.

---

## 11. Event stream

- One subscription mechanism on the existing Drogon WebSocket controller (and SSE for MCP clients), topic-filtered.
- Topics include trigger firings (with the payload the trigger's `emit` action specifies), lifecycle, capability changes and sample-point publications.
- Timestamps: T-state + TTD frame index on every event.
- Delivery: at-most-once with sequence numbers; gaps detectable by clients; per-subscriber bounded queues.

---

## 12. Performance requirements

| Req | Requirement |
|---|---|
| P-1 | Miss-path cost for code not matching any trigger unchanged vs. Phase 0 baseline (benchmarks as gates) |
| P-2 | Fast-predicate hit evaluation ≤ 5 ns; RPN ≤ 30 ns (existing targets) |
| P-3 | Inline log action ≤ 50 ns (ring write) |
| P-4 | 10 000 armed source-annotation triggers without measurable frame-time regression when none match |
| P-5 | Deferred tier: per-frame budget configurable; default ≤ 10 % of frame time |

---

## 13. Phasing

| Phase | Deliverables |
|---|---|
| T0 | Existing plan slices 1a–1f (key re-encoding, intervals, `bpcondition`, slot/physical filters, hit counts, error state) — unchanged |
| T1 | Registry skeleton with observables (CPU, memory, paging, beam, AY/TS, FDC, tape); expression compiler switched to registry symbols |
| T2 | Trigger model + action classes + tiers; breakpoints re-expressed as triggers; observe/annotate/control actions |
| T3 | Mutate actions with journaling; replay-mode enforcement; conformance tests (mutating trigger + seek/replay equivalence) |
| T4 | Event stream; sample points; emit action |
| T5 | Lua roles, logic-state persistence in checkpoints/UNS |
| T6 | Retroactive backend unifying find-last / reverse-continue |
| T7 | Schema generation of automation surfaces and docs (T4 #22) |

---

## 14. Open questions

1. Should device event sources (e.g. "AY register 13 written") be wired through port-decoder hooks or through device-side emit points? Device-side is cleaner for multi-port devices and co-processors.
2. Condition language dialects (DeZog-style, source annotations) — one grammar with dialect flags (as already planned for DeZog) or a canonical form plus translators?
3. Physical-address watchpoints on device-owned memory (GS SRAM, OPL4 RAM): same engine via paged-region write paths (doc 01 §5.1)?
