# tools/poc — Proof-of-Concept research tools

This directory holds research / diagnostic / measurement tools that are
**not** part of the shipping product. They exist to answer specific
design questions and feed their results back into the production code.

## Layout

```
tools/poc/
├── cpp/                          # Pure C++ codec comparison (Google Benchmark)
│   ├── CMakeLists.txt
│   ├── extract_real_buffers.py  # Pull real codec-input buffers from a .ttd
│   ├── real_buffers.bin         # Extracted from active_demo.ttd (4.7 MB)
│   ├── src/
│   │   └── poc_codec_latency.cpp
│   └── vendored/fastlz/          # Single-file public-domain codec
├── capture_workload.py           # Dumps full 48 KB RAM per TTD frame (analysis)
└── poc_ratio_measurement.py      # I-frame / P-frame ratio analyzer
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
cover its design space (small-buffer fast codecs).

## Extracting real codec-input buffers

The PoC ships with a real-data extractor that pulls the exact byte
sequences the production TTD codec hands to its compressor, drawn from
the canonical fixture `active_demo.ttd`:

```sh
python3 tools/poc/cpp/extract_real_buffers.py \
    --ttd testdata/ttd/active_demo.ttd \
    --out tools/poc/cpp/real_buffers.bin
```

This produces `real_buffers.bin` containing two buffer classes:

| Bucket      | Source                                  | Count | Use |
| ----------- | --------------------------------------- | ----: | --- |
| `REAL_full` | Decompressed `ENCODING_FULL` slots      | varies | Raw 4 KB page snapshots (I-frame content) |
| `REAL_xor`  | Decompressed `ENCODING_XOR_PREV` slots  | varies | Real `new XOR prev` delta buffers (P-frame content) |

Buffer counts depend on which fixture is extracted — see "Workload range"
below. Point the extractor at any file in `testdata/ttd/`.

The XOR bucket is the **dominant** production workload — it is 98.7% of
all non-Zero codec inputs in the fixture. Synthetic XOR-deltas (see
below) are kept only as reference shapes.

## Running the C++ codec PoC

```sh
# Full real-data sweep with the recommended statistics:
TTD_REAL_BUFFERS="$(pwd)/tools/poc/cpp/real_buffers.bin" \
    cmake-build-release/bin/poc_codec_latency \
    --benchmark_filter='REAL_' \
    --benchmark_min_time=2s \
    --benchmark_repetitions=3 \
    --benchmark_report_aggregates_only=true

# Or filter to a single codec:
TTD_REAL_BUFFERS="$(pwd)/tools/poc/cpp/real_buffers.bin" \
    cmake-build-release/bin/poc_codec_latency \
    --benchmark_filter='REAL_xor/zstd1'

# Or run all workloads (real + synthetic reference):
cmake-build-release/bin/poc_codec_latency
```

The `TTD_REAL_BUFFERS` env var is required when running the binary from
outside the repo root; otherwise the PoC looks for
`tools/poc/cpp/real_buffers.bin` relative to the cwd.

## Output

At startup the PoC prints the workload Shannon-entropy table to stderr
(the theoretical compression floor — no lossless codec can beat it):

```
workload                 n_bufs     H_mean floor_mean  floor_p50    nz%_p50
-------                  ------     ------ ---------- ----------    -------
REAL_full                    16     3.1211     1598.0     1612.9     29.077
REAL_xor                   1186     0.2299      117.7       15.1      0.220
(illustrative shape only — these came from the old synthetic fixture;
 current numbers are in "Workload range" below)
SYN_random_4k              1000     7.9544     4072.7     4072.7     99.609
SYN_xor_sparse_5pct        1000     0.6188      316.8      317.0      4.858
SYN_xor_clustered_5pct     1000     0.5866      300.3      305.0      4.688
SYN_zero_4k                1000     0.0000        0.0        0.0      0.000
```

Each benchmark line then reports:

| Counter | Meaning |
|---|---|
| `BLI` | Bytes-Length-Increase ratio = compressed_size / raw_size (lower is better) |
| `BLI/floor` | BLI divided by the Shannon floor — 1.0 = perfect entropy coder |
| `floor_B` | Per-buffer theoretical minimum size in bytes |
| `enc_us_p50` / `enc_us_p95` / `enc_us_p99` | Encode latency percentiles (µs) |
| `dec_us_p50` / `dec_us_p95` / `dec_us_p99` | Decode latency percentiles (µs) |
| `enc_MBps` | Encode throughput (MiB/s) |
| `dec_MBps` | Decode throughput (MiB/s) |

## Workloads

| Workload                 | Class     | Description |
| ------------------------ | --------- | ----------- |
| `REAL_full`              | Real      | Raw 4 KB page snapshots from a recorded session |
| `REAL_xor`               | Real      | Real XOR-delta buffers from a recorded session (the dominant TTD workload) |
| `SYN_random_4k`          | Synthetic | Uniform-random 4 KB baseline (worst case) |
| `SYN_xor_sparse_5pct`    | Synthetic | 5% scattered XOR-delta (reference shape) |
| `SYN_xor_clustered_5pct` | Synthetic | 5% clustered XOR-delta (reference shape) |
| `SYN_zero_4k`            | Synthetic | All-zero 4 KB (TTD ZeroPayload short-circuit) |

The `REAL_*` workloads are the basis for the codec decision. The
`SYN_*` workloads are kept only to demonstrate why synthetic data alone
is unreliable — see Section 6.6 of `phase-5-codec-poc-results.md` for
the analysis of why the synthetic 5% XOR-deltas (which have a ~300-byte
entropy floor) led to wrong conclusions about real TTD buffers (which
have a ~15-byte entropy floor).

## Workload range (re-measured 2026-08-20)

Earlier runs of this PoC drew on a *synthesised* `active_demo.ttd` — a Python
script wrote the file byte by byte and filled its pages with random bytes over
zeros. Fixtures now come from real recordings made through the emulator API
(`tools/verification/ttd-analyzer/scripts/record_fixtures.py`), and a single
fixture turned out not to be representative either. Four workloads, 300 frames
each:

| Fixture | Full: entropy / nonzero | XOR: entropy / nonzero (p50) | XOR floor mean / p50 |
|---|---|---|---|
| `idle_session` (128 BASIC menu) | 1.09 / 3.9% | 0.048 / 0.049% | 24 B / 3.4 B |
| `active_demo` (Dizzy Y, a game) | 3.42 / 58.1% | 0.019 / 0.171% | 9.8 B / 10.2 B |
| `demo_7threality` | 5.69 / **93.1%** | 0.055 / 0.122% | 28 B / 6.9 B |
| `demo_across-the-edge-second` | 3.72 / 59.9% | 0.144 / 0.073% | **74 B / 5.0 B** |

Two things the synthetic model got wrong:

* **Full pages.** A demo doing precalculation fills memory — 93% of bytes
  nonzero on 7threality against 3.9% on an idle machine. Any sizing that
  assumes lightly-populated pages is calibrated on the idle case.
* **XOR deltas.** The synthetic `SYN_xor_sparse_5pct` models 5% of bytes
  changing, uniformly. Real deltas are far sparser (0.05–0.17% at the median)
  **and heavy-tailed**: across-the-edge averages a 74-byte entropy floor against
  a 5-byte median, so the mean is dominated by a minority of large frames. The
  synthetic model is wrong in shape, not only in magnitude.

## Findings

Measured on `demo_across-the-edge-second`, the heaviest of the four (BLI shown
as bytes per 4 KB buffer, p50 latencies):

| Codec | XOR bytes | XOR enc | XOR dec | Full bytes | Full enc | Full dec |
|---|---|---|---|---|---|---|
| zstd (n1/1/3) | **63** | 0.96 µs | 1.0 µs | **1343** | 8.6 µs | 2.0 µs |
| brotli-1 | 72 | 2.9 µs | 6.8 µs | 1389 | 14.9 µs | 9.2 µs |
| zlib-1 | 86 | 4.7 µs | 3.4 µs | 1355 | 24.3 µs | 6.6 µs |
| lz4-fast | 99 | 0.54 µs | 0.54 µs | 1546 | 1.25 µs | 0.54 µs |
| snappy | 250 | 0.42 µs | 0.38 µs | 1588 | 0.92 µs | 0.54 µs |

The original verdict survives re-measurement on real data: **zstd-1 is
Pareto-optimal**. Every zstd level produces byte-identical output on the XOR
workload, so level tuning buys nothing there; lz4 is ~1.6× larger for ~2× the
speed, and everything slower than zstd is also no smaller.

Full results and entropy analysis: Section 6 of
`docs/inprogress/2026-07-19-time-travel/phase-5-codec-poc-results.md`.
