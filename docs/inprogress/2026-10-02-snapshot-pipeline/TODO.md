# TODO: one snapshot pipeline for every machine and every format

Design: [proposal.md](proposal.md). Plan: [PLAN.md](../PLAN.md) #84 (T3, owner: lower priority).
First user: the Sprinter ZX mode, phase Z5 ([tdd-zx-mode.md](../2026-09-28-sprinter/tdd-zx-mode.md) §9, Q4;
owner decision 2026-10-02: yes, via this shared pipeline, lower priority).

Status: proposal written 2026-10-02 (documents only, no code). Open questions Q1-Q8 in
[proposal.md §11](proposal.md#11-open-questions) wait for the owner.

| Step | Item | Size | Status |
|:--|:--|:--|:--|
| P0 | Golden commit digests for every SNA / Z80 / SZX fixture × creatable model on `master`; pin the suspected defects (Pentagon 1024 lock, ATM / TS-Conf after a cold reset, 128K on 48K) as current behavior | S-M | open |
| P1 | `SnapshotImage` + `SnapshotReport`; parsers (SNA, Z80, SZX, SPG, ZXP) fill the image; pipeline inside `LoadSnapshotStaged`; `LegacyCommit` = today's code | M | open |
| P2 | Plan step, `ISnapshotCommitPolicy`, named-policy registry, `GetSnapshotPolicy()` on the port decoder | S | open |
| P3 | `commit` option + `inspect` on WebAPI / OpenAPI, MCP, CLI, Lua, Python; recipe `.recipe/media/load-snapshot.md` | S-M | open |
| P4 | Sprinter ZX commit (= Sprinter Z5): cell-table mapping, refusal outside ZX mode, T-ZX-11 / T-ZX-12 | M | open |
| P5 | Fit checks and machine policies after the owner's answers: 128K on 48K (Q1), 48K on 128K (Q2), Pentagon 1024 compatibility, ATM family, TS-Conf | M | open |
| P6 | Save path: capture → image → writer; 48K SNA writer stops touching live RAM | M | open |
| P7 | One model-switch orchestrator for SZX / SPG / RZX; Qt uses it (Q6) | S | open |
| P8 | TTD: a load during a recording continues the track with a full checkpoint + marker (Q5), after TTD v2 regions | S-M | open |
| P9 | Clean-up: legacy commits read the image; remove duplicated staging and SNA dead code | M | open |
