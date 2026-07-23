#include "regionmap.h"

void RegionMap::Build(const BlockTagMap& tagMap)
{
    Clear();
    buildRawRegions(tagMap);
    buildAggregatedRegions();
}

void RegionMap::Build(const BlockTagMap& tagMap, const std::vector<bool>& isUnreachedCode)
{
    Clear();
    buildRawRegions(tagMap);
    buildAggregatedRegionsWithCodeFlow(isUnreachedCode);
}

void RegionMap::buildRawRegions(const BlockTagMap& tagMap)
{
    _rawRegions.clear();

    if (tagMap.getTypes().empty())
        return;

    uint16_t regionStart = 0;
    BlockType currentType = tagMap.getType(0);
    MemoryTags currentTags = tagMap.getTags(0);

    for (uint32_t addr = 1; addr <= 0x10000; addr++)
    {
        BlockType type = (addr < 0x10000) ? tagMap.getType(static_cast<uint16_t>(addr)) : BlockType::UNKNOWN;
        MemoryTags tags = (addr < 0x10000) ? tagMap.getTags(static_cast<uint16_t>(addr)) : 0;

        // End of region when type or tags change, or at end of address space
        if (type != currentType || tags != currentTags || addr == 0x10000)
        {
            RawRegion region;
            region.startAddress = regionStart;
            region.endAddress = static_cast<uint16_t>(addr - 1);
            region.type = currentType;
            region.tags = currentTags;
            _rawRegions.push_back(region);

            regionStart = static_cast<uint16_t>(addr);
            currentType = type;
            currentTags = tags;
        }
    }
}

void RegionMap::buildAggregatedRegions()
{
    _aggregatedRegions.clear();

    if (_rawRegions.empty())
        return;

    // Merge adjacent raw regions with the same type
    // Tags are unioned, boundaries extended
    AggregatedRegion current;
    current.startAddress = _rawRegions[0].startAddress;
    current.endAddress = _rawRegions[0].endAddress;
    current.type = _rawRegions[0].type;
    current.unionTags = _rawRegions[0].tags;
    current.rawRegionIndices.push_back(0);

    for (size_t i = 1; i < _rawRegions.size(); i++)
    {
        const RawRegion& raw = _rawRegions[i];

        // Check if this raw region is adjacent and same type
        bool isAdjacent = (raw.startAddress == current.endAddress + 1);
        bool sameType = (raw.type == current.type);

        if (isAdjacent && sameType)
        {
            // Extend current aggregated region
            current.endAddress = raw.endAddress;
            current.unionTags |= raw.tags;
            current.rawRegionIndices.push_back(static_cast<uint16_t>(i));
        }
        else
        {
            // Save current and start new aggregated region
            _aggregatedRegions.push_back(current);

            current.startAddress = raw.startAddress;
            current.endAddress = raw.endAddress;
            current.type = raw.type;
            current.unionTags = raw.tags;
            current.rawRegionIndices.clear();
            current.rawRegionIndices.push_back(static_cast<uint16_t>(i));
        }
    }

    // Don't forget the last region
    _aggregatedRegions.push_back(current);
}

void RegionMap::buildAggregatedRegionsWithCodeFlow(const std::vector<bool>& isUnreachedCode)
{
    _aggregatedRegions.clear();

    if (_rawRegions.empty())
        return;

    // Helper: get effective type for aggregation
    // UNKNOWN bytes that are unreached CODE should merge with CODE
    auto getEffectiveType = [&](const RawRegion& region) -> BlockType {
        if (region.type == BlockType::UNKNOWN)
        {
            // Check if ALL bytes in this region are marked as unreached code
            bool allUnreachedCode = true;
            for (uint32_t addr = region.startAddress; addr <= region.endAddress; addr++)
            {
                if (!isUnreachedCode[addr])
                {
                    allUnreachedCode = false;
                    break;
                }
            }
            if (allUnreachedCode)
                return BlockType::CODE;  // Treat as CODE for aggregation
        }
        return region.type;
    };

    AggregatedRegion current;
    current.startAddress = _rawRegions[0].startAddress;
    current.endAddress = _rawRegions[0].endAddress;
    current.type = getEffectiveType(_rawRegions[0]);
    current.unionTags = _rawRegions[0].tags;
    current.rawRegionIndices.push_back(0);

    for (size_t i = 1; i < _rawRegions.size(); i++)
    {
        const RawRegion& raw = _rawRegions[i];
        BlockType effectiveType = getEffectiveType(raw);

        bool isAdjacent = (raw.startAddress == current.endAddress + 1);
        bool sameEffectiveType = (effectiveType == current.type);

        if (isAdjacent && sameEffectiveType)
        {
            current.endAddress = raw.endAddress;
            current.unionTags |= raw.tags;
            current.rawRegionIndices.push_back(static_cast<uint16_t>(i));
        }
        else
        {
            _aggregatedRegions.push_back(current);

            current.startAddress = raw.startAddress;
            current.endAddress = raw.endAddress;
            current.type = effectiveType;
            current.unionTags = raw.tags;
            current.rawRegionIndices.clear();
            current.rawRegionIndices.push_back(static_cast<uint16_t>(i));
        }
    }

    _aggregatedRegions.push_back(current);
}

const RawRegion* RegionMap::FindRawRegion(uint16_t address) const
{
    for (const auto& region : _rawRegions)
    {
        if (address >= region.startAddress && address <= region.endAddress)
            return &region;
    }
    return nullptr;
}

const AggregatedRegion* RegionMap::FindAggregatedRegion(uint16_t address) const
{
    for (const auto& region : _aggregatedRegions)
    {
        if (address >= region.startAddress && address <= region.endAddress)
            return &region;
    }
    return nullptr;
}

std::vector<RawRegion> RegionMap::GetRegionsByType(BlockType type) const
{
    std::vector<RawRegion> result;
    for (const auto& region : _rawRegions)
    {
        if (region.type == type)
            result.push_back(region);
    }
    return result;
}

std::vector<RawRegion> RegionMap::GetRegionsByTag(MemoryTag tag) const
{
    std::vector<RawRegion> result;
    for (const auto& region : _rawRegions)
    {
        if (region.hasTag(tag))
            result.push_back(region);
    }
    return result;
}

SegmentationStats RegionMap::GetStats() const
{
    SegmentationStats stats;
    stats.totalRegions = static_cast<uint32_t>(_rawRegions.size());

    for (const auto& region : _rawRegions)
    {
        uint32_t len = region.length();
        switch (region.type)
        {
            case BlockType::CODE:
                stats.codeBytes += len;
                break;
            case BlockType::DATA:
                stats.dataBytes += len;
                break;
            case BlockType::VARIABLE:
                stats.variableBytes += len;
                break;
            case BlockType::ROM_CODE_IN_RAM:
                stats.copiedCodeBytes += len;
                break;
            case BlockType::SMC:
                stats.smcBytes += len;
                break;
            case BlockType::UNKNOWN:
            default:
                stats.unknownBytes += len;
                break;
        }

        if (region.tags != 0)
            stats.taggedRegions++;
    }

    return stats;
}

void RegionMap::Clear()
{
    _rawRegions.clear();
    _aggregatedRegions.clear();
}
