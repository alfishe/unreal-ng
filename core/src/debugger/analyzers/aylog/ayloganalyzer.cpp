#include "ayloganalyzer.h"

#include "debugger/analyzers/analyzermanager.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/chips/soundchip_turbosound.h"
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
    _ring.resize(_capacity);
}

AYLogAnalyzer::~AYLogAnalyzer()
{
    // Taps are removed by AnalyzerManager on deactivation
}

void AYLogAnalyzer::onActivate(AnalyzerManager* manager)
{
    _manager = manager;

    // Install the tap on the TurboSound wrapper — it is the registered port
    // device that receives every AY OUT (#FFFD / #BFFD) and knows which of
    // its two chips is active at the moment of the write
    bool installed = false;
    if (_context && _context->pSoundManager)
    {
        SoundChip_TurboSound* turboSound = _context->pSoundManager->getTurboSound();
        if (turboSound)
        {
            turboSound->setLogSink(&AYLogAnalyzer::onAYWrite, this);
            installed = true;
        }
    }

    // No TurboSound (e.g. model without AY) — stay inactive so callers can
    // distinguish "started" from "actually recording" via getEntryCount()
    _active.store(installed, std::memory_order_release);
}

void AYLogAnalyzer::onDeactivate()
{
    // Remove the tap regardless of what activation found
    if (_context && _context->pSoundManager)
    {
        SoundChip_TurboSound* turboSound = _context->pSoundManager->getTurboSound();
        if (turboSound)
        {
            turboSound->setLogSink(nullptr, nullptr);
        }
    }

    _active.store(false, std::memory_order_release);
    _manager = nullptr;
}

void AYLogAnalyzer::clear()
{
    std::lock_guard<std::mutex> lock(_mutex);
    _writeIndex = 0;
    _count = 0;
    _dropped = 0;
}

void AYLogAnalyzer::setCapacity(size_t capacity)
{
    capacity = std::clamp(capacity, MIN_CAPACITY, MAX_CAPACITY);

    std::lock_guard<std::mutex> lock(_mutex);
    _capacity = capacity;
    _ring.assign(_capacity, AYLogRecord{});
    _writeIndex = 0;
    _count = 0;
    _dropped = 0;
}

size_t AYLogAnalyzer::getEntryCount() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _count;
}

std::vector<AYLogRecord> AYLogAnalyzer::getEntries(size_t offset, size_t limit) const
{
    std::lock_guard<std::mutex> lock(_mutex);

    if (offset >= _count)
        return {};

    const size_t available = _count - offset;
    const size_t take = limit == 0 ? available : std::min(limit, available);

    std::vector<AYLogRecord> result;
    result.reserve(take);

    // Oldest valid entry position in the ring
    const size_t start = (_writeIndex + _capacity - _count) % _capacity;
    for (size_t i = 0; i < take; i++)
    {
        result.push_back(_ring[(start + offset + i) % _capacity]);
    }

    return result;
}

void AYLogAnalyzer::onAYWrite(void* context, const AYLogRecord& record)
{
    AYLogAnalyzer* self = static_cast<AYLogAnalyzer*>(context);
    if (!self->_active.load(std::memory_order_relaxed))
        return;

    // TTD silent-replay suppression (same observational rule as AnalyzerManager)
    if (self->_context && self->_context->ttdReplayActive)
        return;

    std::lock_guard<std::mutex> lock(self->_mutex);
    if (self->_count < self->_capacity)
    {
        self->_count++;
    }
    else
    {
        // Ring full — overwrite the oldest entry and count it as dropped
        self->_dropped++;
    }
    self->_ring[self->_writeIndex] = record;
    self->_writeIndex = (self->_writeIndex + 1) % self->_capacity;
}
