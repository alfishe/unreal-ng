#pragma once

#include "../itagclassifier.h"
#include "../analysiscontext.h"
#include "../blocktagmap.h"
#include "../types.h"

/// @brief Classifier for decompressor/unpacker code.
/// Currently a placeholder - full implementation requires caller tracking
/// which isn't available in MemoryAccessTracker.
/// Future: integrate with CallTraceBuffer for burst write pattern detection.
class DecompressorClassifier : public ITagClassifier
{
public:
    std::string name() const override { return "decompressor"; }

    std::vector<std::string> dependencies() const override
    {
        return {"generic"};  // Run after basic tagging
    }

    void classify([[maybe_unused]] const AnalysisContext& ctx,
                  [[maybe_unused]] BlockTagMap& tagMap) override
    {
        // Decompressor detection requires tracking which CODE regions
        // write to which memory regions. MemoryAccessTracker only tracks
        // R/W/X counts, not callers.
        //
        // To implement this properly we'd need:
        // 1. Per-address write caller tracking (like PortTracker has)
        // 2. Or integration with CallTraceBuffer to detect LDIR/LDD patterns
        //
        // For now, SMC regions (write+execute) are already tagged by
        // BlockSegmenter. The code that CREATED those SMC regions
        // would be the decompressor, but we can't identify it without
        // caller tracking.
    }
};
