# 046 — conclusions

| Goal | Answer |
|:--|:--|
| G1 off costs nothing | **Yes.** With the profile off (the flow hook's null test in the CALL / RST / RET handlers present), `core-benchmarks` against master over 20 interleaved rounds: paired medians -1.9 % .. +0.2 %, this tree slower in 8-11 of 20 rounds: within the noise, the sign does not hold. |
| G2 cost of each option | Counts +3-8 %; + T-states +15-19 % (7-8.6 ns an instruction); 8 B cells no cheaper than 16 B; the shadow stack from the CPU flow events +0 over the cells (per-step decoding +1-3 ns); the per-frame series 1.1-1.3 µs a frame (+0-5 %, noise level); beam bins +2-5 %; a mark 9-22 µs; the halted idle stepped +95-115 % on a HALT-paced demo, in one go +1-5 %. |
| G3 accuracy against L1 | **Exact per instruction**: counts equal for every instruction of Nether Earth play and Zynaps r1, T-states equal except the acknowledges, which the core books to the handler (their sum is exactly 2 x `acknowledge_tstates`); window frames and T-states equal. Per routine: self by address equals L1 (0.000 ‰ for the top 24); `fig_budget.py` draws the same figure from the profile as from the L1 (shares within 0.01 %). Self by the stack differs by design where L1's CFG ownership splits fall-through or JP-entered code. Inclusive within 0.1 ‰ on Nether; on Zynaps L1 stacks the RET chains (its inclusive reaches 5.7x the window), the profile keeps a chain one frame deep. Discovery: 100 % of L1's CALL / RST / interrupt entries. |
| G4 sizes | 1-3 pages of cells (256 KiB each) for a 48K game; 7-226 routines; 4-30 active routines a frame (52 at most), so a frame record is 60-640 bytes and the 4 MiB ring keeps thousands of frames; stack depth 14-19. |

## Criteria

| Criterion | Result |
|:--|:--|
| Off: within the noise against master | met |
| Counts + T-states ≤ 10 % | **not met**: +15-19 %. Accepted: the T-states are the point of the profile; counts alone save ~12 % and answer nothing on their own |
| + stack ≤ 20 % | met with the flow events (+17-19 % total, the stack itself +0); not met by per-step decoding (+21-25 %) |
| beam, per frame ≤ 3 % more each | per frame met within the noise (on by default); beam +2-5 %: off by default |
| frame flush ≤ 10 µs with 300 routines | met: 1.1-1.3 µs with 30 records a frame |
| per instruction equal to L1 | met (acknowledge placement documented and summed) |
| self by address within 1 % for the top 24 | met (0.000 ‰) |
| inclusive within 1 % where L1 has frames | met on Nether (0.1 ‰); on Zynaps the two definitions differ for RET chains (L1 stacks them), an L1 property, not a profile error |
| ≥ 90 % of L1's call / interrupt entries discovered | met: 100 % |

**Verdict: go** (adopted, [proposal.md](proposal.md)).

Findings fixed in the core along the way: the halted idle must not be stepped for the profile (idle-tolerant
subscription), RET chains must replace their dispatch frame (Zynaps overflowed the 1024-frame stack), `Reset`
before the first activation crashed, a stop must book the last instruction and the frame that just ended (the
window was a frame short), and a refused `start` must not switch a running profile off.
