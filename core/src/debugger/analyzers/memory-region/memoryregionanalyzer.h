#pragma once

#include <memory>
#include <string>
#include <vector>

#include "debugger/analyzers/ianalyzer.h"
#include "blocksegmenter.h"
#include "blocktagmap.h"
#include "codeflowanalyzer.h"
#include "regionmap.h"
#include "tagpipeline.h"
#include "types.h"

class EmulatorContext;
class AnalyzerManager;

/// @brief Compound analyzer that classifies the full 64KB address space into semantic regions.
class MemoryRegionAnalyzer : public IAnalyzer
{
public:
    static constexpr const char* ANALYZER_ID = "memory-region";
    static constexpr const char* ANALYZER_NAME = "MemoryRegionAnalyzer";

    explicit MemoryRegionAnalyzer(EmulatorContext* context);
    ~MemoryRegionAnalyzer() override = default;

    // === IAnalyzer interface ===
    std::string getName() const override { return ANALYZER_NAME; }
    std::string getUUID() const override { return _uuid; }
    void onActivate(AnalyzerManager* mgr) override;
    void onDeactivate() override;
    void onFrameEnd() override;

    // === Query API (Raw Regions - exact boundaries) ===

    /// Get all raw regions (exact type+tag boundaries).
    const std::vector<RawRegion>& getRegions() const { return _regionMap.GetRawRegions(); }

    /// Get raw region containing a specific address.
    const RawRegion* getRegionAt(uint16_t address) const { return _regionMap.FindRawRegion(address); }

    /// Get all raw regions of a specific primary type.
    std::vector<RawRegion> getRegionsByType(BlockType type) const { return _regionMap.GetRegionsByType(type); }

    /// Get all raw regions with a specific tag.
    std::vector<RawRegion> getRegionsByTag(MemoryTag tag) const { return _regionMap.GetRegionsByTag(tag); }

    /// Get raw region count.
    size_t getRawRegionCount() const { return _regionMap.GetRawRegionCount(); }

    // === Query API (Aggregated Regions - merged same-type) ===

    /// Get all aggregated regions (adjacent same-type merged, tags unioned).
    const std::vector<AggregatedRegion>& getAggregatedRegions() const { return _regionMap.GetAggregatedRegions(); }

    /// Get aggregated region containing a specific address.
    const AggregatedRegion* getAggregatedRegionAt(uint16_t address) const { return _regionMap.FindAggregatedRegion(address); }

    /// Get aggregated region count.
    size_t getAggregatedRegionCount() const { return _regionMap.GetAggregatedRegionCount(); }

    // === Per-Address Queries (O(1)) ===

    /// Get primary type at a specific address.
    BlockType getTypeAt(uint16_t address) const { return _tagMap.getType(address); }

    /// Get tags at a specific address.
    MemoryTags getTagsAt(uint16_t address) const { return _tagMap.getTags(address); }

    /// Get summary statistics.
    SegmentationStats getStats() const { return _regionMap.GetStats(); }

    /// Force a full refresh.
    void refresh();

    // === Configuration ===

    /// Set refresh interval (every N frames, default=10).
    void setRefreshInterval(uint32_t frames) { _refreshInterval = frames; }

    /// Get code flow analysis results from last refresh.
    const CodeFlowAnalyzer::AnalysisResult& getCodeFlowResult() const { return _lastCodeFlowResult; }

private:
    std::string _uuid;
    EmulatorContext* _context;
    AnalyzerManager* _manager = nullptr;

    // Core data structures
    BlockSegmenter _segmenter;
    BlockTagMap _tagMap;
    RegionMap _regionMap;
    TagPipeline _pipeline;
    CodeFlowAnalyzer::AnalysisResult _lastCodeFlowResult;

    // Refresh control
    uint32_t _refreshInterval = 10;
    uint32_t _framesSinceRefresh = 0;

    void runClassificationPipeline();
    void applyHardcodedRegions();
};
