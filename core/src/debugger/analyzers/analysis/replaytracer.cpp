#include "debugger/analyzers/analysis/replaytracer.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>

#include "base/featuremanager.h"
#include "common/filehelper.h"
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
#include "emulator/media/mediamanager.h"
#include "emulator/media/medium.h"
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
    if (!Describe(source, out, error))
        return false;
    out.session = bytes;
    out.firstFrame = ttd->GetCheckpoint(0)->time.frame;
    out.endFrame = ttd->SessionEndPosition().frame;
    out.checkpoints = ttd->GetCheckpointCount();
    out.coverage = ttd->IsCoverageIndexEnabled();
    return true;
}

bool TraceBundle::UseSessionFile(const std::string& path, std::string& error)
{
    std::ifstream file(path, std::ios::binary);
    std::ostringstream bytes(std::ios::binary);
    if (file)
        bytes << file.rdbuf();
    if (!file || bytes.str().empty())
    {
        error = "the session file '" + path + "' could not be read";
        return false;
    }
    session = std::make_shared<const std::string>(bytes.str());
    name = path;
    // Known once a clone has loaded it (ReplayWindow)
    firstFrame = 0;
    endFrame = 0;
    checkpoints = 0;
    // The access facts, the coverage and the call trace are the source's own session's, not the file's
    facts = false;
    coverage = false;
    callTrace = false;
    return true;
}

bool TraceBundle::Describe(Emulator& source, TraceBundle& out, std::string& error)
{
    out = TraceBundle{};
    EmulatorContext* context = source.GetContext();
    if (context == nullptr)
    {
        error = "the instance has no machine";
        return false;
    }
    out.sourceId = source.GetId();
    out.config = std::make_shared<const CONFIG>(context->config);
    if (context->pSlotManager != nullptr)
        out.slots = std::make_shared<const SlotConfig>(SlotManager::ConfigOf(context->pSlotManager->Current()));
    const TMemModel* model = Config::FindModelByEnum(context->config.mem_model);
    out.model = model != nullptr && model->ShortName != nullptr ? model->ShortName : "";
    // The floppies and the tape as they are now: a free machine forked from the replay reads them itself (the
    // replay reads the port journal and needs none)
    if (MediaManager* media = context->pMediaManager)
    {
        for (const SlotInfo& slot : media->List())
        {
            if (!slot.present || !MachineStateTransfer::MediumMovesWithState(slot.descriptor.kind))
                continue;
            const Medium* medium = media->GetMedium(slot.descriptor.id);
            std::unique_ptr<Medium> copy = medium ? MachineStateTransfer::CopyMovingMedium(*medium, "analysis") : nullptr;
            if (!copy)
                continue;
            BundleMedium entry;
            entry.slot = slot.descriptor.id;
            entry.medium = std::shared_ptr<const Medium>(std::move(copy));
            entry.writeProtect = slot.writeProtect;
            out.media.push_back(std::move(entry));
        }
    }
    if (Memory* memory = source.GetMemory())
    {
        MemoryAccessTracker& tracker = memory->GetAccessTracker();
        out.facts = tracker.GetLevel() == TrackerLevel::Facts && tracker.SummarizeFacts().allocated;
        out.factsMode = tracker.GetTrackingMode() == TrackingMode::PhysicalMemory ? "physical" : "z80";
        out.callTrace = tracker.GetCallTraceBuffer() != nullptr;
    }
    return true;
}

bool TraceBundle::AddMediumFile(const std::string& slot, const std::string& path, std::string& error,
                                bool writeProtect)
{
    if (!FileHelper::IsFile(path))
    {
        error = "no image file '" + path + "' for " + slot;
        return false;
    }
    media.erase(std::remove_if(media.begin(), media.end(), [&slot](const BundleMedium& m) { return m.slot == slot; }),
                media.end());
    BundleMedium entry;
    entry.slot = slot;
    entry.path = path;
    entry.writeProtect = writeProtect;
    media.push_back(std::move(entry));
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
    StateNode list = StateNode::Array();
    for (const BundleMedium& m : media)
    {
        StateNode item = StateNode::Object();
        item["slot"] = m.slot;
        item["what"] = m.medium ? m.medium->Describe() : m.path;
        list.push(std::move(item));
    }
    node["media"] = std::move(list);
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
    uint64_t frameShift = 0;  ///< what puts the clone's frames on the build's axis (0 in a build of one)
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
    return ReplayBuild(std::vector<TraceBundle>{bundle}, options, out, error);
}

bool ReplayBuild(const std::vector<TraceBundle>& bundles, const ReplayOptions& options, L1Result& out,
                 std::string& error)
{
    if (bundles.empty())
    {
        error = "no recording to replay";
        return false;
    }
    const bool multi = bundles.size() > 1;
    if (multi && (options.fromFrame != 0 || options.fromTInFrame != 0 || options.toFrame != 0 || options.maxFrames != 0))
    {
        error = "a window (from_frame, to_frame, frames) is for a build of one session";
        return false;
    }
    for (const TraceBundle& bundle : bundles)
        if (!bundle.session || !bundle.config)
        {
            error = "the trace bundle holds no recording";
            return false;
        }
    const auto started = std::chrono::steady_clock::now();

    std::unique_ptr<L1Builder> builder;
    Tap tap;
    ReplaySummary summary;
    std::vector<SessionInfo> sessions;
    std::vector<PageImage> images;
    uint16_t startBanks[4] = {};
    uint64_t axis = 0;       // where the next session's first frame stands
    uint64_t framesDone = 0;  // of the sessions before this one

    for (size_t index = 0; index < bundles.size(); ++index)
    {
        const TraceBundle& bundle = bundles[index];
        const auto fail = [&](const std::string& why) {
            error = multi ? "session " + std::to_string(index) + " (" + bundle.name + "): " + why : why;
            return false;
        };
        // The clone, seeked to the window's start (replaywindow.h: TTD does the restore and the replay)
        ReplayWindow window;
        std::string why;
        if (!window.Open(bundle, "analysis-replay", ttd::TTDTimePoint{options.fromFrame, options.fromTInFrame}, why))
            return fail(why);

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
            const std::string frames =
                std::to_string(window.First().frame) + ".." + std::to_string(window.SessionEnd().frame);
            return fail(options.fromFrame != 0 || options.toFrame != 0
                            ? "the window holds no frame of the recording (frames " + frames + ")"
                            : "the recording is shorter than one frame");
        }

        // The memory the replay starts from, per mapped bank (the detectors read initial values and the unread bytes)
        const auto snap = [memory](uint8_t bank) {
            std::vector<uint8_t> bytes(0x4000);
            for (uint32_t i = 0; i < 0x4000; ++i)
                bytes[i] = memory->DirectReadFromZ80Memory(static_cast<uint16_t>((bank << 14) | i));
            return bytes;
        };
        const auto imageOf = [&images](uint16_t page) -> PageImage& {
            auto it = std::find_if(images.begin(), images.end(), [&](const PageImage& i) { return i.page == page; });
            if (it == images.end())
            {
                images.push_back(PageImage{page, {}, {}});
                it = images.end() - 1;
            }
            return *it;
        };
        for (uint8_t bank = 0; bank < 4; ++bank)
        {
            const uint16_t page = memory->GetPageForBank(bank);
            if (index == 0)
                startBanks[bank] = page;
            // The first session that maps a page at its start gives the page's start image
            if (PageImage& image = imageOf(page); image.start.empty() && image.end.empty())
                image.start = snap(bank);
        }

        const L1Builder::PageOf pageOf = [memory](uint16_t address) {
            return memory->GetPageForBank(static_cast<uint8_t>(address >> 14));
        };
        // Session 0 keeps its own frames; the next one follows where the one before ended
        const uint64_t base = index == 0 ? first : axis;
        if (index == 0)
        {
            builder = std::make_unique<L1Builder>(pageOf, options.salt);
            builder->IgnoreReaders(options.ignoreReaders);
            summary.firstFrame = first;
        }
        else
            builder->NextSession(pageOf, base, first);
        tap.builder = builder.get();
        tap.z80 = z80;
        tap.memory = memory;
        tap.state = &context->emulatorState;
        tap.interrupts = z80->interruptsAccepted;
        tap.frameShift = base - first;

        SessionInfo info;
        info.name = bundle.name;
        info.firstFrame = first;
        info.lastFrame = end;
        info.frameBase = base;
        const uint64_t instructionsBefore = builder->Instructions();
        const uint64_t tstatesBefore = builder->TStates();
        const uint64_t interruptsBefore = builder->Interrupts();

        ReplayHooks hooks;
        hooks.instruction = [&tap](uint16_t pc) {
            Z80& cpu = *tap.z80;
            const bool interrupted = cpu.interruptsAccepted != tap.interrupts;
            tap.interrupts = cpu.interruptsAccepted;
            uint16_t returnAddress = 0;
            if (interrupted)
                returnAddress = static_cast<uint16_t>(
                    tap.memory->DirectReadFromZ80Memory(cpu.sp) |
                    (tap.memory->DirectReadFromZ80Memory(static_cast<uint16_t>(cpu.sp + 1)) << 8));
            const uint16_t page = tap.memory->GetPageForBank(static_cast<uint8_t>(pc >> 14));
            // The arrival's in-frame position in TTD units (a window's from_tinframe, memory_at's t), not CPU
            // T-states: they differ on a machine whose TTD clock counts finer (a turbo, a fractional clock)
            tap.builder->OnStart(pc, page, RegsOf(cpu, pc), tap.state->t_states + cpu.t,
                                 tap.state->frame_counter + tap.frameShift, cpu.halted != 0, interrupted, false,
                                 returnAddress, tap.state->TtdTInFrame(static_cast<uint32_t>(cpu.t)));
        };
        L1Builder* fold = builder.get();
        hooks.bus = [fold](char type, uint16_t address, uint8_t value) { fold->OnBus(type, address, value); };
        // A tool edit the recording holds (the fast tape load: a whole block at the ROM loader's entry) writes memory
        // past the bus: its bytes are the writes of the instruction it replaced
        hooks.editWrite = [fold](uint16_t address, uint8_t value) { fold->OnEditWrite(address, value); };
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
                    ++info.checked;
                    const ttd::TTDCpuState& c = cp->cpu;
                    const bool same = c.pc == z80->pc && c.sp == z80->sp && c.af == z80->af && c.bc == z80->bc &&
                                      c.de == z80->de && c.hl == z80->hl && c.ix == z80->ix && c.iy == z80->iy;
                    if (!same)
                    {
                        if (info.diverged == 0)
                            info.firstDivergence = frame;
                        ++info.diverged;
                    }
                }
            }
            if (!options.progress)
                return true;
            // The sessions to come are as long as the ones seen so far, until they are opened
            const uint64_t seen = framesDone + (end - first);
            const uint64_t total = seen + seen / (index + 1) * (bundles.size() - index - 1);
            return options.progress(framesDone + (frame - first), total);
        };
        const ReplayWindow::Run run = window.RunTo(end, options.chunkFrames != 0 ? options.chunkFrames : 25, hooks);
        if (run == ReplayWindow::Run::Stopped)
        {
            error = "cancelled";
            return false;
        }
        if (run == ReplayWindow::Run::Stalled)
            return fail("the analysis instance stopped running at frame " +
                        std::to_string(context->emulatorState.frame_counter));

        builder->Flush(RegsOf(*z80, z80->pc), context->emulatorState.t_states + z80->t);
        // The last session that maps a page at its end gives the page's end image
        for (uint8_t bank = 0; bank < 4; ++bank)
        {
            const uint16_t page = memory->GetPageForBank(bank);
            bool once = true;
            for (uint8_t before = 0; before < bank; ++before)
                once = once && memory->GetPageForBank(before) != page;
            if (once)
                imageOf(page).end = snap(bank);
        }

        info.frames = context->emulatorState.frame_counter - first;
        info.instructions = builder->Instructions() - instructionsBefore;
        info.tstates = builder->TStates() - tstatesBefore;
        info.interrupts = builder->Interrupts() - interruptsBefore;
        summary.frames += info.frames;
        summary.checked += info.checked;
        if (info.diverged != 0 && summary.diverged == 0)
            summary.firstDivergence = info.firstDivergence + tap.frameShift;
        summary.diverged += info.diverged;
        axis = base + info.frames;
        summary.lastFrame = multi ? axis : end;
        framesDone += info.frames;
        sessions.push_back(std::move(info));

        MemoryAccessTracker& tracker = memory->GetAccessTracker();
        const auto nameOf = [&tracker](uint16_t page) {
            const PageInfo pageInfo = tracker.GetPageInfo(page);
            static const char* const kTypes[] = {"RAM", "ROM", "CACHE", "MISC"};
            const char* type = pageInfo.pageType < 4 ? kTypes[pageInfo.pageType] : "PAGE";
            return pageInfo.pageType == 3 ? std::string(type)
                                          : std::string(type) + " " + std::to_string(pageInfo.logicalPageNum);
        };
        if (index + 1 == bundles.size())
        {
            // The machines are one model: the last clone names every page
            const uint64_t builderBytes = builder->MemoryBytes();
            out = builder->Build(summary, nameOf);
            out.replay.builderBytes = builderBytes;
        }
    }

    if (multi)
        out.sessions = std::move(sessions);
    std::sort(images.begin(), images.end(), [](const PageImage& a, const PageImage& b) { return a.page < b.page; });
    out.images = std::move(images);
    std::copy(std::begin(startBanks), std::end(startBanks), std::begin(out.startBanks));
    out.DiscountSweeps();
    out.replay.hostMicros = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started).count());
    return true;
}

}  // namespace analysis
