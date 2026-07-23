#pragma once

#include <vector>

#include "types.h"
#include "blocktagmap.h"

/// @brief A raw region - exact boundaries from access patterns.
/// Every type+tag change creates a new raw region.
struct RawRegion
{
    uint16_t startAddress = 0;
    uint16_t endAddress = 0;
    BlockType type = BlockType::UNKNOWN;
    MemoryTags tags = 0;

    uint16_t length() const { return endAddress - startAddress + 1; }
    bool hasTag(MemoryTag tag) const { return (tags & static_cast<uint32_t>(tag)) != 0; }
};

/// @brief An aggregated region - merges adjacent same-type raw regions.
/// Tags are unioned, small gaps are NOT absorbed (preserves accuracy).
struct AggregatedRegion
{
    uint16_t startAddress = 0;
    uint16_t endAddress = 0;
    BlockType type = BlockType::UNKNOWN;
    MemoryTags unionTags = 0;              // OR of all constituent raw region tags
    std::vector<uint16_t> rawRegionIndices; // Indices into raw regions vector

    uint16_t length() const { return endAddress - startAddress + 1; }
    bool hasTag(MemoryTag tag) const { return (unionTags & static_cast<uint32_t>(tag)) != 0; }
    uint32_t rawRegionCount() const { return static_cast<uint32_t>(rawRegionIndices.size()); }
};

/// @brief Merges per-address types/tags into regions.
/// Provides both raw (exact) and aggregated (merged same-type) views.
class RegionMap
{
public:
    RegionMap() = default;

    /// @brief Build regions from a BlockTagMap.
    void Build(const BlockTagMap& tagMap);

    /// @brief Build with code flow info - treats unreached CODE as coalesce-able with CODE.
    /// @param tagMap The tag map
    /// @param isUnreachedCode Per-address flags from CodeFlowAnalyzer
    void Build(const BlockTagMap& tagMap, const std::vector<bool>& isUnreachedCode);

    // === Raw Regions (exact boundaries) ===

    /// @brief Get all raw regions.
    const std::vector<RawRegion>& GetRawRegions() const { return _rawRegions; }

    /// @brief Find the raw region containing a specific address.
    const RawRegion* FindRawRegion(uint16_t address) const;

    /// @brief Get raw region count.
    size_t GetRawRegionCount() const { return _rawRegions.size(); }

    // === Aggregated Regions (merged same-type) ===

    /// @brief Get all aggregated regions.
    const std::vector<AggregatedRegion>& GetAggregatedRegions() const { return _aggregatedRegions; }

    /// @brief Find the aggregated region containing a specific address.
    const AggregatedRegion* FindAggregatedRegion(uint16_t address) const;

    /// @brief Get aggregated region count.
    size_t GetAggregatedRegionCount() const { return _aggregatedRegions.size(); }

    // === Legacy API (uses raw regions for compatibility) ===

    /// @brief Get all regions (raw regions for backward compatibility).
    const std::vector<RawRegion>& GetAllRegions() const { return _rawRegions; }

    /// @brief Find region containing address (uses raw regions).
    const RawRegion* FindRegion(uint16_t address) const { return FindRawRegion(address); }

    /// @brief Get all regions of a specific type (raw regions).
    std::vector<RawRegion> GetRegionsByType(BlockType type) const;

    /// @brief Get all regions with a specific tag (raw regions).
    std::vector<RawRegion> GetRegionsByTag(MemoryTag tag) const;

    /// @brief Get segmentation statistics.
    SegmentationStats GetStats() const;

    /// @brief Clear all regions.
    void Clear();

private:
    void buildRawRegions(const BlockTagMap& tagMap);
    void buildAggregatedRegions();
    void buildAggregatedRegionsWithCodeFlow(const std::vector<bool>& isUnreachedCode);

    std::vector<RawRegion> _rawRegions;
    std::vector<AggregatedRegion> _aggregatedRegions;
};
