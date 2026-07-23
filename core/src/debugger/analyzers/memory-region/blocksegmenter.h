#pragma once

#include <array>
#include <cstdint>

#include "types.h"

class MemoryAccessTracker;

/// @brief Classifies each Z80 address into a primary BlockType based on R/W/X counters.
class BlockSegmenter
{
public:
    BlockSegmenter() = default;

    /// @brief Classify a single address based on its R/W/X counts.
    /// @param address     The Z80 address being classified
    /// @param readCount   Number of reads from this address
    /// @param writeCount  Number of writes to this address
    /// @param execCount   Number of times this address was executed
    /// @return The BlockType classification
    static BlockType ClassifyAddress(uint16_t address, uint32_t readCount, uint32_t writeCount, uint32_t execCount);

    /// @brief Perform full classification refresh from MemoryAccessTracker.
    /// @param tracker The memory access tracker to read counters from
    void Refresh(const MemoryAccessTracker* tracker);

    /// @brief Get the type at a specific address.
    BlockType GetType(uint16_t address) const { return _types[address]; }

    /// @brief Get direct access to the type array.
    const std::array<BlockType, 65536>& GetTypes() const { return _types; }

private:
    std::array<BlockType, 65536> _types{};
};
