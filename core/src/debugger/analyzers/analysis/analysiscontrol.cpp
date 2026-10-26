#include "debugger/analyzers/analysis/analysiscontrol.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <optional>

#include "base/featuremanager.h"
#include "debugger/debugmanager.h"
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
    static const std::vector<std::string> actions = {"start", "stop", "status", "explain"};
    return actions;
}

const std::vector<std::string>& OptionsFor(const std::string& action)
{
    static const std::vector<std::string> none;
    static const std::vector<std::string> start = {"level", "mode"};
    static const std::vector<std::string> explain = {"address", "page"};
    if (action == "start")
        return start;
    if (action == "explain")
        return explain;
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

    Reply reply = Status(emulator, tracker);
    reply.text = std::string("Analysis started: level ") + MemoryAccessTracker::LevelName(level) + ", mode " +
                 ModeName(mode) + ". Run the program, then explain an address.\n" + reply.text;
    return reply;
}

Reply Stop(Emulator& emulator, MemoryAccessTracker& tracker)
{
    {
        Emulator::CpuHold hold(emulator, "analysis stop");
        tracker.StopMemorySession();
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

}  // namespace

Reply Execute(Emulator& emulator, const std::string& action, const Options& options)
{
    const std::vector<std::string>& actions = Actions();
    if (std::find(actions.begin(), actions.end(), action) == actions.end())
        return Refuse(Error::BadRequest, "unknown action '" + action + "' (start, stop, status, explain)");
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
    return Explain(emulator, *tracker, options);
}

}  // namespace analysis
