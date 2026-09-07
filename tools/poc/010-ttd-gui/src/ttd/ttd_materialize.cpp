//
// ttd_materialize.cpp — RAM reconstruction from .ttd page store.
//
// Walks each checkpoint's ram_sub_slots, decompresses each 4 KB sub-page
// (with XorPrev chain walking), and stitches them into a contiguous buffer.
//
// Ports materialize_ram() and get_sub_page() from ttd_format.py.
//

#include "ttd_materialize.h"
#include "ttd_format.h"

#include <zstd.h>
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace ttd {

// ---------------------------------------------------------------------------
// zstd decompression helper
// ---------------------------------------------------------------------------
static std::vector<uint8_t> decompressZstd(const uint8_t* payload, size_t payloadSize,
                                            size_t expectedSize) {
    if (payloadSize == 0)
        throw std::runtime_error("empty payload for Full/XorPrev slot");

    std::vector<uint8_t> out(expectedSize);
    size_t result = ZSTD_decompress(out.data(), expectedSize, payload, payloadSize);
    if (ZSTD_isError(result))
        throw std::runtime_error(std::string("zstd decompress failed: ") +
                                 ZSTD_getErrorName(result));
    if (result != expectedSize)
        throw std::runtime_error("decompressed to " + std::to_string(result) +
                                 " bytes, expected " + std::to_string(expectedSize));
    return out;
}

// ---------------------------------------------------------------------------
// XOR two equal-length buffers
// ---------------------------------------------------------------------------
static void xorBuffers(uint8_t* dst, const uint8_t* a, const uint8_t* b, size_t len) {
    // Process in 8-byte strides for speed, then remainder
    size_t i = 0;
    for (; i + 8 <= len; i += 8) {
        uint64_t va, vb;
        std::memcpy(&va, a + i, 8);
        std::memcpy(&vb, b + i, 8);
        va ^= vb;
        std::memcpy(dst + i, &va, 8);
    }
    for (; i < len; ++i)
        dst[i] = a[i] ^ b[i];
}

// ---------------------------------------------------------------------------
// Materializer implementation
// ---------------------------------------------------------------------------

const std::vector<uint8_t>& Materializer::getSubPage(const TtdDump& dump, uint32_t slotIndex) {
    if (slotIndex == kNeverTouchedSlot)
        throw std::runtime_error("getSubPage called on NEVER_TOUCHED sentinel");

    auto it = _subPageCache.find(slotIndex);
    if (it != _subPageCache.end())
        return it->second;

    const PageSlot& s = dump.slot(slotIndex);
    std::vector<uint8_t> result(kSubPageSize);

    if (s.encoding == kEncodingZero) {
        // Already zero-initialized
    } else if (s.encoding == kEncodingFull) {
        result = decompressZstd(s.payload, s.payload_size, kSubPageSize);
    } else if (s.encoding == kEncodingXorPrev) {
        if (s.prev_slot == kNeverTouchedSlot)
            throw std::runtime_error("slot " + std::to_string(slotIndex) +
                                     ": XorPrev but prev_slot is sentinel");
        const std::vector<uint8_t>& base = getSubPage(dump, s.prev_slot);
        std::vector<uint8_t> delta = decompressZstd(s.payload, s.payload_size, kSubPageSize);
        if (delta.size() != kSubPageSize)
            throw std::runtime_error("XorPrev slot " + std::to_string(slotIndex) +
                                     " decompressed to wrong size");
        xorBuffers(result.data(), base.data(), delta.data(), kSubPageSize);
    } else {
        throw std::runtime_error("slot " + std::to_string(slotIndex) +
                                 ": unknown encoding " + std::to_string(s.encoding));
    }

    auto [inserted, _] = _subPageCache.emplace(slotIndex, std::move(result));
    return inserted->second;
}

std::vector<uint8_t> Materializer::materialize(const TtdDump& dump, const Checkpoint& cp) {
    const uint8_t pages = dump.header().model_ram_pages;
    std::vector<uint8_t> out(static_cast<size_t>(pages) * kEmuPageSize, 0);

    const size_t expectedRefs = static_cast<size_t>(pages) * kSubPagesPerEmuPage;
    if (cp.ram_sub_slots.size() != expectedRefs)
        throw std::runtime_error("checkpoint has " +
                                 std::to_string(cp.ram_sub_slots.size()) +
                                 " sub-slots, expected " + std::to_string(expectedRefs));

    for (uint8_t pageIdx = 0; pageIdx < pages; ++pageIdx) {
        size_t base = static_cast<size_t>(pageIdx) * kEmuPageSize;
        for (uint32_t sub = 0; sub < kSubPagesPerEmuPage; ++sub) {
            uint32_t ref = cp.ram_sub_slots[pageIdx * kSubPagesPerEmuPage + sub];
            if (ref == kNeverTouchedSlot)
                continue;  // leave zeros
            const std::vector<uint8_t>& subBytes = getSubPage(dump, ref);
            std::memcpy(out.data() + base + sub * kSubPageSize,
                        subBytes.data(), kSubPageSize);
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Free-function convenience (no caching)
// ---------------------------------------------------------------------------
std::vector<uint8_t> MaterializeRam(const TtdDump& dump, const Checkpoint& cp) {
    Materializer m;
    return m.materialize(dump, cp);
}

} // namespace ttd
