# DONE — peripheral standby

Idle sound cards (classic GS, NeoGS, MoonSound, TSFM) advance in closed form while idle, exactly as the stepped
card; Pentagon turbo frame 548 -> 170 µs, 1x frame 1555 -> 1038 µs.

- What landed, how it stays exact, the tests and the numbers: [results.md](results.md)
- How each card's overhead was found and removed, step by step: [walkthrough.md](walkthrough.md)
- ymfm local patch 6 (idle fast path): [core/src/3rdparty/ymfm/PATCHES.md](../../../core/src/3rdparty/ymfm/PATCHES.md)
- The benchmark rows: [core/benchmarks/emulator/profiles/README.md](../../../core/benchmarks/emulator/profiles/README.md)

Optional follow-ups (not tracked in PLAN.md): [results.md §5](results.md#5-not-done).
