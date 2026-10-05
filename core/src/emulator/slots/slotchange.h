#pragma once

/// @file slotchange.h
/// @brief A slot change applied by a restart (docs/inprogress/2026-10-03-zx-bus-slots/architecture.md §5, owner
/// decision Q6, R-OP-3 / R-OP-8): no card is created or destroyed in a running machine.
///
/// One request (plug / remove / set options, slots::SlotRequest) is planned against the instance's slot set
/// (SlotManager::PlanChange: the plan engine, the TTD guard, the media with unsaved changes). A refused plan changes
/// nothing and carries the plan; a dry run returns the plan. An allowed plan restarts the instance through the
/// model-switch path with the same model and the planned [SLOTS]: the new machine is created first, next to the old
/// one, with the instance's own create-time override applied again (a machine variant's board, a create-time option);
/// the media follow into the slots with the same id, the media of a removed card (`sd.ngs` of a NeoGS) are reported
/// as closed or detached, and a dirty one needs the request's disposition (save / discard, R-OP-6). If the new
/// machine cannot be created (a card that cannot be built), the old machine stays as it was and the result carries
/// the error. The running [NETWORK] settings (changed at run time or not) are carried into the restarted machine:
/// they are configuration, not machine state; the slot set decides the ZX-bus network cards.
///
/// Example: a Pentagon with `ay-socket = tsfm`, `zxbus.1 = neogs`; the request "plug multisound into zxbus.next"
/// without replaceIfIncompatible is refused, listing the TSFM and the NeoGS it would displace; with the flag the
/// machine restarts with `zxbus.2 = multisound` only, and `sd.ngs` is reported closed.
///
/// The surfaces (CLI, WebAPI, MCP, Lua, Python, Qt; SL-7) call Run with their request and show the result 1:1: the
/// plan (removed cards with their full options for an Undo, shadowed devices, lost functions, media, fit), the
/// refusal and whether the instance was restarted (a new emulator id, like a model switch).

// Qt defines `slots` / `signals` as macros; this header names the slots namespace
#pragma push_macro("slots")
#pragma push_macro("signals")
#undef slots
#undef signals

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "emulator/media/mediamanager.h"
#include "emulator/media/modelswitch.h"
#include "emulator/slots/slotmanager.h"

class Emulator;

struct SlotChangeRequest
{
    std::string emulatorId;
    slots::SlotRequest change;   ///< op, slot, card, options, adapter, replaceIfIncompatible, dryRun, mediaDisposition
    /// Instead of `change`: this exact slot set (an Undo puts the set from before a change back, SlotManager::ConfigOf
    /// of that plan). Checked as a creation is (Q8) and against the TTD guard; `change.dryRun` and
    /// `change.mediaDisposition` still apply
    std::optional<SlotConfig> slotSet;
    /// Instead of `change`: several requests planned one after the other into one restart
    /// (SlotManager::PlanChanges; the network settings' card set, Q11: one card out, another in). `change.dryRun` and
    /// `change.mediaDisposition` still apply
    std::vector<slots::SlotRequest> changes;
    /// Called with the old machine stopped, before it is destroyed (a GUI unbinds its views here)
    std::function<void(Emulator& old)> beforeRelease;
};

enum class SlotChangeStatus : uint8_t
{
    Applied,      ///< the machine was restarted with the new slot set
    DryRun,       ///< the plan only (allowed)
    Refused,      ///< the plan refuses it, or it needs replaceIfIncompatible: nothing changed
    Recording,    ///< a TTD session records (R-OP-7): nothing changed
    NoMachine,    ///< no such emulator, or one without a slot set
    Failed,       ///< the new machine could not be created, or a dirty medium could not be saved: the old one stays
};

struct SlotChangeResult
{
    SlotChangeStatus status = SlotChangeStatus::NoMachine;
    std::string message;                        ///< the refusal or the error; "" when applied / a dry run
    SlotManager::ChangePlan plan;               ///< the plan (also for a refusal: what it would remove, why)
    std::shared_ptr<Emulator> emulator;         ///< Applied: the restarted machine, created and not started
    std::string previousEmulatorId;
    bool wasRunning = false;                    ///< the old machine ran: the caller starts the new one
    MediaTransferReport media;                  ///< where the media went (Applied)
    std::vector<SlotInfo> stranded;             ///< dirty media of removed cards (Failed: no disposition given)

    bool Applied() const
    {
        return status == SlotChangeStatus::Applied;
    }
};

class SlotChange
{
public:
    static SlotChangeResult Run(const SlotChangeRequest& request);

    /// "applied" | "dry-run" | "refused" | "recording" | "no-machine" | "failed"
    static const char* StatusName(SlotChangeStatus status);
};

#pragma pop_macro("signals")
#pragma pop_macro("slots")
