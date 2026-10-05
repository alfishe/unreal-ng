// DeviceState::Slots (emulator/state/devicestate.h): the slot report of a running machine, built from its
// SlotManager (R-REP-1). Every automation surface renders this one tree (SL-7 adds the surfaces).

#include "emulator/state/devicestate.h"

#include "emulator/emulatorcontext.h"
#include "emulator/platform.h"
#include "emulator/slots/slotmanager.h"
#include "emulator/slots/slotvocabulary.h"

using namespace slots;

namespace
{

const char* FitName(Fit fit)
{
    switch (fit)
    {
        case Fit::Real:
            return "real";
        case Fit::Adapter:
            return "adapter";
        case Fit::Unrealistic:
            return "unrealistic";
    }
    return "real";
}

const char* DirName(Dir dir)
{
    switch (dir)
    {
        case Dir::In:
            return "read";
        case Dir::Out:
            return "write";
        default:
            return "read/write";
    }
}

bool Applies(const CardDef& card, const CardOptions& options, const When& when)
{
    return when.option == Opt::None || (OptionBits(card, options, when.option) & when.anyOf) != 0;
}

StateNode SlotNode(const SlotManager::Slot& slot, const SlotPlanner& planner)
{
    StateNode node = StateNode::Object();
    node["slot"] = slot.entry.slot;
    node["card"] = slot.entry.card;
    const CardDef* card = planner.FindCard(slot.entry.card);
    if (card != nullptr)
    {
        node["name"] = card->name;
        node["options"] = FormatCardOptions(*card, slot.entry.options);
    }
    else if (slot.entry.card == kEmptySocket)
    {
        node["name"] = "empty AY socket";
    }
    node["adapter"] = slot.entry.adapter;
    node["fit"] = FitName(slot.fit);
    node["state"] = slot.entry.disabled ? "disabled" : "active";
    if (slot.entry.disabled)
    {
        node["reason"] = slot.entry.disabledReason;
    }
    node["source"] = slot.source;
    if (card != nullptr)
    {
        StateNode functions = StateNode::Array();
        for (const FunctionUse& use : card->functions)
        {
            if (Applies(*card, slot.entry.options, use.when))
            {
                functions.push(Describe(use.function).id);
            }
        }
        node["functions"] = std::move(functions);
        StateNode ports = StateNode::Array();
        for (const PortClaim& claim : card->claims)
        {
            if (!Applies(*card, slot.entry.options, claim.when))
            {
                continue;
            }
            StateNode port = StateNode::Object();
            port["range"] = FormatPortRange(claim.mask, claim.match);
            port["direction"] = DirName(claim.dir);
            port["iorqge"] = Describe(claim.iorqge).id;
            port["gate"] = Describe(claim.gate).id;
            ports.push(std::move(port));
        }
        node["ports"] = std::move(ports);
        StateNode media = StateNode::Array();
        for (const char* mediaSlot : card->media)
        {
            media.push(mediaSlot);
        }
        node["media"] = std::move(media);
    }
    StateNode notes = StateNode::Array();
    for (const std::string& note : slot.notes)
    {
        notes.push(note);
    }
    node["notes"] = std::move(notes);
    return node;
}

} // namespace

namespace DeviceState
{

StateNode Slots(EmulatorContext* context)
{
    StateNode node = StateNode::Object();
    SlotManager* manager = context != nullptr ? context->pSlotManager : nullptr;
    if (manager == nullptr)
    {
        node["available"] = false;
        node["description"] = "no machine";
        return node;
    }
    const SlotManager::Result& result = manager->Current();
    if (result.machine == nullptr)
    {
        node["available"] = false;
        node["description"] = "the model has no slot declaration";
        return node;
    }
    static const SlotPlanner planner;

    node["available"] = true;
    node["model"] = result.machine->name;
    node["board"] = result.machine->variant;
    node["source"] = result.fromSlotsSection ? "[SLOTS]" : "legacy keys";

    StateNode buses = StateNode::Array();
    for (const BusDef& bus : result.machine->buses)
    {
        StateNode b = StateNode::Object();
        b["id"] = bus.id;
        b["kind"] = Describe(bus.kind).id;
        b["arbitration"] = Describe(bus.arbitration).id;
        b["physicalSlots"] = static_cast<int>(bus.physicalSlots);
        b["retrofit"] = bus.retrofit;
        if (bus.retrofit)
        {
            b["retrofitNote"] = "the board has no connector for this bus: its cards are bolted on";
        }
        b["note"] = bus.note;
        buses.push(std::move(b));
    }
    node["buses"] = std::move(buses);

    StateNode slotList = StateNode::Array();
    for (const SlotManager::Slot& slot : result.entries)
    {
        slotList.push(SlotNode(slot, planner));
    }
    node["slots"] = std::move(slotList);

    StateNode builtIns = StateNode::Array();
    for (const SlotManager::BuiltIn& builtIn : result.builtIns)
    {
        StateNode b = StateNode::Object();
        b["id"] = builtIn.id;
        b["name"] = builtIn.name;
        b["kind"] = Describe(builtIn.kind).id;
        b["state"] = builtIn.state;
        if (builtIn.removed)
        {
            b["removed"] = true;   // a socketed chip a card took out of its socket (Q7)
        }
        if (!builtIn.source.empty())
        {
            b["source"] = builtIn.source;
        }
        builtIns.push(std::move(b));
    }
    node["builtIns"] = std::move(builtIns);

    StateNode log = StateNode::Array();
    for (const std::string& line : result.log)
    {
        log.push(line);
    }
    node["log"] = std::move(log);
    return node;
}

} // namespace DeviceState
