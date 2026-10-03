#include "emulator/io/sprinter/isa/sprinterisabus.h"

#include <cstdio>
#include <cstring>

using sprinterisa::CardKind;
using sprinterisa::IIsaCard;
using sprinterisa::IsaCycle;

namespace
{
std::string Hex(unsigned value, int digits)
{
    char text[16];
    std::snprintf(text, sizeof(text), "#%0*X", digits, value);
    return text;
}

/// The kind a fitted card reports, as the enum (unknown names: None)
CardKind KindOf(const IIsaCard* card)
{
    if (!card)
        return CardKind::None;
    CardKind kind = CardKind::None;
    sprinterisa::ParseKind(card->Kind(), kind);
    return kind;
}
}  // namespace

SprinterIsaBus::SprinterIsaBus() = default;
SprinterIsaBus::~SprinterIsaBus() = default;

bool SprinterIsaBus::PageToSlot(uint8_t page, Space& space, int& slot)
{
    if ((page & 0xF9) != 0xD0)
        return false;
    space = (page & 0x04) ? Space::Io : Space::Memory;
    slot = (page >> 1) & 1;
    return true;
}

void SprinterIsaBus::WriteLatch(uint8_t value)
{
    const bool wasReset = (_latch & kLatchReset) != 0;
    const bool reset = (value & kLatchReset) != 0;
    _latch = value;
    if (wasReset == reset)
        return;
    Note(-1, true, true, 0x9FBD, value, reset ? "RESET DRV asserted" : "RESET DRV released");
    // RESET DRV reaches both slots (research §4.3; the Wi-Fi kit's #C0 / #00 pulse)
    for (Slot& s : _slots)
    {
        if (reset)
            ++s.counters.resetPulses;
        if (s.card)
            s.card->SetReset(reset);
    }
}

void SprinterIsaBus::PowerOn()
{
    WriteLatch(0);
}

uint8_t SprinterIsaBus::ReadAt(Space space, int slot, uint32_t address)
{
    Slot& s = _slots[slot & 1];
    const IsaCycle cycle{address & 0xFFFFF, (_latch & kLatchAen) != 0};
    uint8_t value = 0xFF;
    const char* name = s.card && _journalOn ? s.card->RegisterName(space == Space::Io, cycle.address, false) : "";
    if (space == Space::Io)
    {
        ++s.counters.ioReads;
        if (s.card && !(_latch & kLatchReset) && !s.card->IoRead(cycle, value))
            value = 0xFF;
    }
    else
    {
        ++s.counters.memReads;
        if (s.card && !(_latch & kLatchReset) && !s.card->MemRead(cycle, value))
            value = 0xFF;
    }
    if (_tracer)
        _tracer(false, space, slot & 1, cycle.address, value);
    if (_journalOn)
        Note(slot & 1, space == Space::Io, false, cycle.address, value, name);
    AfterCycle(slot & 1);
    return value;
}

void SprinterIsaBus::WriteAt(Space space, int slot, uint32_t address, uint8_t value)
{
    Slot& s = _slots[slot & 1];
    const IsaCycle cycle{address & 0xFFFFF, (_latch & kLatchAen) != 0};
    const char* name = s.card && _journalOn ? s.card->RegisterName(space == Space::Io, cycle.address, true) : "";
    if (space == Space::Io)
    {
        ++s.counters.ioWrites;
        if (s.card && !(_latch & kLatchReset))
            s.card->IoWrite(cycle, value);
    }
    else
    {
        ++s.counters.memWrites;
        if (s.card && !(_latch & kLatchReset))
            s.card->MemWrite(cycle, value);
    }
    if (_tracer)
        _tracer(true, space, slot & 1, cycle.address, value);
    if (_journalOn)
        Note(slot & 1, space == Space::Io, true, cycle.address, value, name);
    AfterCycle(slot & 1);
}

void SprinterIsaBus::Note(int slot, bool io, bool write, uint32_t address, uint8_t value, std::string what)
{
    JournalEntry e;
    if (_clock)
        _clock(e.frame, e.t, e.pc);
    e.slot = static_cast<int8_t>(slot);
    e.io = io;
    e.write = write;
    e.address = address;
    e.value = value;
    e.what = std::move(what);
    _journal.push_back(std::move(e));
    while (_journal.size() > kJournalLength)
        _journal.pop_front();
}

void SprinterIsaBus::AfterCycle(int slot)
{
    IIsaCard* card = _slots[slot].card.get();
    if (card && card->Stalled())
    {
        if (_journalOn)
            Note(slot, true, false, 0, 0xFF, "stall: the card does not finish the cycle (the CPU waits for RESET)");
        if (_stall)
            _stall(slot);
    }
}

uint8_t SprinterIsaBus::PeekAt(Space space, int slot, uint32_t address) const
{
    const Slot& s = _slots[slot & 1];
    uint8_t value = 0xFF;
    if (!s.card || (_latch & kLatchReset))
        return 0xFF;
    address &= 0xFFFFF;
    const bool answered = space == Space::Io ? s.card->IoPeek(address, value) : s.card->MemPeek(address, value);
    return answered ? value : 0xFF;
}

void SprinterIsaBus::Configure(const sprinterisa::IsaConfig& config)
{
    for (int n = 0; n < kSlots; ++n)
    {
        Slot& s = _slots[n];
        s.config = config.slot[n];
        s.refusal.clear();
        std::string why;
        const auto kind = static_cast<CardKind>(s.config.kind);
        if (!sprinterisa::KindAvailable(kind, &why))
            s.refusal = why;
    }
}

void SprinterIsaBus::SetConfiguredPeer(int slot, const std::string& peer)
{
    sprinterisa::SlotConfig& c = _slots[slot & 1].config;
    std::snprintf(c.peer, sizeof(c.peer), "%s", peer.c_str());
}

void SprinterIsaBus::Fit(int slot, std::unique_ptr<IIsaCard> card)
{
    Slot& s = _slots[slot & 1];
    s.card = std::move(card);
    if (s.card)
        s.refusal.clear();
}

void SprinterIsaBus::FrameEnd()
{
    for (Slot& s : _slots)
    {
        if (s.card)
            s.card->FrameEnd();
    }
}

CardKind SprinterIsaBus::FittedKind(int slot) const
{
    return KindOf(_slots[slot & 1].card.get());
}

StateNode SprinterIsaBus::Describe() const
{
    StateNode ret = StateNode::Object();
    StateNode latch = StateNode::Object();
    latch["value"] = Hex(_latch, 2);
    latch["a19_a14"] = Hex(_latch & kLatchAddressMask, 2);
    latch["address_base"] = Hex(static_cast<unsigned>(_latch & kLatchAddressMask) << 14, 5);
    latch["aen"] = (_latch & kLatchAen) != 0;
    latch["reset"] = (_latch & kLatchReset) != 0;
    ret["latch"] = latch;

    StateNode slots = StateNode::Array();
    for (int n = 0; n < kSlots; ++n)
    {
        const Slot& s = _slots[n];
        StateNode slot = StateNode::Object();
        slot["slot"] = n + 1;
        slot["connector"] = n == 0 ? "J6" : "J7";
        slot["page_io"] = Hex(SlotPage(Space::Io, n), 2);
        slot["page_mem"] = Hex(SlotPage(Space::Memory, n), 2);
        const auto configured = static_cast<CardKind>(s.config.kind);
        slot["configured"] = sprinterisa::KindKey(configured);
        slot["card"] = s.card ? std::string(s.card->Kind()) : std::string("none");
        if (!s.refusal.empty())
            slot["not_fitted"] = s.refusal;
        slot["enabled"] = s.card != nullptr;
        StateNode resources = StateNode::Object();
        StateNode access = StateNode::Object();
        uint32_t first = 0, last = 0;
        if (s.card && s.card->IoRange(first, last))
        {
            resources["io"] = Hex(first, 3) + "-" + Hex(last, 3);
            // The Z80 reaches the range through window 3 (#C000-#FFFF): #1FFD bit 4 and the slot's I/O page; an I/O
            // card decodes ISA A9-A0 only, so every #400 of the window and any #9FBD value mirror it (AEN must be 0)
            const std::string note = s.card->DecodeNote();
            const bool aenBlind = s.card->IgnoresAen();
            access["io"] = "#1FFD bit 4 set, window 3 page " + Hex(SlotPage(Space::Io, n), 2) +
                           (aenBlind ? std::string(", any #9FBD AEN: CPU ") : std::string(", #9FBD AEN = 0: CPU ")) +
                           Hex(0xC000 | (first & 0x3FFF), 4) + "-" + Hex(0xC000 | (last & 0x3FFF), 4) +
                           (note.empty() ? std::string(" (A9-A0 decoded: mirrored every #400 of the window, any A19-A14)")
                                         : " (" + note + ")");
            access["io_now"] = (_latch & kLatchReset) ? "no: #9FBD RESET DRV held"
                               : ((_latch & kLatchAen) && !aenBlind) ? "no: #9FBD AEN = 1, an I/O card ignores the cycles"
                                                                    : "yes";
        }
        else
            resources["io"] = "none";
        if (s.card && s.card->MemRange(first, last))
        {
            resources["memory"] = Hex(first, 5) + "-" + Hex(last, 5);
            access["memory"] = "#1FFD bit 4 set, window 3 page " + Hex(SlotPage(Space::Memory, n), 2) +
                               ", #9FBD A19-A14 = " + Hex(first >> 14, 2) + ": CPU #C000-#FFFF";
        }
        else
            resources["memory"] = "none";
        const int irq = s.card ? s.card->IrqLine() : -1;
        resources["irq"] = irq >= 0 ? StateNode(irq) : StateNode("none");
        resources["irq_route"] = "Z84C15 PIO port B bit " + std::to_string(n) + " (not wired yet: ISA phase I4)";
        resources["dma"] = "none (the Sprinter has no ISA DMA controller)";
        slot["resources"] = resources;
        if (s.card)
            slot["z80_access"] = access;
        if (s.card)
            s.card->Describe(slot);
        StateNode counters = StateNode::Object();
        counters["io_reads"] = s.counters.ioReads;
        counters["io_writes"] = s.counters.ioWrites;
        counters["mem_reads"] = s.counters.memReads;
        counters["mem_writes"] = s.counters.memWrites;
        counters["reset_pulses"] = s.counters.resetPulses;
        slot["counters"] = counters;
        slots.push(slot);
    }
    ret["slots"] = slots;
    // One line per slot for a status bar / the Sprinter report
    std::string summary;
    for (int n = 0; n < kSlots; ++n)
    {
        const Slot& s = _slots[n];
        if (n)
            summary += "; ";
        summary += "slot " + std::to_string(n + 1) + ": ";
        uint32_t first = 0, last = 0;
        if (!s.card)
            summary += s.config.kind && !s.refusal.empty() ? sprinterisa::KindKey(static_cast<CardKind>(s.config.kind)) +
                                                                 " not fitted (" + s.refusal + ")"
                                                           : std::string("empty");
        else
        {
            summary += s.card->Kind();
            if (s.card->IoRange(first, last))
                summary += " I/O " + Hex(first, 3) + "-" + Hex(last, 3);
            if (s.card->IrqLine() >= 0)
                summary += " IRQ " + std::to_string(s.card->IrqLine());
        }
    }
    ret["summary"] = summary;
    // The two slots are separate selects (page bit 1): equal I/O ranges in both do not collide. No on-board device
    // answers ISA cycles. What remains is a card whose range leaves the 10-bit ISA I/O space
    StateNode conflicts = StateNode::Array();
    for (int n = 0; n < kSlots; ++n)
    {
        uint32_t first = 0, last = 0;
        if (_slots[n].card && _slots[n].card->IoRange(first, last) && last > 0x3FF)
            conflicts.push("slot " + std::to_string(n + 1) + ": I/O range " + Hex(first, 3) + "-" + Hex(last, 3) +
                           " passes #3FF: it wraps onto " + Hex(first & 0x3FF, 3));
    }
    ret["conflicts"] = conflicts;
    ret["conflict_rule"] = "each slot has its own select (page bit 1): cards in different slots may use the same "
                           "addresses; no on-board device decodes ISA cycles";
    ret["journal_entries"] = static_cast<uint64_t>(_journal.size());
    return ret;
}

size_t SprinterIsaBus::StateSize() const
{
    size_t size = 1 + 1 + kSlots;
    for (const Slot& s : _slots)
        size += s.card ? s.card->StateSize() : 0;
    return size;
}

void SprinterIsaBus::SaveState(uint8_t* dst) const
{
    size_t at = 0;
    dst[at++] = kStateVersion;
    dst[at++] = _latch;
    for (int n = 0; n < kSlots; ++n)
        dst[at++] = static_cast<uint8_t>(FittedKind(n));
    for (const Slot& s : _slots)
    {
        if (s.card && s.card->StateSize())
        {
            s.card->SaveState(dst + at);
            at += s.card->StateSize();
        }
    }
}

bool SprinterIsaBus::PopulationMatches(const uint8_t* src, size_t size, std::string& why) const
{
    if (size < 2 + kSlots || src[0] != kStateVersion)
    {
        why = "SprinterIsa blob of another version";
        return false;
    }
    for (int n = 0; n < kSlots; ++n)
    {
        const auto recorded = static_cast<CardKind>(src[2 + n]);
        const CardKind fitted = FittedKind(n);
        if (recorded != fitted)
        {
            why = "ISA slot " + std::to_string(n + 1) + " mismatch: recorded with " + sprinterisa::KindKey(recorded) +
                  ", fitted: " + sprinterisa::KindKey(fitted) + " - set [ISA] Slot" + std::to_string(n + 1) + "=" +
                  sprinterisa::KindName(recorded) + " and restart";
            return false;
        }
    }
    if (size != StateSize())
    {
        why = "SprinterIsa blob size differs from this population";
        return false;
    }
    return true;
}

bool SprinterIsaBus::LoadState(const uint8_t* src, size_t size, std::string& why)
{
    if (!PopulationMatches(src, size, why))
        return false;
    // The latch as recorded. No RESET edge reaches the cards: a restore is not a pulse, and their own blobs hold
    // their state (while RESET is held the bus keeps every cycle away from them)
    _latch = src[1];
    size_t at = 2 + kSlots;
    for (Slot& s : _slots)
    {
        if (!s.card)
            continue;
        if (s.card->StateSize())
        {
            s.card->LoadState(src + at);
            at += s.card->StateSize();
        }
    }
    return true;
}
