#include "debugger/analyzers/profile/profilecontrol.h"

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>

#include "common/filehelper.h"
#include "debugger/analyzers/analyzermanager.h"
#include "debugger/analyzers/profile/profileanalyzer.h"
#include "debugger/debugmanager.h"
#include "debugger/disassembler/z80disasm.h"
#include "debugger/labels/labelmanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/state/statenodejson.h"

namespace profile
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
    return "Internal Server Error";
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

const std::vector<std::string>& Operations()
{
    static const std::vector<std::string> kOps = {"start", "stop", "reset", "mark", "status", "report", "frames", "export"};
    return kOps;
}

const std::vector<std::string>& OptionsFor(const std::string& op)
{
    static const std::map<std::string, std::vector<std::string>> kOptions = {
        {"start", {"stack", "beam", "frames", "ring_kib", "stream", "reset"}},
        {"stop", {}},
        {"reset", {}},
        {"mark", {"name", "drop"}},
        {"status", {}},
        {"report", {"since", "top", "sort", "callers", "beam"}},
        {"frames", {"last", "top"}},
        {"export", {"format", "path", "since"}},
    };
    static const std::vector<std::string> kNone;
    auto it = kOptions.find(op);
    return it == kOptions.end() ? kNone : it->second;
}

namespace
{
Reply Refuse(Error error, std::string message)
{
    Reply reply;
    reply.error = error;
    reply.message = std::move(message);
    return reply;
}

bool ParseBool(const std::string& text, bool& out)
{
    if (text == "true" || text == "1" || text == "on" || text == "yes")
        out = true;
    else if (text == "false" || text == "0" || text == "off" || text == "no")
        out = false;
    else
        return false;
    return true;
}

bool ParseUnsigned(const std::string& text, uint64_t& out)
{
    if (text.empty() || text.size() > 19 || !std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; }))
        return false;
    out = std::stoull(text);
    return true;
}

std::string Hex4(uint16_t value)
{
    char buffer[8];
    std::snprintf(buffer, sizeof(buffer), "$%04X", value);
    return buffer;
}

std::string PageName(uint16_t page)
{
    if (page < MAX_RAM_PAGES)
        return "RAM " + std::to_string(page);
    if (page < MAX_RAM_PAGES + MAX_CACHE_PAGES)
        return "CACHE " + std::to_string(page - MAX_RAM_PAGES);
    if (page < MAX_RAM_PAGES + MAX_CACHE_PAGES + MAX_MISC_PAGES)
        return "MISC";
    return "ROM " + std::to_string(page - MAX_RAM_PAGES - MAX_CACHE_PAGES - MAX_MISC_PAGES);
}

bool IsRomPage(uint16_t page)
{
    return page >= MAX_RAM_PAGES + MAX_CACHE_PAGES + MAX_MISC_PAGES;
}

std::string Permille(uint64_t part, uint64_t whole)
{
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%5.1f%%", whole ? 100.0 * static_cast<double>(part) / static_cast<double>(whole) : 0.0);
    return buffer;
}

ProfileAnalyzer* Find(Emulator& emulator)
{
    DebugManager* debug = emulator.GetDebugManager();
    AnalyzerManager* manager = debug ? debug->GetAnalyzerManager() : nullptr;
    return manager ? manager->getAnalyzer<ProfileAnalyzer>(ProfileAnalyzer::kId) : nullptr;
}

/// Names for the routines: a label at the entry (of its page when the label says one), else sub_XXXX; banked code
/// (two pages ran in one 16K window) carries its page: sub_C000_r3, sub_0100_rom1
class Namer
{
public:
    Namer(Emulator& emulator, const Snapshot& s)
    {
        DebugManager* debug = emulator.GetDebugManager();
        _labels = debug ? debug->GetLabelManager() : nullptr;
        std::map<int, std::set<uint16_t>> pagesPerWindow;
        for (size_t id = 1; id < s.routines.size(); ++id)
            pagesPerWindow[s.routines[id].address >> 14].insert(s.routines[id].page);
        for (const auto& [window, pages] : pagesPerWindow)
            if (pages.size() > 1)
                _banked.insert(window);
    }
    std::string Name(const RoutineView& r) const
    {
        if (r.id == 0)
            return "(outside any call)";
        if (_labels)
            if (auto label = _labels->GetLabelByZ80Address(r.address))
                if (Fits(*label, r.page))
                    return label->name;
        char buffer[32];
        if (_banked.count(r.address >> 14))
        {
            if (IsRomPage(r.page))
                std::snprintf(buffer, sizeof(buffer), "sub_%04X_rom%u", r.address,
                              static_cast<unsigned>(r.page - MAX_RAM_PAGES - MAX_CACHE_PAGES - MAX_MISC_PAGES));
            else
                std::snprintf(buffer, sizeof(buffer), "sub_%04X_r%u", r.address, static_cast<unsigned>(r.page));
        }
        else
            std::snprintf(buffer, sizeof(buffer), "sub_%04X", r.address);
        return buffer;
    }

private:
    static bool Fits(const Label& label, uint16_t page)
    {
        const bool rom = IsRomPage(page);
        if (label.bank == UINT16_MAX)
            return label.isROM() == rom || label.address >= 0x4000;
        if (label.isROM() != rom)
            return false;
        const uint16_t logical = rom ? static_cast<uint16_t>(page - MAX_RAM_PAGES - MAX_CACHE_PAGES - MAX_MISC_PAGES) : page;
        return label.bank == logical;
    }
    LabelManager* _labels = nullptr;
    std::set<int> _banked;
};

std::string KindsText(const RoutineView& r)
{
    static const char* kNames[EntryKinds] = {"call", "rst", "interrupt", "dispatch"};
    std::string text;
    for (uint32_t k = 0; k < EntryKinds; ++k)
        if (r.entries[k])
            text += (text.empty() ? "" : ",") + std::string(kNames[k]);
    if (r.jumpIns)
        text += (text.empty() ? "" : ",") + std::string("jump");
    return text.empty() ? "root" : text;
}

uint64_t Entries(const RoutineView& r)
{
    uint64_t n = 0;
    for (uint32_t k = 0; k < EntryKinds; ++k)
        n += r.entries[k];
    return n;
}

/// The attribution notes of one routine, readable
std::string Notes(const RoutineView& r)
{
    std::string text;
    auto add = [&text](const std::string& s) { text += (text.empty() ? "" : "; ") + s; };
    if (r.jumpIns)
        add(std::to_string(r.jumpIns) + " jump-in(s): its time then stays with the routine that jumped");
    if (r.dispatchesOut)
        add(std::to_string(r.dispatchesOut) + " PUSH+RET dispatch(es) out");
    if (r.entries[EntryDispatch])
        add(std::to_string(r.entries[EntryDispatch]) + " entered by a RET dispatch");
    if (r.abandoned)
        add(std::to_string(r.abandoned) + " frame(s) abandoned (SP moved past the return address)");
    if (r.patched)
        add(std::to_string(r.patched) + " patched return(s)");
    return text;
}

StateNode TotalsNode(const Totals& t)
{
    StateNode n = StateNode::Object();
    n["active"] = t.active;
    StateNode& set = n["settings"];
    set = StateNode::Object();
    set["stack"] = t.settings.stack;
    set["beam"] = t.settings.beam;
    set["frames"] = t.settings.frames;
    set["ring_kib"] = static_cast<unsigned>(t.settings.ringKiB);
    set["stream"] = t.settings.stream;
    n["start_frame"] = t.startFrame;
    n["frames"] = t.frames;
    n["tstates"] = t.tstates;
    n["instructions"] = t.instructions;
    n["acknowledges"] = t.acknowledges;
    n["acknowledge_tstates"] = t.acknowledgeT;
    n["discontinuities"] = t.discontinuities;
    n["stack_overflows"] = t.stackOverflows;
    n["routine_overflows"] = t.routineOverflows;
    n["frame_tstates"] = static_cast<unsigned>(t.frameT);
    n["pages"] = static_cast<unsigned>(t.pages);
    n["routines"] = static_cast<unsigned>(t.routines);
    n["depth"] = static_cast<unsigned>(t.depth);
    n["frames_kept"] = t.framesKept;
    n["frames_streamed"] = t.streamed;
    n["frames_lost"] = t.framesLost;
    return n;
}

std::string TotalsText(const Totals& t)
{
    std::ostringstream out;
    out << "CPU profile " << (t.active ? "running" : "stopped") << ": " << t.frames << " frame(s), " << t.tstates
        << " T, " << t.instructions << " instruction(s), " << t.acknowledges << " acknowledge(s), " << t.routines
        << " routine(s) found, " << t.pages << " page(s) of cells";
    if (t.settings.stack)
        out << ", stack depth " << t.depth;
    if (t.discontinuities)
        out << ", " << t.discontinuities << " discontinuit" << (t.discontinuities == 1 ? "y" : "ies");
    out << "\n";
    return out.str();
}

Reply Status(ProfileAnalyzer& analyzer)
{
    Reply reply;
    const Totals t = analyzer.ReadTotals();
    reply.body = TotalsNode(t);
    StateNode marks = StateNode::Array();
    for (const std::string& name : analyzer.Marks())
        marks.push(name);
    reply.body["marks"] = std::move(marks);
    reply.text = TotalsText(t);
    return reply;
}

Reply Start(Emulator& emulator, AnalyzerManager& manager, ProfileAnalyzer& analyzer, const Options& options)
{
    Settings settings = analyzer.ReadTotals().settings;
    bool reset = false;
    for (const auto& [name, value] : options)
    {
        bool flag = false;
        if (name == "stack" || name == "beam" || name == "frames" || name == "reset")
        {
            if (!ParseBool(value, flag))
                return Refuse(Error::BadRequest, "'" + name + "' must be true or false");
            (name == "stack" ? settings.stack : name == "beam" ? settings.beam : name == "frames" ? settings.frames : reset) = flag;
        }
        else if (name == "ring_kib")
        {
            uint64_t kib = 0;
            if (!ParseUnsigned(value, kib) || kib > 262144)
                return Refuse(Error::BadRequest, "ring_kib must be 64..262144");
            settings.ringKiB = static_cast<uint32_t>(kib);
        }
        else if (name == "stream")
            settings.stream = value;
    }
    if (!settings.stack && !options.count("beam"))
        settings.beam = false;
    if (!settings.stack && !options.count("frames"))
        settings.frames = false;
    std::string error;
    if (!ProfileAnalyzer::Validate(settings, error))
        return Refuse(Error::BadRequest, error);  // before the switch: a running profile goes on
    {
        Emulator::CpuHold hold(emulator, "cpuprofile start");
        if (manager.isActive(ProfileAnalyzer::kId))
            manager.deactivate(ProfileAnalyzer::kId);
        if (!analyzer.Configure(settings, error))
            return Refuse(Error::BadRequest, error);
        if (reset)
            analyzer.Reset();
        if (!manager.activate(ProfileAnalyzer::kId))
            return Refuse(Error::Conflict, "the profile could not be switched on");
    }
    Reply reply = Status(analyzer);
    reply.text = "CPU profile started (stack " + std::string(settings.stack ? "on" : "off") + ", beam " +
                 (settings.beam ? "on" : "off") + ", frames " + (settings.frames ? "on" : "off") +
                 (settings.stream.empty() ? "" : ", streaming to " + settings.stream) +
                 "). Run the program, then read the report.\n" + reply.text;
    return reply;
}

Reply Report(Emulator& emulator, ProfileAnalyzer& analyzer, const Options& options)
{
    const auto since = options.find("since");
    if (since != options.end() && !since->second.empty() && since->second != "start" && !analyzer.HasMark(since->second))
        return Refuse(Error::NotFound, "no mark '" + since->second + "' (marks: status lists them)");
    uint64_t top = 24;
    if (auto it = options.find("top"); it != options.end() && (!ParseUnsigned(it->second, top) || top == 0))
        return Refuse(Error::BadRequest, "top must be a positive number");
    std::string sort = "self";
    if (auto it = options.find("sort"); it != options.end())
        sort = it->second;
    if (sort != "self" && sort != "inclusive" && sort != "calls")
        return Refuse(Error::BadRequest, "sort must be self, inclusive or calls");
    bool callers = true, beam = false;
    if (auto it = options.find("callers"); it != options.end() && !ParseBool(it->second, callers))
        return Refuse(Error::BadRequest, "callers must be true or false");
    if (auto it = options.find("beam"); it != options.end() && !ParseBool(it->second, beam))
        return Refuse(Error::BadRequest, "beam must be true or false");

    const Snapshot s = analyzer.Read(since == options.end() || since->second == "start" ? "" : since->second, false);
    const Namer namer(emulator, s);
    std::vector<const RoutineView*> rows;
    for (const RoutineView& r : s.routines)
        rows.push_back(&r);
    std::sort(rows.begin(), rows.end(), [&sort](const RoutineView* a, const RoutineView* b) {
        const uint64_t ka = sort == "self" ? a->self : sort == "inclusive" ? a->inclusive : Entries(*a);
        const uint64_t kb = sort == "self" ? b->self : sort == "inclusive" ? b->inclusive : Entries(*b);
        return ka != kb ? ka > kb : a->id < b->id;
    });
    if (rows.size() > top)
        rows.resize(top);

    Reply reply;
    reply.body = TotalsNode(s.totals);
    reply.body["format"] = "unreal-ng cpu profile";
    reply.body["version"] = 1;
    reply.body["since"] = s.since.empty() ? "start" : s.since;
    StateNode& window = reply.body["window"];
    window = StateNode::Object();
    window["frames"] = s.sinceFrames;
    window["tstates"] = s.sinceT;
    window["instructions"] = s.sinceInstructions;
    window["acknowledges"] = s.sinceAcknowledges;
    window["acknowledge_tstates"] = s.sinceAcknowledgeT;
    StateNode list = StateNode::Array();
    std::ostringstream text;
    text << "CPU profile since " << (s.since.empty() ? "the start" : "mark " + s.since) << ": " << s.sinceFrames
         << " frame(s), " << s.sinceT << " T, " << s.totals.routines << " routine(s) found\n";
    if (!s.totals.settings.stack)
        text << "(the call stack is off: no routines; export format=kit has the per-instruction profile)\n";
    else
        text << "  self    incl    calls  routine (entry, page, kinds)\n";
    uint64_t notes = 0;
    for (const RoutineView* r : rows)
    {
        StateNode node = StateNode::Object();
        node["id"] = static_cast<unsigned>(r->id);
        node["name"] = namer.Name(*r);
        node["entry"] = Hex4(r->address);
        node["page"] = static_cast<unsigned>(r->page);
        node["page_name"] = PageName(r->page);
        node["kinds"] = r->id == 0 ? std::string("root") : KindsText(*r);
        node["self"] = r->self;
        node["inclusive"] = r->inclusive;
        node["self_permille"] = s.sinceT ? r->self * 1000 / s.sinceT : 0;
        node["inclusive_permille"] = s.sinceT ? r->inclusive * 1000 / s.sinceT : 0;
        node["calls"] = r->entries[EntryCall];
        node["rsts"] = r->entries[EntryRst];
        node["interrupts"] = r->entries[EntryInterrupt];
        node["dispatches_in"] = r->entries[EntryDispatch];
        node["jump_ins"] = r->jumpIns;
        node["dispatches_out"] = r->dispatchesOut;
        node["abandoned"] = r->abandoned;
        node["patched_returns"] = r->patched;
        node["max_depth"] = static_cast<unsigned>(r->maxDepth);
        node["on_stack"] = r->onStack;
        const std::string note = Notes(*r);
        if (!note.empty())
        {
            node["notes"] = note;
            ++notes;
        }
        if (callers)
        {
            StateNode from = StateNode::Array();
            for (const auto& [caller, count] : r->callers)
            {
                StateNode c = StateNode::Object();
                c["id"] = static_cast<unsigned>(caller);
                c["name"] = caller < s.routines.size() ? namer.Name(s.routines[caller]) : std::string("?");
                c["entries"] = count;
                from.push(std::move(c));
            }
            node["callers"] = std::move(from);
            if (r->callersMore)
                node["callers_more"] = r->callersMore;
        }
        if (beam && !r->beam.empty())
        {
            StateNode bins = StateNode::Array();
            for (uint64_t v : r->beam)
                bins.push(v);
            node["beam"] = std::move(bins);
        }
        if (s.totals.settings.stack)
        {
            char line[160];
            std::snprintf(line, sizeof(line), "  %s  %s  %7" PRIu64 "  %s (%s, %s, %s)%s\n",
                          Permille(r->self, s.sinceT).c_str(), Permille(r->inclusive, s.sinceT).c_str(), Entries(*r),
                          namer.Name(*r).c_str(), Hex4(r->address).c_str(), PageName(r->page).c_str(),
                          r->id == 0 ? "root" : KindsText(*r).c_str(), note.empty() ? "" : " *");
            text << line;
        }
        list.push(std::move(node));
    }
    if (notes)
        text << "  * attribution notes in the answer's \"notes\" (JP entries and tail calls stay with the caller)\n";
    reply.body["routines"] = std::move(list);
    reply.text = text.str();
    return reply;
}

Reply Frames(Emulator& emulator, ProfileAnalyzer& analyzer, const Options& options)
{
    uint64_t last = 50, top = 8;
    if (auto it = options.find("last"); it != options.end() && (!ParseUnsigned(it->second, last) || last == 0))
        return Refuse(Error::BadRequest, "last must be a positive number");
    if (auto it = options.find("top"); it != options.end() && (!ParseUnsigned(it->second, top) || top == 0))
        return Refuse(Error::BadRequest, "top must be a positive number");
    const Totals totals = analyzer.ReadTotals();
    if (!totals.settings.frames)
        return Refuse(Error::Conflict, "the per-frame series is off: start with frames=true (and stack=true)");
    const std::vector<FrameView> frames = analyzer.ReadFrames(static_cast<uint32_t>(std::min<uint64_t>(last, 16383)));
    const Snapshot s = analyzer.Read("", false);
    const Namer namer(emulator, s);
    Reply reply;
    reply.body["frame_tstates"] = static_cast<unsigned>(totals.frameT);
    StateNode list = StateNode::Array();
    std::set<uint32_t> used;
    std::ostringstream text;
    text << "CPU profile, the last " << frames.size() << " frame(s) (self / inclusive T of the top " << top
         << " routines a frame):\n";
    for (const FrameView& f : frames)
    {
        std::vector<std::array<uint32_t, 3>> rows = f.routines;
        std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a[1] != b[1] ? a[1] > b[1] : a[0] < b[0]; });
        StateNode node = StateNode::Object();
        node["frame"] = f.frame;
        node["tstates"] = static_cast<unsigned>(f.total);
        node["active"] = static_cast<unsigned>(f.routines.size());
        StateNode rs = StateNode::Array();
        text << "  frame " << f.frame << " (" << f.routines.size() << " active):";
        for (size_t k = 0; k < rows.size() && k < top; ++k)
        {
            StateNode row = StateNode::Array();
            row.push(static_cast<unsigned>(rows[k][0]));
            row.push(static_cast<unsigned>(rows[k][1]));
            row.push(static_cast<unsigned>(rows[k][2]));
            rs.push(std::move(row));
            used.insert(rows[k][0]);
            if (k < 4 && rows[k][0] < s.routines.size())
                text << " " << namer.Name(s.routines[rows[k][0]]) << " " << rows[k][1];
        }
        text << "\n";
        node["routines"] = std::move(rs);
        list.push(std::move(node));
    }
    reply.body["frames"] = std::move(list);
    StateNode names = StateNode::Object();
    for (uint32_t id : used)
        if (id < s.routines.size())
            names[std::to_string(id)] = namer.Name(s.routines[id]);
    reply.body["names"] = std::move(names);
    reply.text = text.str();
    return reply;
}

/// The instruction's bytes at a cell (the kit's code column: 76 is the HALT)
std::string CodeAt(Emulator& emulator, ProfileAnalyzer& analyzer, const CellView& c)
{
    std::vector<uint8_t> bytes(4);
    for (uint16_t i = 0; i < 4; ++i)
        bytes[i] = analyzer.ByteAt(c.page, static_cast<uint16_t>((c.address + i) & 0x3FFF));
    uint8_t length = 1;
    if (DebugManager* debug = emulator.GetDebugManager())
        if (auto& disasm = debug->GetDisassembler())
            disasm->disassembleSingleCommand(bytes, c.address, &length);
    if (length == 0 || length > 4)
        length = 1;
    std::string hex;
    char buffer[4];
    for (uint8_t i = 0; i < length; ++i)
    {
        std::snprintf(buffer, sizeof(buffer), "%02X", bytes[i]);
        hex += buffer;
    }
    return hex;
}

Reply Export(Emulator& emulator, ProfileAnalyzer& analyzer, const Options& options)
{
    const auto formatIt = options.find("format");
    const std::string format = formatIt == options.end() ? "kit" : formatIt->second;
    if (format != "kit" && format != "json" && format != "entries" && format != "sym" && format != "ann")
        return Refuse(Error::BadRequest, "format must be kit, json, entries, sym or ann");
    std::string since;
    if (auto it = options.find("since"); it != options.end() && it->second != "start")
    {
        since = it->second;
        if (!analyzer.HasMark(since))
            return Refuse(Error::NotFound, "no mark '" + since + "'");
    }
    const bool cells = format == "kit" || format == "json";
    const Snapshot s = analyzer.Read(since, cells);
    const Namer namer(emulator, s);
    std::string content;
    if (format == "kit" || format == "json")
    {
        // The L1 subset kit/fig_budget.py reads (replay, pages, routines' profile, instructions), so the kit's figures
        // draw from a live profile as from an L1 build
        StateNode doc = StateNode::Object();
        doc["format"] = format == "kit" ? "unreal-ng analysis L1" : "unreal-ng cpu profile";
        doc["version"] = 1;
        doc["source"] = "cpuprofile";
        doc["since"] = since.empty() ? "start" : since;
        StateNode& replay = doc["replay"];
        replay = StateNode::Object();
        replay["first_frame"] = s.totals.startFrame + (s.totals.frames - s.sinceFrames);
        replay["last_frame"] = s.totals.startFrame + s.totals.frames;
        replay["frames"] = s.sinceFrames;
        replay["instructions"] = s.sinceInstructions;
        replay["tstates"] = s.sinceT;
        replay["interrupts"] = s.sinceAcknowledges;
        replay["acknowledge_tstates"] = s.sinceAcknowledgeT;
        replay["frame_tstates"] = static_cast<unsigned>(s.totals.frameT);
        std::set<uint16_t> pageSet;
        for (const CellView& c : s.cells)
            pageSet.insert(c.page);
        StateNode pages = StateNode::Array();
        for (uint16_t page : pageSet)
        {
            StateNode p = StateNode::Object();
            p["page"] = static_cast<unsigned>(page);
            p["name"] = PageName(page);
            pages.push(std::move(p));
        }
        doc["pages"] = std::move(pages);
        StateNode routines = StateNode::Array();
        for (size_t id = 1; id < s.routines.size(); ++id)
        {
            const RoutineView& r = s.routines[id];
            StateNode node = StateNode::Object();
            node["entry"] = Hex4(r.address);
            node["page"] = static_cast<unsigned>(r.page);
            node["name"] = namer.Name(r);
            node["kinds"] = KindsText(r);
            StateNode& prof = node["profile"];
            prof = StateNode::Object();
            prof["tstates"] = r.self;
            prof["tstates_inclusive"] = r.inclusive;
            prof["tstates_permille"] = s.sinceT ? r.self * 1000 / s.sinceT : 0;
            prof["entries"] = Entries(r);
            prof["jump_ins"] = r.jumpIns;
            prof["dispatches_out"] = r.dispatchesOut;
            prof["abandoned"] = r.abandoned;
            prof["patched_returns"] = r.patched;
            prof["max_depth"] = static_cast<unsigned>(r.maxDepth);
            routines.push(std::move(node));
        }
        doc["routines"] = std::move(routines);
        if (!s.routines.empty())
            doc["root_tstates"] = s.routines[0].self;
        // instructions: [z80 "$ADDR", page, code hex, class, count, tstates, smc]
        StateNode rows = StateNode::Array();
        for (const CellView& c : s.cells)
        {
            StateNode row = StateNode::Array();
            row.push(Hex4(c.address));
            row.push(static_cast<unsigned>(c.page));
            const std::string code = CodeAt(emulator, analyzer, c);
            row.push(code);
            row.push(code == "76" ? "halt" : "");
            row.push(c.count);
            row.push(c.tstates);
            row.push(false);
            rows.push(std::move(row));
        }
        doc["instructions"] = std::move(rows);
        content = StateNodeToJsonText(doc);
    }
    else if (format == "entries")
    {
        StateNode list = StateNode::Array();
        for (size_t id = 1; id < s.routines.size(); ++id)
        {
            const RoutineView& r = s.routines[id];
            StateNode node = StateNode::Object();
            node["entry"] = Hex4(r.address);
            node["page"] = static_cast<unsigned>(r.page);
            node["page_name"] = PageName(r.page);
            node["name"] = namer.Name(r);
            node["kinds"] = KindsText(r);
            node["calls"] = r.entries[EntryCall];
            node["rsts"] = r.entries[EntryRst];
            node["interrupts"] = r.entries[EntryInterrupt];
            node["dispatches_in"] = r.entries[EntryDispatch];
            node["jump_ins"] = r.jumpIns;
            StateNode from = StateNode::Array();
            for (const auto& [caller, count] : r.callers)
            {
                StateNode c = StateNode::Object();
                c["entry"] = caller < s.routines.size() && caller ? Hex4(s.routines[caller].address) : std::string("-");
                c["page"] = static_cast<unsigned>(caller < s.routines.size() ? s.routines[caller].page : 0);
                c["entries"] = count;
                from.push(std::move(c));
            }
            node["callers"] = std::move(from);
            list.push(std::move(node));
        }
        StateNode doc = StateNode::Object();
        doc["format"] = "unreal-ng cpu profile entries";
        doc["version"] = 1;
        doc["entries"] = std::move(list);
        content = StateNodeToJsonText(doc);
    }
    else
    {
        std::vector<const RoutineView*> byAddress;
        for (size_t id = 1; id < s.routines.size(); ++id)
            byAddress.push_back(&s.routines[id]);
        std::sort(byAddress.begin(), byAddress.end(), [](const RoutineView* a, const RoutineView* b) {
            return a->address != b->address ? a->address < b->address : a->page < b->page;
        });
        std::ostringstream out;
        for (const RoutineView* r : byAddress)
        {
            char line[200];
            if (format == "sym")
                std::snprintf(line, sizeof(line), "%s = %s ; %s, %s, %" PRIu64 " entries\n", namer.Name(*r).c_str(),
                              Hex4(r->address).c_str(), PageName(r->page).c_str(), KindsText(*r).c_str(), Entries(*r));
            else
                std::snprintf(line, sizeof(line), "%s c %s ; %s, %s, %" PRIu64 " entries\n", Hex4(r->address).c_str(),
                              namer.Name(*r).c_str(), PageName(r->page).c_str(), KindsText(*r).c_str(), Entries(*r));
            out << line;
        }
        content = out.str();
    }

    Reply reply;
    reply.body["format"] = format;
    reply.body["routines"] = static_cast<unsigned>(s.routines.empty() ? 0 : s.routines.size() - 1);
    reply.body["instructions"] = static_cast<uint64_t>(s.cells.size());
    reply.body["bytes"] = static_cast<uint64_t>(content.size());
    if (auto it = options.find("path"); it != options.end() && !it->second.empty())
    {
        std::ofstream file(FileHelper::ToFsPath(it->second), std::ios::binary | std::ios::trunc);
        if (!file.is_open())
            return Refuse(Error::BadRequest, "cannot write " + it->second);
        file << content;
        if (!file.good())
            return Refuse(Error::BadRequest, "cannot write " + it->second);
        reply.body["path"] = it->second;
        reply.text = "CPU profile exported as " + format + " to " + it->second + " (" + std::to_string(content.size()) +
                     " bytes, " + std::to_string(s.routines.empty() ? 0 : s.routines.size() - 1) + " routine(s), " +
                     std::to_string(s.cells.size()) + " instruction(s))\n";
    }
    else
    {
        reply.body["content"] = content;
        reply.text = format == "sym" || format == "ann" ? content
                                                        : "CPU profile export (" + format + ", " +
                                                              std::to_string(content.size()) + " bytes) in \"content\"\n";
    }
    return reply;
}
}  // namespace

Reply Execute(Emulator& emulator, const std::string& op, const Options& options)
{
    const auto& ops = Operations();
    if (std::find(ops.begin(), ops.end(), op) == ops.end())
        return Refuse(Error::BadRequest, "unknown cpuprofile operation '" + op +
                                             "' (start, stop, reset, mark, status, report, frames, export)");
    const auto& allowed = OptionsFor(op);
    for (const auto& [name, _] : options)
        if (std::find(allowed.begin(), allowed.end(), name) == allowed.end())
            return Refuse(Error::BadRequest, "cpuprofile " + op + " takes no option '" + name + "'");
    DebugManager* debug = emulator.GetDebugManager();
    AnalyzerManager* manager = debug ? debug->GetAnalyzerManager() : nullptr;
    ProfileAnalyzer* analyzer = Find(emulator);
    if (!manager || !analyzer)
        return Refuse(Error::NotAvailable, "no CPU profile on this instance (no debug manager)");

    if (op == "start")
        return Start(emulator, *manager, *analyzer, options);
    if (op == "stop")
    {
        {
            Emulator::CpuHold hold(emulator, "cpuprofile stop");
            if (manager->isActive(ProfileAnalyzer::kId))
                manager->deactivate(ProfileAnalyzer::kId);
        }
        Reply reply = Status(*analyzer);
        reply.text = "CPU profile stopped; the counters stay readable.\n" + reply.text;
        return reply;
    }
    if (op == "reset")
    {
        {
            Emulator::CpuHold hold(emulator, "cpuprofile reset");
            const bool active = manager->isActive(ProfileAnalyzer::kId);
            if (active)
                manager->deactivate(ProfileAnalyzer::kId);
            analyzer->Reset();
            if (active)
                manager->activate(ProfileAnalyzer::kId);
        }
        Reply reply = Status(*analyzer);
        reply.text = "CPU profile reset: a fresh window from now.\n" + reply.text;
        return reply;
    }
    if (op == "mark")
    {
        const auto name = options.find("name");
        if (name == options.end() || name->second.empty())
            return Refuse(Error::BadRequest, "mark needs a name");
        bool drop = false;
        if (auto it = options.find("drop"); it != options.end() && !ParseBool(it->second, drop))
            return Refuse(Error::BadRequest, "drop must be true or false");
        if (drop)
        {
            if (!analyzer->DropMark(name->second))
                return Refuse(Error::NotFound, "no mark '" + name->second + "'");
            Reply reply = Status(*analyzer);
            reply.text = "Mark " + name->second + " dropped.\n" + reply.text;
            return reply;
        }
        std::string error;
        if (!analyzer->Mark(name->second, error))
            return Refuse(Error::Conflict, error);
        Reply reply = Status(*analyzer);
        reply.text = "Mark " + name->second + " set: report since=" + name->second + " reads the time after it.\n" + reply.text;
        return reply;
    }
    if (op == "status")
        return Status(*analyzer);
    if (op == "report")
        return Report(emulator, *analyzer, options);
    if (op == "frames")
        return Frames(emulator, *analyzer, options);
    return Export(emulator, *analyzer, options);
}

}  // namespace profile
