#include "debugger/analyzers/analysis/replaytracer.h"

#include <algorithm>
#include <chrono>
#include <sstream>

#include "base/featuremanager.h"
#include "common/modulelogger.h"
#include "debugger/analyzers/analysis/analysisinstance.h"
#include "debugger/analyzers/analysis/l1builder.h"
#include "debugger/analyzers/analysis/replaywindow.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "emulator/config.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/memory/memoryaccesstracker.h"
#include "emulator/platform.h"
#include "emulator/slots/slotmanager.h"
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
    if (context->pSlotManager != nullptr)
        out.slots = std::make_shared<const SlotConfig>(SlotManager::ConfigOf(context->pSlotManager->Current()));
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
    // The clone, seeked to the window's start (replaywindow.h: TTD does the restore and the replay)
    ReplayWindow window;
    if (!window.Open(bundle, "analysis-replay", ttd::TTDTimePoint{options.fromFrame, options.fromTInFrame}, error))
        return false;

    EmulatorContext* context = window.Context();
    ttd::TimeTravelController* ttd = window.Ttd();
    Z80* z80 = window.Cpu();
    Memory* memory = window.Mem();
    const size_t checkpointCount = ttd->GetCheckpointCount();
    const uint64_t first = window.Start().frame;
    uint64_t end = window.SessionEnd().frame;
    if (options.toFrame != 0 && options.toFrame < end)
        end = options.toFrame;
    if (options.maxFrames != 0 && end > first + options.maxFrames)
        end = first + options.maxFrames;
    if (end <= first)
    {
        const std::string frames = std::to_string(window.First().frame) + ".." + std::to_string(window.SessionEnd().frame);
        error = options.fromFrame != 0 || options.toFrame != 0
                    ? "the window holds no frame of the recording (frames " + frames + ")"
                    : "the recording is shorter than one frame";
        return false;
    }

    // The memory the replay starts from, per mapped bank (the detectors read initial values and the unread bytes)
    std::vector<PageImage> images;
    uint16_t startBanks[4] = {};
    const auto snap = [memory](uint8_t bank) {
        std::vector<uint8_t> bytes(0x4000);
        for (uint32_t i = 0; i < 0x4000; ++i)
            bytes[i] = memory->DirectReadFromZ80Memory(static_cast<uint16_t>((bank << 14) | i));
        return bytes;
    };
    for (uint8_t bank = 0; bank < 4; ++bank)
    {
        startBanks[bank] = memory->GetPageForBank(bank);
        if (std::none_of(images.begin(), images.end(), [&](const PageImage& i) { return i.page == startBanks[bank]; }))
            images.push_back(PageImage{startBanks[bank], snap(bank), {}});
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
    ReplayHooks hooks;
    hooks.instruction = [&tap](uint16_t pc) {
        Z80& cpu = *tap.z80;
        const bool interrupted = cpu.interruptsAccepted != tap.interrupts;
        tap.interrupts = cpu.interruptsAccepted;
        uint16_t returnAddress = 0;
        if (interrupted)
            returnAddress = static_cast<uint16_t>(tap.memory->DirectReadFromZ80Memory(cpu.sp) |
                                                  (tap.memory->DirectReadFromZ80Memory(static_cast<uint16_t>(cpu.sp + 1)) << 8));
        const uint16_t page = tap.memory->GetPageForBank(static_cast<uint8_t>(pc >> 14));
        // The arrival's in-frame position in TTD units (a window's from_tinframe, memory_at's t), not CPU
        // T-states: they differ on a machine whose TTD clock counts finer (a turbo, a fractional clock)
        tap.builder->OnStart(pc, page, RegsOf(cpu, pc), tap.state->t_states + cpu.t, tap.state->frame_counter,
                             cpu.halted != 0, interrupted, false, returnAddress,
                             tap.state->TtdTInFrame(static_cast<uint32_t>(cpu.t)));
    };
    hooks.bus = [&builder](char type, uint16_t address, uint8_t value) { builder.OnBus(type, address, value); };
    // A tool edit the recording holds (the fast tape load: a whole block at the ROM loader's entry) writes memory
    // past the bus: its bytes are the writes of the instruction it replaced
    hooks.editWrite = [&builder](uint16_t address, uint8_t value) { builder.OnEditWrite(address, value); };
    // At each chunk's frame boundary: the replay against the recording (the CPU and the keyframe there), then
    // the progress report, which may cancel
    size_t checkpointCursor = 0;
    hooks.chunk = [&](uint64_t frame) {
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
        return !options.progress || options.progress(frame - first, end - first);
    };
    const ReplayWindow::Run run = window.RunTo(end, options.chunkFrames != 0 ? options.chunkFrames : 25, hooks);
    const bool cancelled = run == ReplayWindow::Run::Stopped;
    const bool stalled = run == ReplayWindow::Run::Stalled;
    if (!stalled && !cancelled)
    {
        builder.Flush(RegsOf(*z80, z80->pc), context->emulatorState.t_states + z80->t);
        for (uint8_t bank = 0; bank < 4; ++bank)
        {
            const uint16_t page = memory->GetPageForBank(bank);
            auto it = std::find_if(images.begin(), images.end(), [&](const PageImage& i) { return i.page == page; });
            if (it == images.end())
            {
                images.push_back(PageImage{page, {}, {}});
                it = images.end() - 1;
            }
            if (it->end.empty())
                it->end = snap(bank);
        }
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
    std::sort(images.begin(), images.end(), [](const PageImage& a, const PageImage& b) { return a.page < b.page; });
    out.images = std::move(images);
    std::copy(std::begin(startBanks), std::end(startBanks), std::begin(out.startBanks));
    out.DiscountSweeps();
    out.replay.hostMicros = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started).count());
    return true;
}

}  // namespace analysis
