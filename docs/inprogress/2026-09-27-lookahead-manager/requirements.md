# Look-Ahead Manager — Requirements

**Created:** 2026-09-27
**Status:** draft for review
**Design:** [design.md](design.md) · **Prior art:** [prior-art.md](prior-art.md)
**Related:** [ZX DLSS GigaScreen](../2026-09-27-zxdlss-gigascreen/requirements.md),
[Metadata Manager](../2026-09-27-metadata-manager/requirements.md),
[Game Mode §7](../2026-09-24-core-performance/unreal-ng-input-latency-and-game-mode.md)

## Goal

Let the emulator compute the next few frames **before** they are due, without
disturbing the real ("canonical") run — so that:

1. video analysis (GigaScreen first) sees what is coming and can start mixing
   on the first frame of an effect instead of after a warm-up;
2. Game Mode can show a future frame and remove the game's own frame of input
   lag;
3. tools can ask "what happens if…" questions (e.g. how many frames until a key
   press shows on screen).

## Terms

| Term | Meaning |
|---|---|
| Canonical run | the real emulation: what is heard, recorded, debugged and journaled by TTD |
| Speculative run | frames computed ahead from a copy of the canonical state, assuming the input does not change |
| Shadow | the separate machine copy that performs speculative runs |
| Resync | copy the canonical state into the shadow again and restart speculation |
| Depth | how many frames ahead the shadow is |

## Requirements

| ID | Requirement |
|---|---|
| LA-1 | One manager serves all consumers: analysis preview (up to 10 frames), Game Mode run-ahead (1–3 frames), guest-lag detection, later preemptive frames / rollback netplay. Consumers declare the depth and data they need; the shadow runs to the maximum depth requested. |
| LA-2 | The canonical run is **never rolled back** by look-ahead. Audio, TTD recording, video/audio recording, debugger and automation observe canonical frames only. |
| LA-3 | Speculation runs in a separate shadow machine that may use its own CPU core. |
| LA-4 | Fast in-memory state copy at frame boundaries (raw, uncompressed). Target ≤ 0.2 ms for a 128K machine; larger machines measured and reported. |
| LA-5 | State copy is **complete**: verified per machine model and per peripheral by a run → restore → run hash test (CPU, memory, ports, video raster state incl. FLASH phase, sound chips, coprocessors, FDC, tape position). |
| LA-6 | Prediction assumes held input (keyboard matrix, joystick, absolute mouse position and buttons as at the frame boundary). |
| LA-7 | Resync on: input change, reset, snapshot/state load, TTD seek or replay, media insert/eject, disk write, tape start/stop, debugger memory/register writes, config or machine change, frame-count gap, detected divergence. |
| LA-8 | The shadow cannot cause side effects **by construction**: its context role disables audio output and recording, TTD journaling, debugger/breakpoints, message-bus notifications, shared-memory sync, host pacing changes, and writes to host media. |
| LA-9 | Determinism: every input to the emulated machine is a function of state + input. Host-time sources (RTC, CMOS clocks) read an emulated-time clock shared by canonical and shadow runs. |
| LA-10 | Divergence monitor: when no resync happened, each predicted frame is compared with the canonical frame when it arrives (screen digest + light state hash). Mismatches are counted, logged with frame numbers, and trigger a resync. This doubles as a permanent determinism check. |
| LA-11 | Speculative output is **advisory**: every predicted frame carries a generation number; a resync invalidates older generations; consumers must cope with fewer frames than requested. |
| LA-12 | Speculation never delays the canonical frame deadline. If the shadow is late, consumers get what is ready. |
| LA-13 | Auto-stand-aside (configurable) while: turbo/fast-load, TTD replay/seek, debugger paused or stepping, tape running, disk command in flight (until media overlays exist), speed ≠ 1×. |
| LA-14 | Skip work consumers do not need: e.g. no RGBA rendering in the shadow when only the meaning plane (GigaScreen plane B) is consumed. |
| LA-15 | Game Mode display of frame t+N is opt-in and labeled; the audio/video offset it creates is documented and measured. |
| LA-16 | Observability via WebAPI/MCP: depth reached, resyncs per second by reason, divergences, snapshot/restore/frame cost, stand-aside reasons. |
| LA-17 | Test suite: completeness (LA-5) for all models, resync triggers, side-effect isolation (nothing reaches audio, TTD, recordings, message bus), divergence detection with injected non-determinism, cost benchmarks. |

## Out of scope for v1

- Rollback of the canonical run (preemptive frames) — designed as a later
  mode reusing the same state copy.
- Rollback netplay.
- Speculation across disk writes (needs a copy-on-write media overlay).
