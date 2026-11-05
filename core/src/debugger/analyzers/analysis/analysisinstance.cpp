#include "debugger/analyzers/analysis/analysisinstance.h"

#include <mutex>
#include <sstream>

#include "base/featuremanager.h"
#include "common/modulelogger.h"
#include "debugger/analyzers/analysis/replaytracer.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "emulator/config.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/media/mediamanager.h"
#include "emulator/media/medium.h"
#include "emulator/memory/memory.h"
#include "emulator/platform.h"
#include "emulator/slots/slotmanager.h"
#include "emulator/sound/soundmanager.h"
#include "loaders/snapshot/machinestatetransfer.h"

namespace analysis
{

namespace
{

/// Making and releasing an emulator touches process-wide registries: one at a time
std::mutex& LifecycleMutex()
{
    static std::mutex mutex;
    return mutex;
}

/// The bundle's floppies and tape into the same slots. The replay itself reads the port journal and needs none, but
/// a free machine forked from this one gets its media from here (MachineStateTransfer): without them its first disk
/// read after the fork finds an empty drive. Inserted before the session loads (an insert would end it), so every
/// seek's SetMediaToCheckpoint finds them; an untouched medium has the recording's identity and is bit-exact there
bool InsertBundleMedia(const TraceBundle& bundle, MediaManager& media, std::string& error)
{
    for (const BundleMedium& entry : bundle.media)
    {
        InsertOptions options;
        options.immediate = true;  // no swap delay: the restored controller state expects the medium in place
        options.writeProtect = entry.writeProtect;
        options.journal = JournalChoice::Off;  // a throwaway machine: no session journal next to anybody's image
        MediaResult inserted;
        if (entry.medium)
        {
            std::unique_ptr<Medium> copy = MachineStateTransfer::CopyMovingMedium(*entry.medium, "analysis");
            if (!copy)
                continue;
            inserted = media.Insert(entry.slot, std::move(copy), options);
        }
        else if (!entry.path.empty())
        {
            options.access = AccessMode::Session;  // the guest's writes stay in memory: the image file is never written
            MediaSource source;
            source.type = MediaSourceType::File;
            source.path = entry.path;
            inserted = media.Insert(entry.slot, source, options);
        }
        else
            continue;
        media.WaitApplied(entry.slot, 1000);
        if (!inserted.Ok())
        {
            error = "the recording's medium in " + entry.slot + " did not go into the analysis instance: " +
                    inserted.message;
            return false;
        }
    }
    return true;
}

}  // namespace

AnalysisInstance::~AnalysisInstance()
{
    if (_emulator)
    {
        std::lock_guard<std::mutex> lock(LifecycleMutex());
        _emulator->Stop();
        _emulator->Release();
        _emulator.reset();
    }
}

EmulatorContext* AnalysisInstance::Context() const
{
    return _emulator ? _emulator->GetContext() : nullptr;
}

uint32_t AnalysisInstance::FrameT() const
{
    EmulatorContext* context = Context();
    return context != nullptr ? context->emulatorState.BaseToCpuT(context->config.frame) : 0;
}

uint64_t AnalysisInstance::Frame() const
{
    EmulatorContext* context = Context();
    return context != nullptr ? context->emulatorState.frame_counter : 0;
}

bool AnalysisInstance::Make(const TraceBundle& bundle, const char* name, bool session, std::string& error)
{
    if (!bundle.config || (session && !bundle.session))
    {
        error = "the trace bundle holds no recording";
        return false;
    }
    std::unique_lock<std::mutex> lock(LifecycleMutex());
    const CONFIG source = *bundle.config;
    _emulator = std::make_unique<Emulator>(name, LoggerLevel::LogError);
    _emulator->SetPreferredModel(source.mem_model, source.ramsize);
    // The same model: the source's cards, and its slot set as it is (a card plugged at run time, a Kempston for a
    // game's joystick, is part of what the recording checks before it loads, and of the state an experiment gets)
    _emulator->SetConfigOverride([source, slots = bundle.slots](CONFIG& config) {
        MachineStateTransfer::FitSourceDevices(source, config);
        if (slots)
            SlotManager::UseSlots(*slots, config);
    });
    bool initialized = false;
    try
    {
        initialized = _emulator->Init();
    }
    catch (const std::exception& e)
    {
        error = std::string("the analysis instance could not be made: ") + e.what();
        return false;
    }
    if (!initialized)
    {
        error = "the analysis instance could not be made (model " + bundle.model + ")";
        return false;
    }
    EmulatorContext* context = _emulator->GetContext();
    if (context->pModuleLogger != nullptr)
        context->pModuleLogger->TurnOffLoggingForAll();
    if (FeatureManager* features = _emulator->GetFeatureManager())
    {
        features->setFeature(Features::kSoundGeneration, false);
        features->setFeature(Features::kTimeTravel, session);
        // A recording never runs with a fast loader (FeatureManager::ttdLocksFastLoader: fast disk compresses the
        // WD1793's timing, fast / turbo tape trap the ROM loader; no edit records either), and a free machine is
        // the recording's continuation: with fast disk on, a fork taken while a disk command runs ends the command
        // at the compressed timing and the first seek after the fork steps the head several times faster than the
        // recording did; with a fast tape the next load would skip the real-time loader. Either way the run leaves
        // the recorded path at once
        for (const char* loader : {Features::kFastDisk, Features::kFastTape, Features::kTurboTape})
            features->setFeature(loader, false);
    }
    if (context->pSoundManager != nullptr)
        context->pSoundManager->mute();
    if (context->pMemory != nullptr)
        context->pMemory->UpdateFeatureCache();
    lock.unlock();
    if (!session)
        return true;
    ttd::TimeTravelController* ttd = context->pTimeTravelController;
    if (ttd == nullptr)
    {
        error = "the analysis instance has no time travel engine";
        return false;
    }
    if (context->pMediaManager != nullptr && !InsertBundleMedia(bundle, *context->pMediaManager, error))
        return false;
    std::istringstream stream(*bundle.session, std::ios::binary);
    std::string why;
    if (!ttd->DeserializeSession(stream, why))
    {
        error = "the recording did not load on the analysis instance: " + why;
        return false;
    }
    return true;
}

bool AnalysisInstance::RunToFrame(uint64_t frame)
{
    EmulatorContext* context = Context();
    if (context == nullptr)
        return false;
    Z80* z80 = context->pCore->GetZ80();
    const uint32_t frameT = FrameT();
    while (context->emulatorState.frame_counter < frame)
    {
        const uint64_t frames = frame - context->emulatorState.frame_counter;
        const uint64_t run = frames * frameT;
        const uint64_t t = z80->t;
        const uint64_t before = context->emulatorState.t_states + t;
        _emulator->RunTStates(run > t ? run - t : 1, /*skipBreakpoints=*/true);
        if (context->emulatorState.t_states + z80->t == before)
            return false;
    }
    return true;
}

bool AnalysisInstance::RunFrames(uint64_t frames)
{
    return RunToFrame(Frame() + frames);
}

}  // namespace analysis
