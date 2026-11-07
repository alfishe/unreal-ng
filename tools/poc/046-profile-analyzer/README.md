# 046 — profile-analyzer

Feature: [docs/inprogress/2026-11-05-profile-analyzer](../../../docs/inprogress/2026-11-05-profile-analyzer/README.md)
(the CPU time profile, "where the CPU time goes" per routine, live, without TTD / L1).

## Context

The reverse-engineering cases draw a per-routine time budget from an L1 build of a TTD recording (minutes, a 158 MB
session for Zynaps `r1`). The design proposes a step callback beside the coverage analyzer that sums the T-states
between instruction starts per physical byte, with options: a shadow call stack (self / inclusive per routine found at
run time), beam bins, a per-frame series, marks. Before the core gets it, the costs and the attribution must be known
by number.

## Goals

- **G1 Off costs nothing.** With the profile off, the machine pays nothing measurable (no subscriber).
- **G2 What each option costs on**, in ns per instruction and % of the turbo frame time: counts only; + T-states
  (16 B cells vs 8 B cells with a spill); + the shadow stack (self / inclusive, entries, anomalies); + beam bins;
  + the per-frame series (flush at the frame end); a mark's baseline copy; the halted idle stepped vs run in one go.
- **G3 Accuracy against L1** on the same execution: per instruction (count, T-states), per routine (self by address
  ownership, inclusive by the shadow stack), the discovered entries against L1's routines, on Zynaps `r1` (RET chains,
  nested-interrupt returns) and one Nether Earth recording.
- **G4 Sizes**: pages allocated, routines found, active routines per frame (the per-frame ring's bytes a frame).

## Requirements

- Workloads: Zynaps (`r1/start.szx`, 48K, play), Nether Earth (`play/start.szx` and `deep/start.szx`, 48K), and a
  HALT-paced one for the halted idle question; turbo, N frames on the calling thread, thread CPU time.
- Release build (LTO, as shipped), macOS Apple Silicon; A/B in interleaved rounds A B C ... then reversed, no wait for
  a quiet machine (owner rule 2026-10-09), more rounds instead; the load recorded.
- Accuracy on the replay of the recording L1 was built from (`r1/session.ttd`, `nether-earth/work/play/session.ttd`):
  the same instruction stream, through the replay window's instruction hook.

## Constraints

- POC code in `cpp/`, built only with `-DBUILD_PROFILE_ANALYZER_POC=ON`; not linked into the emulator.
- The only core changes are what the design needs anyway and the POC cannot do without: `Memory::ExecPhysOffset`
  (the physical byte of an instruction), `Z80::acknowledges` / `lastAcknowledgeT` (the acknowledge's length).
- The reverse-cases repo is read only (inputs copied to `scratch/2026-11-05-poc-046/in`).
- Raw data in `scratch/2026-11-05-poc-046/`.

## Acceptance criteria

- **Off**: the A/B against master within the noise (|diff| < 1 %, the sign not holding across rounds).
- **On**: counts + T-states ≤ 10 % of the turbo frame; + stack ≤ 20 %; each of beam, per frame ≤ 3 % more; the per-frame
  flush ≤ 10 µs a frame with 300 routines. An option above its budget is off by default or dropped.
- **Accuracy**: per instruction, count and T-states equal to L1's for every instruction L1 lists (the acknowledge
  folded back as L1 books it; HALT repeats as one execution); per routine with L1's entries as the map, self by
  address within 1 % of the window of L1's self for the top 24 (the difference is L1's CFG ownership vs the nearest
  entry); inclusive within 1 % of the window for the routines L1 has frames for; ≥ 90 % of L1's call / interrupt
  entries found by discovery.

## Running

```bash
cmake -B cmake-build-agent-release -DBUILD_PROFILE_ANALYZER_POC=ON
tools/build/build.sh poc046-profile
# cost: rounds of each variant on a workload (run from the build's bin directory: ROMs, unreal.ini)
cmake-build-agent-release/bin/poc046-profile bench --snapshot scratch/2026-11-05-poc-046/in/zynaps-r1-start.szx \
    --frames 500 --rounds 10
# accuracy: profile the replay of a recording, write the profile, compare with its L1
cmake-build-agent-release/bin/poc046-profile replay --session <session.ttd> --model 48K --out profile.json
python3 tools/poc/046-profile-analyzer/python/compare_l1.py profile.json <l1.json>
```

## Results

See [measurements-2026-11-05-cost-accuracy.md](measurements-2026-11-05-cost-accuracy.md),
[analysis.md](analysis.md), [conclusions.md](conclusions.md), [proposal.md](proposal.md).
