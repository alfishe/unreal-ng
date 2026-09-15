#include "coverageanalyzer.h"

#include "debugger/analyzers/analyzermanager.h"

#include <cstring>
#include <sstream>
#include <iomanip>

// Generate a stable ID for this analyzer instance (same pattern as ROMPrintDetector)
static std::string generateCoverageUUID()
{
    static int counter = 0;
    std::stringstream ss;
    ss << "coverageanalyzer-" << std::hex << std::setw(8) << std::setfill('0') << counter++;
    return ss.str();
}

CoverageAnalyzer::CoverageAnalyzer()
    : _uuid(generateCoverageUUID())
{
    clear();
}

CoverageAnalyzer::~CoverageAnalyzer()
{
    // Subscriptions are cleaned up by AnalyzerManager on deactivation
}

void CoverageAnalyzer::onActivate(AnalyzerManager* manager)
{
    _manager = manager;

    // Subscribe to the CPU step hot path; ownership is tracked via the
    // registration ID and the subscription is released on deactivation
    manager->subscribeCPUStep(&CoverageAnalyzer::onCPUStep, this, _registrationId);

    _recording.store(true, std::memory_order_release);
}

void CoverageAnalyzer::onDeactivate()
{
    _recording.store(false, std::memory_order_release);

    // AnalyzerManager automatically cleans up subscriptions
    _manager = nullptr;
}

void CoverageAnalyzer::clear()
{
    std::memset(_coverageMap, 0, sizeof(_coverageMap));
    _instructions.store(0, std::memory_order_relaxed);
}

bool CoverageAnalyzer::isExecuted(uint16_t address) const
{
    return (_coverageMap[address >> 3] & (0x80u >> (address & 7))) != 0;
}

size_t CoverageAnalyzer::getExecutedCount() const
{
    return getExecutedCountInRange(0, 65535);
}

size_t CoverageAnalyzer::getExecutedCountInRange(uint16_t start, uint16_t end) const
{
    size_t count = 0;
    for (uint32_t address = start; address <= end; address++)
    {
        if (isExecuted(static_cast<uint16_t>(address)))
        {
            count++;
        }
    }
    return count;
}

std::vector<std::pair<uint16_t, uint16_t>> CoverageAnalyzer::getExecutedRanges(size_t maxRanges) const
{
    std::vector<std::pair<uint16_t, uint16_t>> ranges;

    int32_t rangeStart = -1;
    for (uint32_t address = 0; address <= 65535; address++)
    {
        if (isExecuted(static_cast<uint16_t>(address)))
        {
            if (rangeStart < 0)
            {
                rangeStart = static_cast<int32_t>(address);
            }
        }
        else if (rangeStart >= 0)
        {
            ranges.emplace_back(static_cast<uint16_t>(rangeStart), static_cast<uint16_t>(address - 1));
            rangeStart = -1;

            if (maxRanges != 0 && ranges.size() >= maxRanges)
            {
                return ranges;
            }
        }
    }

    if (rangeStart >= 0)
    {
        ranges.emplace_back(static_cast<uint16_t>(rangeStart), 65535);
    }

    return ranges;
}

std::vector<std::pair<uint16_t, uint16_t>> CoverageAnalyzer::getGaps(uint16_t start, uint16_t end, size_t maxGaps) const
{
    std::vector<std::pair<uint16_t, uint16_t>> gaps;

    // Complement of the executed ranges within [start, end]
    int32_t gapStart = static_cast<int32_t>(start);
    for (uint32_t address = start; address <= end; address++)
    {
        if (isExecuted(static_cast<uint16_t>(address)))
        {
            if (static_cast<int32_t>(address) > gapStart)
            {
                gaps.emplace_back(static_cast<uint16_t>(gapStart), static_cast<uint16_t>(address - 1));

                if (maxGaps != 0 && gaps.size() >= maxGaps)
                {
                    return gaps;
                }
            }
            gapStart = static_cast<int32_t>(address) + 1;
        }
    }

    if (gapStart <= end)
    {
        gaps.emplace_back(static_cast<uint16_t>(gapStart), end);
    }

    return gaps;
}

void CoverageAnalyzer::onCPUStep(void* context, Z80* cpu, uint16_t pc)
{
    (void)cpu;

    CoverageAnalyzer* self = static_cast<CoverageAnalyzer*>(context);
    if (self->_recording.load(std::memory_order_relaxed))
    {
        // Monotonic bit set — see the thread-safety note in the header
        self->_coverageMap[pc >> 3] |= static_cast<uint8_t>(0x80u >> (pc & 7));
        self->_instructions.fetch_add(1, std::memory_order_relaxed);
    }
}
