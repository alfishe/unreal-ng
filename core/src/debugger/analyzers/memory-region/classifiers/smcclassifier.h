#pragma once

#include "../itagclassifier.h"
#include "../analysiscontext.h"
#include "../blocktagmap.h"
#include "../types.h"

/// @brief Classifier for self-modifying code patterns.
/// Tags BlockType::SMC regions with SMCProcedure tag.
class SMCClassifier : public ITagClassifier
{
public:
    std::string name() const override { return "smc"; }

    void classify([[maybe_unused]] const AnalysisContext& ctx, BlockTagMap& tagMap) override
    {
        // Find all SMC regions (BlockType::SMC = executed AND written)
        // and tag them as SMCProcedure
        for (uint32_t addr = 0; addr < 65536; addr++)
        {
            uint16_t a = static_cast<uint16_t>(addr);
            if (tagMap.getType(a) == BlockType::SMC)
            {
                tagMap.addTag(a, MemoryTag::SMCProcedure);
            }
        }
    }
};
