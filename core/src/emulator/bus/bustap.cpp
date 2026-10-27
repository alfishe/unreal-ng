#include "stdafx.h"

#include "emulator/bus/bustap.h"

#include <algorithm>

#include "emulator/bus/busmanager.h"
#include "emulator/cpu/core.h"
#include "emulator/emulatorcontext.h"

BusTap::BusTap(std::string id, BusProtocol protocol, std::string label)
    : _baseId(id), _id(std::move(id)), _protocol(protocol), _label(std::move(label))
{
}

BusTap::~BusTap()
{
    Detach();
}

void BusTap::Attach(EmulatorContext* context)
{
    Detach();
    _context = context;
    if (context && context->pBusManager)
        context->pBusManager->Register(this);
}

void BusTap::Detach()
{
    if (_manager)
        _manager->Unregister(this);
    _manager = nullptr;
}

void BusTap::SetStamp(std::function<void(BusStamp&)> stamp)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _stamp = std::move(stamp);
}

void BusTap::SetDevice(uint8_t slot, std::string name)
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto it = std::find_if(_devices.begin(), _devices.end(), [slot](const BusDevice& d) { return d.slot == slot; });
    if (name.empty())
    {
        if (it != _devices.end())
            _devices.erase(it);
        return;
    }
    if (it != _devices.end())
        it->name = std::move(name);
    else
    {
        _devices.push_back(BusDevice{slot, std::move(name)});
        std::sort(_devices.begin(), _devices.end(), [](const BusDevice& a, const BusDevice& b) { return a.slot < b.slot; });
    }
}

void BusTap::SetByteTimeNs(double ns)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _byteTimeNs = ns;
}

bool BusTap::Stamp(BusStamp& stamp) const
{
    if (_stamp)
    {
        _stamp(stamp);
        return !stamp.skip;
    }
    if (!_context)
        return true;
    if (_context->ttdReplayActive)
        return false;
    stamp.frame = static_cast<uint32_t>(_context->emulatorState.frame_counter);
    if (Z80* z80 = _context->pCore ? _context->pCore->GetZ80() : nullptr)
    {
        stamp.t = z80->t;
        stamp.pc = z80->m1_pc;
    }
    return true;
}

void BusTap::Record(BusEventKind kind, uint8_t slot, uint8_t a, uint8_t b, uint8_t flags, uint32_t arg, uint64_t block)
{
    if (!_tracing.load(std::memory_order_relaxed))
        return;
    std::lock_guard<std::mutex> lock(_mutex);
    if (_protocolOnly && (kind == BusEventKind::SpiByte || kind == BusEventKind::AtaData || kind == BusEventKind::AtaRegRead))
        return;
    BusStamp stamp;
    if (!Stamp(stamp))
        return;
    BusTraceEvent e;
    e.epoch = _epoch;
    e.frame = stamp.frame;
    e.t = stamp.t;
    e.pc = stamp.pc;
    e.kind = kind;
    e.source = _source;
    e.slot = slot;
    e.a = a;
    e.b = b;
    e.flags = flags;
    e.arg = arg;
    e.block = block;
    _trace.Append(e);
}

void BusTap::Finding(BusRule rule, uint8_t slot, uint8_t value)
{
    if (!_checking.load(std::memory_order_relaxed))
        return;
    std::lock_guard<std::mutex> lock(_mutex);
    BusStamp stamp;
    if (!Stamp(stamp))
        return;
    BusFinding f;
    f.epoch = _epoch;
    f.frame = stamp.frame;
    f.t = stamp.t;
    f.pc = stamp.pc;
    f.rule = rule;
    f.source = _source;
    f.slot = slot;
    f.value = value;
    _trace.AddFinding(f);
}

/// region <SPI>

void BusTap::SpiSelect(uint8_t slot, bool selected, bool byteInFlight)
{
    Record(selected ? BusEventKind::SpiSelect : BusEventKind::SpiDeselect, slot);
    if (byteInFlight)
        Finding(BusRule::SelectMidByte, slot);
}

void BusTap::SpiByte(uint8_t slot, uint8_t mosi, uint8_t miso, bool read)
{
    Record(BusEventKind::SpiByte, slot, mosi, miso, read ? BusEventFlag::Read : 0);
}

void BusTap::SpiEarly(uint8_t slot, bool write, uint8_t value)
{
    Record(BusEventKind::SpiEarly, slot, value, 0, write ? BusEventFlag::Write : BusEventFlag::Read);
    Finding(BusRule::EarlyAccess, slot, value);
}

void BusTap::SpiTruncated(uint8_t slot)
{
    Record(BusEventKind::SpiTruncated, slot);
}

void BusTap::DmaCollision(bool write, uint8_t value)
{
    Record(BusEventKind::SpiEarly, 0, value, 0, static_cast<uint8_t>(write ? BusEventFlag::Write : BusEventFlag::Read));
    Finding(BusRule::DmaCollision, 0, value);
}

/// endregion </SPI>

/// region <SD>

void BusTap::SdCommand(uint8_t slot, uint8_t index, bool app, uint32_t argument, bool crcValid, uint8_t r1)
{
    const uint8_t flags = static_cast<uint8_t>((app ? BusEventFlag::App : 0) | (crcValid ? BusEventFlag::CrcValid : 0));
    Record(BusEventKind::SdCommand, slot, index, r1, flags, argument);
}

void BusTap::SdBlock(uint8_t slot, bool write, uint64_t block, bool ok)
{
    Record(write ? BusEventKind::SdWriteBlock : BusEventKind::SdReadBlock, slot, 0, 0, ok ? BusEventFlag::Ok : 0, 0, block);
}

/// endregion </SD>

/// region <ATA>

void BusTap::AtaRegister(uint8_t unit, uint8_t reg, uint8_t value, bool write)
{
    Record(write ? BusEventKind::AtaRegWrite : BusEventKind::AtaRegRead, unit, reg, value, write ? BusEventFlag::Write : 0);
}

void BusTap::AtaCommand(uint8_t unit, uint8_t command)
{
    Record(BusEventKind::AtaCommand, unit, command, 0, BusEventFlag::Write);
}

void BusTap::AtaData(uint8_t unit, uint16_t word, bool write)
{
    Record(BusEventKind::AtaData, unit, 0, 0, write ? BusEventFlag::Write : BusEventFlag::Read, word);
}

/// endregion </ATA>

void BusTap::SetSdCard(uint8_t slot, SdCardSpi* card)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _sdCards.erase(std::remove_if(_sdCards.begin(), _sdCards.end(), [slot](const auto& entry) { return entry.first == slot; }),
                   _sdCards.end());
    if (card)
        _sdCards.emplace_back(slot, card);
}

bool BusTap::WithSdCard(uint8_t slot, const std::function<void(SdCardSpi&)>& fn) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    for (const auto& [s, card] : _sdCards)
    {
        if (s == slot)
        {
            fn(*card);
            return true;
        }
    }
    return false;
}

std::vector<uint8_t> BusTap::SdCardSlots() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    std::vector<uint8_t> slots;
    for (const auto& entry : _sdCards)
        slots.push_back(entry.first);
    std::sort(slots.begin(), slots.end());
    return slots;
}

void BusTap::NextEpoch()
{
    std::lock_guard<std::mutex> lock(_mutex);
    _epoch++;
}

void BusTap::Control(int enable, int checks, bool clear, size_t capacity, int protocolOnly)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (protocolOnly >= 0)
        _protocolOnly = protocolOnly != 0;
    if (enable == 1 || (enable < 0 && _tracing && capacity))
        _trace.EnableEvents(capacity);
    else if (enable == 0)
        _trace.DisableEvents();
    if (checks == 1)
        _trace.EnableFindings();
    else if (checks == 0)
        _trace.DisableFindings();
    if (clear)
        _trace.Clear();
    _tracing.store(_trace.EventsOn(), std::memory_order_relaxed);
    _checking.store(_trace.FindingsOn(), std::memory_order_relaxed);
    _active.store(_tracing || _checking, std::memory_order_relaxed);
}

BusTapStatus BusTap::Status() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    BusTapStatus s;
    s.id = _id;
    s.protocol = _protocol;
    s.label = _label;
    s.byteTimeNs = _byteTimeNs;
    s.devices = _devices;
    s.tracing = _tracing;
    s.checking = _checking;
    s.protocolOnly = _protocolOnly;
    s.size = _trace.Size();
    s.capacity = _trace.Capacity();
    s.evicted = _trace.Evicted();
    s.rewound = _trace.Rewound();
    s.lastSeq = _trace.LastSeq();
    s.findings = 0;
    for (size_t i = 0; i < s.byRule.size(); i++)
    {
        s.byRule[i] = _trace.FindingsOf(static_cast<BusRule>(i));
        s.findings += s.byRule[i];
    }
    s.findingsKept = _trace.FindingsSize();
    return s;
}

std::vector<BusTraceEvent> BusTap::Events(const BusEventQuery& query) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _trace.Events(query);
}

std::vector<BusFinding> BusTap::Findings(const BusFindingQuery& query) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _trace.Findings(query);
}
