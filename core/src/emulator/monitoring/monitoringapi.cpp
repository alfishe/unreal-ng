#include "emulator/monitoring/monitoringapi.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>

#include "3rdparty/message-center/messagecenter.h"
#include "common/processstats.h"
#include "debugger/analyzers/analyzermanager.h"
#include "debugger/analyzers/coverage/coverageanalyzer.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/debugmanager.h"
#include "debugger/labels/symbolcontrol.h"
#include "debugger/pchistory/pchistory.h"
#include "debugger/ttd/timetravelhooks.h"
#include "debugger/ttd/ttdsessiontypes.h"
#include "emulator/actions/surfacesapi.h"
#include "emulator/config.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/opcode_profiler.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/execution/companionregistry.h"
#include "emulator/io/controllers/controllermanager.h"
#include "emulator/io/controllers/controllertypes.h"
#include "emulator/mainloop.h"
#include "emulator/media/mediamanager.h"
#include "emulator/memory/memory.h"
#include "emulator/memory/memoryaccesstracker.h"
#include "emulator/monitoring/clientstats.h"
#include "emulator/platform.h"
#ifdef ENABLE_RECORDING
#include "recordingmanager.h"
#endif

namespace monitoring
{

namespace
{
using Clock = std::chrono::steady_clock;
/// A verb that is not a topic: the groups with their ids
constexpr const char* kBreakpointGroups = "breakpoint_groups";

/// Values a person reads: two decimals (and no churn of the last bits between two equal readings)
double Round2(double value)
{
    return std::round(value * 100.0) / 100.0;
}

/// region <Cache>

/// A value computed from a manager's state, kept while its key (a revision) stays and it is younger than `maxAge`:
/// a 50 Hz reader does not recount a list that did not change
struct Cached
{
    uint64_t key = 0;
    Clock::time_point at{};
    bool have = false;
    StateNode value;
};

struct Cache
{
    std::mutex mutex;
    std::map<std::string, Cached> entries;  ///< "name|instance"
    std::atomic<uint64_t> labelsGeneration{0};
    uint64_t labelsObserver = 0;
};

Cache& TheCache()
{
    static Cache* cache = new Cache();  // never destroyed: read until exit
    return *cache;
}

/// NC_LABEL_CHANGED carries no instance: any label change makes every instance's `debug.labels` stale
void EnsureObservers()
{
    static std::once_flag once;
    std::call_once(once, [] {
        Cache& cache = TheCache();
        cache.labelsObserver = MessageCenter::DefaultMessageCenter().AddObserver(
            NC_LABEL_CHANGED, ObserverCallbackFunc([](int, Message*) {
                TheCache().labelsGeneration.fetch_add(1, std::memory_order_relaxed);
            }));
    });
}

template <typename Compute>
StateNode CachedValue(const std::string& name, const std::string& instance, uint64_t key, double maxAgeSeconds,
                      Compute compute)
{
    Cache& cache = TheCache();
    const std::string slot = name + "|" + instance;
    const Clock::time_point now = Clock::now();
    {
        std::lock_guard<std::mutex> lock(cache.mutex);
        auto it = cache.entries.find(slot);
        if (it != cache.entries.end() && it->second.have && it->second.key == key &&
            std::chrono::duration<double>(now - it->second.at).count() < maxAgeSeconds)
            return it->second.value;
    }
    StateNode value = compute();  // outside the cache's lock: it may take a manager's
    std::lock_guard<std::mutex> lock(cache.mutex);
    Cached& entry = cache.entries[slot];
    entry.key = key;
    entry.at = now;
    entry.have = true;
    entry.value = value;
    return value;
}

/// endregion </Cache>

/// region <The process>

StateNode HostStats()
{
    const ProcessStats stats = ProcessStatsSampler::Instance().Read();
    StateNode node = StateNode::Object();
    node["available"] = stats.sample.ok;
    node["cpu_percent"] = Round2(stats.cpuPercent);
    node["cpu_percent_host"] = Round2(stats.cpuPercentOfHost);
    node["cores"] = static_cast<int64_t>(stats.cores);
    node["rss_bytes"] = static_cast<int64_t>(stats.sample.residentBytes);
    node["peak_rss_bytes"] = static_cast<int64_t>(stats.sample.peakResidentBytes);
    node["virtual_bytes"] = static_cast<int64_t>(stats.sample.virtualBytes);
    node["threads"] = static_cast<int64_t>(stats.sample.threads);
    node["cpu_time_s"] = Round2(static_cast<double>(stats.sample.cpuTimeUs) / 1e6);
    node["uptime_s"] = static_cast<int64_t>(stats.uptimeSeconds);
    node["interval_s"] = Round2(stats.intervalSeconds);
    return node;
}

StateNode HostClients()
{
    StateNode node = StateNode::Object();
    for (size_t i = 0; i < static_cast<size_t>(ClientPlane::Count); i++)
    {
        const ClientPlane plane = static_cast<ClientPlane>(i);
        const ClientCounts counts = ClientStats::Instance().Read(plane);
        StateNode& entry = node[ClientPlaneName(plane)];
        entry = StateNode::Object();
        entry["connections"] = std::max<int64_t>(counts.connections, 0);
        entry["opened"] = static_cast<int64_t>(counts.opened);
        entry["requests"] = static_cast<int64_t>(counts.requests);
    }
    node["companions"] = static_cast<int64_t>(companion::CompanionRegistry::Instance().List().size());
    node["instances"] = static_cast<int64_t>(EmulatorManager::GetInstance()->GetEmulatorIds().size());
    return node;
}

/// endregion </The process>

/// region <An instance>

const char* RunStateOf(Emulator& emulator)
{
    return !emulator.IsRunning() ? "stopped" : emulator.IsPaused() ? "paused" : "running";
}

StateNode EmulatorStats(Emulator& emulator)
{
    EmulatorContext* context = emulator.GetContext();
    StateNode node = StateNode::Object();
    node["state"] = RunStateOf(emulator);
    const MainLoop* loop = context->pMainLoop;
    EmulationStats stats = loop != nullptr ? loop->GetEmulationStats().Read() : EmulationStats{};
    if (emulator.IsPaused() || !emulator.IsRunning())
        stats.measuring = false;  // the last window's figures are of the run before the pause
    node["measuring"] = stats.measuring;
    node["fps"] = stats.measuring ? Round2(stats.fps) : 0.0;
    const CONFIG& config = context->config;
    node["nominal_fps"] = Round2(config.frame_duration_us > 0 ? 1e6 / config.frame_duration_us : 0.0);
    node["speed_percent"] = stats.measuring ? Round2(stats.speedPercent) : 0.0;
    StateNode& frame = node["frame_time_us"];
    frame = StateNode::Object();
    frame["avg"] = Round2(stats.frameWorkUsAvg);
    frame["max"] = Round2(stats.frameWorkUsMax);
    node["frame_budget_percent"] = Round2(stats.frameBudgetPercent);
    node["halted_percent"] = Round2(stats.haltedPercent);
    node["interrupts_per_frame"] = Round2(stats.interruptsPerFrame);
    // The machine's counters: while it runs as the meter took them at the window's end (the value changes twice a
    // second, not every frame: a topic sends it at that rate); else the plain fields (they do not move)
    const EmulatorState& state = context->emulatorState;
    node["frame"] = static_cast<int64_t>(stats.measuring ? stats.frame : state.frame_counter);
    node["t_states"] = static_cast<int64_t>(stats.measuring ? stats.tStates : state.t_states);
    node["turbo"] = static_cast<bool>(config.turbo_mode);
    StateNode& cpu = node["cpu"];
    cpu = StateNode::Object();
    cpu["mhz"] = Round2(static_cast<double>(state.current_z80_frequency) / 1e6);
    cpu["base_mhz"] = Round2(static_cast<double>(state.base_z80_frequency) / 1e6);
    cpu["multiplier"] = static_cast<int64_t>(state.current_z80_frequency_multiplier);
    cpu["host_multiplier"] = static_cast<int64_t>(state.next_z80_frequency_multiplier);
    cpu["hw_turbo_ratio"] = static_cast<int64_t>(state.hw_turbo_ratio_applied ? state.hw_turbo_ratio_applied : 1);
    return node;
}

StateNode BreakpointsSummary(Emulator& emulator)
{
    EmulatorContext* context = emulator.GetContext();
    BreakpointManager* manager = context->pDebugManager ? context->pDebugManager->GetBreakpointsManager() : nullptr;
    if (manager == nullptr)
    {
        StateNode node = StateNode::Object();
        node["available"] = false;
        return node;
    }
    // Recounted when the set changed or another breakpoint became the last hit (the id: an atomic, lock-free)
    const uint16_t last = manager->GetLastTriggeredBreakpointID();
    const uint64_t key = manager->Revision() * 0x10001u + last;
    return CachedValue("debug.breakpoints", emulator.GetId(), key, 2.0, [manager, last] {
        const BreakpointManager::BreakpointSummary summary = manager->GetSummary();
        StateNode node = StateNode::Object();
        node["available"] = true;
        node["total"] = static_cast<int64_t>(summary.total);
        node["active"] = static_cast<int64_t>(summary.active);
        node["inactive"] = static_cast<int64_t>(summary.total - summary.active);
        node["owned"] = static_cast<int64_t>(summary.owned);
        StateNode& kinds = node["by_kind"];
        kinds = StateNode::Object();
        kinds["memory"] = static_cast<int64_t>(summary.memory);
        kinds["exec"] = static_cast<int64_t>(summary.exec);
        kinds["read"] = static_cast<int64_t>(summary.read);
        kinds["write"] = static_cast<int64_t>(summary.write);
        kinds["port"] = static_cast<int64_t>(summary.port);
        kinds["port_in"] = static_cast<int64_t>(summary.portIn);
        kinds["port_out"] = static_cast<int64_t>(summary.portOut);
        kinds["keyboard"] = static_cast<int64_t>(summary.keyboard);
        kinds["nextreg"] = static_cast<int64_t>(summary.nextreg);
        kinds["beam"] = static_cast<int64_t>(summary.beam);
        kinds["copper"] = static_cast<int64_t>(summary.copper);
        kinds["magic"] = static_cast<int64_t>(summary.magic);
        StateNode& groups = node["groups"];
        groups = StateNode::Array();
        for (const BreakpointManager::BreakpointSummary::Group& group : summary.groups)
        {
            StateNode entry = StateNode::Object();
            entry["name"] = group.name;
            entry["total"] = static_cast<int64_t>(group.total);
            entry["active"] = static_cast<int64_t>(group.active);
            groups.push(std::move(entry));
        }
        if (last == BRK_INVALID)
            node["last_hit"] = StateNode();
        else
        {
            const BreakpointManager::BreakpointStatusInfo info = manager->GetLastTriggeredBreakpointInfo();
            StateNode hit = StateNode::Object();
            hit["id"] = static_cast<int64_t>(last);
            if (info.valid)
            {
                hit["type"] = info.type;
                hit["address"] = static_cast<int64_t>(info.address);
                hit["access"] = info.access;
                hit["group"] = info.group;
                hit["note"] = info.note;
            }
            else
                hit["removed"] = true;  // the breakpoint that stopped is gone
            node["last_hit"] = std::move(hit);
        }
        return node;
    });
}

/// Every group of the user breakpoints with its ids (the verb `breakpoint_groups`, GET .../breakpoints/groups)
StateNode BreakpointGroups(Emulator& emulator)
{
    EmulatorContext* context = emulator.GetContext();
    BreakpointManager* manager = context->pDebugManager ? context->pDebugManager->GetBreakpointsManager() : nullptr;
    StateNode node = StateNode::Object();
    StateNode& groups = node["groups"];
    groups = StateNode::Array();
    if (manager == nullptr)
        return node;
    for (const BreakpointManager::BreakpointSummary::Group& group : manager->GetSummary().groups)
    {
        StateNode entry = StateNode::Object();
        entry["name"] = group.name;
        entry["total"] = static_cast<int64_t>(group.total);
        entry["active"] = static_cast<int64_t>(group.active);
        StateNode& ids = entry["ids"];
        ids = StateNode::Array();
        for (uint16_t id : group.ids)
            ids.push(static_cast<int64_t>(id));
        groups.push(std::move(entry));
    }
    return node;
}

std::string BaseName(const std::string& path)
{
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

StateNode MediaSlots(Emulator& emulator)
{
    EmulatorContext* context = emulator.GetContext();
    MediaManager* media = context->pMediaManager;
    if (media == nullptr)
    {
        StateNode node = StateNode::Object();
        node["available"] = false;
        return node;
    }
    // The revision moves on insert / eject; the changed units of a disk being written do not move it: 1 s at most
    return CachedValue("media.slots", emulator.GetId(), media->Revision(), 1.0, [media] {
        StateNode node = StateNode::Object();
        node["available"] = true;
        StateNode& slots = node["slots"];
        slots = StateNode::Array();
        bool dirty = false;
        int64_t loaded = 0;
        for (const SlotInfo& info : media->List())
        {
            StateNode slot = StateNode::Object();
            slot["id"] = info.descriptor.id;
            slot["kind"] = info.tags.empty() ? std::string() : info.tags.front();
            slot["present"] = info.present;
            slot["pending"] = info.pending;
            slot["name"] = info.present ? BaseName(info.source) : std::string();
            slot["format"] = info.format;
            slot["dirty"] = info.dirty;
            slot["changed_units"] = static_cast<int64_t>(info.changedUnits);
            slot["protect"] = info.writeProtect;
            dirty = dirty || info.dirty;
            loaded += info.present ? 1 : 0;
            slots.push(std::move(slot));
        }
        node["loaded"] = loaded;
        node["dirty"] = dirty;
        return node;
    });
}

StateNode RecordingState([[maybe_unused]] Emulator& emulator)
{
    StateNode node = StateNode::Object();
#ifdef ENABLE_RECORDING
    RecordingManager* recording = emulator.GetContext()->pRecordingManager;
    if (recording == nullptr)
    {
        node["available"] = false;
        return node;
    }
    // Plain getters (no lock); the stats are written by the emulation thread once a frame: a read may be a frame old
    node["available"] = true;
    node["enabled"] = recording->isFeatureEnabled();
    const bool active = recording->IsRecording() || recording->IsPaused();
    node["recording"] = recording->IsRecording();
    node["paused"] = recording->IsPaused();
    const char* mode = "single_track";
    switch (recording->GetRecordingMode())
    {
        case RecordingMode::SingleTrack:
            mode = "single_track";
            break;
        case RecordingMode::MultiTrack:
            mode = "multi_track";
            break;
        case RecordingMode::ChannelSplit:
            mode = "channel_split";
            break;
        case RecordingMode::AudioOnly:
            mode = "audio_only";
            break;
    }
    node["mode"] = mode;
    const RecordingManager::RecordingStats stats = recording->GetStats();
    node["file"] = active ? recording->GetOutputFilename() : std::string();
    node["frames"] = static_cast<int64_t>(stats.framesRecorded);
    node["duration_s"] = Round2(stats.recordedDuration);
    node["emulated_s"] = Round2(stats.emulatedDuration);
    node["file_bytes"] = static_cast<int64_t>(stats.outputFileSize);
    node["fps"] = Round2(stats.recentFps);
#else
    node["available"] = false;
#endif
    return node;
}

StateNode Labels(Emulator& emulator)
{
    EmulatorContext* context = emulator.GetContext();
    if (context->pDebugManager == nullptr || context->pDebugManager->GetLabelManager() == nullptr)
    {
        StateNode node = StateNode::Object();
        node["available"] = false;
        return node;
    }
    EnsureObservers();
    // The symbol sets are copied to be counted: only after NC_LABEL_CHANGED, or every 2 s
    return CachedValue("debug.labels", emulator.GetId(),
                       TheCache().labelsGeneration.load(std::memory_order_relaxed), 2.0, [context] {
                           const SymbolReply sets = SymbolControl(context).Execute({"sets", {}});
                           StateNode node = StateNode::Object();
                           node["available"] = true;
                           const StateNode* labels = sets.body.find("labels");
                           node["total"] = labels != nullptr ? *labels : StateNode(static_cast<int64_t>(0));
                           StateNode& out = node["sets"];
                           out = StateNode::Array();
                           if (const StateNode* list = sets.body.find("sets"))
                               for (const StateNode& set : list->items)
                               {
                                   StateNode entry = StateNode::Object();
                                   for (const char* field : {"id", "title", "enabled", "symbols"})
                                       if (const StateNode* value = set.find(field))
                                           entry[field] = *value;
                                   out.push(std::move(entry));
                               }
                           return node;
                       });
}

StateNode TtdStatus(Emulator& emulator)
{
    EmulatorContext* context = emulator.GetContext();
    const ttd::ITimeTravelHooks* hooks = context->pTimeTravelHooks;
    StateNode node = StateNode::Object();
    if (hooks == nullptr)
    {
        node["available"] = false;
        return node;
    }
    // The cursor: a seqlock, every read. The session's sizes: the published summary, asked at most every 2 s (each
    // ask has the emulation thread publish a fresh one at a frame boundary: never 10 times a second for a monitor)
    const ttd::TTDCursor cursor = hooks->PublishedCursor();
    node["available"] = true;
    node["state"] = ttd::TTDSessionStateToString(cursor.state);
    node["recording"] = cursor.state == ttd::TTDSessionState::Recording;
    node["history"] = cursor.history;
    node["frame"] = static_cast<int64_t>(cursor.frame);
    node["first"] = static_cast<int64_t>(cursor.first);
    node["end"] = static_cast<int64_t>(cursor.end);
    node["frames"] = static_cast<int64_t>(cursor.end >= cursor.first ? cursor.end - cursor.first : 0);
    const StateNode session = CachedValue("ttd.status", emulator.GetId(), 0, 2.0, [hooks] {
        const ttd::TTDSessionInfo info = hooks->GetPublishedSessionInfo();
        StateNode sizes = StateNode::Object();
        sizes["checkpoints"] = static_cast<int64_t>(info.checkpointCount);
        sizes["memory_bytes"] = static_cast<int64_t>(info.sessionHeapBytes);
        sizes["history_bytes"] = static_cast<int64_t>(info.historyBytes);
        sizes["page_store_bytes"] = static_cast<int64_t>(info.pageStoreUsedBytes);
        sizes["bookmarks"] = static_cast<int64_t>(info.bookmarkCount);
        sizes["write_journal_records"] = static_cast<int64_t>(info.writeJournalRecords);
        sizes["input_events"] = static_cast<int64_t>(info.inputEventCount);
        sizes["evicted_checkpoints"] = static_cast<int64_t>(info.evictedCheckpoints);
        return sizes;
    });
    for (const auto& [name, value] : session.members)
        node[name] = value;
    return node;
}

const char* SessionStateName(ProfilerSessionState state)
{
    switch (state)
    {
        case ProfilerSessionState::Stopped:
            return "stopped";
        case ProfilerSessionState::Capturing:
            return "capturing";
        case ProfilerSessionState::Paused:
            return "paused";
    }
    return "unknown";
}

StateNode Analyzers(Emulator& emulator)
{
    // Session states only (atomics or enums the control threads write); never the counters the emulation thread
    // grows, never the PC history's report (that one runs on the emulation thread)
    EmulatorContext* context = emulator.GetContext();
    StateNode node = StateNode::Object();
    Z80* z80 = context->pCore ? context->pCore->GetZ80() : nullptr;
    OpcodeProfiler* opcode = z80 ? z80->GetOpcodeProfiler() : nullptr;
    node["opcode"] = opcode ? SessionStateName(opcode->GetSessionState()) : "unavailable";
    if (context->pMemory != nullptr)
    {
        MemoryAccessTracker& tracker = context->pMemory->GetAccessTracker();
        node["memory"] = SessionStateName(tracker.GetMemorySessionState());
        node["calltrace"] = SessionStateName(tracker.GetCalltraceSessionState());
    }
    else
    {
        node["memory"] = "unavailable";
        node["calltrace"] = "unavailable";
    }
    StateNode& coverage = node["coverage"];
    coverage = StateNode::Object();
    AnalyzerManager* analyzers = context->pDebugManager ? context->pDebugManager->GetAnalyzerManager() : nullptr;
    CoverageAnalyzer* analyzer = analyzers ? analyzers->getAnalyzer<CoverageAnalyzer>("coverage") : nullptr;
    coverage["available"] = analyzer != nullptr;
    coverage["recording"] = analyzer != nullptr && analyzer->isRecording();
    coverage["instructions"] = static_cast<int64_t>(analyzer != nullptr ? analyzer->getInstructionCount() : 0);
    PcHistory* history = context->pDebugManager ? context->pDebugManager->GetPcHistory() : nullptr;
    node["pchist"] = history != nullptr && history->IsArmed() ? "armed" : "off";
    return node;
}

StateNode Controllers(Emulator& emulator)
{
    controllers::ControllerManager* manager = emulator.GetContext()->pControllerManager;
    if (manager == nullptr)
    {
        StateNode node = StateNode::Object();
        node["available"] = false;
        return node;
    }
    return CachedValue("controllers.state", emulator.GetId(), manager->Revision(), 5.0, [manager] {
        StateNode node = StateNode::Object();
        node["available"] = true;
        StateNode& connectors = node["connectors"];
        connectors = StateNode::Array();
        int64_t plugged = 0;
        for (const controllers::Connector& connector : manager->Connectors())
        {
            StateNode entry = StateNode::Object();
            entry["id"] = connector.id;
            entry["kind"] = controllers::ConnectorKindName(connector.kind);
            entry["device"] = controllers::RetroDeviceKindName(connector.device);
            const bool on = connector.device != controllers::RetroDeviceKind::None;
            entry["plugged"] = on;
            entry["fixed"] = connector.fixed;
            plugged += on ? 1 : 0;
            connectors.push(std::move(entry));
        }
        node["plugged"] = plugged;
        return node;
    });
}

/// endregion </An instance>

/// region <Verbs>

Reply Fail(int status, const std::string& error, const std::string& message)
{
    Reply reply;
    reply.status = status;
    reply.body["ok"] = false;
    reply.body["error"] = error;
    reply.body["message"] = message;
    return reply;
}

std::string Text(const StateNode& args, const char* name)
{
    const StateNode* value = args.find(name);
    if (value == nullptr)
        return {};
    if (value->kind == StateNode::Kind::String)
        return value->s;
    if (value->kind == StateNode::Kind::Int)
        return std::to_string(value->i);
    return {};
}

/// An id or an index; "" = the followed instance. "" with `error` when none fits
std::string InstanceOf(const StateNode& args, std::string& error)
{
    const std::string named = Text(args, "instance");
    EmulatorManager* manager = EmulatorManager::GetInstance();
    if (named.empty())
        return actions::surfaces::DefaultInstance(error);
    if (manager->GetEmulator(named))
        return named;
    const std::vector<std::string> ids = manager->GetEmulatorIds();
    if (named.find_first_not_of("0123456789") == std::string::npos && named.size() < 6)
    {
        const size_t index = static_cast<size_t>(std::stoul(named));
        if (index < ids.size())
            return ids[index];
    }
    error = "no instance '" + named + "'";
    return {};
}

StateNode InstanceEntry(const std::string& id, size_t index, Emulator& emulator)
{
    StateNode entry = StateNode::Object();
    entry["instance"] = id;
    entry["index"] = static_cast<int64_t>(index);
    const TMemModel* model = Config::FindModelByEnum(emulator.GetContext()->config.mem_model);
    if (model != nullptr && model->ShortName != nullptr)
        entry["model"] = std::string(model->ShortName);
    else
        entry["model"] = StateNode();
    for (const std::string& name : InstanceNames())
        entry[name] = ReadInstance(name, emulator);
    return entry;
}

/// endregion </Verbs>
}  // namespace

const std::vector<std::string>& ProcessNames()
{
    static const std::vector<std::string> names = {"host.stats", "host.clients"};
    return names;
}

const std::vector<std::string>& InstanceNames()
{
    static const std::vector<std::string> names = {"emulator.stats", "debug.breakpoints", "media.slots",
                                                   "recording.state", "debug.labels",     "ttd.status",
                                                   "debug.analyzers", "controllers.state"};
    return names;
}

bool IsProcessName(const std::string& name)
{
    const std::vector<std::string>& names = ProcessNames();
    return std::find(names.begin(), names.end(), name) != names.end();
}

bool IsInstanceName(const std::string& name)
{
    const std::vector<std::string>& names = InstanceNames();
    return std::find(names.begin(), names.end(), name) != names.end();
}

StateNode ReadProcess(const std::string& name)
{
    if (name == "host.stats")
        return HostStats();
    if (name == "host.clients")
        return HostClients();
    return {};
}

StateNode ReadInstance(const std::string& name, Emulator& emulator)
{
    EmulatorContext* context = emulator.GetContext();
    if (context == nullptr || context->pCore == nullptr)
        return {};
    if (name == "emulator.stats")
        return EmulatorStats(emulator);
    if (name == "debug.breakpoints")
        return BreakpointsSummary(emulator);
    if (name == "media.slots")
        return MediaSlots(emulator);
    if (name == "recording.state")
        return RecordingState(emulator);
    if (name == "debug.labels")
        return Labels(emulator);
    if (name == "ttd.status")
        return TtdStatus(emulator);
    if (name == "debug.analyzers")
        return Analyzers(emulator);
    if (name == "controllers.state")
        return Controllers(emulator);
    return {};
}

const std::vector<std::string>& Verbs()
{
    static const std::vector<std::string> verbs = [] {
        std::vector<std::string> all = {"all"};
        all.insert(all.end(), ProcessNames().begin(), ProcessNames().end());
        all.insert(all.end(), InstanceNames().begin(), InstanceNames().end());
        all.push_back(kBreakpointGroups);
        return all;
    }();
    return verbs;
}

Reply Call(std::string_view verbView, const StateNode& args)
{
    const std::string verb(verbView.empty() ? std::string_view("all") : verbView);
    Reply reply;
    reply.body["ok"] = true;
    EmulatorManager* manager = EmulatorManager::GetInstance();
    if (verb == "all")
    {
        reply.body["host"] = HostStats();
        reply.body["clients"] = HostClients();
        StateNode& instances = reply.body["instances"];
        instances = StateNode::Array();
        const std::vector<std::string> ids = manager->GetEmulatorIds();
        std::string only;
        if (!Text(args, "instance").empty())
        {
            std::string error;
            only = InstanceOf(args, error);
            if (only.empty())
                return Fail(404, "Not Found", error);
        }
        for (size_t i = 0; i < ids.size(); i++)
        {
            if (!only.empty() && ids[i] != only)
                continue;
            if (std::shared_ptr<Emulator> emulator = manager->GetEmulator(ids[i]))
                instances.push(InstanceEntry(ids[i], i, *emulator));
        }
        return reply;
    }
    if (IsProcessName(verb))
    {
        for (auto& [name, value] : ReadProcess(verb).members)
            reply.body[name] = std::move(value);
        return reply;
    }
    if (!IsInstanceName(verb) && verb != kBreakpointGroups)
    {
        std::string names;
        for (const std::string& name : Verbs())
            names += (names.empty() ? "" : ", ") + name;
        return Fail(404, "Not Found", "no monitor '" + verb + "' (" + names + ")");
    }
    std::string error;
    const std::string id = InstanceOf(args, error);
    std::shared_ptr<Emulator> emulator = id.empty() ? nullptr : manager->GetEmulator(id);
    if (!emulator)
        return Fail(404, "Not Found", error.empty() ? "no instance" : error);
    reply.body["instance"] = id;
    StateNode read = verb == kBreakpointGroups ? BreakpointGroups(*emulator) : ReadInstance(verb, *emulator);
    for (auto& [name, value] : read.members)
        reply.body[name] = std::move(value);
    return reply;
}

}  // namespace monitoring
