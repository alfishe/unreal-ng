# 038 — analysis

## Against the expectations

| Criterion | Expected | Measured | |
|-----------|----------|----------|---|
| Exact (gate) | 0 mismatches, answers = oracle | 0 machine mismatches on every stripe of W1-W3, every answer equal to the oracle and across N, **for clones without turbo and windows after the baseline interval** | met, with two conditions |
| 50k W1 frames, O1-O4, N=8, setup included | <= 5 s | 1.61 s + 0.35 s setup (serialize 85, create 171, load 96 ms) = **1.96 s** | met |
| frames/s at N=8 | >= 10,000 | 31,000 (W1), 9,700 (W2), 8,550 (W3) | met on W1; W2 and W3 frames are 3x busier, 16 clones reach 18,600 / 15,900 |
| vs a-inplace, same question | >= 8x | O1-O4 (W1): >= 127 s of verbs vs 1.96 s = **65x**; reads, probes and AY have no verb at all. PC profile: 106.8 s vs 1.61 s = 66x | met |
| Observer cost | taps <= +30 %, M1 <= +50 % | taps +3 to +6 %, M1 +1 to +25 % (W2 N=1 only; +4 % at N >= 4) | met |
| Memory per clone | <= session + 64 MB | 35-77 MB with sessions of 4.6-8.8 MB | met (W2 N=1: 72 MB vs 71.3 MB limit) |
| Live machine at N=8 | <= +5 % | +4 % to +5 % | met; 16 clones cost the live machine +24 % on O5 |
| Setup at 50k frames, N=8 | <= 1 s | 0.35 s | met |

## What the numbers mean

1. **A clone is faster than today's replay even alone.** One clone replays W1 at 0.23 ms per frame. Today's
   frame-by-frame replay on the source costs 2.1 ms per frame. The in-place verbs restore a checkpoint per frame,
   save and restore the user's live state around each step, and the frame cache records registers per instruction.
   A clone runs the frames back to back from one seek. So most of the 65x comes from that, not from the cores:
   10x from the forward run, 8x from 8 clones.

2. **It scales linearly to the performance cores.** From 1 to 16 clones the speed grows 14.6x on W1, 14.8x on W2
   and 14.8x on W3. Stripes are independent: each starts from its checkpoint, and nothing is shared but the
   read-only session bytes. Past the 16 performance cores the live machine pays: +24 % at 16 clones on O5. A
   default of `performance cores / 2` (8 here) keeps the user's machine within 5 %.

3. **Observers are almost free.** The cost is the replay itself. Breakpoint taps reuse the debugger's matching, so
   pages, ranges and port masks mean what they mean in a breakpoint or a profile probe. They added 3-6 %. The M1
   hook (`std::function` per instruction) added 1-6 % except on one W2 run. So the engine can run every observer a
   question needs in one pass.

4. **One pass answers what today takes several verbs, and more.**
   - Today the agent's W1 question costs three paged verbs over 50,000 frames: exec hits 21 s, all writes 53 s,
     changes 54 s.
   - Reads with their PC (fonts, tables), probe-fired frames and AY register writes have no verb at all.
   - One clone pass collects all of them, plus the PC profile.

5. **Exactness has two conditions, and both are engine findings, not clone findings.**
   - **Turbo.** The machine that replays must not run in turbo. Turbo renders sound by the host clock, so the TSFM
     render phases depend on wall time. The CPU-visible state and every answer stayed exact in the POC. The
     device blob is still not the recorded one, and later audio from such a state would differ. The in-place
     replay uses the same `RunTStates` path; whether it inherits the user's turbo is not checked here.
   - **The baseline interval.** A forward run from the baseline checkpoint (inside a frame) reaches the next
     checkpoint with three TSFM time fields different: both FM clock phases and the render cursor. Every later
     interval is exact. This affects any replay in the first frame of a session, including today's seeks there.
   - **Both fixed 2026-10-28** in the engine, not in the POC. The cause of the turbo case is wider than turbo: the
     sound off and LQ change the TSFM render loop too, and the fix pins the recording's render mode. Remeasured
     with the POC's `POC038_CLONE_SOUND` / `POC038_CLONE_HQ` and turbo switches: 0 mismatches on W1-W3 in all 15
     combinations, the baseline interval included.

6. **Clones cost memory, not the user's time.** 35-77 MB each, so 8 clones are 300-460 MB. Creating and loading
   them takes 0.1-0.3 s, and a session of 50,000 frames serializes in 85 ms. A pool kept warm while the session
   does not change makes later questions start at once. This is how an agent works: many questions over one
   recording.

## Limits of the method

- The fingerprint compares the decoded device states, CPU, chipset and RAM pages. It does not cover the host-side
  rendering caches, by the same policy as the device serializers.
- The clones' stripes are equal frame counts. A real recording with uneven frames (loading, idle) would balance
  better with a work queue of chunks.
- Barriers (external events: debugger edits, media changes) are absent from W1-W3. The engine must stop a stripe at
  a barrier, as the verbs do.
- Only Pentagon and 48K. TS-Conf, Sprinter, ZX Next, ATM and Profi devices are not measured. Each model needs its
  exactness run before it is offered.
- macOS only. The Linux gcc build of the tool and a Windows run are adoption checks.
