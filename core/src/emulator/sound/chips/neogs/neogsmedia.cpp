#include "neogsmedia.h"

#include <functional>
#include <memory>

#include "common/filehelper.h"
#include "debugger/ttd/timetravelmanager.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/chips/gs/generalsoundcard.h"
#include "emulator/sound/soundmanager.h"

namespace
{
/// Machine thread: the NeoGS card fitted now
GeneralSoundCard* neogsCard(EmulatorContext* context)
{
    GeneralSoundCard* gs = context && context->pSoundManager ? context->pSoundManager->getGeneralSound() : nullptr;
    return gs && gs->implementation() == GSCardImplementation::NGS ? gs : nullptr;
}

/// Caller's thread: whether the slot holds NeoGS (from the published copy -
/// the card itself may be replaced on the machine thread meanwhile)
bool neogsFitted(EmulatorContext* context)
{
    return context && context->pSoundManager && context->pSoundManager->fittedGeneralSoundKind() == GSTypeKind::NGS;
}

bool recording(EmulatorContext* context)
{
    return context->pTimeTravelManager && context->pTimeTravelManager->IsRecording();
}

/// Carry `action` out on the machine's thread, on the card fitted by then
NeoGSMediaResult onMachineThread(EmulatorContext* context, std::function<bool(GeneralSoundCard*)> action)
{
    auto result = std::make_shared<bool>(false);
    auto task = [context, action, result]
    {
        GeneralSoundCard* gs = neogsCard(context);
        *result = gs && action(gs);
        if (context->pSoundManager)
            context->pSoundManager->publishGeneralSoundSlot(); // the SD image may have changed
    };
    if (!context->pTimeTravelManager)
    {
        task(); // no manager (bare contexts): the caller is the only thread
        return *result ? NeoGSMediaResult::Done : NeoGSMediaResult::Failed;
    }
    switch (context->pTimeTravelManager->SubmitMachineTask(task))
    {
        case ttd::TimeTravelManager::MachineTaskResult::RanNow:
            return *result ? NeoGSMediaResult::Done : NeoGSMediaResult::Failed;
        case ttd::TimeTravelManager::MachineTaskResult::Queued:
            return NeoGSMediaResult::Queued;
        default:
            return NeoGSMediaResult::ReplayOwnsInput;
    }
}
} // namespace

NeoGSMediaResult NeoGSRequestSdInsert(EmulatorContext* context, const std::string& path)
{
    if (!neogsFitted(context))
        return NeoGSMediaResult::NoNeoGS;
    if (recording(context))
        return NeoGSMediaResult::TtdRecording;
    if (path.empty())
        return NeoGSMediaResult::NoPath;
    // The card resolves a relative path against the executable's folder too
    if (!FileHelper::FileExists(FileHelper::NormalizePath(path)) &&
        !FileHelper::FileExists(FileHelper::PathCombine(FileHelper::GetExecutablePath(), path)))
        return NeoGSMediaResult::NoFile;
    return onMachineThread(context, [path](GeneralSoundCard* gs) { return gs->insertSdCard(path); });
}

NeoGSMediaResult NeoGSRequestSdEject(EmulatorContext* context)
{
    if (!neogsFitted(context))
        return NeoGSMediaResult::NoNeoGS;
    if (recording(context))
        return NeoGSMediaResult::TtdRecording;
    return onMachineThread(context, [](GeneralSoundCard* gs) { return gs->ejectSdCard(); });
}

NeoGSMediaResult NeoGSRequestFlashSave(EmulatorContext* context)
{
    if (!neogsFitted(context))
        return NeoGSMediaResult::NoNeoGS;
    return onMachineThread(context, [](GeneralSoundCard* gs) { return gs->saveFlash(); });
}

const char* NeoGSMediaResultText(NeoGSMediaResult r)
{
    switch (r)
    {
        case NeoGSMediaResult::Done: return "done";
        case NeoGSMediaResult::Queued: return "queued: carried out at the next instruction boundary (while paused: when execution continues)";
        case NeoGSMediaResult::NoNeoGS: return "only the NeoGS card (GSType=NGS) has an SD slot and a flash chip";
        case NeoGSMediaResult::TtdRecording: return "refused: a TTD recording is running - the machine's configuration is fixed while recording";
        case NeoGSMediaResult::ReplayOwnsInput: return "refused: a TTD replay owns the machine";
        case NeoGSMediaResult::NoPath: return "needs an SD image path";
        case NeoGSMediaResult::NoFile: return "the SD image does not exist";
        case NeoGSMediaResult::Failed: return "failed (see the log)";
    }
    return "?";
}
