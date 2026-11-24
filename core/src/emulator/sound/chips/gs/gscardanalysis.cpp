#include "emulator/sound/chips/gs/gscardanalysis.h"

#include <algorithm>

const char* GSCardAnalysis::ChannelName(Channel channel)
{
    switch (channel)
    {
        case Fetch: return "fetch";
        case DataRead: return "read";
        case DataWrite: return "write";
        case DacFetch: return "dac";
        case SdDma: return "sd_dma";
        case ZxDma: return "zx_dma";
        case ChannelCount: break;
    }
    return "unknown";
}

bool GSCardAnalysis::ParseChannel(const std::string& name, Channel& out)
{
    for (uint8_t c = 0; c < ChannelCount; c++)
    {
        if (name == ChannelName(static_cast<Channel>(c)))
        {
            out = static_cast<Channel>(c);
            return true;
        }
    }
    return false;
}

GSCardAnalysis::GSCardAnalysis(size_t romBytes, size_t ramBytes, const uint64_t* frameSource)
    : _romBytes(romBytes), _ramBytes(ramBytes), _frameSource(frameSource)
{
    _romFlags.resize(romBytes);
    _ramFlags.resize(ramBytes);
    while ((ramBytes >> _cellShift) > 65536)
        _cellShift++;
    _cells = static_cast<uint32_t>((ramBytes + (size_t{1} << _cellShift) - 1) >> _cellShift);
    _heat.resize(static_cast<size_t>(_cells) * ChannelCount);
    _heatFrame.resize(static_cast<size_t>(_cells) * ChannelCount);
    _uninitSeen.resize((ramBytes + 7) / 8);
    _createdFrame = Frame();
    _lastResetFrame = _createdFrame;
}

uint8_t GSCardAnalysis::ReachedBy(uint8_t op0, uint8_t op1)
{
    // CALL nn, CALL cc,nn, RST n: a subroutine entry
    if (op0 == 0xCD || (op0 & 0xC7) == 0xC4 || (op0 & 0xC7) == 0xC7)
        return Sub;
    // RET, RET cc, RETI / RETN: back to a caller, not a new target
    if (op0 == 0xC9 || (op0 & 0xC7) == 0xC0 || (op0 == 0xED && (op1 & 0xC7) == 0x45))
        return 0;
    // A jump, or an interrupt / NMI entry after any instruction
    return Jump;
}

void GSCardAnalysis::BusMaster(Channel channel, uint32_t offset, uint32_t length, bool write)
{
    if (_ramBytes == 0)
        return;
    for (uint32_t i = 0; i < length; i++)
    {
        const uint32_t at = static_cast<uint32_t>((static_cast<size_t>(offset) + i) % _ramBytes);
        if (write)
            _ramFlags[at] |= static_cast<uint8_t>(Write | Written);
        Heat(channel, at);
    }
}

void GSCardAnalysis::CardReset()
{
    uint8_t* flags = _ramFlags.data();
    for (size_t i = 0; i < _ramBytes; i++)
        flags[i] &= static_cast<uint8_t>(~Written);
    _uninitSeen.zero();
    _havePrevious = false;
    std::lock_guard<std::mutex> lock(_uninitMutex);
    _uninitRows.clear();
    _uninitDropped = 0;
    _cardResets++;
    _lastResetFrame = Frame();
}

void GSCardAnalysis::Clear()
{
    _romFlags.zero();
    _ramFlags.zero();
    _heat.zero();
    _heatFrame.zero();
    _uninitSeen.zero();
    std::fill(std::begin(_accesses), std::end(_accesses), 0);
    std::lock_guard<std::mutex> lock(_uninitMutex);
    _uninitRows.clear();
    _uninitDropped = 0;
    _lastResetFrame = Frame();
}

size_t GSCardAnalysis::MemoryBytes() const
{
    return _romFlags.size() + _ramFlags.size() + _heat.size() + _heatFrame.size() * sizeof(uint32_t) +
           _uninitSeen.size() + kMaxUninitRows * sizeof(UninitRead);
}

uint8_t GSCardAnalysis::HeatAt(Channel channel, uint32_t cell, uint64_t now) const
{
    if (cell >= _cells)
        return 0;
    const size_t index = static_cast<size_t>(channel) * _cells + cell;
    const uint32_t last = _heatFrame[index];
    if (last == 0)
        return 0;
    const uint32_t stamp = static_cast<uint32_t>(now) + 1;
    return Decayed(_heat[index], stamp - last);
}

bool GSCardAnalysis::LastTouch(Channel channel, uint32_t cell, uint64_t& frame) const
{
    if (cell >= _cells)
        return false;
    const uint32_t last = _heatFrame[static_cast<size_t>(channel) * _cells + cell];
    if (last == 0)
        return false;
    frame = last - 1;
    return true;
}

void GSCardAnalysis::NoteUninit(uint32_t offset, uint16_t address, uint16_t pc, bool fetch)
{
    uint8_t& seen = _uninitSeen[offset >> 3];
    const uint8_t bit = static_cast<uint8_t>(1u << (offset & 7));
    if (seen & bit)
        return;
    seen |= bit;
    std::lock_guard<std::mutex> lock(_uninitMutex);
    if (_uninitRows.size() >= kMaxUninitRows)
    {
        _uninitDropped++;
        return;
    }
    UninitRead row;
    row.offset = offset;
    row.address = address;
    row.pc = pc;
    row.frame = Frame();
    row.fetch = fetch;
    _uninitRows.push_back(row);
}

std::vector<GSCardAnalysis::UninitRead> GSCardAnalysis::UninitRows(uint64_t& dropped) const
{
    std::lock_guard<std::mutex> lock(_uninitMutex);
    dropped = _uninitDropped;
    return _uninitRows;
}

/// region <GeneralSoundCard: the analysis switch>

#include "emulator/sound/chips/gs/generalsoundcard.h"

bool GeneralSoundCard::setAnalysisOn(bool on, const uint64_t* frameSource, std::string* error)
{
    const size_t romBytes = static_cast<size_t>(cardPageCount(CardPage::Rom)) * 0x4000;
    const size_t ramBytes = static_cast<size_t>(cardPageCount(CardPage::Ram)) * 0x4000;
    if (!hasCoprocessor() || ramBytes == 0)
    {
        if (error)
            *error = "not available: the " + deviceDescription() + " has no card CPU";
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(_analysisMutex);
        if (on && !_analysisOwned)
            _analysisOwned = std::make_shared<GSCardAnalysis>(romBytes, ramBytes, frameSource);
        _analysisWanted.store(on, std::memory_order_relaxed);
    }
    _analysisRequest.fetch_add(1, std::memory_order_release);
    return true;
}

bool GeneralSoundCard::releaseAnalysis()
{
    bool had = false;
    {
        std::lock_guard<std::mutex> lock(_analysisMutex);
        had = _analysisOwned != nullptr;
        _analysisOwned.reset();
        _analysisWanted.store(false, std::memory_order_relaxed);
    }
    _analysisRequest.fetch_add(1, std::memory_order_release);
    return had;
}

std::shared_ptr<GSCardAnalysis> GeneralSoundCard::analysis() const
{
    std::lock_guard<std::mutex> lock(_analysisMutex);
    return _analysisOwned;
}

void GeneralSoundCard::applyAnalysisRequest()
{
    _analysisSeen = _analysisRequest.load(std::memory_order_acquire);
    std::shared_ptr<GSCardAnalysis> held;
    {
        std::lock_guard<std::mutex> lock(_analysisMutex);
        if (_analysisWanted.load(std::memory_order_relaxed))
            held = _analysisOwned;
    }
    // The machine thread keeps its own reference while bound: a release on another thread cannot free the arrays
    // under a run
    _analysisHeld = std::move(held);
    _analysis = _analysisHeld.get();
    _analysisBound.store(_analysis != nullptr, std::memory_order_relaxed);
    bindAnalysisBus();
}

/// endregion </GeneralSoundCard: the analysis switch>
