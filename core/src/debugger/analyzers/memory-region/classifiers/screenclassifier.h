#pragma once

#include "../itagclassifier.h"
#include "../analysiscontext.h"
#include "../blocktagmap.h"
#include "../types.h"
#include "emulator/io/porttracker.h"

/// @brief Classifier for screen-related memory regions.
/// Tags shadow screen VRAM when it's been used (port 0x7FFD bit 3).
class ScreenClassifier : public ITagClassifier
{
public:
    std::string name() const override { return "screen"; }

    void classify(const AnalysisContext& ctx, BlockTagMap& tagMap) override
    {
        // ScreenClassifier tags DATA regions in VRAM address ranges.
        // We don't have caller tracking in MemoryAccessTracker, so we can't
        // identify which CODE wrote to VRAM. Instead, we tag the VRAM regions themselves.

        // Main screen VRAM: 0x4000-0x5AFF (bitmap + attributes)
        // Already tagged by GenericTagClassifier as ScreenBitmap/ScreenAttributes

        // Shadow screen VRAM (128K): 0xC000-0xDAFF when page 7 mapped
        constexpr uint16_t SHADOW_VRAM_START = 0xC000;
        constexpr uint16_t SHADOW_VRAM_END = 0xDAFF;
        constexpr uint16_t PAGE_SELECT_PORT = 0x7FFD;

        // Tag main VRAM as screen-related (already done by GenericTagClassifier
        // with ScreenBitmap/ScreenAttributes, but we could add GraphicsCode
        // to CODE regions that write here if we had caller tracking)

        // Shadow screen tagging requires port tracking to know if it was used
        if (ctx.portTracker)
        {
            auto pageSelectValues = ctx.portTracker->GetWriteValues(PAGE_SELECT_PORT);
            bool shadowScreenUsed = false;
            for (const auto& [value, count] : pageSelectValues)
            {
                if (value & 0x08)  // Bit 3 = shadow screen select
                {
                    shadowScreenUsed = true;
                    break;
                }
            }

            if (shadowScreenUsed)
            {
                // Tag shadow VRAM as screen buffer
                for (uint32_t addr = SHADOW_VRAM_START; addr <= SHADOW_VRAM_END; addr++)
                {
                    tagMap.addTag(static_cast<uint16_t>(addr), MemoryTag::ScreenBuffer);
                }
            }
        }
    }
};
