#include "slotchange.h"

#include "emulator/config.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"

#include <cstdio>
#include <string>

namespace
{

/// The media of removed cards follow the request's disposition; none given: a dirty one refuses (the plan already
/// refused it, R-OP-6; the switch checks again)
StrandedMedia StrandedOf(slots::MediaDisposition disposition)
{
    switch (disposition)
    {
        case slots::MediaDisposition::Save:
            return StrandedMedia::Save;
        case slots::MediaDisposition::Discard:
            return StrandedMedia::Discard;
        case slots::MediaDisposition::None:
            break;
    }
    return StrandedMedia::Refuse;
}

} // namespace

const char* SlotChange::StatusName(SlotChangeStatus status)
{
    switch (status)
    {
        case SlotChangeStatus::Applied:
            return "applied";
        case SlotChangeStatus::DryRun:
            return "dry-run";
        case SlotChangeStatus::Refused:
            return "refused";
        case SlotChangeStatus::Recording:
            return "recording";
        case SlotChangeStatus::NoMachine:
            return "no-machine";
        case SlotChangeStatus::Failed:
            return "failed";
    }
    return "?";
}

SlotChangeResult SlotChange::Run(const SlotChangeRequest& request)
{
    SlotChangeResult out;
    EmulatorManager& emulators = *EmulatorManager::GetInstance();
    std::shared_ptr<Emulator> old = emulators.GetEmulator(request.emulatorId);
    EmulatorContext* context = old ? old->GetContext() : nullptr;
    if (context == nullptr || context->pSlotManager == nullptr)
    {
        out.message = "no emulator '" + request.emulatorId + "' with a slot set";
        return out;
    }
    if (emulators.GetZXPolyGroup(request.emulatorId) != nullptr)
    {
        out.status = SlotChangeStatus::Refused;
        out.message = "a ZX-Poly machine's modules share one configuration: slot changes are not supported there";
        return out;
    }
    const TMemModel* model = Config::FindModelByEnum(context->config.mem_model);
    if (model == nullptr)
    {
        out.message = "the machine's model has no name to restart it with";
        return out;
    }

    out.plan = request.slotSet           ? context->pSlotManager->PlanSet(*request.slotSet)
               : !request.changes.empty() ? context->pSlotManager->PlanChanges(request.changes)
                                          : context->pSlotManager->PlanChange(request.change);
    if (!out.plan.Allowed())
    {
        out.status = out.plan.recording ? SlotChangeStatus::Recording : SlotChangeStatus::Refused;
        out.message = out.plan.refusal;
        return out;
    }
    if (request.change.dryRun)
    {
        out.status = SlotChangeStatus::DryRun;
        return out;
    }

    // Apply = restart (Q6): the same model, the new slot set, the instance's own override kept
    ModelSwitchRequest restart;
    restart.emulatorId = request.emulatorId;
    restart.model = model->ShortName;
    restart.ramKb = context->config.ramsize;
    restart.stranded = StrandedOf(request.change.mediaDisposition);
    restart.beforeRelease = request.beforeRelease;
    restart.slotSet = out.plan.config;
    restart.keepConfigOverride = true;
    // The running [NETWORK] settings are configuration, not machine state (owner decision 2026-10-05): the restarted
    // machine keeps them - a hosts entry or host_access changed at run time survives the plug of an unrelated card. The
    // slot set then decides the ZX-bus card bits of Card=; a request's own settings (verb network) are applied to the
    // restarted machine afterwards, so they win
    //
    // The firmware choices set through the network settings live in other sections ([EVO] Avr=, [ATM] Kbc= with its
    // [ROM] ATM2KBC= image) but are configuration as well (owner decision 2026-10-06): carried the same way
    restart.carrySettings = [network = context->config.network, avr = context->config.atm.evo_avr,
                             kbc = context->config.atm.kbc_firmware,
                             kbcRom = std::string(context->config.atm.kbc_rom_path)](CONFIG& config) {
        config.network = network;
        config.atm.evo_avr = avr;
        config.atm.kbc_firmware = kbc;
        std::snprintf(config.atm.kbc_rom_path, sizeof(config.atm.kbc_rom_path), "%s", kbcRom.c_str());
    };
    out.previousEmulatorId = request.emulatorId;
    out.wasRunning = old->IsRunning() && !old->IsPaused();
    context = nullptr;
    old.reset();

    const ModelSwitchResult restarted = ModelSwitch::Run(restart);
    out.stranded = restarted.stranded;
    if (!restarted.result.Ok() || !restarted.emulator)
    {
        out.status = SlotChangeStatus::Failed;
        out.message = "the machine was not restarted, it keeps its slot set: " + restarted.result.message;
        return out;
    }
    out.status = SlotChangeStatus::Applied;
    out.emulator = restarted.emulator;
    out.media = restarted.media;
    return out;
}
