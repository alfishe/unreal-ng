# DONE — Scorpion ZS-256 clone (PROFSCORP) (2026-09-07)

**Status:** complete.

## What landed
- Scorpion ZS-256 / ProfROM machine support: `PROFSCORP` model (authoritative model
  list in AGENTS.md / `GET /emulator/models`), Scorpion-256 port decoder with ProfROM
  paging/shadow-monitor semantics, turbo mode, ProfROM boot & NMI behavior per the
  disassembly findings in this folder, base-ROM RAM-size detection, e2e verification
  (Base ROM + ProfROM instantiation + boot discriminators).
- Pentagon 1024K decoder (6-bit paging) landed alongside (`ef117d18`).

## Evidence
- `core/src/emulator/ports/models/portdecoder_scorpion256.h`,
  `portdecoder_pentagon1024.h`; hardware-reference + verification docs in this folder;
  golden bank-map tests (`modelsregression_test.cpp`, `574f8cfa`).

## Follow-ups
- SMUC board stays absent by default (opt-in testing convention) — deliberate.
- Automation triage for Scorpion models improved by the 2026-09-14 program (paging,
  ports, video-mode names — all landed).
