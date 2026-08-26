# zstd v1.5.7 (embedded source)

This directory contains the **embedded** zstd v1.5.7 source tree, compiled
directly into the `zstd::libzstd` static library target by
[`CMakeLists.txt`](CMakeLists.txt).

## Layout

```
core/src/3rdparty/zstd/
├── CMakeLists.txt       # compiles lib/ into libzstd_static
├── README.md            # this file
├── LICENSE              # upstream BSD-3-Clause (zstd's preferred license)
├── COPYING              # upstream GPL-2.0 (alternative license)
└── lib/                 # source tree from zstd v1.5.7 release tarball
    ├── zstd.h           # public API
    ├── zstd_errors.h    # public error codes
    ├── zdict.h          # public dictionary API
    ├── common/          # shared internal helpers
    ├── compress/        # encoder + match-finders
    ├── decompress/      # decoder
    └── dictBuilder/     # dictionary training
```

`legacy/`, `deprecated/`, `dll/`, `programs/`, `tests/`, `contrib/`, and
`modules.modulemap` from the upstream tarball are intentionally NOT included
(see "Configuration" below).

## Why embed source?

Previous iterations of this directory used `FetchContent` to download the
tarball at configure time. That worked, but:

- **Network dependency**: builds on air-gapped CI boxes failed. Configure
  time was at the mercy of github.com availability.
- **No source auditability**: the exact code shipping in the binary was
  not visible in `git log` / `git blame` — only a URL + SHA256 was.

Embedding the source fixes both. The trade-off is ~3 MB of additional
repository size, which is negligible relative to the project's existing
vendored dependencies (liblzma, QHexView, jsoncpp, etc.).

The other reasons we vendor (instead of `find_package(zstd REQUIRED)`)
remain unchanged:

- **Reproducible builds**: every developer and CI box gets the exact same
  zstd version regardless of what's installed on the host.
- **No prerequisite to install**: contributors no longer need
  `brew install zstd` (or the apt/yum equivalent) before the first build.
- **Protocol-level dependency**: the C++ TTD codec emits zstd-compressed
  frames in the `.ttd` on-disk format (see `core/src/debugger/ttd/ttd.ksy`).
  The compression format is part of the wire protocol — we cannot ship
  without it, so depending on the host's installed version is fragile.

## Codec PoC

The codec comparison (zstd vs lz4 vs snappy vs brotli vs FastLZ vs Lizard
vs zlib-ng) lives in
[`tools/poc/cpp/`](../../../../tools/poc/cpp/) and is built with Google
Benchmark. zstd-1 wins on the combined latency + ratio + decode-speed
score.

Full results are documented in
[`docs/inprogress/2026-07-19-time-travel/phase-5-codec-poc-results.md`](../../../../docs/inprogress/2026-07-19-time-travel/phase-5-codec-poc-results.md).

## Configuration

The build is configured for the TTD codec workload. The configuration
is hard-coded in [`CMakeLists.txt`](CMakeLists.txt) via
`target_compile_definitions`:

| Define | Value | Why |
|---|---|---|
| `ZSTD_LEGACY_SUPPORT` | `0` | Drop decoders for v0.x frame formats |
| `ZSTD_DISABLE_ASM` | (defined) | Portable C only; matches Homebrew's build |
| `ZSTD_MULTITHREAD` | (not defined) | Single-threaded; the TTD codec is synchronous |

What is **not** built:

| Skipped | Why |
|---|---|
| `lib/legacy/` | We disable `ZSTD_LEGACY_SUPPORT` |
| `lib/dll/` | We don't ship a Windows DLL |
| `lib/deprecated/` | Deprecated APIs |
| `programs/`, `tests/`, `contrib/` | Not part of `lib/` |
| `ZSTD_MULTITHREAD` | TTD codec is sync; `zstdmt_compress.c` falls back to sequential stub |

## Upgrading

To upgrade zstd:

1. Download the new release tarball:
   ```sh
   curl -sLO https://github.com/facebook/zstd/releases/download/v<X.Y.Z>/zstd-<X.Y.Z>.tar.gz
   shasum -a 256 zstd-<X.Y.Z>.tar.gz
   ```
2. Replace the contents of `lib/common/`, `lib/compress/`,
   `lib/decompress/`, and `lib/dictBuilder/` with the new versions.
3. Replace `lib/zstd.h`, `lib/zstd_errors.h`, `lib/zdict.h`.
4. Replace `LICENSE` and `COPYING`.
5. Bump `ZSTD_VERSION` in [`CMakeLists.txt`](CMakeLists.txt).
6. Re-run `tools/poc/cpp/` (Google Benchmark PoC) to confirm zstd-1 is
   still the optimal level for the TTD codec workload.
7. Re-run the TTD test suite:
   ```sh
   cmake --build cmake-build-release --target core-tests -j 8
   cmake-build-release/bin/core-tests --gtest_filter='TTD_*'
   ```
   All 248+ tests must remain green.

## License

zstd is dual-licensed under BSD-3-Clause (see [`LICENSE`](LICENSE)) and
GPL-2.0 (see [`COPYING`](COPYING)). The project consumes it under the
BSD-3-Clause terms.
