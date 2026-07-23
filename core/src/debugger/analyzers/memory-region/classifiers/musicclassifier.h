#pragma once

#include "../itagclassifier.h"
#include "../analysiscontext.h"
#include "../blocktagmap.h"
#include "../types.h"
#include "emulator/io/porttracker.h"

/// @brief Classifier for music player code.
/// Identifies code that accesses AY sound chip registers or beeper port.
class MusicClassifier : public ITagClassifier
{
public:
    std::string name() const override { return "music"; }

    void classify(const AnalysisContext& ctx, BlockTagMap& tagMap) override
    {
        if (!ctx.portTracker)
            return;

        // AY-3-8912 register select: port 0xFFFD (address latch)
        // AY-3-8912 data write: port 0xBFFD
        // Beeper: port 0xFE bit 4
        constexpr uint16_t AY_REG_SELECT = 0xFFFD;
        constexpr uint16_t AY_DATA_WRITE = 0xBFFD;
        constexpr uint16_t BEEPER_PORT = 0xFE;

        // Check AY register select port
        const auto& aySelectCallers = ctx.portTracker->GetWriteCallers(AY_REG_SELECT);
        for (const auto& [callerPC, count] : aySelectCallers)
        {
            (void)count;
            BlockType type = tagMap.getType(callerPC);
            if (type == BlockType::CODE || type == BlockType::SMC)
            {
                tagMap.addTag(callerPC, MemoryTag::MusicPlayerCode);
            }
        }

        // Check AY data write port
        const auto& ayDataCallers = ctx.portTracker->GetWriteCallers(AY_DATA_WRITE);
        for (const auto& [callerPC, count] : ayDataCallers)
        {
            (void)count;
            BlockType type = tagMap.getType(callerPC);
            if (type == BlockType::CODE || type == BlockType::SMC)
            {
                tagMap.addTag(callerPC, MemoryTag::MusicPlayerCode);
            }
        }

        // Check beeper port (writes with bit 4 toggling)
        const auto& beeperCallers = ctx.portTracker->GetWriteCallers(BEEPER_PORT);
        for (const auto& [callerPC, count] : beeperCallers)
        {
            (void)count;
            BlockType type = tagMap.getType(callerPC);
            if (type == BlockType::CODE || type == BlockType::SMC)
            {
                tagMap.addTag(callerPC, MemoryTag::MusicPlayerCode);
            }
        }
    }
};
