#include "emulator/slots/portowners.h"

#include <algorithm>

#include "emulator/emulatorcontext.h"
#include "emulator/slots/slotplanner.h"

namespace slots
{

PortOwners PortOwners::Of(const SlotManager::Result& result, const SlotPlanner& planner)
{
    PortOwners owners;
    if (result.machine == nullptr)
    {
        return owners;
    }
    owners._declared = true;
    for (const SlotManager::Slot& slot : result.entries)
    {
        const CardDef* card = slot.entry.disabled ? nullptr : planner.FindCard(slot.entry.card);
        if (card == nullptr)
        {
            continue;
        }
        for (const PortClaim& claim : card->claims)
        {
            if (claim.when.option != Opt::None &&
                (OptionBits(*card, slot.entry.options, claim.when.option) & claim.when.anyOf) == 0)
            {
                continue;
            }
            owners.Add(slot.entry.slot, claim);
        }
    }
    // The devices the emulator fits on its own: a card on the same port answers first, a board device after them
    for (const BuiltInDef& def : result.machine->fitted)
    {
        for (const PortClaim& claim : def.claims)
        {
            owners.Add(def.id, claim);
        }
    }
    // A board in a built-in's socket (the TurboSound / TSFM in the AY socket) declares no ports of its own: the
    // board's decode of the socket drives it, so the socket's ports are that slot's
    auto socketTaken = [&result](const char* socket) {
        return socket != nullptr && std::any_of(result.entries.begin(), result.entries.end(),
                                                [socket](const SlotManager::Slot& slot) {
                                                    return !slot.entry.disabled && slot.entry.slot == socket;
                                                });
    };
    for (const SlotManager::BuiltIn& builtIn : result.builtIns)
    {
        for (const BuiltInDef& def : result.machine->builtIns)
        {
            if (builtIn.id != def.id)
            {
                continue;
            }
            const bool taken = socketTaken(def.socket);
            if (!taken && (!builtIn.on || builtIn.removed))
            {
                continue;
            }
            const std::string owner = taken ? std::string(def.socket) : builtIn.id;
            for (const PortClaim& claim : def.claims)
            {
                owners.Add(owner, claim);
            }
        }
    }
    return owners;
}

void PortOwners::Add(const std::string& owner, const PortClaim& claim)
{
    Claim entry;
    entry.owner = owner;
    entry.mask = claim.mask;
    entry.match = static_cast<uint16_t>(claim.match & claim.mask);
    entry.dir = claim.dir;
    entry.gate = claim.gate;
    entry.port = claim.port != 0 ? claim.port : static_cast<uint16_t>(entry.match | ~claim.mask);
    entry.channel = claim.channel;
    entry.channelMask = claim.channelMask;
    entry.input = claim.input;
    entry.channelName = claim.channelName != nullptr ? claim.channelName : "";
    _claims.push_back(std::move(entry));
}

PortOwners PortOwners::Of(const EmulatorContext* context)
{
    static const SlotPlanner planner;
    if (context == nullptr || context->pSlotManager == nullptr)
    {
        return {};
    }
    return Of(context->pSlotManager->Current(), planner);
}

int PortOwners::FindClaim(uint16_t port, bool out, bool dos) const
{
    const Dir dir = out ? Dir::Out : Dir::In;
    for (size_t i = 0; i < _claims.size(); i++)
    {
        const Claim& claim = _claims[i];
        if ((port & claim.mask) != claim.match || (static_cast<uint8_t>(claim.dir) & static_cast<uint8_t>(dir)) == 0)
        {
            continue;
        }
        if ((claim.gate == Gate::DosOnly && !dos) || (claim.gate == Gate::NonDos && dos))
        {
            continue;
        }
        return static_cast<int>(i);
    }
    return -1;
}

const std::string& PortOwners::Find(uint16_t port, bool out, bool dos) const
{
    static const std::string none;
    const int claim = FindClaim(port, out, dos);
    return claim < 0 ? none : _claims[static_cast<size_t>(claim)].owner;
}

std::string PortOwnerName(const EmulatorContext* context, uint16_t port, const char* fallback)
{
    const std::string owner = PortOwners::Of(context).Find(port, true, false);  // a copy: the owners are a temporary
    return owner.empty() ? std::string(fallback) : owner;
}

}  // namespace slots
