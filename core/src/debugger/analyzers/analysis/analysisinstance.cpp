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
