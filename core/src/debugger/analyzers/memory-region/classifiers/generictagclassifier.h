#pragma once

#include "../itagclassifier.h"
#include "../blocktagmap.h"
#include "../analysiscontext.h"
#include "../types.h"

/// @brief Fallback classifier that assigns GenericCode/GenericData to untagged regions.
/// Also assigns hardcoded VRAM and system variable tags.
class GenericTagClassifier : public ITagClassifier
{
public:
    std::string name() const override { return "generic"; }

    void classify([[maybe_unused]] const AnalysisContext& ctx, BlockTagMap& tagMap) override
    {
        // Hardcoded regions first
        // Screen bitmap: 0x4000-0x57FF
        for (uint16_t addr = 0x4000; addr <= 0x57FF; addr++)
        {
            tagMap.addTag(addr, MemoryTag::ScreenBitmap);
        }

        // Screen attributes: 0x5800-0x5AFF
        for (uint16_t addr = 0x5800; addr <= 0x5AFF; addr++)
        {
            tagMap.addTag(addr, MemoryTag::ScreenAttributes);
        }

        // System variables: 0x5B00-0x5CBF
        for (uint16_t addr = 0x5B00; addr <= 0x5CBF; addr++)
        {
            tagMap.addTag(addr, MemoryTag::SystemVariables);
        }

        // Then generic fallback tags
        for (uint32_t addr = 0; addr < 65536; addr++)
        {
            uint16_t a = static_cast<uint16_t>(addr);
            BlockType type = tagMap.getType(a);
            MemoryTags tags = tagMap.getTags(a);

            // Only tag if no semantic tags assigned yet
            if (tags == 0)
            {
                switch (type)
                {
                    case BlockType::CODE:
                    case BlockType::SMC:
                        tagMap.addTag(a, MemoryTag::GenericCode);
                        break;
                    case BlockType::DATA:
                        tagMap.addTag(a, MemoryTag::GenericData);
                        break;
                    case BlockType::VARIABLE:
                        tagMap.addTag(a, MemoryTag::Variables);
                        break;
                    default:
                        break;
                }
            }
        }
    }
};
