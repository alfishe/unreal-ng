#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

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
    /// The 8 bytes of a 64-bit value, low byte first
    static uint64_t MixDigest(uint64_t digest, uint64_t value)
    {
        for (int shift = 0; shift < 64; shift += 8)
            digest = MixValue(digest, static_cast<uint8_t>((value >> shift) & 0xFF));
        return digest;
    }
    /// FNV-1a 64 over any buffer (a device's own video memory)
    static uint64_t DigestBytes(const uint8_t* data, size_t size, uint64_t seed = kInitialValue);
};

/// A picture a machine keeps outside the RAM pages (Screen::DigestSurface): the Sprinter's 256 KB
/// video RAM with the latches that place it. The digest's default and active modes hash it
/// instead of RAM pages 5 / 7, which say nothing about such a picture
struct ScreenDigestSurface
{
    std::string name;         ///< "vram"
    std::string description;  ///< what is hashed
    uint64_t digest = 0;
    size_t bytes = 0;
};

/// What to hash (every interface parses into this: ScreenDigestQueryFromStrings)
struct ScreenDigestQuery
{
    bool active = false;            ///< mode=active: what the current video mode displays
    std::vector<uint16_t> banks;    ///< explicit RAM pages (overrides active)
    bool range = false;             ///< explicit Z80 range (overrides banks and active)
    uint16_t start = 0x4000;
    uint16_t end = 0x7FFF;
    bool includeBorder = true;
};

/// The digest and the change tracking against the previous poll, the same on every interface
/// (WebAPI /state/screen/digest, CLI digest, Lua / Python screen_digest, MCP screen_digest)
struct ScreenDigestResult
{
    bool ok = false;
    std::string error;
    uint64_t frame = 0;
    uint64_t combined = 0;
    bool changed = false;
    uint64_t previousDigest = 0;
    uint64_t previousFrame = 0;

    bool range = false;
    uint16_t start = 0, end = 0;
    uint64_t rangeDigest = 0;

    std::vector<std::pair<uint16_t, uint64_t>> banks;  ///< page, digest

    bool activeSurface = false;       ///< the pages / surface came from the video mode
    std::string videoMode;
    std::vector<uint16_t> activePages;

    bool deviceSurface = false;       ///< a ScreenDigestSurface was hashed (the Sprinter's video RAM)
    ScreenDigestSurface surface;

    bool includeBorder = true;
    uint8_t border = 0;
};

namespace ScreenDigestCompute
{
/// mode "default" / "active", banks "5,7", start / end (hex or decimal), include_border "true" / "false";
/// empty = not given. False with `error` on a bad value
bool QueryFromStrings(const std::string& mode, const std::string& banks, const std::string& start, const std::string& end,
                      const std::string& includeBorder, ScreenDigestQuery& query, std::string& error);
/// Hash, compare with the previous poll and remember this one (EmulatorState::last_screen_digest)
ScreenDigestResult Compute(EmulatorContext* context, const ScreenDigestQuery& query);
}  // namespace ScreenDigestCompute
