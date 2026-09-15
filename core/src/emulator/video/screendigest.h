#pragma once

#include <cstddef>
#include <cstdint>

// Forward declarations
class EmulatorContext;
class Memory;

/// ScreenDigest — deterministic FNV-1a 64-bit digests over screen memory
///
/// Purpose: cheap change detection for automation (polling loops, digest-
/// quantum frame sampling). Physical RAM pages are hashed rather than the
/// Z80 address space so results are independent of bank paging:
///   - Screen 0 (normal): RAM page 5, 16 KiB — fixed at 0x4000 in every model
///   - Screen 1 (shadow): RAM page 7, 16 KiB (128K-class models only)
/// The memory subsystem always allocates the full 256-page RAM array, so
/// hashing page 7 on a 48K model is safe (the page just stays untouched).
///
/// A combined digest folds in the border color (port $FE) so border-only
/// changes are still visible. xxHash is not vendored in core/src/3rdparty,
/// so the plan's FNV-1a 64 fallback is used — no dependencies, ~1 GiB/s.
class ScreenDigest
{
public:
    static constexpr uint16_t kScreen0RAMPage = 5;
    static constexpr uint16_t kScreen1RAMPage = 7;
    static constexpr size_t kRAMPageSize = 16 * 1024;

    /// FNV-1a 64 initial value
    static constexpr uint64_t kInitialValue = 14695981039346656037ull;
    /// FNV-1a 64 prime
    static constexpr uint64_t kPrime = 1099511628211ull;

    /// FNV-1a 64 over one physical 16 KiB RAM page
    /// @param memory Memory subsystem (page array is always fully allocated)
    /// @param page Physical RAM page number (0..255)
    static uint64_t DigestRAMPage(Memory* memory, uint16_t page);

    /// FNV-1a 64 over a Z80-visible address range [start, end] (inclusive)
    static uint64_t DigestZ80Range(Memory* memory, uint16_t start, uint16_t end);

    /// One FNV-1a mix step — used to fold extra values (border color) into a digest
    static uint64_t MixValue(uint64_t digest, uint8_t value)
    {
        return (digest ^ value) * kPrime;
    }
};
