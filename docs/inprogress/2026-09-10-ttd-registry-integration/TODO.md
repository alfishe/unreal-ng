# TODO — TTD peripheral registry integration (2026-09-10)

**Status:** partially done — [PLAN.md](PLAN.md) Phase 1 (blocking bug fixes)
complete; Phases 2–5 (wiring, proofs, tests, docs) not started. Tracked work
sits on the `atm` branch per the plan header.

## Progress
- Phase 1 all FIXED: use-after-free in `RestoreAll` (1.1), silent corruption
  on delta-without-prev (1.2), `uncompressedSize` uint16 cap that truncated
  GeneralSound 512 KB SRAM (1.3), doc inconsistencies (1.4), Dizzy Y
  divergence (1.5, side effect), Python analyzer/`ttd.ksy` sync (1.6).
- Registry itself implemented (`ttdperipheralregistry.*`); TD-2/3/4
  automation follow-ups committed on master (`212b7098`, `372c3840`,
  `f4fdcf74`) — see
  [`../2026-09-14-automation-triage-gaps/ttd-coverage-evaluation.md`](../2026-09-14-automation-triage-gaps/ttd-coverage-evaluation.md).

## Remaining (per PLAN priority order)
1. **Phase 2: registry wiring (P0)** — replace the legacy 4-slot blob vectors
   with the registry end-to-end.
2. **Phase 3: proofs & benchmarks (P1)** — zero-bytes POC for non-connected
   peripherals, size/latency measurements.
3. **Phase 4: test coverage (P1)** — delta round-trip and divergence corpus
   extensions.
4. **Phase 5: documentation (P2)** — label measured-vs-target numbers.

## Pointers
- Cumulative plan: [`../PLAN.md`](../PLAN.md) — Phases 2–5 ride the ATM branch
  merge (item #9); related T1 TTD items #1 (docs truth pass) / #4 (memory dump).
