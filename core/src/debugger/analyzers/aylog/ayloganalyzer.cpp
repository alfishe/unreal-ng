#include "ayloganalyzer.h"

#include "debugger/analyzers/analyzermanager.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/chips/ayhistory.h"
#include "emulator/sound/soundmanager.h"

#include <algorithm>
#include <sstream>
#include <iomanip>

// Generate a stable ID for this analyzer instance (same pattern as CoverageAnalyzer)
static std::string generateAYLogUUID()
{
    static int counter = 0;
    std::stringstream ss;
    ss << "ayloganalyzer-" << std::hex << std::setw(8) << std::setfill('0') << counter++;
    return ss.str();
}

AYLogAnalyzer::AYLogAnalyzer(EmulatorContext* context)
    : _context(context), _uuid(generateAYLogUUID())
{
}

AYHistory* AYLogAnalyzer::History() const
{
    return _context && _context->pSoundManager ? _context->pSoundManager->ayHistory() : nullptr;
}

void AYLogAnalyzer::onActivate(AnalyzerManager* manager)
{
    _manager = manager;
    // No AY (e.g. a model without one): nothing records, so callers can tell "started" from "actually recording"
    // via isRecording()
    if (AYHistory* ay = History())
    {
        std::string error;
        ay->Configure({ .mode = history::Mode::Ring, .limit = _capacity }, error);
    }
}

void AYLogAnalyzer::onDeactivate()
{
    if (AYHistory* ay = History())
    {
        std::string error;
        ay->Configure({ .mode = history::Mode::Off }, error);
    }
    _manager = nullptr;
}

void AYLogAnalyzer::clear()
{
    if (AYHistory* ay = History())
        ay->Clear();
}

void AYLogAnalyzer::setCapacity(size_t capacity)
{
    _capacity = std::clamp(capacity, MIN_CAPACITY, MAX_CAPACITY);
    AYHistory* ay = History();
    if (ay && ay->On())
    {
        std::string error;
        ay->Configure({ .mode = history::Mode::Ring, .limit = _capacity }, error);
    }
}

bool AYLogAnalyzer::isRecording() const
{
    const AYHistory* ay = History();
    return ay && ay->On();
}

size_t AYLogAnalyzer::getEntryCount() const
{
    const AYHistory* ay = History();
    return ay ? static_cast<size_t>(ay->GetStats().kept) : 0;
}

size_t AYLogAnalyzer::getDroppedCount() const
{
    const AYHistory* ay = History();
    return ay ? static_cast<size_t>(ay->GetStats().dropped) : 0;
}

std::vector<AYLogRecord> AYLogAnalyzer::getEntries(size_t offset, size_t limit) const
{
    const AYHistory* ay = History();
    if (!ay)
        return {};
    std::vector<AYLogRecord> all = ay->Snapshot();
    if (offset >= all.size())
        return {};
    const size_t take = limit == 0 ? all.size() - offset : std::min(limit, all.size() - offset);
    return { all.begin() + static_cast<std::ptrdiff_t>(offset), all.begin() + static_cast<std::ptrdiff_t>(offset + take) };
}
