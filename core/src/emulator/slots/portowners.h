#pragma once
/// @file portowners.h
/// @brief Which device of the machine's registry a port access belongs to (docs/inprogress/2026-10-31-debugger-device-io):
/// the claims of the fitted cards (as their options configure them) and of the active built-ins, from the slot
/// manager's current result. The port trace names its events by it and a debugger tabs its I/O window by it.
///
/// Built on the control path (a report, a trace read); Find is a scan of the claims, for reports and not the access
/// path.

// Qt defines `slots` and `signals` as macros; this header reaches Qt translation units through the debugger
#pragma push_macro("slots")
#pragma push_macro("signals")
#undef slots
#undef signals

#include <cstdint>
#include <string>
#include <vector>

#include "emulator/slots/slotmanager.h"

class EmulatorContext;

namespace slots
{

class SlotPlanner;

class PortOwners
{
public:
    /// The owners of a machine: the fitted cards in slot order first (a card answers before the board's device on the
    /// same port), then the built-ins that are on and in their socket; a board in a built-in's socket (the TSFM in the
    /// AY socket) owns that built-in's ports under its slot (`ay-socket`)
    static PortOwners Of(const SlotManager::Result& result, const SlotPlanner& planner);
    /// The owners of a running machine (none without its slot manager)
    static PortOwners Of(const EmulatorContext* context);

    /// The registry id of the device that answers the access: a card's slot ("zxbus.1", "ay-socket") or a built-in's
    /// id ("beta128", "ula"); "" when no device claims it (the board's own ports). `dos`: the TR-DOS ROM is paged
    const std::string& Find(uint16_t port, bool out, bool dos) const;

    /// The index of the claim that answers the access (its owner: ClaimOwner), -1 for the board's own ports
    int FindClaim(uint16_t port, bool out, bool dos) const;
    const std::string& ClaimOwner(int claim) const
    {
        return _claims[static_cast<size_t>(claim)].owner;
    }
    size_t ClaimCount() const
    {
        return _claims.size();
    }

    /// Whether the machine has a declaration (else every access is the board's)
    bool Declared() const
    {
        return _declared;
    }

private:
    struct Claim
    {
        std::string owner;
        uint16_t mask = 0;
        uint16_t match = 0;
        Dir dir = Dir::InOut;
        Gate gate = Gate::Always;
    };

    std::vector<Claim> _claims;
    bool _declared = false;
};

/// The registry id of the device a main-CPU OUT to `port` goes to (a copy), `fallback` when the registry names none:
/// a device history's name
std::string PortOwnerName(const EmulatorContext* context, uint16_t port, const char* fallback);

}  // namespace slots

#pragma pop_macro("signals")
#pragma pop_macro("slots")
