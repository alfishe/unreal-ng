#pragma once
//
// ttd_materialize.h — Reconstructs full RAM for a checkpoint.
//

#include <cstdint>
#include <vector>
#include <unordered_map>
#include "ttd_reader.h"

namespace ttd {

/// Materialize the full RAM image (model_ram_pages * 16 KB) for one checkpoint.
/// Walks ram_sub_slots, decodes each 4 KB sub-page via zstd, applies XOR deltas.
/// Uses an internal cache so consecutive scrub steps don't re-decode chains.
std::vector<uint8_t> MaterializeRam(const TtdDump& dump, const Checkpoint& cp);

/// Stateful materializer with sub-page caching (use for scrubbing).
class Materializer {
public:
    /// Get the full RAM image for a checkpoint. Cached per-slot.
    std::vector<uint8_t> materialize(const TtdDump& dump, const Checkpoint& cp);

    /// Clear the cache (e.g., when loading a new file).
    void clearCache() { _subPageCache.clear(); }

private:
    /// Decompress one 4 KB sub-page from a slot, walking XorPrev chains.
    /// Result is cached by slot index.
    const std::vector<uint8_t>& getSubPage(const TtdDump& dump, uint32_t slotIndex);

    std::unordered_map<uint32_t, std::vector<uint8_t>> _subPageCache;
};

} // namespace ttd
