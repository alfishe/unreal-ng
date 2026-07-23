#pragma once

#include "../itagclassifier.h"
#include "../analysiscontext.h"
#include "../blocktagmap.h"
#include "../types.h"

/// @brief Classifier for interrupt service routines.
/// Tags code at known ISR entry points based on IM mode.
class InterruptClassifier : public ITagClassifier
{
public:
    std::string name() const override { return "interrupt"; }

    void classify([[maybe_unused]] const AnalysisContext& ctx, BlockTagMap& tagMap) override
    {
        // IM 1: Fixed ISR at 0x0038
        constexpr uint16_t IM1_VECTOR = 0x0038;

        // IM 2: Vector table at (I * 256 + data_bus). Common setups use 0xFDFD or similar.
        // We tag the IM1 vector unconditionally since it's always valid.

        BlockType type = tagMap.getType(IM1_VECTOR);
        if (type == BlockType::CODE || type == BlockType::SMC)
        {
            tagMap.addTag(IM1_VECTOR, MemoryTag::ISRCode);
        }

        // For IM2, we'd need to know the I register value and trace the vector table.
        // This would require runtime state tracking which is beyond static analysis.
        // Future: could check common IM2 patterns like 257-byte tables.
    }
};
