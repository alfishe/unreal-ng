#include "neogsmedia.h"

#include <functional>
#include <memory>

#include "common/filehelper.h"
#include "debugger/ttd/timetravelmanager.h"
#include "common/logger.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/media/mediamanager.h"
#include "emulator/sound/chips/neogs/soundchip_neogs.h"
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
    return context->pTimeTravelHooks && context->pTimeTravelHooks->IsRecording();
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
    if (!context->pTimeTravelHooks)
    {
        task(); // no manager (bare contexts): the caller is the only thread
        return *result ? NeoGSMediaResult::Done : NeoGSMediaResult::Failed;
    }
    switch (context->pTimeTravelHooks->SubmitMachineTask(task))
    {
        case ttd::TTDMachineTaskResult::RanNow:
            return *result ? NeoGSMediaResult::Done : NeoGSMediaResult::Failed;
        case ttd::TTDMachineTaskResult::Queued:
            return NeoGSMediaResult::Queued;
        default:
            return NeoGSMediaResult::ReplayOwnsInput;
    }
}

/// With a media manager the slot `sd.ngs` takes the request on any thread: at
/// once while the machine is not running, else at the next frame boundary
bool machineRunning(EmulatorContext* context)
{
    Emulator* emulator = context->pEmulator;
    return emulator && emulator->IsRunning() && !emulator->IsPaused();
}

NeoGSMediaResult viaMediaManager(bool running, const MediaResult& result)
{
    if (!result.Ok())
    {
        LOGWARNING("NeoGS: SD card request refused: %s", result.message.c_str());
        return NeoGSMediaResult::Failed;
    }
    return running ? NeoGSMediaResult::Queued : NeoGSMediaResult::Done;
}
} // namespace

NeoGSMediaResult NeoGSRequestSdInsert(EmulatorContext* context, const std::string& path)
{
    if (!neogsFitted(context))
        return NeoGSMediaResult::NoNeoGS;
    if (recording(context))
        return NeoGSMediaResult::TtdRecording;
    if (context->ttdReplayActive)
        return NeoGSMediaResult::ReplayOwnsInput;
    if (path.empty())
        return NeoGSMediaResult::NoPath;
    const std::string resolved = FileHelper::NormalizePath(path);
    const bool folder = FileHelper::IsFolder(resolved);
    if (!folder && !FileHelper::FileExists(resolved))
        return NeoGSMediaResult::NoFile;
    if (MediaManager* manager = context->pMediaManager; manager && manager->HasSlot(SoundChip_NeoGS::SD_SLOT_ID))
    {
        MediaSource source;
        source.path = resolved;
        source.type = folder ? MediaSourceType::Folder : MediaSourceType::File;
        InsertOptions options;
        options.access = SoundChip_NeoGS::configuredSdAccess(context->config.ngs);
        options.disposition = Disposition::Discard; // automation's sd_insert always replaced the card
        const bool running = machineRunning(context);
        return viaMediaManager(running, manager->Insert(SoundChip_NeoGS::SD_SLOT_ID, source, options));
    }
    return onMachineThread(context, [resolved](GeneralSoundCard* gs) { return gs->insertSdCard(resolved); });
}

NeoGSMediaResult NeoGSRequestSdEject(EmulatorContext* context)
{
    if (!neogsFitted(context))
        return NeoGSMediaResult::NoNeoGS;
    if (recording(context))
        return NeoGSMediaResult::TtdRecording;
    if (context->ttdReplayActive)
        return NeoGSMediaResult::ReplayOwnsInput;
    if (MediaManager* manager = context->pMediaManager; manager && manager->HasSlot(SoundChip_NeoGS::SD_SLOT_ID))
    {
        EjectOptions options;
        options.disposition = Disposition::Discard;
        const bool running = machineRunning(context);
        return viaMediaManager(running, manager->Eject(SoundChip_NeoGS::SD_SLOT_ID, options));
    }
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
        case NeoGSMediaResult::NoFile: return "no SD image or folder at that path";
        case NeoGSMediaResult::Failed: return "failed or refused by the media manager (see the log)";
    }
    return "?";
}
