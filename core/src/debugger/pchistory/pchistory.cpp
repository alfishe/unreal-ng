#include "pchistory.h"

#include <algorithm>

#include "debugger/debugmanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

void PcHistory::Arm(bool on)
{
    if (on && !_armed)
        Clear();
    _armed = on;
    _context->SetStepWork(EmulatorContext::kStepWorkPcHistory, on);
}

void PcHistory::Clear()
{
    _next = 0;
    _total = 0;
}

void PcHistory::Record(uint16_t address)
{
    const MemoryPageDescriptor where = _context->pMemory->MapZ80AddressToPhysicalPage(address);
    _ring[_next] = Entry{address, static_cast<uint8_t>(where.mode), where.page};
    _next = (_next + 1) % kCapacity;
    ++_total;
}

std::vector<PcHistory::Entry> PcHistory::Newest(size_t depth) const
{
    const size_t count = static_cast<size_t>(std::min<uint64_t>({depth, _total, kCapacity}));
    std::vector<Entry> entries;
    entries.reserve(count);
    for (size_t i = 1; i <= count; ++i)
        entries.push_back(_ring[(_next + kCapacity - i) % kCapacity]);
    return entries;
}

StateNode PcHistory::ReportNow(size_t depth)
{
    StateNode node = StateNode::Object();
    const bool wasArmed = _armed;
    if (!wasArmed)
        Arm(true);
    node["armed"] = true;
    node["started_now"] = !wasArmed;
    node["total"] = _total;
    node["capacity"] = static_cast<int>(kCapacity);
    StateNode entries = StateNode::Array();
    for (const Entry& entry : Newest(std::min(depth, kCapacity)))
    {
        StateNode item = StateNode::Object();
        item["address"] = static_cast<int>(entry.address);
        item["kind"] = std::string(entry.kind == BANK_ROM     ? "rom"
                                   : entry.kind == BANK_RAM   ? "ram"
                                   : entry.kind == BANK_CACHE ? "cache"
                                                              : "none");
        item["page"] = static_cast<int>(entry.page);
        entries.push(item);
    }
    node["entries"] = entries;
    return node;
}

PcHistory::Result PcHistory::Report(Emulator* emulator, size_t depth)
{
    Result result;
    EmulatorContext* context = emulator ? emulator->GetContext() : nullptr;
    if (!context || !context->pDebugManager || !context->pMemory)
    {
        result.error = "emulator not available";
        return result;
    }
    PcHistory& history = *context->pDebugManager->GetPcHistory();
    const Emulator::CoherentMoment where =
        emulator->RunAtCoherentMoment([&]() { result.report = history.ReportNow(depth); }, 500);
    if (where == Emulator::CoherentMoment::Busy)
    {
        result.busy = true;
        result.error = "no coherent moment within 500 ms (the emulator is stepping or changing state); try again";
    }
    return result;
}

std::string PcHistory::SetArmed(Emulator* emulator, bool on)
{
    EmulatorContext* context = emulator ? emulator->GetContext() : nullptr;
    if (!context || !context->pDebugManager)
        return "emulator not available";
    PcHistory& history = *context->pDebugManager->GetPcHistory();
    const Emulator::CoherentMoment where = emulator->RunAtCoherentMoment(
        [&]() {
            if (on && history.IsArmed())
                history.Arm(false);  // re-arming starts empty
            history.Arm(on);
        },
        500);
    return where == Emulator::CoherentMoment::Busy
               ? "no coherent moment within 500 ms (the emulator is stepping or changing state); try again"
               : std::string();
}
