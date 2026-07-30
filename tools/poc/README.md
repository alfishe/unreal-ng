# tools/poc — Proof-of-Concept research tools

This directory holds research / diagnostic / measurement tools that are
**not** part of the shipping product. They exist to answer specific
design questions and feed their results back into the production code.

## Layout

```
tools/poc/
├── cpp/                     # Pure C++ codec comparison (Google Benchmark)
│   ├── CMakeLists.txt
│   ├── README.md            # See this file for build/run instructions
│   ├── src/
│   │   └── poc_codec_latency.cpp
│   └── vendored/fastlz/     # Single-file public-domain codec
├── capture_workload.py      # Dumps full 48 KB RAM per TTD frame (analysis)
└── poc_ratio_measurement.py # I-frame / P-frame ratio analyzer
```

## Building the C++ codec PoC

The C++ PoC is **opt-in** — it does not build by default. Enable it with
`-DBUILD_POC=ON`:

```sh
cmake -S . -B cmake-build-release -G Ninja -DBUILD_POC=ON
cmake --build cmake-build-release --target poc_codec_latency -j 8
```

System codec libraries (lz4, snappy, brotli, zlib) are auto-detected at
configure time via CMake `find_library`. Missing codecs are silently
dropped from the comparison. On macOS install them via Homebrew:

```sh
brew install lz4 snappy brotli zlib
```

FastLZ is vendored at `cpp/vendored/fastlz/` (public domain, single
file). Lizard was considered but is not vendored because (a) the upstream
repo on github.com is no longer reachable, and (b) FastLZ + lz4 already
cover its design space (small-binary fast codecs).

## Running the C++ codec PoC

```sh
# Full matrix: every codec × every workload.
cmake-build-release/bin/poc_codec_latency

# Filter to a single workload (e.g. the TTD P-frame XOR-delta case).
cmake-build-release/bin/poc_codec_latency --benchmark_filter='BM_E_4k_xor_clustered_5pct'

# Longer time per benchmark for tighter statistics.
cmake-build-release/bin/poc_codec_latency --benchmark_min_time=5s
```

## Output

Each benchmark line reports:

| Counter | Meaning |
|---|---|
| `BLI` | Bytes-Length-Increase ratio = compressed_size / raw_size (lower is better) |
| `enc_us_p50` / `enc_us_p95` / `enc_us_p99` | Encode latency percentiles (µs) |
| `dec_us_p50` / `dec_us_p95` / `dec_us_p99` | Decode latency percentiles (µs) |
| `enc_MBps` | Encode throughput (MiB/s) |
| `dec_MBps` | Decode throughput (MiB/s) |

## Workloads

The four synthetic workloads mirror what the TTD codec hands to the
compressor:

| Workload | Description | Mirrors |
|---|---|---|
| `A_4k_full` | Random 4 KB baseline | Worst case (random RAM) |
| `C_4k_xor_sparse_5pct` | 5% scattered XOR-delta | TTD P-frame, scattered writes |
| `E_4k_xor_clustered_5pct` | 5% clustered XOR-delta | TTD P-frame, working-set writes |
| `F_4k_zero` | All-zero 4 KB | TTD ZeroPayload short-circuit |

The XOR-delta workloads produce buffers that are **95% zeros** with 5%
non-zero bytes — exactly the byte distribution that `InternXor()` hands
to `ZSTD_compressCCtx()` in production.

## What about real-workload data?

The previous Python PoC extracted buffers from `active_demo.ttd` by
scanning for the zstd magic and decompressing each frame. The C++ PoC
focuses on synthetic workloads that bracket the design space cleanly.
Adding real-workload extraction in C++ would require linking against
the TTD analyzer library, which is overkill for a codec comparison.

The Python PoC's real-workload findings (Section 5 of phase-5-codec-poc-results.md)
confirmed that real `.ttd` XOR-delta buffers behave essentially
identically to the synthetic `E_4k_xor_clustered_5pct` workload, which
is why the synthetic is a sufficient proxy here.
