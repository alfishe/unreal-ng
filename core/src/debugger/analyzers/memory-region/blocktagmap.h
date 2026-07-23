#pragma once

#include <array>
#include <cstdint>

#include "types.h"

/// @brief Per-address storage for BlockType and MemoryTags.
class BlockTagMap
{
public:
    BlockTagMap() { clear(); }

    void clear()
    {
        _types.fill(BlockType::UNKNOWN);
        _tags.fill(0);
    }

    // Type access
    void setType(uint16_t addr, BlockType type) { _types[addr] = type; }
    BlockType getType(uint16_t addr) const { return _types[addr]; }
    const std::array<BlockType, 65536>& getTypes() const { return _types; }

    // Tag access
    void addTag(uint16_t addr, MemoryTag tag) { _tags[addr] |= static_cast<uint32_t>(tag); }
    void setTags(uint16_t addr, MemoryTags tags) { _tags[addr] = tags; }
    MemoryTags getTags(uint16_t addr) const { return _tags[addr]; }
    bool hasTag(uint16_t addr, MemoryTag tag) const
    {
        return (_tags[addr] & static_cast<uint32_t>(tag)) != 0;
    }
    const std::array<MemoryTags, 65536>& getAllTags() const { return _tags; }

private:
    std::array<BlockType, 65536> _types{};
    std::array<MemoryTags, 65536> _tags{};
};
