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
            owners._claims.push_back({ slot.entry.slot, claim.mask, static_cast<uint16_t>(claim.match & claim.mask),
                                       claim.dir, claim.gate });
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
                owners._claims.push_back({ owner, claim.mask, static_cast<uint16_t>(claim.match & claim.mask),
                                           claim.dir, claim.gate });
            }
        }
    }
    return owners;
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

const std::string& PortOwners::Find(uint16_t port, bool out, bool dos) const
{
    static const std::string none;
    const Dir dir = out ? Dir::Out : Dir::In;
    for (const Claim& claim : _claims)
    {
        if ((port & claim.mask) != claim.match || (static_cast<uint8_t>(claim.dir) & static_cast<uint8_t>(dir)) == 0)
        {
            continue;
        }
        if ((claim.gate == Gate::DosOnly && !dos) || (claim.gate == Gate::NonDos && dos))
        {
            continue;
        }
        return claim.owner;
    }
    return none;
}

}  // namespace slots
