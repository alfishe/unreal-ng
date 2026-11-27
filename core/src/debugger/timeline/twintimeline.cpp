#include "debugger/timeline/twintimeline.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <optional>

#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/debugmanager.h"
#include "debugger/disassembler/z80disasm.h"
#include "debugger/labels/labelmanager.h"
#include "debugger/pchistory/pchistory.h"
#include "debugger/targets/gscardtarget.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/execution/runcontrol.h"
#include "emulator/memory/memory.h"
#include "emulator/notifications.h"
#include "emulator/sound/chips/gs/generalsoundcard.h"
#include "emulator/sound/chips/gs/gscommandlog.h"
#include "emulator/sound/chips/gs/gshostclock.h"

namespace timeline
{

namespace
{
constexpr DebugCpuId kCardCpus[2] = {DebugCpuId::GS, DebugCpuId::MultiSoundGS};

size_t CardIndex(DebugCpuId cpu)
{
    return cpu == DebugCpuId::MultiSoundGS ? 1 : 0;
}

GeneralSoundCard* CardOf(EmulatorContext* context, DebugCpuId cpu)
{
    DebugManager* debug = context ? context->pDebugManager : nullptr;
    GSCardTarget* target = debug ? debug->GetCardTarget(cpu) : nullptr;
    return target ? target->Card() : nullptr;
}

/// A card with a CPU (the lightweight player has none: no lane)
bool HasCpu(const GeneralSoundCard* card)
{
    return card && card->cardUnitsPerSecond() > 0.0;
}

uint32_t Delta32(uint64_t now, uint64_t before)
{
    return now >= before ? static_cast<uint32_t>(std::min<uint64_t>(now - before, UINT32_MAX)) : 0;
}

std::string Hex4(uint16_t value)
{
    char text[8];
    std::snprintf(text, sizeof(text), "#%04X", value);
    return text;
}

/// 119985 -> "119,985"
std::string Grouped(int64_t value)
{
    std::string digits = std::to_string(value < 0 ? -value : value);
    std::string out;
    for (size_t i = 0; i < digits.size(); i++)
    {
        if (i && (digits.size() - i) % 3 == 0)
            out += ',';
        out += digits[i];
    }
    return value < 0 ? "-" + out : out;
}

/// 6.6 (one decimal, a whole number without one)
std::string OneDecimal(double value)
{
    char text[32];
    const double rounded = std::round(value * 10.0) / 10.0;
    if (rounded == std::floor(rounded))
        std::snprintf(text, sizeof(text), "%.0f", rounded);
    else
        std::snprintf(text, sizeof(text), "%.1f", rounded);
    return text;
}

double Round3(double value)
{
    return std::round(value * 1000.0) / 1000.0 + 0.0;  // + 0.0: no "-0" in the node
}

std::string MHz(uint32_t hz)
{
    return OneDecimal(static_cast<double>(hz) / 1e6) + " MHz";
}

const char* EventName(uint8_t kind)
{
    switch (kind)
    {
        case ExecRecord::kInt: return "int";
        case ExecRecord::kNmi: return "nmi";
        case ExecRecord::kClock: return "clock_change";
        default: return "instruction";
    }
}

/// An active execute breakpoint of a debugger (not a tool's hidden one) that matches the instruction at `pc` in the
/// window showing `kind` / `page`
bool ExecBreakpointAt(const BreakpointDescriptor& bp, uint16_t pc, uint8_t kind, uint8_t page)
{
    if (!bp.active || bp.hidden || bp.type != BRK_MEMORY || !(bp.memoryType & BRK_MEM_EXECUTE))
        return false;
    if (bp.matchType == BRK_MATCH_ADDR)
        return pc >= bp.z80address && pc <= bp.EndAddress();
    const uint16_t offset = static_cast<uint16_t>(pc & 0x3FFF);
    return kind == static_cast<uint8_t>(bp.pageType) && page == bp.page && offset >= bp.PageOffset() &&
           offset <= bp.PageOffsetEnd();
}

/// One lane's way to read its code and name it
struct LaneSource
{
    EmulatorContext* context = nullptr;
    GeneralSoundCard* card = nullptr;  ///< null: the main CPU
    LabelManager* labels = nullptr;

    std::string Text(uint16_t pc, uint8_t* length = nullptr) const
    {
        DebugManager* debug = context ? context->pDebugManager : nullptr;
        if (!debug || !debug->GetDisassembler())
            return {};
        std::vector<uint8_t> bytes(4);
        for (uint16_t i = 0; i < 4; i++)
        {
            const uint16_t at = static_cast<uint16_t>(pc + i);
            if (card)
            {
                uint8_t value = 0xFF;
                card->peekCardMemory(at, value);
                bytes[i] = value;
            }
            else if (context->pMemory)
                bytes[i] = context->pMemory->DirectReadFromZ80Memory(at);
        }
        // A card's own labels (never the main CPU's); main: the default scope
        std::optional<Z80Disassembler::LabelsScope> cardLabels;
        std::optional<Z80Disassembler::NoLabelsScope> noLabels;
        if (card)
        {
            if (labels)
                cardLabels.emplace(labels);
            else
                noLabels.emplace();
        }
        uint8_t size = 0;
        std::string text = debug->GetDisassembler()->disassembleSingleCommand(bytes, pc, &size);
        if (length)
            *length = size;
        return text;
    }
    std::string Label(uint16_t pc) const
    {
        if (!labels)
            return {};
        std::shared_ptr<::Label> label = labels->GetLabelByZ80Address(pc);
        return label ? label->name : std::string();
    }
};

/// The window's records of an execution history (oldest first): the newest `count`
std::vector<ExecRecord> Newest(history::DeviceHistory<ExecRecord>* history, size_t count)
{
    if (!history)
        return {};
    return history->Snapshot(count);
}
}  // namespace

/// region <Density>

void DensityTick::ToState(StateNode& node) const
{
    node["frame"] = frame;
    StateNode& main = node["main"];
    main = StateNode::Object();
    main["commands"] = hostCommands;
    main["data_writes"] = hostDataWrites;
    main["data_reads"] = hostDataReads;
    main["status_polls"] = hostStatusPolls;
    main["accesses"] = hostCommands + hostDataWrites + hostDataReads + hostStatusPolls;
    StateNode& card = node["card"];
    card = StateNode::Object();
    card["steps"] = cardSteps;
    card["idle_steps"] = cardIdleSteps;
    card["busy_steps"] = cardSteps - std::min(cardSteps, cardIdleSteps);
    card["interrupts"] = cardInterrupts;
    card["nmis"] = cardNmis;
    card["commands_taken"] = cardCommandsTaken;
    card["profiled"] = profiled;
    if (profiled)
    {
        card["cycles"] = cycles;
        card["isr_cycles"] = isrCycles;
        card["idle_cycles"] = idleCycles;
        card["halt_cycles"] = haltCycles;
        card["busy_cycles"] = cycles - std::min(cycles, isrCycles + idleCycles + haltCycles);
    }
}

/// endregion </Density>

/// region <Blocks>

std::vector<Block> Blocks(const std::vector<ExecRecord>& records, uint32_t frame, int64_t frameLength, int64_t end,
                          uint32_t clockNowHz)
{
    auto startOf = [&](const ExecRecord& r) {
        return static_cast<int64_t>(r.t) + static_cast<int64_t>(static_cast<int32_t>(r.frame - frame)) * frameLength;
    };
    // The records up to `end`, the clock changes apart (markers, no time of their own)
    std::vector<size_t> kept;
    kept.reserve(records.size());
    for (size_t i = 0; i < records.size(); i++)
        if (records[i].kind != ExecRecord::kClock && startOf(records[i]) <= end)
            kept.push_back(i);

    std::vector<Block> blocks;
    blocks.reserve(kept.size());
    for (size_t k = 0; k < kept.size(); k++)
    {
        const ExecRecord& r = records[kept[k]];
        Block block;
        block.index = kept[k];
        block.start = startOf(r);
        if (k + 1 < kept.size())
        {
            const ExecRecord& next = records[kept[k + 1]];
            block.duration = startOf(next) - block.start;
            // An instruction recorded again after a stop before it (an execute breakpoint, a card park): one block
            if (block.duration == 0 && next.pc == r.pc && !r.IsEvent() && !next.IsEvent())
                continue;
        }
        else
        {
            block.duration = end - block.start;
            block.open = true;
            // Recorded as it was about to run, the lane stopped before it: the next instruction, not a block
            if (block.duration == 0 && !r.IsEvent())
                continue;
        }
        blocks.push_back(block);
    }

    // The clock each ran at: the one now, walked back over the clock changes (each knows the clock before it)
    if (clockNowHz)
    {
        uint32_t clock = clockNowHz;
        size_t b = blocks.size();
        for (size_t i = records.size(); i-- > 0;)
        {
            while (b > 0 && blocks[b - 1].index > i)
                b--;
            if (b > 0 && blocks[b - 1].index == i)
                blocks[b - 1].clockHz = clock;
            if (records[i].kind == ExecRecord::kClock && records[i].page)
                clock = static_cast<uint32_t>(records[i].page) * 1000000u;
        }
    }
    return blocks;
}

/// endregion </Blocks>

/// region <Timeline: leases, histories, density>

Timeline::Timeline(EmulatorContext* context) : _context(context) {}

Timeline::~Timeline() = default;

void Timeline::SetClockForTest(Clock clock)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _clock = std::move(clock);
}

std::chrono::steady_clock::time_point Timeline::Now() const
{
    return _clock ? _clock() : std::chrono::steady_clock::now();
}

void Timeline::UpdateDeadlineLocked()
{
    int64_t soonest = 0;
    for (const auto& [owner, lease] : _leases)
        if (lease.timed)
        {
            const int64_t ticks = lease.until.time_since_epoch().count();
            soonest = soonest == 0 ? ticks : std::min(soonest, ticks);
        }
    _deadline.store(soonest, std::memory_order_relaxed);
}

bool Timeline::ExpireLocked(std::chrono::steady_clock::time_point now)
{
    bool dropped = false;
    for (auto it = _leases.begin(); it != _leases.end();)
    {
        if (it->second.timed && it->second.until <= now)
        {
            it = _leases.erase(it);
            dropped = true;
        }
        else
            ++it;
    }
    if (dropped)
    {
        UpdateDeadlineLocked();
        if (_leases.empty() && _armed.load(std::memory_order_relaxed))
        {
            ApplyLocked(false);
            _armed.store(false, std::memory_order_relaxed);
        }
        _revision.fetch_add(1, std::memory_order_acq_rel);
    }
    return dropped;
}

void Timeline::ApplyLocked(bool on)
{
    if (!_context)
        return;
    history::Registry& registry = _context->histories;
    if (!on)
    {
        // Only the ones this timeline switched on and nobody configured since (still the ring it set)
        for (const std::string& device : _switchedOn)
            registry.With(device, [](history::IDeviceHistory& h) {
                if (h.GetStats().config.mode == history::Mode::Ring)
                {
                    std::string error;
                    h.Configure(history::Config{}, error);
                }
            });
        _switchedOn.clear();
        return;
    }
    auto switchOn = [this](history::IDeviceHistory* h) {
        if (!h || h->GetStats().config.mode != history::Mode::Off)
            return;
        history::Config config;
        config.mode = history::Mode::Ring;
        std::string error;
        if (h->Configure(config, error))
        {
            const std::string device = h->Device();
            if (std::find(_switchedOn.begin(), _switchedOn.end(), device) == _switchedOn.end())
                _switchedOn.push_back(device);
        }
    };
    DebugManager* debug = _context->pDebugManager;
    if (debug && debug->GetPcHistory())
        switchOn(&debug->GetPcHistory()->Exec());
    for (DebugCpuId cpu : kCardCpus)
    {
        GeneralSoundCard* card = CardOf(_context, cpu);
        if (!HasCpu(card))
            continue;
        switchOn(card->execHistory());
        if (GSCommandLog* log = card->commandLog())
            switchOn(&log->History());
    }
}

bool Timeline::Arm(const std::string& owner, uint32_t leaseMs)
{
    std::lock_guard<std::mutex> lock(_mutex);
    const auto now = Now();
    ExpireLocked(now);
    Lease& lease = _leases[owner.empty() ? std::string("local") : owner];
    lease.timed = leaseMs != 0;
    lease.until = now + std::chrono::milliseconds(leaseMs);
    UpdateDeadlineLocked();
    const bool started = !_armed.load(std::memory_order_relaxed);
    // A renewal too: a card fitted since the first lease gets its histories
    ApplyLocked(true);
    if (started)
    {
        _epoch.fetch_add(1, std::memory_order_acq_rel);  // the emulation thread starts new baselines
        for (Ring& ring : _rings)
            ring = Ring();
        _armed.store(true, std::memory_order_relaxed);
        _revision.fetch_add(1, std::memory_order_acq_rel);
    }
    return started;
}

void Timeline::Release(const std::string& owner)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _leases.erase(owner.empty() ? std::string("local") : owner);
    UpdateDeadlineLocked();
    ExpireLocked(Now());
    if (_leases.empty() && _armed.load(std::memory_order_relaxed))
    {
        ApplyLocked(false);
        _armed.store(false, std::memory_order_relaxed);
        _revision.fetch_add(1, std::memory_order_acq_rel);
    }
}

void Timeline::Expire()
{
    std::lock_guard<std::mutex> lock(_mutex);
    ExpireLocked(Now());
}

StateNode Timeline::LeasesNode() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    const auto now = Now();
    StateNode leases = StateNode::Array();
    for (const auto& [owner, lease] : _leases)
    {
        StateNode entry = StateNode::Object();
        entry["owner"] = owner;
        int64_t left = 0;
        if (lease.timed)
            left = std::max<int64_t>(
                1, std::chrono::duration_cast<std::chrono::milliseconds>(lease.until - now).count());
        entry["left_ms"] = left;
        leases.push(std::move(entry));
    }
    return leases;
}

void Timeline::OnFrameEnd(uint64_t frame)
{
    // A timed lease ran out: one clock read per frame while armed
    const int64_t deadline = _deadline.load(std::memory_order_relaxed);
    if (deadline != 0 && Now().time_since_epoch().count() >= deadline)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        ExpireLocked(Now());
        if (!_armed.load(std::memory_order_relaxed))
            return;
    }

    // Armed anew since the last frame: new baselines (the samples are the emulation thread's alone)
    const uint64_t epoch = _epoch.load(std::memory_order_acquire);
    if (epoch != _sampleEpoch)
    {
        _sampleEpoch = epoch;
        _samples = {};
    }
    std::array<std::optional<DensityTick>, 2> ticks;
    for (DebugCpuId cpu : kCardCpus)
    {
        const size_t index = CardIndex(cpu);
        CardSample& sample = _samples[index];
        GeneralSoundCard* card = CardOf(_context, cpu);
        if (!card)
        {
            sample = CardSample();
            continue;
        }
        const GSActivityCounters& c = card->getActivityCounters();
        const uint64_t slept = card->cardStepsSlept();
        const bool baseline = sample.card == card;
        if (baseline)
        {
            DensityTick tick;
            tick.frame = frame;
            tick.hostCommands = Delta32(c.hostCommandsReceived, sample.commands);
            tick.hostDataWrites = Delta32(c.hostDataWritten, sample.dataWrites);
            tick.hostDataReads = Delta32(c.hostDataRead, sample.dataReads);
            tick.hostStatusPolls = Delta32(c.hostStatusPolls, sample.polls);
            tick.cardSteps = Delta32(c.cpuSteps, sample.steps);
            tick.cardIdleSteps = std::min(tick.cardSteps, Delta32(slept, sample.slept));
            tick.cardInterrupts = Delta32(c.interruptsAccepted, sample.ints);
            tick.cardNmis = Delta32(c.nmisAccepted, sample.nmis);
            tick.cardCommandsTaken = Delta32(c.cardCommandsRead, sample.taken);
            GSProfileActivity activity;
            if (card->profiler().On() && card->profiler().LastActivity(activity) &&
                activity.hostFrame != sample.profiledFrame)
            {
                sample.profiledFrame = activity.hostFrame;
                tick.profiled = true;
                tick.cycles = activity.cycles;
                tick.isrCycles = activity.isrCycles;
                tick.idleCycles = activity.idleCycles;
                tick.haltCycles = activity.haltCycles;
            }
            ticks[index] = tick;
        }
        sample.card = card;
        sample.steps = c.cpuSteps;
        sample.slept = slept;
        sample.ints = c.interruptsAccepted;
        sample.nmis = c.nmisAccepted;
        sample.taken = c.cardCommandsRead;
        sample.commands = c.hostCommandsReceived;
        sample.dataWrites = c.hostDataWritten;
        sample.dataReads = c.hostDataRead;
        sample.polls = c.hostStatusPolls;
    }
    if (!ticks[0] && !ticks[1])
        return;
    // Published under a short lock: readers copy the ring, never wait for the machine
    std::lock_guard<std::mutex> lock(_mutex);
    for (size_t i = 0; i < ticks.size(); i++)
    {
        if (!ticks[i])
            continue;
        Ring& ring = _rings[i];
        ring.ticks[ring.head] = *ticks[i];
        ring.head = (ring.head + 1) % kDensityFrames;
        ring.count = std::min(ring.count + 1, kDensityFrames);
    }
    _revision.fetch_add(1, std::memory_order_acq_rel);
}

std::vector<DensityTick> Timeline::Density(DebugCpuId cpu, size_t last) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    const Ring& ring = _rings[CardIndex(cpu)];
    const size_t count = last == 0 ? ring.count : std::min(last, ring.count);
    std::vector<DensityTick> out;
    out.reserve(count);
    for (size_t i = count; i > 0; i--)
        out.push_back(ring.ticks[(ring.head + kDensityFrames - i) % kDensityFrames]);
    return out;
}

/// endregion </Timeline>

/// region <The node>

bool CardCpuOf(EmulatorContext* context, const std::string& name, DebugCpuId& out)
{
    if (!name.empty())
    {
        if (!DebugManager::ParseCpuName(name, out) || !IsCardCpu(out))
            return false;
        return HasCpu(CardOf(context, out));
    }
    for (DebugCpuId cpu : kCardCpus)
        if (HasCpu(CardOf(context, cpu)))
        {
            out = cpu;
            return true;
        }
    return false;
}

namespace
{
std::string CardName(const GeneralSoundCard* card, DebugCpuId cpu)
{
    if (cpu == DebugCpuId::MultiSoundGS)
        return "MultiSound GS Z80";
    return card->implementation() == GSCardImplementation::NGS ? "NeoGS Z80" : "GS Z80";
}

StateNode DensityNode(const std::vector<DensityTick>& ticks)
{
    StateNode list = StateNode::Array();
    for (const DensityTick& tick : ticks)
    {
        StateNode node = StateNode::Object();
        tick.ToState(node);
        list.push(std::move(node));
    }
    return list;
}

/// The main lane's readout: "6.6 / 11 T · 60% · port write next" inside an instruction, else where it stands
std::string MainReadout(const MainPosition& main, double gapExact, uint16_t pc, bool atBoundary)
{
    if (main.known && main.timed && !main.onBoundary)
    {
        const double elapsed = std::max(0.0, static_cast<double>(main.tInInstruction) - gapExact);
        std::string total = std::to_string(main.totalT);
        if (main.totalTAlt)
            total += " or " + std::to_string(main.totalTAlt);
        const int percent = main.totalT ? static_cast<int>(std::lround(100.0 * elapsed / main.totalT)) : 0;
        std::string text = OneDecimal(elapsed) + " / " + total + " T · " + std::to_string(percent) + "%";
        if (main.waitT)
            text += " · +" + std::to_string(main.waitT) + " T wait";
        if (main.contended)
            text += " · contended";
        const int next = main.busCycle + 1;
        if (main.busCycle >= 0 && next < main.cycles.count)
            text += std::string(" · ") + Z80MCycleDecoder::Description(main.cycles.cycles[next].kind) + " next";
        else if (main.busCycle >= 0)
            text += std::string(" · ") + Z80MCycleDecoder::Description(main.cycles.cycles[main.busCycle].kind);
        return text;
    }
    if (main.known && main.frameEnd)
        return "between frames";
    return (atBoundary ? "at " : "in ") + Hex4(pc) + (atBoundary ? " (instruction start)" : "");
}
}  // namespace

namespace
{
/// The blocks of each lane and the markers, a line each (BuildNow's text after the summary)
std::string DetailText(const StateNode& node)
{
    std::string out;
    char line[160];
    if (const StateNode* recent = node.find("recent"))
        for (const auto& [lane, list] : recent->members)
        {
            out += "\n" + lane + " blocks (at T from NOW, width T):";
            for (const StateNode& item : list.items)
            {
                const StateNode* text = item.find("text");
                const StateNode* label = item.find("label");
                const StateNode* cycles = item.find("duration_cycles");
                const StateNode* durationT = item.find("duration_t");
                const double width = durationT ? (durationT->kind == StateNode::Kind::Double ? durationT->d
                                                                                              : static_cast<double>(durationT->i))
                                               : 0.0;
                const std::string& kind = item.find("kind")->s;
                const std::string body = text && !text->s.empty() ? text->s : std::string();
                const std::string what = kind == "instruction" ? body : body.empty() ? kind : kind + " " + body;
                std::snprintf(line, sizeof(line), "\n  %8.1f %6.1f%s  #%04X %s%s%s", item.find("at")->d, width,
                              item.find("open") ? "+" : " ", static_cast<unsigned>(item.find("pc")->i), what.c_str(),
                              label ? ("  ; " + label->s).c_str() : "",
                              cycles ? ("  (" + OneDecimal(cycles->d) + " cycles)").c_str() : "");
                out += line;
            }
        }
    if (const StateNode* next = node.find("next"))
        for (const auto& [lane, item] : next->members)
        {
            const StateNode* text = item.find("text");
            std::snprintf(line, sizeof(line), "\n%s next: #%04X %s", lane.c_str(),
                          static_cast<unsigned>(item.find("pc")->i), text ? text->s.c_str() : "");
            out += line;
        }
    if (const StateNode* markers = node.find("markers"))
        if (!markers->items.empty())
        {
            out += "\nmarkers:";
            for (const StateNode& m : markers->items)
            {
                std::snprintf(line, sizeof(line), "\n  %8.1f %-4s %-12s %s", m.find("at")->d, m.find("lane")->s.c_str(),
                              m.find("kind")->s.c_str(), m.find("text")->s.c_str());
                out += line;
            }
        }
    return out;
}
}  // namespace

StateNode BuildNow(Emulator& emulator, const Query& query)
{
    StateNode node = StateNode::Object();
    EmulatorContext* context = emulator.GetContext();
    DebugManager* debug = context ? context->pDebugManager : nullptr;
    Z80* z80 = context && context->pCore ? context->pCore->GetZ80() : nullptr;
    if (!debug || !z80)
    {
        node["available"] = false;
        node["message"] = std::string("emulator not available");
        return node;
    }
    Timeline* timeline = debug->GetTimeline();
    DebugCpuId cpu = DebugCpuId::GS;
    const bool hasCard = CardCpuOf(context, query.cpu, cpu);
    GeneralSoundCard* card = hasCard ? CardOf(context, cpu) : nullptr;
    const bool running = emulator.IsRunning() && !emulator.IsPaused();
    const double span = std::clamp(query.spanT, 1.0, kMaxSpanT);

    node["available"] = true;
    node["state"] = std::string(running ? "running" : emulator.IsPaused() ? "paused" : "stopped");
    node["armed"] = timeline && timeline->Armed();
    node["cpu"] = hasCard ? std::string(DebugCpuName(cpu)) : std::string();
    const uint32_t frameLength = context->GetFrameTStates();
    const double mainHz = context->config.frame_duration_us
                              ? static_cast<double>(frameLength) * 1e6 / context->config.frame_duration_us
                              : 0.0;

    // Lanes: main on top, the card below
    StateNode lanes = StateNode::Array();
    StateNode mainLane = StateNode::Object();
    mainLane["cpu"] = std::string("main");
    mainLane["name"] = std::string("Z80");
    mainLane["clock_hz"] = static_cast<int64_t>(std::llround(mainHz));
    mainLane["unit"] = std::string("T");
    mainLane["recording"] = debug->GetPcHistory() && debug->GetPcHistory()->Exec().On();
    const double unitsPerT = HasCpu(card) ? GSHostClock::unitsPerZxTact(context, card->cardUnitsPerSecond()) : 0.0;
    StateNode cardLane = StateNode::Object();
    if (card)
    {
        cardLane["cpu"] = std::string(DebugCpuName(cpu));
        cardLane["name"] = CardName(card, cpu);
        cardLane["clock_hz"] = static_cast<int64_t>(card->cardCpuClockHz());
        cardLane["unit"] = std::string("cycle");
        cardLane["units_per_second"] = card->cardUnitsPerSecond();
        cardLane["units_per_t"] = Round3(unitsPerT);
        cardLane["recording"] = card->execHistory() && card->execHistory()->On();
    }

    if (running)
    {
        // Published state only: the running machine is never parked for a read
        if (hasCard)
        {
            cardLane["readout"] = MHz(card->cardCpuClockHz());
            lanes.push(mainLane);
            lanes.push(cardLane);
        }
        else
            lanes.push(mainLane);
        node["lanes"] = lanes;
        const size_t ticks = std::min<size_t>(query.density ? query.density : kDensityFrames, kDensityFrames);
        node["density"] = timeline && hasCard ? DensityNode(timeline->Density(cpu, ticks)) : StateNode::Array();
        node["revision"] = timeline ? timeline->Revision() : uint64_t(0);
        node["text"] = std::string("running: ") + (node["armed"].b ? "density at the frame rate" : "not armed");
        return node;
    }

    // NOW: the card's moment (main T minus the gap); without a card, main's own
    const uint64_t frameCounter = context->emulatorState.frame_counter;
    const uint32_t mainT = z80->t;
    const StopPositions positions = emulator.StopPositionsNow();
    const CardLanePosition* lane = hasCard ? &positions.cards[CardIndex(cpu)] : nullptr;
    const bool laneKnown = lane && lane->known;
    const bool frameEnd = laneKnown && lane->frameEnd;
    const double gap = laneKnown ? lane->gapT : 0.0;
    const uint64_t nowFrame = frameCounter - (frameEnd ? 1 : 0);
    const double mainRel = static_cast<double>(mainT) + (frameEnd ? frameLength : 0);
    const double nowT = mainRel - gap;

    // Where main stands: a card's stop parked it inside the card's run (MainPosition), else at an instruction start
    MainPosition main;
    const Emulator::DebugStop stop = emulator.LastStop();
    if (emulator.IsParkedInsideCardRun() && stop.breakpoint.main.known)
        main = stop.breakpoint.main;
    const bool mainAtBoundary = !main.known || main.onBoundary || main.frameEnd;
    const uint16_t mainPc = main.known ? main.pc : z80->pc;

    mainLane["readout"] = MainReadout(main, gap, mainPc, mainAtBoundary);
    lanes.push(mainLane);
    if (card)
    {
        cardLane["readout"] = "cycle " + Grouped(laneKnown ? lane->cycle : 0) + " · " + MHz(card->cardCpuClockHz());
        lanes.push(cardLane);
    }
    node["lanes"] = lanes;

    StateNode now = StateNode::Object();
    now["frame"] = nowFrame;
    now["t"] = Round3(nowT);
    now["gap_t"] = Round3(gap);
    now["main_t"] = static_cast<int64_t>(mainT);
    now["main_frame"] = frameCounter;
    if (laneKnown)
    {
        now["card_units"] = lane->units;
        now["card_cycle"] = lane->cycle;
    }
    node["now"] = now;

    StateNode position = StateNode::Object();
    if (main.known)
        position["main"] = execution::MainPositionNode(main);
    position["main_text"] = mainLane["readout"].s;
    if (laneKnown)
    {
        position["card_text"] = cardLane["readout"].s;
        if (gap != 0.0)
            position["gap_t"] = Round3(gap);
    }
    node["position"] = position;

    StateNode window = StateNode::Object();
    window["span_t"] = span;
    window["from_t"] = -span;
    window["to_t"] = span;
    window["frame_t"] = static_cast<int64_t>(frameLength);
    node["window"] = window;

    // The lanes' sources
    LaneSource mainSource{context, nullptr, debug->GetLabelManager()};
    LaneSource cardSource{context, card, card ? debug->GetLabelManager(cpu) : nullptr};
    BreakpointListByID mainBreakpoints;
    if (BreakpointManager* bps = debug->GetBreakpointsManager())
        mainBreakpoints = bps->GetAllBreakpoints();
    BreakpointListByID cardBreakpoints;
    if (card)
        if (BreakpointManager* bps = debug->GetBreakpointsManager(cpu))
            cardBreakpoints = bps->GetAllBreakpoints();

    StateNode markers = StateNode::Array();
    auto addBreakpointMarkers = [&](const char* laneName, const BreakpointListByID& bps, double at, uint16_t pc,
                                    uint8_t kind, uint8_t page) {
        for (const auto& [id, bp] : bps)
            if (bp && ExecBreakpointAt(*bp, pc, kind, page))
            {
                StateNode marker = StateNode::Object();
                marker["lane"] = std::string(laneName);
                marker["kind"] = std::string("breakpoint");
                marker["at"] = Round3(at);
                marker["id"] = static_cast<int>(id);
                marker["pc"] = static_cast<int>(pc);
                marker["text"] = "breakpoint #" + std::to_string(id) + " at " + Hex4(pc);
                markers.push(std::move(marker));
            }
    };

    const size_t recentMax = std::clamp<size_t>(query.recent ? query.recent : 64, 1, kMaxRecent);
    StateNode recent = StateNode::Object();
    StateNode next = StateNode::Object();
    std::string textMain, textCard;

    // Main lane: T relative to NOW = the record's T relative to main's frame start - main's T + the gap
    {
        PcHistory* pcHistory = debug->GetPcHistory();
        const size_t want = static_cast<size_t>(std::min(65536.0, span * 0.7 + 64.0));
        std::vector<ExecRecord> records = Newest(pcHistory ? &pcHistory->Exec() : nullptr, want);
        // Main's records up to its time now, in its own frame (the newest lasts until main's T)
        const std::vector<Block> blocks =
            Blocks(records, static_cast<uint32_t>(frameCounter), frameLength, static_cast<int64_t>(mainT), 0);
        StateNode list = StateNode::Array();
        size_t first = blocks.size();
        while (first > 0 && blocks.size() - first < recentMax)
        {
            const Block& b = blocks[first - 1];
            const double start = static_cast<double>(b.start) - mainT + gap;
            if (start + static_cast<double>(b.duration) < -span)
                break;
            first--;
        }
        for (size_t i = first; i < blocks.size(); i++)
        {
            const Block& b = blocks[i];
            const ExecRecord& r = records[b.index];
            const double at = static_cast<double>(b.start) - mainT + gap;
            StateNode item = StateNode::Object();
            item["pc"] = static_cast<int>(r.pc);
            item["at"] = Round3(at);
            item["duration_t"] = b.duration;
            item["frame"] = static_cast<int64_t>(r.frame);
            item["t"] = static_cast<int64_t>(r.t);
            item["kind"] = std::string(EventName(r.kind));
            if (!r.IsEvent())
            {
                item["text"] = mainSource.Text(r.pc);
                const std::string label = mainSource.Label(r.pc);
                if (!label.empty())
                    item["label"] = label;
                if (r.IdleIterations())
                    item["idle_iterations"] = static_cast<int64_t>(r.idle);
                addBreakpointMarkers("main", mainBreakpoints, at, r.pc, r.kind, r.page);
            }
            else
            {
                StateNode marker = StateNode::Object();
                marker["lane"] = std::string("main");
                marker["kind"] = std::string(EventName(r.kind));
                marker["at"] = Round3(at);
                marker["pc"] = static_cast<int>(r.pc);
                marker["text"] = std::string(r.kind == ExecRecord::kNmi ? "NMI" : "INT") + " accepted at " + Hex4(r.pc);
                markers.push(std::move(marker));
            }
            if (b.open)
                item["open"] = true;
            list.push(std::move(item));
        }
        recent["main"] = list;

        // Main's next instruction: where it stands at an instruction start
        if (mainAtBoundary && !(main.known && main.frameEnd))
        {
            StateNode n = StateNode::Object();
            n["pc"] = static_cast<int>(z80->pc);
            n["at"] = Round3(gap);
            n["text"] = mainSource.Text(z80->pc);
            const std::string label = mainSource.Label(z80->pc);
            if (!label.empty())
                n["label"] = label;
            const MemoryPageDescriptor where =
                context->pMemory ? context->pMemory->MapZ80AddressToPhysicalPage(z80->pc) : MemoryPageDescriptor{};
            addBreakpointMarkers("main", mainBreakpoints, gap, z80->pc, static_cast<uint8_t>(where.mode),
                                 static_cast<uint8_t>(where.page));
            next["main"] = n;
            textMain = Hex4(z80->pc) + " " + n["text"].s;
        }
        else
            textMain = Hex4(mainPc) + " " + mainSource.Text(mainPc);
    }

    // Card lane: units relative to NOW = the record's units relative to the card's frame start - the card's units now
    if (laneKnown)
    {
        const int64_t units = lane->units;
        const int64_t frameUnits = std::max<int64_t>(1, card->cardFrameLength());
        const double perT = lane->unitsPerT > 0 ? lane->unitsPerT : 1.0;
        const size_t want = static_cast<size_t>(std::min(65536.0, span * 4.0 + 64.0));
        std::vector<ExecRecord> records = Newest(card->execHistory(), want);
        const std::vector<Block> blocks =
            Blocks(records, static_cast<uint32_t>(nowFrame), frameUnits, units, lane->clockHz);
        const double unitsPerSecond = card->cardUnitsPerSecond();
        StateNode list = StateNode::Array();
        size_t first = blocks.size();
        while (first > 0 && blocks.size() - first < recentMax)
        {
            const Block& b = blocks[first - 1];
            if (static_cast<double>(b.start + b.duration - units) / perT < -span)
                break;
            first--;
        }
        for (size_t i = first; i < blocks.size(); i++)
        {
            const Block& b = blocks[i];
            const ExecRecord& r = records[b.index];
            const double at = static_cast<double>(b.start - units) / perT;
            StateNode item = StateNode::Object();
            item["pc"] = static_cast<int>(r.pc);
            item["at"] = Round3(at);
            item["duration_t"] = Round3(static_cast<double>(b.duration) / perT);
            item["frame"] = static_cast<int64_t>(r.frame);
            item["units"] = static_cast<int64_t>(r.t);
            item["duration_units"] = b.duration;
            if (b.clockHz && unitsPerSecond > 0)
            {
                item["duration_cycles"] = Round3(static_cast<double>(b.duration) * b.clockHz / unitsPerSecond);
                item["clock_hz"] = static_cast<int64_t>(b.clockHz);
            }
            item["kind"] = std::string(EventName(r.kind));
            if (!r.IsEvent())
            {
                item["text"] = cardSource.Text(r.pc);
                const std::string label = cardSource.Label(r.pc);
                if (!label.empty())
                    item["label"] = label;
                if (r.IdleIterations())
                {
                    item["kind"] = std::string("idle");
                    item["idle_iterations"] = static_cast<int64_t>(r.idle);
                }
                addBreakpointMarkers(DebugCpuName(cpu), cardBreakpoints, at, r.pc, r.kind, r.page);
            }
            else
            {
                StateNode marker = StateNode::Object();
                marker["lane"] = std::string(DebugCpuName(cpu));
                marker["kind"] = std::string(EventName(r.kind));
                marker["at"] = Round3(at);
                marker["pc"] = static_cast<int>(r.pc);
                marker["text"] = std::string(r.kind == ExecRecord::kNmi ? "NMI" : "INT") + " accepted at " + Hex4(r.pc);
                markers.push(std::move(marker));
            }
            if (b.open)
                item["open"] = true;
            list.push(std::move(item));
        }
        recent[DebugCpuName(cpu)] = list;

        // The clock changes in the window (NeoGS): ◆ "12→24 MHz at cycle 118,400"
        for (const ExecRecord& r : records)
        {
            if (r.kind != ExecRecord::kClock)
                continue;
            const int64_t start = static_cast<int64_t>(r.t) +
                                  static_cast<int64_t>(static_cast<int32_t>(r.frame - static_cast<uint32_t>(nowFrame))) *
                                      frameUnits;
            const double at = static_cast<double>(start - units) / perT;
            if (at < -span || at > span || start > units)
                continue;
            const uint32_t fromHz = static_cast<uint32_t>(r.page) * 1000000u;
            const int64_t cycle = card->cardCycles(start);
            StateNode marker = StateNode::Object();
            marker["lane"] = std::string(DebugCpuName(cpu));
            marker["kind"] = std::string("clock_change");
            marker["at"] = Round3(at);
            marker["pc"] = static_cast<int>(r.pc);
            marker["from_hz"] = static_cast<int64_t>(fromHz);
            marker["to_hz"] = static_cast<int64_t>(r.idle);
            marker["units"] = start;
            marker["text"] = OneDecimal(fromHz / 1e6) + "→" + OneDecimal(r.idle / 1e6) + " MHz at cycle " + Grouped(cycle);
            markers.push(std::move(marker));
        }

        // The card's next instruction: it stands on a boundary at every stop
        StateNode n = StateNode::Object();
        n["pc"] = static_cast<int>(lane->pc);
        n["at"] = 0.0;
        n["text"] = cardSource.Text(lane->pc);
        const std::string label = cardSource.Label(lane->pc);
        if (!label.empty())
            n["label"] = label;
        const GeneralSoundCard::CardPage page = card->mapCardAddress(lane->pc);
        addBreakpointMarkers(DebugCpuName(cpu), cardBreakpoints, 0.0, lane->pc, static_cast<uint8_t>(page.kind), page.page);
        next[DebugCpuName(cpu)] = n;
        textCard = Hex4(lane->pc) + " " + n["text"].s;
    }

    // Host port accesses in the window (the command log): ⇄ at main's moment of the access
    if (card && card->commandLog())
    {
        const std::vector<GSCommandLogEntry> entries = card->commandLog()->History().Snapshot(256);
        for (const GSCommandLogEntry& e : entries)
        {
            const char* port = nullptr;
            const char* dir = "out";
            uint8_t value = e.value;
            switch (e.kind)
            {
                case GSCommandLogKind::Command: port = "#BB"; value = e.command; break;
                case GSCommandLogKind::Param:
                case GSCommandLogKind::Data: port = "#B3"; break;
                case GSCommandLogKind::Reply: port = "#B3"; dir = "in"; break;
                default: break;
            }
            if (!port || (e.flags & GSCommandLogFlags::kAutomation))
                continue;
            const double rel = static_cast<double>(e.mainT) +
                               static_cast<double>(static_cast<int32_t>(e.frame - static_cast<uint32_t>(frameCounter))) *
                                   frameLength;
            const double at = rel - mainT + gap;
            if (at < -span || at > span)
                continue;
            char text[48];
            std::snprintf(text, sizeof(text), "%s %s %s #%02X", dir[0] == 'o' ? "OUT" : "IN", port,
                          dir[0] == 'o' ? "<-" : "->", value);
            StateNode marker = StateNode::Object();
            marker["lane"] = std::string("main");
            marker["kind"] = std::string("host_port");
            marker["at"] = Round3(at);
            marker["frame"] = static_cast<int64_t>(e.frame);
            marker["t"] = static_cast<int64_t>(e.mainT);
            marker["pc"] = static_cast<int>(e.mainPc);
            marker["port"] = std::string(port);
            marker["dir"] = std::string(dir);
            marker["value"] = static_cast<int>(value);
            marker["entry"] = std::string(GSCommandLogKindName(e.kind));
            marker["text"] = text;
            markers.push(std::move(marker));
        }
    }

    // The breakpoint that stopped the machine, at its lane's moment (a main stop: main's time, NOW + gap)
    if (stop.reason == Emulator::DebugStop::Reason::Breakpoint)
    {
        const bool cardStop = stop.breakpoint.cpu != DebugCpuId::Main;
        bool present = false;
        for (const StateNode& m : markers.items)
            if (m.find("kind")->s == "breakpoint" && m.find("id")->i == stop.breakpoint.breakpointId &&
                m.find("lane")->s == (cardStop ? std::string(DebugCpuName(stop.breakpoint.cpu)) : std::string("main")))
                present = true;
        if (!present && (!cardStop || stop.breakpoint.cpu == cpu))
        {
            StateNode marker = StateNode::Object();
            marker["lane"] = cardStop ? std::string(DebugCpuName(stop.breakpoint.cpu)) : std::string("main");
            marker["kind"] = std::string("breakpoint");
            marker["at"] = cardStop ? 0.0 : Round3(gap);
            marker["id"] = static_cast<int>(stop.breakpoint.breakpointId);
            marker["pc"] = static_cast<int>(cardStop ? stop.breakpoint.cpuPc : stop.breakpoint.address);
            marker["text"] = "breakpoint #" + std::to_string(stop.breakpoint.breakpointId) + " (the stop)";
            markers.push(std::move(marker));
        }
    }

    std::stable_sort(markers.items.begin(), markers.items.end(),
                     [](const StateNode& a, const StateNode& b) { return a.find("at")->d < b.find("at")->d; });
    node["recent"] = recent;
    node["next"] = next;
    node["markers"] = markers;
    node["density"] = StateNode::Array();

    std::string text = "MAIN  " + textMain + "  " + mainLane["readout"].s + "\nNOW   frame " + std::to_string(nowFrame) +
                       " · T " + OneDecimal(nowT) + (gap != 0.0 ? " · gap " + OneDecimal(gap) + " T" : std::string());
    if (laneKnown)
        text += "\n" + std::string(DebugCpuName(cpu)) + "    " + textCard + "  " + cardLane["readout"].s;
    else if (!hasCard)
        text += "\n(no card CPU: the main lane only)";
    text += "\nrecent: main " + std::to_string(recent["main"].size()) +
            (laneKnown ? ", " + std::string(DebugCpuName(cpu)) + " " + std::to_string(recent[DebugCpuName(cpu)].size())
                       : std::string()) +
            " block(s) in ±" + OneDecimal(span) + " T; " + std::to_string(markers.size()) + " marker(s)" +
            (node["armed"].b ? "" : "; not armed: no blocks are recorded");
    text += DetailText(node);
    node["text"] = text;
    return node;
}

std::string TextOf(const StateNode& node)
{
    const StateNode* text = node.find("text");
    return text && text->kind == StateNode::Kind::String ? text->s : std::string();
}

Reply Read(Emulator* emulator, const Query& query)
{
    Reply reply;
    EmulatorContext* context = emulator ? emulator->GetContext() : nullptr;
    if (!context || !context->pDebugManager || !context->pDebugManager->GetTimeline())
    {
        reply.status = 400;
        reply.error = "emulator not available";
        return reply;
    }
    if (!(query.spanT > 0.0) || query.spanT > kMaxSpanT)
    {
        reply.status = 400;
        reply.error = "span is 1.." + std::to_string(static_cast<int>(kMaxSpanT)) + " T";
        return reply;
    }
    DebugCpuId cpu = DebugCpuId::GS;
    if (!query.cpu.empty() && !CardCpuOf(context, query.cpu, cpu))
    {
        reply.status = 404;
        reply.error = "no card CPU '" + query.cpu + "' (gs, ms: a card with a CPU in its slot)";
        return reply;
    }
    Timeline& timeline = *context->pDebugManager->GetTimeline();
    bool started = false;
    if (query.arm)
        started = timeline.Arm(query.client, query.leaseMs);
    else
        timeline.Expire();

    const bool running = emulator->IsRunning() && !emulator->IsPaused();
    if (running)
        reply.body = BuildNow(*emulator, query);
    else
    {
        // waitMs 0 (a GUI thread): one try - parked and nobody else holding the CPU, or stopped - never a wait
        const Emulator::CoherentMoment where =
            emulator->RunAtCoherentMoment([&]() { reply.body = BuildNow(*emulator, query); }, query.waitMs);
        if (where == Emulator::CoherentMoment::Busy)
        {
            reply.status = 503;
            reply.error = "no coherent moment within " + std::to_string(query.waitMs) +
                          " ms (the emulator is stepping, stopping or another client holds it); try again";
            return reply;
        }
    }
    reply.body["started_now"] = started;
    reply.body["leases"] = timeline.LeasesNode();
    return reply;
}

Reply SetArmed(Emulator* emulator, const std::string& client, bool on, uint32_t leaseMs)
{
    Reply reply;
    EmulatorContext* context = emulator ? emulator->GetContext() : nullptr;
    if (!context || !context->pDebugManager || !context->pDebugManager->GetTimeline())
    {
        reply.status = 400;
        reply.error = "emulator not available";
        return reply;
    }
    Timeline& timeline = *context->pDebugManager->GetTimeline();
    bool started = false;
    if (on)
        started = timeline.Arm(client, leaseMs);
    else
        timeline.Release(client);
    reply.body = StateNode::Object();
    reply.body["armed"] = timeline.Armed();
    reply.body["started_now"] = started;
    reply.body["leases"] = timeline.LeasesNode();
    return reply;
}

StateNode MonitorNode(Emulator& emulator)
{
    StateNode node = StateNode::Object();
    EmulatorContext* context = emulator.GetContext();
    Timeline* timeline = context && context->pDebugManager ? context->pDebugManager->GetTimeline() : nullptr;
    DebugCpuId cpu = DebugCpuId::GS;
    const bool hasCard = context && CardCpuOf(context, "", cpu);
    node["available"] = timeline != nullptr && hasCard;
    node["armed"] = timeline && timeline->Armed();
    node["state"] = std::string(emulator.IsPaused() ? "paused" : emulator.IsRunning() ? "running" : "stopped");
    node["revision"] = timeline ? timeline->Revision() : uint64_t(0);
    node["cpu"] = hasCard ? std::string(DebugCpuName(cpu)) : std::string();
    node["frame"] = context ? context->emulatorState.frame_counter : uint64_t(0);
    node["density"] = timeline && hasCard ? DensityNode(timeline->Density(cpu, 10)) : StateNode::Array();
    return node;
}

/// endregion </The node>

}  // namespace timeline
