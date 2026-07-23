#pragma once

#include "../itagclassifier.h"
#include "../analysiscontext.h"
#include "../blocktagmap.h"
#include "../types.h"
#include "emulator/io/porttracker.h"

/// @brief Classifier for disk and tape loader code.
/// Identifies code that accesses FDC ports (disk) or tape edge detection (port 0xFE reads).
class LoaderClassifier : public ITagClassifier
{
public:
    std::string name() const override { return "loader"; }

    void classify(const AnalysisContext& ctx, BlockTagMap& tagMap) override
    {
        if (!ctx.portTracker)
            return;

        // FDC (WD1793) ports - TR-DOS uses these
        constexpr uint16_t FDC_COMMAND = 0x1F;   // Command/status
        constexpr uint16_t FDC_TRACK = 0x3F;    // Track register
        constexpr uint16_t FDC_SECTOR = 0x5F;   // Sector register
        constexpr uint16_t FDC_DATA = 0x7F;     // Data register
        constexpr uint16_t TAPE_PORT = 0xFE;    // Tape edge detection (bit 6 read)

        // Check FDC ports
        uint16_t fdcPorts[] = {FDC_COMMAND, FDC_TRACK, FDC_SECTOR, FDC_DATA};
        for (auto port : fdcPorts)
        {
            // Both reads and writes indicate disk access
            const auto& readCallers = ctx.portTracker->GetReadCallers(port);
            for (const auto& [callerPC, count] : readCallers)
            {
                (void)count;
                BlockType type = tagMap.getType(callerPC);
                if (type == BlockType::CODE || type == BlockType::SMC)
                {
                    tagMap.addTag(callerPC, MemoryTag::DiskLoaderCode);
                }
            }

            const auto& writeCallers = ctx.portTracker->GetWriteCallers(port);
            for (const auto& [callerPC, count] : writeCallers)
            {
                (void)count;
                BlockType type = tagMap.getType(callerPC);
                if (type == BlockType::CODE || type == BlockType::SMC)
                {
                    tagMap.addTag(callerPC, MemoryTag::DiskLoaderCode);
                }
            }
        }

        // Check tape port reads - high frequency reads indicate tape loading
        auto tapeReadCallers = ctx.portTracker->GetReadCallers(TAPE_PORT);
        uint32_t totalTapeReads = 0;
        for (const auto& [pc, count] : tapeReadCallers)
        {
            totalTapeReads += count;
        }

        if (totalTapeReads > 100)  // Threshold for "frequent" reads
        {
            for (const auto& [callerPC, count] : tapeReadCallers)
            {
                BlockType type = tagMap.getType(callerPC);
                if (type == BlockType::CODE || type == BlockType::SMC)
                {
                    tagMap.addTag(callerPC, MemoryTag::TapeLoaderCode);
                }
            }
        }
    }
};
