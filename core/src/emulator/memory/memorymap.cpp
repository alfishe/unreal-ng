// Sparse memory map + compact read formats - the single source every
// automation surface renders (TD-3 Phase 1, LLM-optimized memory inspection:
// docs/inprogress/2026-09-14-automation-triage-gaps/ttd-coverage-evaluation.md).

#include <emulator/memory/memorymap.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>

#include <emulator/config.h>
#include <emulator/memory/memory.h>
#include <emulator/platform.h>

namespace
{

/// FNV-1a 64 folding - cheap stable fingerprint so an agent can diff a region
/// across time (seek, reverse-continue) without dumping its bytes.
constexpr uint64_t kHashInit = 0xcbf29ce484222325ULL;

inline uint64_t FoldHashByte(uint64_t hash, uint8_t value)
{
    return (hash ^ value) * 0x100000001B3ULL;
}

struct ScanRegion
{
    uint32_t base = 0;          // Address-space base of the region
    uint8_t bank = 0xFF;        // CPU bank 0-3, or 0xFF when N/A (ram view)
    uint16_t page = 0;          // RAM page (ROM page when isRom)
    bool isRom = false;
    const uint8_t* host = nullptr;
    uint32_t size = 0;
};

std::vector<ScanRegion> CollectRegions(Memory& memory, const CONFIG& config, bool ramView)
{
    std::vector<ScanRegion> regions;

    if (!ramView)
    {
        for (uint8_t bank = 0; bank < 4; bank++)
        {
            bool isRom = false;
            uint16_t page = 0;
            const uint8_t* host = nullptr;

            switch (bank)
            {
                case 0:
                    if (memory.IsBank0ROM())
                    {
                        isRom = true;
                        page = memory.GetROMPage();
                        host = memory.ROMPageHostAddress(static_cast<uint8_t>(page));
                    }
                    else
                    {
                        page = memory.GetRAMPageForBank0();
                        host = memory.RAMPageAddress(page);
                    }
                    break;
                case 1:
                    page = memory.GetRAMPageForBank1();
                    host = memory.RAMPageAddress(page);
                    break;
                case 2:
                    page = memory.GetRAMPageForBank2();
                    host = memory.RAMPageAddress(page);
                    break;
                default:
                    page = memory.GetRAMPageForBank3();
                    host = memory.RAMPageAddress(page);
                    break;
            }

            if (!host)
                continue;

            regions.push_back({static_cast<uint32_t>(bank) * PAGE_SIZE, bank, page, isRom, host, PAGE_SIZE});
        }
    }
    else
    {
        const uint32_t pageCount = std::min<uint32_t>(config.ramsize / 16, MAX_RAM_PAGES);
        for (uint32_t page = 0; page < pageCount; page++)
        {
            const uint8_t* host = memory.RAMPageAddress(static_cast<uint16_t>(page));
            if (!host)
                continue;

            regions.push_back({page * PAGE_SIZE, 0xFF, static_cast<uint16_t>(page), false, host, PAGE_SIZE});
        }
    }

    return regions;
}

std::string RegionTypeName(const ScanRegion& region)
{
    char buffer[24];
    std::snprintf(buffer, sizeof(buffer), "%s%u", region.isRom ? "rom" : "ram", region.page);
    return buffer;
}

/// Merges an emitted zero-fill block into the previous one when they are
/// address-adjacent (ram view: consecutive all-zero pages collapse into a
/// single "ramX-ramY" span so huge RAMs stay readable).
void MergeAdjacentZeroFill(MemoryMapReport& report, MemoryMapBlock& block)
{
    if (!report.blocks.empty())
    {
        MemoryMapBlock& previous = report.blocks.back();
        if (previous.IsZeroFill() && block.IsZeroFill() &&
            previous.address + previous.size == block.address)
        {
            const uint32_t lastPage = (block.address + block.size - 1) / PAGE_SIZE;
            previous.size += block.size;
            if (previous.size > PAGE_SIZE)
                previous.typeName = "ram" + std::to_string(previous.page) + "-ram" + std::to_string(lastPage);
            return;
        }
    }
    report.blocks.push_back(std::move(block));
}

} // namespace

MemoryMapReport BuildMemoryMap(Memory& memory, const CONFIG& config, MemoryMapView view,
                               uint32_t minRun, uint32_t maxBlocks)
{
    if (minRun < 1)
        minRun = kMemoryMapDefaultMinRun;
    if (maxBlocks < 1)
        maxBlocks = 1;

    const bool ramView = (view == MemoryMapView::RamPages);
    const std::vector<ScanRegion> regions = CollectRegions(memory, config, ramView);

    MemoryMapReport report;
    report.model = Config::GetModelFullName(config.mem_model);
    report.ramView = ramView;
    for (const ScanRegion& region : regions)
        report.totalSize += region.size;

    for (;;)
    {
        report.blocks.clear();
        report.nonZeroBytes = 0;
        bool overflow = false;

        for (const ScanRegion& region : regions)
        {
            if (overflow)
                break;

            // Open data region: spans data bytes plus zero runs shorter than
            // minRun folded back into it (including folded leading zeros so
            // consecutive blocks tile the region without gaps).
            uint32_t dataStart = 0;
            bool dataOpen = false;
            uint32_t dataNonZero = 0;
            uint32_t foldedLead = UINT32_MAX;
            size_t zeroRunStart = SIZE_MAX;

            auto flushData = [&]()
            {
                if (!dataOpen)
                    return;
                dataOpen = false;

                const uint32_t end = zeroRunStart == SIZE_MAX ? region.size : static_cast<uint32_t>(zeroRunStart);
                MemoryMapBlock block;
                block.address = region.base + dataStart;
                block.size = end - dataStart;
                block.bank = region.bank;
                block.page = region.page;
                block.isRom = region.isRom;
                block.nonZero = dataNonZero;
                block.typeName = RegionTypeName(region);
                block.hash = kHashInit;
                for (uint32_t j = dataStart; j < end; j++)
                    block.hash = FoldHashByte(block.hash, region.host[j]);
                report.nonZeroBytes += dataNonZero;

                if (report.blocks.size() >= maxBlocks)
                {
                    overflow = true;
                    return;
                }
                report.blocks.push_back(std::move(block));
            };

            auto emitZero = [&](uint32_t start, uint32_t length)
            {
                if (report.blocks.size() >= maxBlocks)
                {
                    overflow = true;
                    return;
                }

                MemoryMapBlock zeroBlock;
                zeroBlock.address = region.base + start;
                zeroBlock.size = length;
                zeroBlock.bank = region.bank;
                zeroBlock.page = region.page;
                zeroBlock.isRom = region.isRom;
                zeroBlock.typeName = RegionTypeName(region);

                if (ramView)
                    MergeAdjacentZeroFill(report, zeroBlock);
                else
                    report.blocks.push_back(std::move(zeroBlock));
            };

            for (uint32_t i = 0; i < region.size && !overflow; i++)
            {
                const uint8_t value = region.host[i];

                if (value != 0)
                {
                    if (zeroRunStart != SIZE_MAX)
                    {
                        const uint32_t zeroRunLength = i - static_cast<uint32_t>(zeroRunStart);
                        if (zeroRunLength >= minRun)
                        {
                            flushData();
                            if (!overflow)
                                emitZero(static_cast<uint32_t>(zeroRunStart), zeroRunLength);
                        }
                        else if (!dataOpen)
                        {
                            // Short leading zeros fold into the upcoming data block
                            foldedLead = static_cast<uint32_t>(zeroRunStart);
                        }
                        zeroRunStart = SIZE_MAX;
                    }
                    if (!dataOpen)
                    {
                        dataOpen = true;
                        dataStart = foldedLead != UINT32_MAX ? foldedLead : i;
                        foldedLead = UINT32_MAX;
                        dataNonZero = 0;
                    }
                    dataNonZero++;
                }
                else if (zeroRunStart == SIZE_MAX)
                {
                    zeroRunStart = i;
                }
            }

            if (!overflow)
            {
                flushData();

                // A region may end on a qualifying zero run - emit its tail block
                if (!overflow && zeroRunStart != SIZE_MAX)
                {
                    const uint32_t tailLength = region.size - static_cast<uint32_t>(zeroRunStart);
                    if (tailLength >= minRun)
                        emitZero(static_cast<uint32_t>(zeroRunStart), tailLength);
                }
            }
        }

        if (!overflow || minRun >= PAGE_SIZE)
        {
            report.truncated = overflow;
            break;
        }

        // Budget exhausted: retry with coarser zero-run granularity.
        minRun = std::min<uint32_t>(minRun * 4, PAGE_SIZE);
    }

    report.minRun = minRun;
    return report;
}

std::string FormatHexDump(const uint8_t* data, size_t size, uint32_t baseAddress)
{
    static const char* hexDigits = "0123456789ABCDEF";

    std::string out;
    out.reserve(size / 16 * 78 + 80);

    for (size_t line = 0; line < size; line += 16)
    {
        const size_t rowLength = std::min<size_t>(16, size - line);

        char address[16];
        std::snprintf(address, sizeof(address), "0x%04X: ", baseAddress + static_cast<uint32_t>(line));
        out += address;

        for (size_t i = 0; i < 16; i++)
        {
            if (i < rowLength)
            {
                const uint8_t value = data[line + i];
                out += hexDigits[value >> 4];
                out += hexDigits[value & 0x0F];
            }
            else
            {
                out += "  ";
            }
            if (i != 15)
                out += ' ';
            if (i == 7)
                out += ' ';
        }

        out += "  |";
        for (size_t i = 0; i < rowLength; i++)
        {
            const uint8_t value = data[line + i];
            out += (value >= 0x20 && value <= 0x7E) ? static_cast<char>(value) : '.';
        }
        out += "|\n";
    }

    return out;
}

std::vector<MemorySparseSegment> BuildSparseSegments(const uint8_t* data, size_t size, uint32_t minRun)
{
    if (minRun < 2)
        minRun = 2;

    static const char* hexDigits = "0123456789abcdef";

    std::vector<MemorySparseSegment> segments;
    size_t i = 0;

    while (i < size)
    {
        const uint8_t value = data[i];
        size_t run = 1;
        if (value == 0x00 || value == 0xFF)
        {
            while (i + run < size && data[i + run] == value)
                run++;
        }

        if ((value == 0x00 || value == 0xFF) && run >= minRun)
        {
            MemorySparseSegment segment;
            segment.offset = static_cast<uint32_t>(i);
            segment.length = static_cast<uint32_t>(run);
            segment.isFill = true;
            segment.fill = value;
            segments.push_back(std::move(segment));
            i += run;
            continue;
        }

        // Data run: short fill runs stay folded into the hex payload
        const size_t start = i;
        i += run;
        while (i < size)
        {
            const uint8_t candidate = data[i];
            if (candidate == 0x00 || candidate == 0xFF)
            {
                size_t candidateRun = 1;
                while (i + candidateRun < size && data[i + candidateRun] == candidate)
                    candidateRun++;
                if (candidateRun >= minRun)
                    break;
                i += candidateRun;
            }
            else
            {
                i++;
            }
        }

        MemorySparseSegment segment;
        segment.offset = static_cast<uint32_t>(start);
        segment.length = static_cast<uint32_t>(i - start);
        segment.hex.reserve(segment.length * 2);
        for (size_t j = start; j < i; j++)
        {
            segment.hex += hexDigits[data[j] >> 4];
            segment.hex += hexDigits[data[j] & 0x0F];
        }
        segments.push_back(std::move(segment));
    }

    return segments;
}

uint32_t CountNonZeroBytes(const uint8_t* data, size_t size)
{
    uint32_t count = 0;
    for (size_t i = 0; i < size; i++)
    {
        if (data[i] != 0)
            count++;
    }
    return count;
}
