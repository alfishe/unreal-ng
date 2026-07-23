#include "blocksegmenter.h"

#include "emulator/memory/memoryaccesstracker.h"

BlockType BlockSegmenter::ClassifyAddress(uint16_t address, uint32_t readCount, uint32_t writeCount, uint32_t execCount)
{
    bool hasRead = readCount > 0;
    bool hasWrite = writeCount > 0;
    bool hasExec = execCount > 0;

    // Code that was both written and executed
    if (hasExec && hasWrite)
    {
        // In RAM (>= 0x4000) this is typically code copied from ROM (bridge routines).
        // True SMC is rare and would require temporal tracking to detect properly.
        // For now, classify RAM written+executed as ROM_CODE_IN_RAM.
        // ROM (< 0x4000) can't be written, so this shouldn't happen there.
        if (address >= 0x4000)
        {
            return BlockType::ROM_CODE_IN_RAM;
        }
        // ROM address with write+exec - shouldn't happen in normal execution
        return BlockType::CODE;
    }

    if (hasExec)
    {
        return BlockType::CODE;
    }

    if (hasRead && hasWrite)
    {
        return BlockType::VARIABLE;
    }

    if (hasRead || hasWrite)
    {
        return BlockType::DATA;
    }

    return BlockType::UNKNOWN;
}

void BlockSegmenter::Refresh(const MemoryAccessTracker* tracker)
{
    if (!tracker)
    {
        _types.fill(BlockType::UNKNOWN);
        return;
    }

    const uint32_t* reads = tracker->GetZ80ReadCountersPtr();
    const uint32_t* writes = tracker->GetZ80WriteCountersPtr();
    const uint32_t* execs = tracker->GetZ80ExecuteCountersPtr();

    if (!reads || !writes || !execs)
    {
        _types.fill(BlockType::UNKNOWN);
        return;
    }

    for (uint32_t addr = 0; addr < 65536; addr++)
    {
        _types[addr] = ClassifyAddress(static_cast<uint16_t>(addr), reads[addr], writes[addr], execs[addr]);
    }
}
