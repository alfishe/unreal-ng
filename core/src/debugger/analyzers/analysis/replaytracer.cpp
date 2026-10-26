#include "debugger/analyzers/analysis/replaytracer.h"

#include <chrono>
#include <sstream>

#include "base/featuremanager.h"
#include "common/modulelogger.h"
#include "debugger/analyzers/analysis/l1builder.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "emulator/config.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/memory/memoryaccesstracker.h"
#include "emulator/platform.h"
#include "emulator/sound/soundmanager.h"
#include "loaders/snapshot/machinestatetransfer.h"

namespace analysis
{

bool TraceBundle::Capture(Emulator& source, TraceBundle& out, std::string& error)
{
    out = TraceBundle{};
    EmulatorContext* context = source.GetContext();
    ttd::TimeTravelController* ttd = context != nullptr ? context->pTimeTravelController : nullptr;
    if (ttd == nullptr)
    {
        error = "no time travel engine on this instance";
        return false;
    }
    if (ttd->IsRecording())
        ttd->StopRecording();
    if (ttd->GetCheckpointCount() == 0)
    {
        error = "no recording to replay: run 'analysis start record=true' (or a TTD recording), then the program";
        return false;
    }

    auto bytes = std::make_shared<std::string>();
    {
        std::ostringstream stream(std::ios::binary);
        std::string why;
        if (!ttd->SerializeSession(stream, why))
        {
            error = "the recording could not be captured: " + why;
            return false;
        }
        *bytes = stream.str();
    }
    out.session = bytes;
    out.sourceId = source.GetId();
    out.config = std::make_shared<const CONFIG>(context->config);
    const TMemModel* model = Config::FindModelByEnum(context->config.mem_model);
    out.model = model != nullptr && model->ShortName != nullptr ? model->ShortName : "";
    out.firstFrame = ttd->GetCheckpoint(0)->time.frame;
    out.endFrame = ttd->SessionEndPosition().frame;
    out.checkpoints = ttd->GetCheckpointCount();
    out.coverage = ttd->IsCoverageIndexEnabled();
    if (Memory* memory = source.GetMemory())
    {
        MemoryAccessTracker& tracker = memory->GetAccessTracker();
        out.facts = tracker.GetLevel() == TrackerLevel::Facts && tracker.SummarizeFacts().allocated;
        out.factsMode = tracker.GetTrackingMode() == TrackingMode::PhysicalMemory ? "physical" : "z80";
        out.callTrace = tracker.GetCallTraceBuffer() != nullptr;
    }
    return true;
}

StateNode TraceBundle::ToStateNode() const
{
    StateNode node = StateNode::Object();
    node["source"] = sourceId;
    node["model"] = model;
    node["first_frame"] = firstFrame;
    node["end_frame"] = endFrame;
    node["frames"] = endFrame > firstFrame ? endFrame - firstFrame : 0;
    node["checkpoints"] = checkpoints;
    node["session_bytes"] = static_cast<uint64_t>(session ? session->size() : 0);
    node["facts"] = facts;
    node["facts_mode"] = factsMode;
    node["coverage"] = coverage;
    node["calltrace"] = callTrace;
    return node;
}

namespace
{

/// The analysis instance: a headless, muted machine like the source, holding the recording
struct Clone
{
    std::unique_ptr<Emulator> emulator;

    ~Clone()
    {
        if (emulator)
        {
            emulator->Stop();
            emulator->Release();
        }
    }

    bool Make(const TraceBundle& bundle, std::string& error)
    {
        const CONFIG source = *bundle.config;
        emulator = std::make_unique<Emulator>("analysis-replay", LoggerLevel::LogError);
        emulator->SetPreferredModel(source.mem_model, source.ramsize);
        emulator->SetConfigOverride([source](CONFIG& config) { MachineStateTransfer::FitSourceDevices(source, config); });
        bool initialized = false;
        try
        {
            initialized = emulator->Init();
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
        EmulatorContext* context = emulator->GetContext();
        if (context->pModuleLogger != nullptr)
            context->pModuleLogger->TurnOffLoggingForAll();
        if (FeatureManager* features = emulator->GetFeatureManager())
        {
            features->setFeature(Features::kSoundGeneration, false);
            features->setFeature(Features::kTimeTravel, true);
        }
        if (context->pSoundManager != nullptr)
            context->pSoundManager->mute();
        if (context->pMemory != nullptr)
            context->pMemory->UpdateFeatureCache();
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
};

/// The hooks' state: what the instruction-start hook reads besides the registers
struct Tap
{
    L1Builder* builder = nullptr;
    Z80* z80 = nullptr;
    Memory* memory = nullptr;
    const EmulatorState* state = nullptr;
    uint64_t interrupts = 0;
};

L1Builder::Regs RegsOf(const Z80& cpu, uint16_t pc)
{
    L1Builder::Regs r;
    r.pc = pc;
    r.sp = cpu.sp;
    r.af = cpu.af;
    r.bc = cpu.bc;
    r.de = cpu.de;
    r.hl = cpu.hl;
    r.ix = cpu.ix;
    r.iy = cpu.iy;
    return r;
}

}  // namespace

bool ReplayBuild(const TraceBundle& bundle, const ReplayOptions& options, L1Result& out, std::string& error)
{
    if (!bundle.session || !bundle.config)
    {
        error = "the trace bundle holds no recording";
        return false;
    }
    const auto started = std::chrono::steady_clock::now();
    Clone clone;
    if (!clone.Make(bundle, error))
        return false;

    EmulatorContext* context = clone.emulator->GetContext();
    ttd::TimeTravelController* ttd = context->pTimeTravelController;
    Z80* z80 = context->pCore->GetZ80();
    Memory* memory = context->pMemory;
    const size_t checkpointCount = ttd->GetCheckpointCount();
    if (checkpointCount == 0)
    {
        error = "the recording has no keyframe";
        return false;
    }
    const uint64_t first = ttd->GetCheckpoint(0)->time.frame;
    uint64_t end = ttd->SessionEndPosition().frame;
    if (options.maxFrames != 0 && end > first + options.maxFrames)
        end = first + options.maxFrames;
    if (end <= first)
    {
        error = "the recording is shorter than one frame";
        return false;
    }
    if (!ttd->SeekTo(ttd->GetCheckpoint(0)->time, nullptr))
    {
        error = "the analysis instance could not seek to the first keyframe";
        return false;
    }

    L1Builder builder([memory](uint16_t address) { return memory->GetPageForBank(static_cast<uint8_t>(address >> 14)); },
                      options.salt);
    Tap tap;
    tap.builder = &builder;
    tap.z80 = z80;
    tap.memory = memory;
    tap.state = &context->emulatorState;
    tap.interrupts = z80->interruptsAccepted;

    ReplaySummary summary;
    summary.firstFrame = first;
    summary.lastFrame = end;
    bool cancelled = false;
    bool stalled = false;
    {
        ttd::TimeTravelController::ReplayModeScope replay(*ttd);
        z80->m1TraceHook = [&tap](uint16_t pc) {
            Z80& cpu = *tap.z80;
            const bool interrupted = cpu.interruptsAccepted != tap.interrupts;
            tap.interrupts = cpu.interruptsAccepted;
            uint16_t returnAddress = 0;
            if (interrupted)
                returnAddress = static_cast<uint16_t>(tap.memory->DirectReadFromZ80Memory(cpu.sp) |
                                                      (tap.memory->DirectReadFromZ80Memory(static_cast<uint16_t>(cpu.sp + 1)) << 8));
            const uint16_t page = tap.memory->GetPageForBank(static_cast<uint8_t>(pc >> 14));
            tap.builder->OnStart(pc, page, RegsOf(cpu, pc), tap.state->t_states + cpu.t, tap.state->frame_counter,
                                 cpu.halted != 0, interrupted, false, returnAddress);
        };
        z80->busTraceHook = [&builder](char type, uint16_t address, uint8_t value) {
            builder.OnBus(type, address, value);
        };

        const uint32_t frameT = context->emulatorState.BaseToCpuT(context->config.frame);
        const uint32_t chunk = options.chunkFrames != 0 ? options.chunkFrames : 25;
        size_t checkpointCursor = 0;
        while (context->emulatorState.frame_counter < end)
        {
            const uint64_t now = context->emulatorState.frame_counter;
            const uint64_t target = std::min<uint64_t>(end, now + chunk);
            while (context->emulatorState.frame_counter < target && !stalled)
            {
                const uint64_t frames = target - context->emulatorState.frame_counter;
                const uint64_t run = frames * frameT;
                const uint64_t t = z80->t;
                const uint64_t before = context->emulatorState.t_states + t;
                clone.emulator->RunTStates(run > t ? run - t : 1, /*skipBreakpoints=*/true);
                stalled = context->emulatorState.t_states + z80->t == before;
            }
            if (stalled)
                break;
            // The replay against the recording: the CPU at this frame boundary and the keyframe's
            const uint64_t frame = context->emulatorState.frame_counter;
            while (checkpointCursor < checkpointCount && ttd->GetCheckpoint(checkpointCursor)->time.frame < frame)
                ++checkpointCursor;
            if (checkpointCursor < checkpointCount)
            {
                const ttd::TTDCheckpoint* cp = ttd->GetCheckpoint(checkpointCursor);
                if (cp->time.frame == frame && cp->time.tInFrame == 0)
                {
                    ++summary.checked;
                    const ttd::TTDCpuState& c = cp->cpu;
                    const bool same = c.pc == z80->pc && c.sp == z80->sp && c.af == z80->af && c.bc == z80->bc &&
                                      c.de == z80->de && c.hl == z80->hl && c.ix == z80->ix && c.iy == z80->iy;
                    if (!same)
                    {
                        if (summary.diverged == 0)
                            summary.firstDivergence = frame;
                        ++summary.diverged;
                    }
                }
            }
            if (options.progress && !options.progress(frame - first, end - first))
            {
                cancelled = true;
                break;
            }
        }
        builder.Flush(RegsOf(*z80, z80->pc), context->emulatorState.t_states + z80->t);
        z80->m1TraceHook = nullptr;
        z80->busTraceHook = nullptr;
    }
    if (cancelled)
    {
        error = "cancelled";
        return false;
    }
    if (stalled)
    {
        error = "the analysis instance stopped running at frame " +
                std::to_string(context->emulatorState.frame_counter);
        return false;
    }

    summary.frames = context->emulatorState.frame_counter - first;
    const uint64_t builderBytes = builder.MemoryBytes();
    MemoryAccessTracker& tracker = memory->GetAccessTracker();
    out = builder.Build(summary, [&tracker](uint16_t page) {
        const PageInfo info = tracker.GetPageInfo(page);
        static const char* const kTypes[] = {"RAM", "ROM", "CACHE", "MISC"};
        const char* type = info.pageType < 4 ? kTypes[info.pageType] : "PAGE";
        return info.pageType == 3 ? std::string(type) : std::string(type) + " " + std::to_string(info.logicalPageNum);
    });
    out.replay.builderBytes = builderBytes;
    out.replay.hostMicros = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started).count());
    return true;
}

}  // namespace analysis
