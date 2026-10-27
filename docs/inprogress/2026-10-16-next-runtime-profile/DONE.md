# DONE: ZX Next runtime profile

Profiling done 2026-10-16: [README.md](README.md) (the table, the samples, the verdict per area). Per-instruction device polling was tried as the first candidate (its removal measured ~3.5 % of a 28 MHz frame) and dropped. The candidates left (tilemap and ULA line renderers, silent AY chips) are **not planned**; the benchmark
`core/benchmarks/emulator/machines/nextruntime_benchmark.cpp` stays to measure them if one is taken up.
