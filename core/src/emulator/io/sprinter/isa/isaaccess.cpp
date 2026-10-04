// DeviceState::Isa and IsaAccess: the ISA slot report and ISA cycles for every automation interface
// (docs/inprogress/2026-10-02-sprinter-isa/tdd.md §10). Built beside the Sprinter code so the shared state
// code names no Sprinter type.

#include "stdafx.h"

#include "emulator/io/sprinter/isa/isaaccess.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <functional>

#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/sprinter/isa/sprinterisabus.h"
#include "emulator/memory/sprinter/sprintermemory.h"
#include "emulator/ports/models/portdecoder_sprinter.h"
#include "emulator/state/devicestate.h"

namespace
{
PortDecoder_Sprinter* SprinterDecoder(EmulatorContext* context)
{
    return context ? dynamic_cast<PortDecoder_Sprinter*>(context->pPortDecoder) : nullptr;
}

std::string Hex(unsigned value, int digits)
{
    char text[16];
    std::snprintf(text, sizeof(text), "#%0*X", digits, value);
    return text;
}
}  // namespace

namespace IsaAccess
{

SprinterIsaBus* Find(EmulatorContext* context, std::string* reason)
{
    PortDecoder_Sprinter* decoder = SprinterDecoder(context);
    if (!decoder)
    {
        if (reason)
            *reason = "no ISA slots on this machine (the Sprinter has two)";
        return nullptr;
    }
    return &decoder->GetIsaBus();
}

bool ParseAddress(const std::string& text, uint32_t& address)
{
    std::string t;
    for (char c : text)
    {
        if (c != ' ' && c != '\t')
            t.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
    int radix = 10;
    if (!t.empty() && (t[0] == '#' || t[0] == '$'))
    {
        t.erase(0, 1);
        radix = 16;
    }
    else if (t.size() > 2 && t[0] == '0' && t[1] == 'X')
    {
        t.erase(0, 2);
        radix = 16;
    }
    else if (!t.empty() && t.back() == 'H')
    {
        t.pop_back();
        radix = 16;
    }
    if (t.empty())
        return false;
    char* end = nullptr;
    const unsigned long value = std::strtoul(t.c_str(), &end, radix);
    if (!end || *end != '\0' || value > 0xFFFFF)
        return false;
    address = static_cast<uint32_t>(value);
    return true;
}

bool Execute(EmulatorContext* context, const std::string& rawAction, int slot, uint32_t address, int value,
             const char* source, StateNode& result, std::string& error)
{
    SprinterIsaBus* bus = Find(context, &error);
    if (!bus)
        return false;
    std::string action;
    for (char c : rawAction)
        action.push_back(c == '-' ? '_' : static_cast<char>(std::tolower(static_cast<unsigned char>(c))));

    const bool needsSlot = action != "reset" && action != "latch" && action.rfind("journal", 0) != 0;
    if (needsSlot && slot != 1 && slot != 2)
    {
        error = "slot: 1 or 2";
        return false;
    }
    if (address > 0xFFFFF)
    {
        error = "address: a 20-bit ISA address (#00000..#FFFFF)";
        return false;
    }
    const bool writes = action == "io_write" || action == "mem_write" || action == "latch";
    if (writes && (value < 0 || value > 0xFF))
    {
        error = "value: 0..255";
        return false;
    }

    const int index = slot - 1;
    const SprinterIsaBus::Space space =
        action.rfind("io_", 0) == 0 ? SprinterIsaBus::Space::Io : SprinterIsaBus::Space::Memory;
    int answer = -1;
    std::function<void()> edit;
    if (action == "io_read" || action == "mem_read")
        edit = [&]() { answer = bus->ReadAt(space, index, address); };
    else if (action == "io_write" || action == "mem_write")
        edit = [&]() { bus->WriteAt(space, index, address, static_cast<uint8_t>(value)); };
    else if (action == "io_peek" || action == "mem_peek")
        answer = bus->PeekAt(space, index, address);
    else if (action == "reset")
    {
        edit = [&]() {
            const uint8_t latch = bus->Latch();
            bus->WriteLatch(static_cast<uint8_t>(latch | SprinterIsaBus::kLatchReset));
            bus->WriteLatch(static_cast<uint8_t>(latch & ~SprinterIsaBus::kLatchReset));
        };
    }
    else if (action == "journal_clear")
        bus->ClearJournal();
    else if (action == "journal_on" || action == "journal_off")
        bus->SetJournalEnabled(action == "journal_on");
    else if (action == "latch")
    {
        edit = [&]() {
            bus->WriteLatch(static_cast<uint8_t>(value));
            if (PortDecoder_Sprinter* decoder = SprinterDecoder(context))
                decoder->GetPldState().isaAddrExt = static_cast<uint8_t>(value & SprinterIsaBus::kLatchAddressMask);
        };
    }
    else
    {
        error = "action: io_read | io_write | io_peek | mem_read | mem_write | mem_peek | reset | latch | journal_clear | "
                "journal_on | journal_off";
        return false;
    }

    if (edit)
    {
        if (context->pEmulator)
            context->pEmulator->EditMemoryFromTool(source ? source : "isa", edit);
        else
            edit();
    }

    result = StateNode::Object();
    result["action"] = action;
    if (needsSlot)
    {
        result["slot"] = slot;
        result["address"] = Hex(address, 5);
    }
    if (answer >= 0)
        result["value"] = Hex(static_cast<unsigned>(answer), 2);
    else if (writes)
        result["value"] = Hex(static_cast<unsigned>(value), 2);
    result["latch"] = Hex(bus->Latch(), 2);
    return true;
}

}  // namespace IsaAccess

StateNode DeviceState::IsaJournal(EmulatorContext* context, unsigned last)
{
    std::string why;
    SprinterIsaBus* bus = IsaAccess::Find(context, &why);
    StateNode ret = StateNode::Object();
    if (!bus)
    {
        ret["available"] = false;
        ret["description"] = why;
        return ret;
    }
    ret["available"] = true;
    ret["enabled"] = bus->JournalEnabled();
    ret["capacity"] = static_cast<uint64_t>(SprinterIsaBus::kJournalLength);
    const auto& journal = bus->Journal();
    const size_t count = last == 0 || last > journal.size() ? journal.size() : last;
    StateNode entries = StateNode::Array();
    for (size_t i = journal.size() - count; i < journal.size(); ++i)
    {
        const SprinterIsaBus::JournalEntry& e = journal[i];
        StateNode n = StateNode::Object();
        n["frame"] = e.frame;
        n["t"] = static_cast<uint64_t>(e.t);
        n["pc"] = Hex(e.pc, 4);
        if (e.irq)
        {
            // An interrupt event: a slot's IRQ line edge (slot set), a PIO port B request, acknowledge or RETI
            n["event"] = "irq";
            if (e.slot >= 0)
                n["slot"] = e.slot + 1;
        }
        else if (e.slot >= 0)
        {
            n["slot"] = e.slot + 1;
            n["space"] = e.io ? "io" : "memory";
            n["access"] = e.write ? "write" : "read";
            n["address"] = Hex(e.address, 5);
            n["cpu_address"] = Hex(0xC000 | (e.address & 0x3FFF), 4);
        }
        else
            n["event"] = "bus";
        n["value"] = Hex(e.value, 2);
        if (!e.what.empty())
            n["what"] = e.what;
        entries.push(std::move(n));
    }
    ret["entries"] = entries;
    // The interrupt events on their own (a polled card floods the access journal)
    const auto& irqs = bus->IrqJournal();
    const size_t irqCount = last == 0 || last > irqs.size() ? irqs.size() : last;
    StateNode irqEntries = StateNode::Array();
    for (size_t i = irqs.size() - irqCount; i < irqs.size(); ++i)
    {
        const SprinterIsaBus::JournalEntry& e = irqs[i];
        StateNode n = StateNode::Object();
        n["frame"] = e.frame;
        n["t"] = static_cast<uint64_t>(e.t);
        n["pc"] = Hex(e.pc, 4);
        n["event"] = "irq";
        if (e.slot >= 0)
            n["slot"] = e.slot + 1;
        n["value"] = Hex(e.value, 2);
        n["what"] = e.what;
        irqEntries.push(std::move(n));
    }
    ret["irq_events"] = irqEntries;
    return ret;
}

StateNode DeviceState::Isa(EmulatorContext* context)
{
    std::string why;
    SprinterIsaBus* bus = IsaAccess::Find(context, &why);
    if (!bus)
    {
        StateNode n = StateNode::Object();
        n["available"] = false;
        n["description"] = why;
        return n;
    }
    StateNode ret = StateNode::Object();
    ret["available"] = true;
    ret["machine"] = "Sprinter Sp2000";

    // What window 3 shows now: an ISA slot (#1FFD bit 4 with page #D0 / #D2 / #D4 / #D6) or not
    StateNode window = StateNode::Object();
    PortDecoder_Sprinter* decoder = SprinterDecoder(context);
    SprinterMemory* memory = decoder ? decoder->GetSprinterMemory() : nullptr;
    const bool mapped = memory && memory->GetReadRedirect(3) == SprinterMemory::ReadRedirect::Isa;
    window["mapped"] = mapped;
    if (mapped)
    {
        const uint8_t page = static_cast<uint8_t>(memory->GetRAMPageForBank(3));
        SprinterIsaBus::Space space = SprinterIsaBus::Space::Memory;
        int slot = 0;
        SprinterIsaBus::PageToSlot(page, space, slot);
        window["page"] = Hex(page, 2);
        window["slot"] = slot + 1;
        window["space"] = space == SprinterIsaBus::Space::Io ? "io" : "memory";
        window["cpu_range"] = "#C000-#FFFF";
    }
    ret["window"] = window;

    const StateNode bus2 = bus->Describe();
    for (const auto& member : bus2.members)
        ret[member.first] = member.second;
    return ret;
}
