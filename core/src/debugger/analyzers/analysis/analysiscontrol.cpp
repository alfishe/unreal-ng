#include "debugger/analyzers/analysis/analysiscontrol.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <optional>

#include "base/featuremanager.h"
#include "common/base64.h"
#include "debugger/analyzers/analysis/analysisjobs.h"
#include "debugger/analyzers/analysis/l2export.h"
#include "debugger/analyzers/analysis/l2gallery.h"
#include "debugger/analyzers/analysis/l3session.h"
#include "debugger/analyzers/analysis/replaytracer.h"
#include "debugger/debugmanager.h"
#include "debugger/disassembler/z80disasm.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/ttdcontrol.h"
#include "debugger/labels/labelmanager.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/memory/memoryaccesstracker.h"

namespace analysis
{

int ErrorHttpStatus(Error error)
{
    switch (error)
    {
        case Error::None:
            return 200;
        case Error::NotAvailable:
            return 503;
        case Error::BadRequest:
            return 400;
        case Error::NotFound:
            return 404;
        case Error::Conflict:
            return 409;
    }
    return 500;
}

const char* ErrorPhrase(Error error)
{
    switch (error)
    {
        case Error::None:
            return "OK";
        case Error::NotAvailable:
            return "Service Unavailable";
        case Error::BadRequest:
            return "Bad Request";
        case Error::NotFound:
            return "Not Found";
        case Error::Conflict:
            return "Conflict";
    }
    return "Internal Error";
}

StateNode Reply::ToValue() const
{
    if (!Ok())
    {
        StateNode value = StateNode::Object();
        value["error"] = ErrorPhrase(error);
        value["message"] = message;
        return value;
    }
    StateNode value = body;
    value["text"] = text;
    return value;
}

const std::vector<std::string>& Actions()
{
    static const std::vector<std::string> actions = {"start",   "stop",     "status", "build",      "cancel",
                                                      "explain", "query",    "export", "gallery",    "findings",
                                                      "review",  "experiment", "cover"};
    return actions;
}

const std::vector<std::string>& OptionsFor(const std::string& action)
{
    static const std::vector<std::string> none;
    static const std::vector<std::string> start = {"level", "mode", "record"};
    static const std::vector<std::string> build = {"frames", "path", "wait"};
    static const std::vector<std::string> explain = {"address", "page", "what"};
    static const std::vector<std::string> query = {"kind", "range", "limit"};
    static const std::vector<std::string> exportOptions = {"format", "path", "ranges", "width"};
    static const std::vector<std::string> gallery = {"address", "kind", "scale", "path", "limit"};
    static const std::vector<std::string> findings = {"op",    "kind",  "status",   "range", "limit", "format",
                                                      "path",  "claim", "evidence", "by",    "title"};
    static const std::vector<std::string> review = {"op",     "id",     "verdict", "label",      "title",
                                                    "comment", "kind",  "names",   "by",         "experiment",
                                                    "ranges", "reason", "offset",  "limit"};
    static const std::vector<std::string> experiment = {"spec", "path", "parallel", "finding"};
    static const std::vector<std::string> cover = {"budget", "per_branch", "frames", "parallel", "limit", "ranges"};
    if (action == "start")
        return start;
    if (action == "build")
        return build;
    if (action == "explain")
        return explain;
    if (action == "query")
        return query;
    if (action == "export")
        return exportOptions;
    if (action == "gallery")
        return gallery;
    if (action == "findings")
        return findings;
    if (action == "review")
        return review;
    if (action == "experiment")
        return experiment;
    if (action == "cover")
        return cover;
    return none;
}

bool ParseAddress(const std::string& text, uint16_t& address)
{
    std::string digits = text;
    int base = 10;
    if (!digits.empty() && (digits[0] == '$' || digits[0] == '#'))
    {
        digits = digits.substr(1);
        base = 16;
    }
    else if (digits.size() > 2 && digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X'))
    {
        digits = digits.substr(2);
        base = 16;
    }
    else if (digits.size() > 1 && (digits.back() == 'h' || digits.back() == 'H'))
    {
        digits.pop_back();
        base = 16;
    }
    if (digits.empty() || digits.size() > 6)
        return false;
    for (const char c : digits)
        if (base == 16 ? !std::isxdigit(static_cast<unsigned char>(c)) : !std::isdigit(static_cast<unsigned char>(c)))
            return false;
    const unsigned long value = std::strtoul(digits.c_str(), nullptr, base);
    if (value > 0xFFFF)
        return false;
    address = static_cast<uint16_t>(value);
    return true;
}

namespace
{

std::string Hex4(unsigned value)
{
    char buffer[8];
    std::snprintf(buffer, sizeof(buffer), "$%04X", value & 0xFFFFu);
    return buffer;
}

std::string Hex2(unsigned value)
{
    char buffer[8];
    std::snprintf(buffer, sizeof(buffer), "$%02X", value & 0xFFu);
    return buffer;
}

std::string Lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

bool ParseBool(const std::string& text, bool& value)
{
    const std::string v = Lower(text);
    if (v == "true" || v == "1" || v == "yes" || v == "on")
        value = true;
    else if (v == "false" || v == "0" || v == "no" || v == "off")
        value = false;
    else
        return false;
    return true;
}

bool ParseCount(const std::string& text, uint64_t limit, uint64_t& value)
{
    if (text.empty() || text.size() > 12 ||
        !std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isdigit(c) != 0; }))
        return false;
    value = std::strtoull(text.c_str(), nullptr, 10);
    return value <= limit;
}

Reply Refuse(Error error, const std::string& message)
{
    Reply reply;
    reply.error = error;
    reply.message = message;
    reply.text = message;
    return reply;
}

MemoryAccessTracker* TrackerOf(Emulator& emulator)
{
    Memory* memory = emulator.GetMemory();
    return memory != nullptr ? &memory->GetAccessTracker() : nullptr;
}

LabelManager* LabelsOf(Emulator& emulator)
{
    EmulatorContext* context = emulator.GetContext();
    return context != nullptr && context->pDebugManager != nullptr ? context->pDebugManager->GetLabelManager() : nullptr;
}

std::string LabelAt(LabelManager* labels, uint16_t address)
{
    if (labels == nullptr)
        return {};
    const std::shared_ptr<Label> label = labels->GetLabelByZ80Address(address);
    return label ? label->name : std::string();
}

/// "$8B70" or "$8B70 (main_loop)"
std::string AddressWithLabel(LabelManager* labels, uint16_t address)
{
    const std::string label = LabelAt(labels, address);
    return label.empty() ? Hex4(address) : Hex4(address) + " (" + label + ")";
}

const char* StateName(ProfilerSessionState state)
{
    switch (state)
    {
        case ProfilerSessionState::Capturing:
            return "capturing";
        case ProfilerSessionState::Paused:
            return "paused";
        case ProfilerSessionState::Stopped:
            break;
    }
    return "stopped";
}

const char* ModeName(TrackingMode mode)
{
    return mode == TrackingMode::PhysicalMemory ? "physical" : "z80";
}

/// "RAM 5", "ROM 1", "CACHE 0", "MISC"
std::string PageName(const MemoryAccessTracker& tracker, uint16_t page)
{
    const PageInfo info = tracker.GetPageInfo(page);
    static const char* const kTypes[] = {"RAM", "ROM", "CACHE", "MISC"};
    const char* type = info.pageType < 4 ? kTypes[info.pageType] : "PAGE";
    if (info.pageType == 3)
        return type;
    return std::string(type) + " " + std::to_string(info.logicalPageNum);
}

/// The absolute page of "ram:5" / "ram5" / "rom:1" / "12" (an absolute index)
bool ParsePage(const std::string& text, uint16_t& page)
{
    const std::string value = Lower(text);
    std::string digits = value;
    uint16_t first = 0;
    if (value.rfind("ram", 0) == 0 || value.rfind("rom", 0) == 0)
    {
        first = value[1] == 'o' ? static_cast<uint16_t>(MAX_RAM_PAGES + MAX_CACHE_PAGES + MAX_MISC_PAGES) : 0;
        digits = value.substr(3);
        if (!digits.empty() && (digits[0] == ':' || digits[0] == ' '))
            digits = digits.substr(1);
    }
    if (digits.empty() || digits.size() > 3 ||
        !std::all_of(digits.begin(), digits.end(), [](unsigned char c) { return std::isdigit(c) != 0; }))
        return false;
    const unsigned index = first + static_cast<unsigned>(std::strtoul(digits.c_str(), nullptr, 10));
    const unsigned limit = first == 0 && value.rfind("ram", 0) == 0 ? MAX_RAM_PAGES : MAX_PAGES;
    if (index >= limit)
        return false;
    page = static_cast<uint16_t>(index);
    return true;
}

StateNode StatusBody(Emulator& emulator, MemoryAccessTracker& tracker, std::string& text)
{
    StateNode body = StateNode::Object();
    const ProfilerSessionState state = tracker.GetMemorySessionState();
    const TrackerLevel level = tracker.GetLevel();
    const TrackingMode mode = tracker.GetTrackingMode();
    body["state"] = StateName(state);
    body["level"] = MemoryAccessTracker::LevelName(level);
    body["mode"] = ModeName(mode);
    EmulatorContext* context = emulator.GetContext();
    const uint64_t frame = context != nullptr ? context->emulatorState.frame_counter : 0;
    body["frame"] = frame;

    const MemoryAccessTracker::FactsSummary facts = tracker.SummarizeFacts();
    StateNode& node = body["facts"];
    node = StateNode::Object();
    node["allocated"] = facts.allocated;
    node["bytes"] = static_cast<uint64_t>(facts.bytes);
    node["z80_bytes_touched"] = static_cast<uint64_t>(facts.z80Touched);
    StateNode pages = StateNode::Array();
    std::string pageList;
    for (const uint16_t page : facts.pages)
    {
        StateNode entry = StateNode::Object();
        entry["page"] = static_cast<unsigned>(page);
        entry["name"] = PageName(tracker, page);
        pages.items.push_back(entry);
        pageList += (pageList.empty() ? "" : ", ") + PageName(tracker, page);
    }
    node["pages"] = pages;

    text = std::string("analysis: ") + StateName(state) + ", level " + MemoryAccessTracker::LevelName(level) + ", mode " +
           ModeName(mode) + ", frame " + std::to_string(frame);
    if (facts.allocated)
    {
        text += "; facts " + std::to_string(facts.bytes / 1024) + " KB";
        if (mode == TrackingMode::Z80AddressSpace)
            text += ", " + std::to_string(facts.z80Touched) + " Z80 addresses touched";
        else
            text += ", pages " + (pageList.empty() ? std::string("none") : pageList);
    }
    else
        text += "; no facts recorded";

    // The recording that feeds L1 and the last build
    ttd::TimeTravelController* ttd = context != nullptr ? context->pTimeTravelController : nullptr;
    body["recording"] = ttd != nullptr && ttd->IsRecording();
    if (ttd != nullptr && ttd->IsRecording())
        text += "; recording";
    const JobView view = L1Jobs::Instance().View(emulator.GetId());
    StateNode& l1 = body["l1"];
    l1 = StateNode::Object();
    l1["progress"] = view.ProgressNode();
    l1["ready"] = view.ReadyNode();
    if (view.id != 0)
    {
        text += "\nL1 build job " + std::to_string(view.id) + ": " + view.state;
        if (view.state == "running")
            text += " " + std::to_string(view.framesDone) + "/" + std::to_string(view.framesTotal) + " frames";
        if (!view.error.empty())
            text += " (" + view.error + ")";
    }
    if (view.ready)
        text += "\nL1 ready (job " + std::to_string(view.readyJob) + "): " + std::to_string(view.routines) +
                " routines, " + std::to_string(view.blocks) + " blocks, " + std::to_string(view.loops) + " loops, " +
                std::to_string(view.instructions) + " instructions over " + std::to_string(view.frames) + " frames" +
                (view.diverged != 0 ? ", " + std::to_string(view.diverged) + " of " + std::to_string(view.checked) +
                                          " keyframes diverged"
                                    : "");
    if (view.ready)
        text += "\nL2: " + std::to_string(view.regions) + " regions, " + std::to_string(view.conflicts) +
                " conflicts (analysis query, analysis export)";
    return body;
}

Reply Status(Emulator& emulator, MemoryAccessTracker& tracker)
{
    Reply reply;
    reply.body = StatusBody(emulator, tracker, reply.text);
    return reply;
}

Reply Start(Emulator& emulator, MemoryAccessTracker& tracker, const Options& options)
{
    TrackerLevel level = TrackerLevel::Facts;
    if (const auto it = options.find("level"); it != options.end())
    {
        const std::string value = Lower(it->second);
        if (value == "counters")
            level = TrackerLevel::Counters;
        else if (value != "facts")
            return Refuse(Error::BadRequest, "level: facts or counters");
    }
    bool record = false;
    if (const auto it = options.find("record"); it != options.end() && !ParseBool(it->second, record))
        return Refuse(Error::BadRequest, "record: true or false");
    TrackingMode mode = TrackingMode::Z80AddressSpace;
    if (const auto it = options.find("mode"); it != options.end())
    {
        const std::string value = Lower(it->second);
        if (value == "physical" || value == "phys")
            mode = TrackingMode::PhysicalMemory;
        else if (value != "z80")
            return Refuse(Error::BadRequest, "mode: z80 or physical");
    }

    // The features the counters need, as the memory profiler's start does (each switch holds the CPU itself)
    if (FeatureManager* features = emulator.GetFeatureManager())
    {
        features->setFeature(Features::kDebugMode, true);
        features->setFeature(Features::kMemoryTracking, true);
    }
    tracker.UpdateFeatureCache();

    {
        // The emulation thread reads the level, the mode, the facts and the Z80's context store without locks
        Emulator::CpuHold hold(emulator, "analysis start");
        tracker.StopMemorySession();
        tracker.SetTrackingMode(mode);
        tracker.SetLevel(level);
        tracker.StartMemorySession();
    }
    if (!tracker.IsMemoryCapturing())
        return Refuse(Error::Conflict, "memory tracking could not start (debug mode or memory tracking is off)");

    if (record)
    {
        // The TTD recording L1 replays (the engine: TimeTravelController, through the TTD verbs)
        EmulatorContext* context = emulator.GetContext();
        if (FeatureManager* features = emulator.GetFeatureManager())
            features->setFeature(Features::kTimeTravel, true);
        if (context == nullptr || context->pTimeTravelController == nullptr)
            return Refuse(Error::NotAvailable, "no time travel engine on this instance: start without record");
        const bool already = context->pTimeTravelController->IsRecording();
        const ttd::TTDReply started = ttd::TTDControl(context).Execute({"start", {}});
        if (!started.Ok())
            return Refuse(Error::Conflict, "the recording did not start: " + started.message);
        if (!already)
            L1Jobs::Instance().NoteRecording(emulator.GetId(), true);
    }

    Reply reply = Status(emulator, tracker);
    reply.text = std::string("Analysis started: level ") + MemoryAccessTracker::LevelName(level) + ", mode " +
                 ModeName(mode) + (record ? ", recording" : "") + ". Run the program, then " +
                 (record ? "build L1 or " : "") + "explain an address.\n" + reply.text;
    return reply;
}

Reply Stop(Emulator& emulator, MemoryAccessTracker& tracker)
{
    {
        Emulator::CpuHold hold(emulator, "analysis stop");
        tracker.StopMemorySession();
    }
    // The recording 'start record=true' began ends with the session (the history stays for build)
    if (L1Jobs::Instance().OwnsRecording(emulator.GetId()))
    {
        L1Jobs::Instance().NoteRecording(emulator.GetId(), false);
        if (EmulatorContext* context = emulator.GetContext())
            if (context->pTimeTravelController != nullptr && context->pTimeTravelController->IsRecording())
                (void)ttd::TTDControl(context).Execute({"stop", {}});
    }
    Reply reply = Status(emulator, tracker);
    reply.text = "Analysis stopped; the facts stay readable until the next start.\n" + reply.text;
    return reply;
}

/// The "written" part: "values $24..$3F (3+ distinct)"
std::string DistinctName(uint8_t distinct)
{
    switch (distinct)
    {
        case 0:
            return "none";
        case 1:
            return "1";
        case 2:
            return "2";
        default:
            return "3+";
    }
}

Reply Build(Emulator& emulator, const Options& options)
{
    ReplayOptions replay;
    if (const auto it = options.find("frames"); it != options.end() && !ParseCount(it->second, 100000000, replay.maxFrames))
        return Refuse(Error::BadRequest, "frames: the most frames to replay, an integer (0: the whole recording)");
    uint64_t wait = 0;
    if (const auto it = options.find("wait"); it != options.end() && !ParseCount(it->second, 30000, wait))
        return Refuse(Error::BadRequest, "wait: milliseconds to wait for the build, 0 to 30000");
    const auto pathIt = options.find("path");
    const std::string path = pathIt != options.end() ? pathIt->second : std::string();

    const std::string id = emulator.GetId();
    L1Jobs& jobs = L1Jobs::Instance();
    if (const JobView view = jobs.View(id); view.state == "running")
        return Refuse(Error::Conflict, "a build is running (job " + std::to_string(view.id) +
                                           "): wait for analysis.ready, or cancel it");
    TraceBundle bundle;
    std::string error;
    bool captured = false;
    {
        // The session file is written from the source's history: its CPU stands still meanwhile
        Emulator::CpuHold hold(emulator, "analysis build");
        captured = TraceBundle::Capture(emulator, bundle, error);
    }
    if (!captured)
        return Refuse(Error::Conflict, error);
    jobs.NoteRecording(id, false);  // Capture ended the recording
    const StateNode bundleNode = bundle.ToStateNode();
    const uint64_t frames = bundle.endFrame > bundle.firstFrame ? bundle.endFrame - bundle.firstFrame : 0;
    uint64_t job = 0;
    if (!jobs.Start(id, std::move(bundle), std::move(replay), path, job, error))
        return Refuse(Error::Conflict, error);
    if (wait != 0)
        jobs.Wait(id, static_cast<int>(wait));

    const JobView view = jobs.View(id);
    Reply reply;
    reply.body["job"] = job;
    reply.body["state"] = view.state;
    reply.body["bundle"] = bundleNode;
    reply.body["progress"] = view.ProgressNode();
    reply.body["ready"] = view.ReadyNode();
    reply.text = "L1 build job " + std::to_string(job) + ": replaying " + std::to_string(frames) +
                 " recorded frames on an analysis instance";
    if (view.state == "running")
        reply.text += "; follow analysis.progress, the result comes with analysis.ready (or 'analysis status')";
    else if (view.state == "done")
        reply.text += "; done: " + std::to_string(view.routines) + " routines, " + std::to_string(view.blocks) +
                      " blocks, " + std::to_string(view.loops) + " loops, " + std::to_string(view.instructions) +
                      " instructions" + (view.path.empty() ? "" : ", written to " + view.path) +
                      ". Explain a routine's address for its fact sheet";
    else
        reply.text += "; " + view.state + (view.error.empty() ? "" : ": " + view.error);
    return reply;
}

Reply Cancel(Emulator& emulator, MemoryAccessTracker& tracker)
{
    if (!L1Jobs::Instance().Cancel(emulator.GetId()))
        return Refuse(Error::Conflict, "no build is running");
    L1Jobs::Instance().Wait(emulator.GetId(), 5000);
    Reply reply = Status(emulator, tracker);
    reply.text = "Build cancelled.\n" + reply.text;
    return reply;
}

Reply ExplainRoutine(Emulator& emulator, const L1Result& l1, const Routine& routine, uint16_t address, LabelManager* labels)
{
    Reply reply;
    reply.body = l1.RoutineNode(routine);
    reply.body["what"] = "routine";
    reply.body["address"] = Hex4(address);
    Z80Disassembler disassembler(emulator.GetContext());
    reply.text = l1.FactSheet(
        routine,
        [&disassembler](const Instruction& ins) {
            const std::vector<uint8_t> bytes(ins.bytes, ins.bytes + sizeof(ins.bytes));
            uint8_t length = 0;
            return disassembler.disassembleSingleCommand(bytes, ins.z80, &length);
        },
        [labels](uint16_t z80) { return LabelAt(labels, z80); });
    reply.body["sheet_bytes"] = static_cast<uint64_t>(reply.text.size());
    return reply;
}

Reply Explain(Emulator& emulator, MemoryAccessTracker& tracker, const Options& options)
{
    const auto addressIt = options.find("address");
    if (addressIt == options.end() || addressIt->second.empty())
        return Refuse(Error::BadRequest, "explain needs an address ($80BC, 0x80BC, #80BC, decimal or a label)");

    LabelManager* labels = LabelsOf(emulator);
    uint16_t address = 0;
    if (!ParseAddress(addressIt->second, address))
    {
        const std::shared_ptr<Label> label = labels != nullptr ? labels->GetLabelByName(addressIt->second) : nullptr;
        if (!label)
            return Refuse(Error::NotFound, "no address or label '" + addressIt->second + "'");
        address = label->address;
    }

    // what: byte (L0), routine (L1) or auto (the routine when L1 has one running there)
    std::string what = "auto";
    if (const auto it = options.find("what"); it != options.end() && !it->second.empty())
    {
        what = Lower(it->second);
        if (what != "auto" && what != "byte" && what != "routine")
            return Refuse(Error::BadRequest, "what: byte, routine or auto");
    }
    if (what != "byte")
    {
        const std::shared_ptr<const L1Result> l1 = L1Jobs::Instance().Result(emulator.GetId());
        int routinePage = -1;
        if (const auto it = options.find("page"); it != options.end() && !it->second.empty())
        {
            uint16_t parsed = 0;
            if (!ParsePage(it->second, parsed))
                return Refuse(Error::BadRequest, "page: an absolute page index, ram:N or rom:N");
            routinePage = parsed;
        }
        const Routine* routine = l1 ? l1->RoutineAtZ80(address, routinePage) : nullptr;
        if (routine != nullptr)
            return ExplainRoutine(emulator, *l1, *routine, address, labels);
        if (what == "routine")
            return l1 ? Refuse(Error::NotFound, "no L1 routine runs at " + Hex4(address))
                      : Refuse(Error::Conflict, "no L1 yet: 'analysis start record=true', run the program, then "
                                                "'analysis build'");
    }

    if (tracker.GetLevel() != TrackerLevel::Facts)
        return Refuse(Error::Conflict, "no access facts: the tracker level is counters; run 'analysis start' (level facts) first");

    const TrackingMode mode = tracker.GetTrackingMode();
    const bool physical = mode == TrackingMode::PhysicalMemory;
    Memory* memory = emulator.GetMemory();

    uint16_t page = 0;
    uint16_t offset = static_cast<uint16_t>(address & 0x3FFF);
    if (physical)
    {
        if (const auto it = options.find("page"); it != options.end() && !it->second.empty())
        {
            if (!ParsePage(it->second, page))
                return Refuse(Error::BadRequest, "page: an absolute page index, ram:N or rom:N");
        }
        else
            page = memory->GetPageForBank(static_cast<uint8_t>(address >> 14));
    }
    else if (options.count("page") && !options.at("page").empty())
        return Refuse(Error::BadRequest, "page: only in physical mode (analysis start mode physical)");

    ByteFacts facts{};
    const bool known = physical ? tracker.ReadPhysicalFacts(page, offset, facts) : tracker.ReadZ80Facts(address, facts);
    const bool touched = known && facts.kinds != 0;

    Reply reply;
    StateNode& body = reply.body;
    body["what"] = "byte";
    body["address"] = Hex4(address);
    body["mode"] = ModeName(mode);
    const std::string where = physical ? PageName(tracker, page) : memory->GetBankNameForAddress(address);
    if (physical)
    {
        StateNode& node = body["page"];
        node = StateNode::Object();
        node["page"] = static_cast<unsigned>(page);
        node["name"] = where;
        node["offset"] = Hex4(offset);
    }
    else
        body["bank"] = where;
    const std::string label = LabelAt(labels, address);
    body["label"] = label;
    body["accessed"] = touched;

    // The counters next to the facts (the same session)
    uint32_t reads = 0, writes = 0, executes = 0;
    if (physical)
    {
        const MemoryAccessTracker::CountersView view = tracker.PhysCountersView();
        const size_t index = static_cast<size_t>(page) * PAGE_SIZE + offset;
        if (view.reads && index < view.size)
        {
            reads = view.reads[index];
            writes = view.writes[index];
            executes = view.executes[index];
        }
    }
    else
    {
        reads = tracker.GetZ80AddressReadCount(address);
        writes = tracker.GetZ80AddressWriteCount(address);
        executes = tracker.GetZ80AddressExecuteCount(address);
    }
    StateNode& counters = body["counters"];
    counters = StateNode::Object();
    counters["reads"] = reads;
    counters["writes"] = writes;
    counters["executes"] = executes;

    std::string head = Hex4(address) + (label.empty() ? "" : " (" + label + ")") + " [" + where + "]";
    if (!touched)
    {
        body["kinds"] = StateNode::Array();
        body["kinds_mask"] = 0;
        reply.text = head + ": not accessed since the analysis started";
        return reply;
    }

    StateNode kinds = StateNode::Array();
    for (const std::string& name : MemoryAccessFacts::KindList(facts.kinds))
        kinds.items.emplace_back(name);
    body["kinds"] = kinds;
    body["kinds_mask"] = static_cast<unsigned>(facts.kinds);
    body["first_frame"] = static_cast<uint64_t>(facts.firstFrame);
    body["last_frame"] = static_cast<uint64_t>(facts.lastFrame);

    const uint8_t readKinds = AccessKind::DataRead | AccessKind::StackRead;
    const uint8_t writeKinds = AccessKind::DataWrite | AccessKind::StackWrite;
    const bool read = (facts.kinds & readKinds) != 0;
    const bool written = (facts.kinds & writeKinds) != 0;
    const bool multiReader = (facts.valueClass & valueclass::kMultiReader) != 0;
    const bool multiWriter = (facts.valueClass & valueclass::kMultiWriter) != 0;
    const uint8_t distinct = facts.valueClass & valueclass::kDistinctMask;

    StateNode reader;
    if (read)
    {
        reader = StateNode::Object();
        reader["pc"] = Hex4(facts.lastReader);
        reader["label"] = LabelAt(labels, facts.lastReader);
    }
    body["last_reader"] = reader;
    body["multi_reader"] = multiReader;
    StateNode writer;
    if (written)
    {
        writer = StateNode::Object();
        writer["pc"] = Hex4(facts.lastWriter);
        writer["label"] = LabelAt(labels, facts.lastWriter);
    }
    body["last_writer"] = writer;
    body["multi_writer"] = multiWriter;
    StateNode& values = body["written"];
    values = StateNode::Object();
    values["distinct"] = DistinctName(distinct);
    if (distinct != 0)
    {
        values["min"] = Hex2(facts.minWritten);
        values["max"] = Hex2(facts.maxWritten);
    }

    std::string text = head + ": " + MemoryAccessFacts::KindNames(facts.kinds);
    if (written)
    {
        text += "\n  written by " + AddressWithLabel(labels, facts.lastWriter) +
                (multiWriter ? " (last of several writers)" : " (the only writer)");
        text += ", values " + (distinct == 1 ? Hex2(facts.minWritten)
                                             : Hex2(facts.minWritten) + ".." + Hex2(facts.maxWritten)) +
                " (" + DistinctName(distinct) + " distinct)";
    }
    if (read)
        text += "\n  read by " + AddressWithLabel(labels, facts.lastReader) +
                (multiReader ? " (last of several readers)" : " (the only reader)");
    text += "\n  frames " + std::to_string(facts.firstFrame) + ".." + std::to_string(facts.lastFrame);
    text += "\n  counters: " + std::to_string(reads) + " reads, " + std::to_string(writes) + " writes, " +
            std::to_string(executes) + " executes";
    reply.text = text;
    return reply;
}


/// "8000-83FF", "$8000-$83FF", "0x8000..0x83FF", one address: that byte
bool ParseRange(const std::string& text, ExportRange& range)
{
    std::string a = text, b;
    size_t at = text.find("..");
    size_t skip = 2;
    if (at == std::string::npos)
    {
        at = text.find('-');
        skip = 1;
    }
    if (at != std::string::npos)
    {
        a = text.substr(0, at);
        b = text.substr(at + skip);
    }
    const auto parse = [](std::string t, uint16_t& v) {
        if (!t.empty() && t[0] != '$' && t[0] != '#' && !(t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) &&
            !(t.back() == 'h' || t.back() == 'H'))
            t = "$" + t;  // a range is hex
        return ParseAddress(t, v);
    };
    if (!parse(a, range.start))
        return false;
    range.end = range.start;
    if (!b.empty() && !parse(b, range.end))
        return false;
    return range.end >= range.start;
}

bool ParseRanges(const std::string& text, std::vector<ExportRange>& ranges)
{
    size_t from = 0;
    while (from <= text.size())
    {
        const size_t comma = text.find(',', from);
        const std::string part = text.substr(from, comma == std::string::npos ? std::string::npos : comma - from);
        ExportRange range;
        if (!ParseRange(part, range))
            return false;
        ranges.push_back(range);
        if (comma == std::string::npos)
            break;
        from = comma + 1;
    }
    std::sort(ranges.begin(), ranges.end(), [](const ExportRange& x, const ExportRange& y) { return x.start < y.start; });
    for (size_t i = 1; i < ranges.size(); ++i)
        if (ranges[i].start <= ranges[i - 1].end)
            return false;
    return !ranges.empty();
}

Reply NoStructure()
{
    return Refuse(Error::Conflict, "no L2 yet: 'analysis start record=true', run the program, then 'analysis build'");
}

/// analysis query [kind range limit]: the claims of one kind (all: every claim; conflicts: the clashing pairs)
Reply Query(Emulator& emulator, const Options& options)
{
    const std::shared_ptr<const L2Result> l2 = L1Jobs::Instance().Structure(emulator.GetId());
    if (!l2)
        return NoStructure();
    std::string kindText = "all";
    if (const auto it = options.find("kind"); it != options.end() && !it->second.empty())
        kindText = Lower(it->second);
    RegionKind kind = RegionKind::Data;
    const bool all = kindText == "all";
    const bool conflicts = kindText == "conflicts";
    if (!all && !conflicts && !ParseRegionKind(kindText, kind))
    {
        std::string list;
        for (int k = 0; k <= static_cast<int>(RegionKind::Untouched); ++k)
            list += std::string(list.empty() ? "" : ", ") + RegionKindName(static_cast<RegionKind>(k));
        return Refuse(Error::BadRequest, "kind: all, conflicts or a region kind (" + list + ")");
    }
    ExportRange range{0x0000, 0xFFFF};
    if (const auto it = options.find("range"); it != options.end() && !it->second.empty() && !ParseRange(it->second, range))
        return Refuse(Error::BadRequest, "range: start-end in hex (8000-83FF, $8000-$83FF)");
    uint64_t limit = 200;
    if (const auto it = options.find("limit"); it != options.end() && !ParseCount(it->second, 100000, limit))
        return Refuse(Error::BadRequest, "limit: the most regions to answer, 0 to 100000");

    const std::vector<Region>& regions = l2->store.Regions();
    std::vector<size_t> picked;
    std::vector<RegionStore::Conflict> clashes;
    const auto inRange = [&](const Region& r) { return r.Z80End() >= range.start && r.z80 <= range.end; };
    if (conflicts)
    {
        for (const RegionStore::Conflict& c : l2->store.Conflicts())
            if (inRange(regions[c.a]) || inRange(regions[c.b]))
                clashes.push_back(c);
    }
    else
        for (size_t i = 0; i < regions.size(); ++i)
            if ((all || regions[i].kind == kind) && inRange(regions[i]))
                picked.push_back(i);

    Reply reply;
    reply.body["kind"] = kindText;
    reply.body["range"] = Hex4(range.start) + "-" + Hex4(range.end);
    StateNode list = StateNode::Array();
    char line[200];
    std::string text;
    const auto row = [&](const Region& r) {
        const std::string name = !r.label.empty() ? r.label : r.title;
        std::snprintf(line, sizeof(line), "%04X-%04X %-13s %-14s %4u %-8s %s\n", r.z80, r.Z80End(),
                      RegionKindName(r.kind), DetectorName(r.detector), static_cast<unsigned>(r.confidence),
                      RegionStatusName(r.status), name.c_str());
        text += line;
    };
    uint64_t shown = 0;
    const uint64_t total = conflicts ? clashes.size() : picked.size();
    if (conflicts)
    {
        for (const RegionStore::Conflict& c : clashes)
        {
            if (shown++ >= limit)
                break;
            StateNode pair = StateNode::Object();
            pair["a"] = RegionNode(regions[c.a]);
            pair["b"] = RegionNode(regions[c.b]);
            list.push(pair);
            row(regions[c.a]);
            text += "  vs ";
            row(regions[c.b]);
        }
    }
    else
        for (const size_t i : picked)
        {
            if (shown++ >= limit)
                break;
            list.push(RegionNode(regions[i]));
            row(regions[i]);
        }
    reply.body[conflicts ? "conflicts" : "regions"] = list;
    reply.body["count"] = total;
    reply.body["truncated"] = total > limit;
    reply.text = "L2 " + kindText + " in " + Hex4(range.start) + "-" + Hex4(range.end) + ": " + std::to_string(total) +
                 (conflicts ? " conflicts" : " regions") +
                 (total > limit ? " (first " + std::to_string(limit) + ")" : "") + "\n" +
                 (text.empty() ? "" : "range     kind          detector       conf status   label / title\n" + text);
    return reply;
}

/// analysis export format [path ranges width]: the draft (ctl, skool, fields, geometry, layout, symbols, map)
Reply ExportDraft(Emulator& emulator, const Options& options)
{
    const std::shared_ptr<const L1Result> l1 = L1Jobs::Instance().Result(emulator.GetId());
    const std::shared_ptr<const L2Result> l2 = L1Jobs::Instance().Structure(emulator.GetId());
    if (!l1 || !l2)
        return NoStructure();
    std::string formats;
    for (const std::string& f : ExportFormats())
        formats += (formats.empty() ? "" : ", ") + f;
    const auto formatIt = options.find("format");
    if (formatIt == options.end() || formatIt->second.empty())
        return Refuse(Error::BadRequest, "export needs a format (" + formats + ")");
    const std::string format = Lower(formatIt->second);
    ExportOptions exportOptions;
    if (const auto it = options.find("ranges"); it != options.end() && !it->second.empty() &&
                                                !ParseRanges(it->second, exportOptions.ranges))
        return Refuse(Error::BadRequest, "ranges: hex ranges without overlap, comma separated (5900-59FF,8000-FFFF)");
    if (const auto it = options.find("width"); it != options.end())
    {
        uint64_t width = 0;
        if (!ParseCount(it->second, 64, width) || width == 0)
            return Refuse(Error::BadRequest, "width: bytes per DEFB line, 1 to 64");
        exportOptions.defbWidth = static_cast<uint16_t>(width);
    }
    Z80Disassembler disassembler(emulator.GetContext());
    exportOptions.disassemble = [&disassembler](const uint8_t* bytes, uint16_t z80) {
        const std::vector<uint8_t> code(bytes, bytes + 4);
        uint8_t length = 0;
        return disassembler.disassembleSingleCommand(code, z80, &length);
    };
    // Plain addresses: the draft names its own entries (@label), and a debugger label in an operand (a ROM symbol,
    // "VR-CONT-$0003 ($0800)") is no assembler text
    const Z80Disassembler::NoLabelsScope plainAddresses;
    // L3: what the review named and noted, and the coverage notes
    exportOptions.notes = L3Sessions::Instance().Notes(emulator.GetId());
    const ExportResult result = Export(*l1, *l2, format, exportOptions);
    if (!result.ok)
        return Refuse(Error::BadRequest, result.error);

    Reply reply;
    reply.body["format"] = format;
    reply.body["entries"] = static_cast<uint64_t>(result.entries);
    reply.body["bytes"] = static_cast<uint64_t>(result.text.size());
    std::string ranges;
    for (const ExportRange& r : exportOptions.ranges.empty() ? std::vector<ExportRange>{{0x4000, 0xFFFF}} : exportOptions.ranges)
        ranges += (ranges.empty() ? "" : ",") + Hex4(r.start).substr(1) + "-" + Hex4(r.end).substr(1);
    reply.body["ranges"] = ranges;
    const auto pathIt = options.find("path");
    if (pathIt != options.end() && !pathIt->second.empty())
    {
        std::ofstream file(pathIt->second, std::ios::binary | std::ios::trunc);
        if (file)
            file << result.text;
        if (!file)
            return Refuse(Error::Conflict, "could not write " + pathIt->second);
        reply.body["path"] = pathIt->second;
        reply.text = "L2 " + format + " (" + ranges + "): " + std::to_string(result.entries) + " entries, " +
                     std::to_string(result.text.size()) + " bytes written to " + pathIt->second;
        return reply;
    }
    reply.body["path"] = "";
    reply.body["content"] = result.text;
    // The text carries the file up to 16 KB (an agent may read only the text); "content" always all of it
    constexpr size_t kTextMax = 16384;
    reply.text = "L2 " + format + " (" + ranges + "): " + std::to_string(result.entries) + " entries, " +
                 std::to_string(result.text.size()) + " bytes" +
                 (result.text.size() > kTextMax ? " (the first 16 KB here, all in content; or give a path)" : "") + "\n" +
                 result.text.substr(0, kTextMax);
    return reply;
}

/// analysis gallery [address kind scale path limit]: the graphics L2 found; with an address, its PNG preview
Reply Gallery(Emulator& emulator, const Options& options)
{
    const std::shared_ptr<const L1Result> l1 = L1Jobs::Instance().Result(emulator.GetId());
    const std::shared_ptr<const L2Result> l2 = L1Jobs::Instance().Structure(emulator.GetId());
    if (!l1 || !l2)
        return NoStructure();
    std::optional<RegionKind> kind;
    if (const auto it = options.find("kind"); it != options.end() && !it->second.empty() && Lower(it->second) != "all")
    {
        RegionKind parsed = RegionKind::Data;
        if (!ParseRegionKind(Lower(it->second), parsed) ||
            (parsed != RegionKind::Font && parsed != RegionKind::Sprite && parsed != RegionKind::Tile &&
             parsed != RegionKind::Screen && parsed != RegionKind::LevelMap))
            return Refuse(Error::BadRequest, "kind: all, font, sprite, tile, screen or level_map");
        kind = parsed;
    }
    uint64_t scale = 2;
    if (const auto it = options.find("scale"); it != options.end() && (!ParseCount(it->second, 8, scale) || scale == 0))
        return Refuse(Error::BadRequest, "scale: an integer zoom, 1 to 8");
    uint64_t limit = 200;
    if (const auto it = options.find("limit"); it != options.end() && !ParseCount(it->second, 100000, limit))
        return Refuse(Error::BadRequest, "limit: the most regions to answer, 0 to 100000");
    std::vector<const Region*> regions;
    for (const Region* r : GalleryRegions(*l2))
        if (!kind || r->kind == *kind)
            regions.push_back(r);
    const auto shape = [](const Region& r) {
        const Geometry& g = r.geometry;
        std::string text = std::to_string(g.width) + "x" + std::to_string(g.height);
        if (g.frames > 1)
            text += "x" + std::to_string(g.frames);
        if (g.mask)
            text += " mask";
        if (g.prefix != 0)
            text += " +" + std::to_string(g.prefix);
        if (g.pitch != 0)
            text += " pitch " + std::to_string(g.pitch);
        return text;
    };

    const auto addressIt = options.find("address");
    if (addressIt == options.end() || addressIt->second.empty())
    {
        Reply reply;
        StateNode list = StateNode::Array();
        std::string text;
        char line[200];
        uint64_t shown = 0;
        for (const Region* r : regions)
        {
            if (shown++ >= limit)
                break;
            list.push(RegionNode(*r));
            const std::string name = !r->label.empty() ? r->label : r->title;
            std::snprintf(line, sizeof(line), "%04X-%04X %-10s %-16s %-10s %4u %s\n", r->z80, r->Z80End(),
                          RegionKindName(r->kind), shape(*r).c_str(), DetectorName(r->detector),
                          static_cast<unsigned>(r->confidence), name.c_str());
            text += line;
        }
        reply.body["regions"] = list;
        reply.body["count"] = static_cast<uint64_t>(regions.size());
        reply.body["truncated"] = regions.size() > limit;
        reply.text = "L2 gallery: " + std::to_string(regions.size()) + " graphics regions" +
                     (regions.size() > limit ? " (first " + std::to_string(limit) + ")" : "") +
                     " (address=<start> draws one as a PNG)\n" +
                     (text.empty() ? "" : "range     kind       shape            detector   conf label / title\n" + text);
        return reply;
    }

    uint16_t address = 0;
    if (!ParseAddress(addressIt->second, address))
    {
        LabelManager* labels = LabelsOf(emulator);
        const std::shared_ptr<Label> label = labels != nullptr ? labels->GetLabelByName(addressIt->second) : nullptr;
        if (!label)
            return Refuse(Error::NotFound, "no address or label '" + addressIt->second + "'");
        address = label->address;
    }
    // The region starting there, else the first one covering it
    const Region* picked = nullptr;
    for (const Region* r : regions)
        if (r->z80 == address)
        {
            picked = r;
            break;
        }
    for (const Region* r : regions)
        if (picked == nullptr && r->z80 <= address && r->Z80End() >= address)
            picked = r;
    if (picked == nullptr)
        return Refuse(Error::NotFound, "no graphics region at " + Hex4(address) +
                                           " (gallery without an address lists them)");
    GalleryOptions galleryOptions;
    galleryOptions.scale = static_cast<uint16_t>(scale);
    GalleryImage image;
    std::string error;
    if (!RenderGallery(*l1, *picked, galleryOptions, image, error))
        return Refuse(Error::Conflict, error);

    Reply reply;
    reply.body["region"] = RegionNode(*picked);
    reply.body["width"] = static_cast<uint64_t>(image.width);
    reply.body["height"] = static_cast<uint64_t>(image.height);
    reply.body["frames"] = static_cast<uint64_t>(image.frames);
    reply.body["scale"] = scale;
    reply.body["bytes"] = static_cast<uint64_t>(image.png.size());
    reply.body["mime_type"] = "image/png";
    const std::string what = "L2 gallery " + Hex4(picked->z80) + "-" + Hex4(picked->Z80End()) + " " +
                             RegionKindName(picked->kind) + " " + shape(*picked) + ": " +
                             std::to_string(image.frames) + (image.frames == 1 ? " frame, " : " frames, ") +
                             std::to_string(image.width) + "x" + std::to_string(image.height) + " PNG, " +
                             std::to_string(image.png.size()) + " bytes";
    const auto pathIt = options.find("path");
    if (pathIt != options.end() && !pathIt->second.empty())
    {
        std::ofstream file(pathIt->second, std::ios::binary | std::ios::trunc);
        if (file)
            file.write(reinterpret_cast<const char*>(image.png.data()), static_cast<std::streamsize>(image.png.size()));
        if (!file)
            return Refuse(Error::Conflict, "could not write " + pathIt->second);
        reply.body["path"] = pathIt->second;
        reply.body["png"] = "";
        reply.text = what + " written to " + pathIt->second;
        return reply;
    }
    reply.body["path"] = "";
    reply.body["png"] = base64::Encode(image.png);
    reply.text = what + " (base64 in png; or give a path)";
    return reply;
}

}  // namespace

bool ParseZ80Range(const std::string& text, ExportRange& range)
{
    return ParseRange(text, range);
}

bool ParseZ80Ranges(const std::string& text, std::vector<ExportRange>& ranges)
{
    return ParseRanges(text, ranges);
}

Reply Execute(Emulator& emulator, const std::string& action, const Options& options)
{
    const std::vector<std::string>& actions = Actions();
    if (std::find(actions.begin(), actions.end(), action) == actions.end())
        return Refuse(Error::BadRequest, "unknown action '" + action +
                                              "' (start, stop, status, build, cancel, explain, query, export, gallery, findings, "
                                              "review, experiment, cover)");
    const std::vector<std::string>& known = OptionsFor(action);
    for (const auto& [name, value] : options)
    {
        (void)value;
        if (std::find(known.begin(), known.end(), name) == known.end())
        {
            std::string list;
            for (const std::string& option : known)
                list += (list.empty() ? "" : ", ") + option;
            return Refuse(Error::BadRequest, action + ": unknown option '" + name + "'" +
                                                 (list.empty() ? std::string(" (it takes none)") : " (options: " + list + ")"));
        }
    }

    MemoryAccessTracker* tracker = TrackerOf(emulator);
    if (tracker == nullptr)
        return Refuse(Error::NotAvailable, "no memory tracker on this machine");

    if (action == "start")
        return Start(emulator, *tracker, options);
    if (action == "stop")
        return Stop(emulator, *tracker);
    if (action == "status")
        return Status(emulator, *tracker);
    if (action == "build")
        return Build(emulator, options);
    if (action == "cancel")
        return Cancel(emulator, *tracker);
    if (action == "query")
        return Query(emulator, options);
    if (action == "export")
        return ExportDraft(emulator, options);
    if (action == "gallery")
        return Gallery(emulator, options);
    if (action == "findings" || action == "review" || action == "experiment" || action == "cover")
        return ExecuteReview(emulator, action, options);
    return Explain(emulator, *tracker, options);
}

}  // namespace analysis
