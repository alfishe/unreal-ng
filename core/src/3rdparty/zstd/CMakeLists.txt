# zstd v1.5.7 — embedded source.
#
# The actual zstd library source tree lives under `lib/` and is compiled
# directly by this CMakeLists.txt into the `zstd::libzstd` static library
# target that the rest of the project links against.
#
# Why embed source instead of FetchContent / find_package:
#   - **Reproducible builds with no network**: FetchContent downloaded the
#     tarball from github.com at configure time, which fails on air-gapped
#     CI boxes and is at the mercy of GitHub availability. Embedded source
#     always builds.
#   - **Auditable**: the exact code that ships in the binary is in the
#     repository, reviewable in `git log` and `git blame`.
#   - **No host prerequisite**: contributors don't need to `brew install
#     zstd` before the first build.
#   - **Protocol-level dependency**: the C++ TTD codec emits zstd-compressed
#     frames in the `.ttd` on-disk format (see `core/src/debugger/ttd/ttd.ksy`).
#     The compression format is part of the wire protocol — we cannot ship
#     without it, so depending on the host's installed version is fragile.
#
# The codec PoC (tools/poc/cpp/) measured zstd-1 as the optimal TTD codec
# in v2 — see docs/inprogress/2026-07-19-time-travel/phase-5-codec-poc-results.md
# for the head-to-head results versus lz4, snappy, brotli, FastLZ, Lizard,
# zlib-ng, and zstd's own negative levels.
#
# To upgrade zstd:
#   1. Download the new release tarball and verify its SHA256.
#   2. Replace lib/common, lib/compress, lib/decompress, lib/dictBuilder,
#      and the three root headers (lib/zstd.h, lib/zstd_errors.h, lib/zstd.h)
#      with the new versions.
#   3. Replace LICENSE and COPYING.
#   4. Bump ZSTD_VERSION below.
#   5. Re-run tools/poc/cpp/ to confirm zstd-1 is still the optimal level.
#   6. Re-run the TTD test suite (cmake-build-release/bin/core-tests
#      --gtest_filter='TTD_*'). All tests must remain green.

set(ZSTD_VERSION "1.5.7")

# All .c sources required for libzstd_static with ZSTD_MULTITHREAD_SUPPORT=OFF
# and ZSTD_LEGACY_SUPPORT=OFF. The set mirrors what zstd's own CMake project
# compiles into libzstd_static under the same configuration.
set(ZSTD_SOURCES
    # common/
    lib/common/debug.c
    lib/common/entropy_common.c
    lib/common/error_private.c
    lib/common/fse_decompress.c
    lib/common/pool.c
    lib/common/threading.c
    lib/common/xxhash.c
    lib/common/zstd_common.c
    # compress/
    lib/compress/fse_compress.c
    lib/compress/hist.c
    lib/compress/huf_compress.c
    lib/compress/zstd_compress.c
    lib/compress/zstd_compress_literals.c
    lib/compress/zstd_compress_sequences.c
    lib/compress/zstd_compress_superblock.c
    lib/compress/zstd_double_fast.c
    lib/compress/zstd_fast.c
    lib/compress/zstd_lazy.c
    lib/compress/zstd_ldm.c
    lib/compress/zstd_opt.c
    lib/compress/zstd_preSplit.c
    lib/compress/zstdmt_compress.c
    # decompress/
    lib/decompress/huf_decompress.c
    lib/decompress/zstd_ddict.c
    lib/decompress/zstd_decompress.c
    lib/decompress/zstd_decompress_block.c
    # dictBuilder/
    lib/dictBuilder/cover.c
    lib/dictBuilder/divsufsort.c
    lib/dictBuilder/fastcover.c
    lib/dictBuilder/zdict.c
)

# Build libzstd_static — a C-language static library.
# C language (not C++) because zstd is a C project; using C avoids any
# name-mangling or C++ ABI concerns and matches the upstream build.
#
# The enclosing projects declare LANGUAGES CXX only (core, unreal-qt when
# configured standalone as CI does), so C must be enabled here explicitly -
# otherwise CMake fails at generate time with "CMAKE_C_COMPILE_OBJECT not set".
enable_language(C)
add_library(libzstd_static STATIC ${ZSTD_SOURCES})

# Public include directory: the lib/ root holds the public headers
# (zstd.h, zstd_errors.h, zdict.h). Consumers reach the internal headers
# via relative paths from lib/ (e.g. "common/zstd_internal.h"), so lib/
# must be on the include path even though only the three root headers
# are part of the public API.
target_include_directories(libzstd_static PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/lib"
)

# Configuration defines (mirror the FetchContent cache vars from before):
#
# ZSTD_LEGACY_SUPPORT=0   — drop decoders for v0.x frame formats.
# ZSTD_MULTITHREAD not    — single-threaded; zstdmt_compress.c falls back
#                          to a sequential stub. The TTD codec is sync.
# ZSTD_DISABLE_ASM        — portable C only; matches Homebrew's build.
target_compile_definitions(libzstd_static PRIVATE
    ZSTD_LEGACY_SUPPORT=0
    ZSTD_DISABLE_ASM
)

# Suppress noisy warnings from zstd's own code (not ours to clean up).
if(MSVC)
    target_compile_options(libzstd_static PRIVATE
        /wd4127   # conditional expression is constant (zstd uses do{}while(0))
        /wd4324   # structure was padded due to alignment specifier
    )
else()
    target_compile_options(libzstd_static PRIVATE
        -Wno-unused-function
        -Wno-unused-variable
        -Wno-missing-braces
    )
endif()

# No precompiled header for zstd's C sources (the project-wide PCH is C++).
set_target_properties(libzstd_static PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
)

# Expose the `zstd::libzstd` ALIAS target so the rest of the build can link
# against the same name regardless of whether zstd came from FindPackage,
# FetchContent, or (now) embedded source.
add_library(zstd::libzstd ALIAS libzstd_static)

message(STATUS "zstd: embedded v${ZSTD_VERSION} (target: zstd::libzstd)")
