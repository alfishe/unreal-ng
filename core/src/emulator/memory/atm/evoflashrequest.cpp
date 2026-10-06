#include "stdafx.h"

#include "evoflashrequest.h"

#include <functional>
#include <memory>

#include "debugger/ttd/timetravelhooks.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/ports/portdecoder.h"

namespace
{
EvoFlash* FlashOf(EmulatorContext* context)
{
    return context && context->pPortDecoder ? context->pPortDecoder->GetEvoFlash() : nullptr;
}

/// Carry @p action out on the machine's thread
EvoFlashResult OnMachineThread(EmulatorContext* context, std::function<bool(EvoFlash&)> action)
{
    if (context->ttdReplayActive)
        return EvoFlashResult::ReplayOwnsInput;
    auto result = std::make_shared<bool>(false);
    auto task = [context, action, result] {
        EvoFlash* flash = FlashOf(context);
        *result = flash && action(*flash);
    };
    if (!context->pTimeTravelHooks)
    {
        task();  // no manager (bare contexts): the caller is the only thread
        return *result ? EvoFlashResult::Done : EvoFlashResult::Failed;
    }
    switch (context->pTimeTravelHooks->SubmitMachineTask(task))
    {
        case ttd::TTDMachineTaskResult::RanNow:
            return *result ? EvoFlashResult::Done : EvoFlashResult::Failed;
        case ttd::TTDMachineTaskResult::Queued:
            return EvoFlashResult::Queued;
        default:
            return EvoFlashResult::ReplayOwnsInput;
    }
}
}  // namespace

bool EvoFlashGetStatus(EmulatorContext* context, EvoFlash::PersistStatus& out)
{
    EvoFlash* flash = FlashOf(context);
    if (!flash)
        return false;
    out = flash->Status();
    return true;
}

EvoFlashResult EvoFlashRequestSave(EmulatorContext* context)
{
    if (!FlashOf(context))
        return EvoFlashResult::NoFlash;
    return OnMachineThread(context, [](EvoFlash& flash) { return flash.Save(); });
}

EvoFlashResult EvoFlashRequestDiscard(EmulatorContext* context)
{
    if (!FlashOf(context))
        return EvoFlashResult::NoFlash;
    return OnMachineThread(context, [context](EvoFlash& flash) {
        if (!flash.Discard())
            return false;
        // The ROM pages still hold the flashed bytes: the next reset rereads the shipped image
        if (context->pEmulator)
            context->pEmulator->RequestRomReload();
        return true;
    });
}

const char* EvoFlashResultText(EvoFlashResult r)
{
    switch (r)
    {
        case EvoFlashResult::Done: return "done";
        case EvoFlashResult::Queued: return "queued";
        case EvoFlashResult::NoFlash: return "this machine's ROM is not a flash (ZX-Evo TS-Conf / ATM3 only)";
        case EvoFlashResult::ReplayOwnsInput: return "refused: a TTD replay owns the machine";
        case EvoFlashResult::Failed: return "failed (no ROM loaded, persistence off, or a file error: see the log)";
    }
    return "unknown";
}
