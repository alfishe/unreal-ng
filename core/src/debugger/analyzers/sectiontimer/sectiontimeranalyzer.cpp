#include "sectiontimeranalyzer.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <string>

#include "3rdparty/message-center/messagecenter.h"
#include "debugger/analyzers/analyzermanager.h"
#include "debugger/debugmanager.h"
#include "debugger/labels/labelmanager.h"
#include "debugger/memory/memoryread.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/notifications.h"
#include "emulator/platform.h"
#include "emulator/sound/chips/gs/gsdebughooks.h"

namespace
{
std::string Trim(const std::string& text)
{
    const size_t first = text.find_first_not_of(" \t");
    if (first == std::string::npos)
        return {};
    return text.substr(first, text.find_last_not_of(" \t") - first + 1);
}

std::string JsonString(const std::string& text)
{
    std::string out = "\"";
    for (const char c : text)
    {
        if (c == '"' || c == '\\')
        {
            out += '\\';
            out += c;
        }
        else if (static_cast<unsigned char>(c) < 0x20)
        {
            char escaped[8];
            std::snprintf(escaped, sizeof(escaped), "\\u%04x", static_cast<unsigned>(c));
            out += escaped;
        }
        else
            out += c;
    }
    return out + "\"";
}

std::string Hex(uint16_t address)
{
    char text[8];
    std::snprintf(text, sizeof(text), "#%04X", address);
    return text;
}
}  // namespace

SectionTimerAnalyzer::SectionTimerAnalyzer(EmulatorContext* context) : _context(context) {}

SectionTimerAnalyzer::~SectionTimerAnalyzer() = default;  // AnalyzerManager deactivates it first

SectionTimerAnalyzer* SectionTimerAnalyzer::Find(EmulatorContext* context)
{
    AnalyzerManager* manager =
        context && context->pDebugManager ? context->pDebugManager->GetAnalyzerManager() : nullptr;
    return manager ? manager->getAnalyzer<SectionTimerAnalyzer>(kId) : nullptr;
}

bool SectionTimerAnalyzer::Start(const Spec& spec, std::string& error)
{
    AnalyzerManager* manager =
        _context && _context->pDebugManager ? _context->pDebugManager->GetAnalyzerManager() : nullptr;
    if (!manager)
    {
        error = "no analyzer manager";
        return false;
    }
    Config config;
    if (!Resolve(spec, config, error))
        return false;
    manager->deactivate(kId);
    configure(config);
    if (!manager->activate(kId) || _sink == BRK_NO_TAP)
    {
        manager->deactivate(kId);
        error = "the timer could not arm its taps";
        return false;
    }
    return true;
}

bool SectionTimerAnalyzer::Stop()
{
    AnalyzerManager* manager =
        _context && _context->pDebugManager ? _context->pDebugManager->GetAnalyzerManager() : nullptr;
    return manager && IsActive() && manager->deactivate(kId);
}

void SectionTimerAnalyzer::Clear()
{
    {
        std::lock_guard<std::mutex> lock(_lock);
        Reset();
    }
    Publish("cleared");
}

void SectionTimerAnalyzer::Publish(const char* event, const BreakStop* stop) const
{
    if (_context == nullptr || _context->pEmulator == nullptr)
        return;
    auto* payload = new SectionTimerPayload(_context->pEmulator->GetId(), event);
    if (stop != nullptr)
    {
        payload->condition = stop->condition;
        if (stop->pass.section < _config.sections.size())
            payload->section = _config.sections[stop->pass.section].name;
        payload->frame = stop->pass.frame;
        payload->t = stop->pass.t;
        payload->elapsed = stop->pass.elapsed;
        payload->net = stop->pass.net;
        payload->interrupts = stop->pass.interrupts;
    }
    MessageCenter::DefaultMessageCenter().Post(NC_SECTION_TIMER, payload, true);
}

bool SectionTimerAnalyzer::IsActive() const
{
    std::lock_guard<std::mutex> lock(_lock);
    return _active;
}

bool SectionTimerAnalyzer::ResolveAddress(const std::string& text, uint16_t& address, std::string& error) const
{
    const std::string token = Trim(text);
    uint32_t number = 0;
    if (MemoryRead::ParseNumber(token, number))
    {
        if (number > 0xFFFF)
        {
            error = "address out of range: " + token;
            return false;
        }
        address = static_cast<uint16_t>(number);
        return true;
    }
    LabelManager* labels =
        _context && _context->pDebugManager ? _context->pDebugManager->GetLabelManager() : nullptr;
    const std::shared_ptr<Label> label = labels ? labels->GetLabelByName(token) : nullptr;
    if (!label)
    {
        error = "no label or number: " + token;
        return false;
    }
    address = label->address;
    return true;
}

bool SectionTimerAnalyzer::Resolve(const Spec& spec, Config& config, std::string& error) const
{
    config = Config{};
    config.recursive = spec.recursive;
    config.frameT = spec.frameT;
    config.capacity = std::min(spec.capacity, kMaxCapacity);

    for (const std::string& raw : spec.sections)
    {
        const std::string text = Trim(raw);
        Section section;
        const size_t equals = text.find('=');
        if (equals == std::string::npos)
        {
            // NAME: the labels NAME and NAME.End
            section.name = text;
            uint16_t exit = 0;
            if (!ResolveAddress(text, section.entry, error) || !ResolveAddress(text + ".End", exit, error))
                return false;
            section.exits.push_back(exit);
        }
        else
        {
            section.name = Trim(text.substr(0, equals));
            const std::string range = text.substr(equals + 1);
            const size_t colon = range.find(':');
            if (section.name.empty() || colon == std::string::npos)
            {
                error = "a section is NAME or name=ENTRY:EXIT[,EXIT]: " + text;
                return false;
            }
            if (!ResolveAddress(range.substr(0, colon), section.entry, error))
                return false;
            std::string exits = range.substr(colon + 1);
            size_t start = 0;
            while (start <= exits.size())
            {
                const size_t comma = exits.find(',', start);
                const std::string one = exits.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
                uint16_t exit = 0;
                if (!ResolveAddress(one, exit, error))
                    return false;
                section.exits.push_back(exit);
                if (comma == std::string::npos)
                    break;
                start = comma + 1;
            }
        }
        for (const Section& other : config.sections)
            if (other.name == section.name)
            {
                error = "two sections named " + section.name;
                return false;
            }
        config.sections.push_back(std::move(section));
    }

    // No section given: every label X that has an X.End
    if (spec.sections.empty())
    {
        LabelManager* labels =
            _context && _context->pDebugManager ? _context->pDebugManager->GetLabelManager() : nullptr;
        std::vector<std::shared_ptr<Label>> all = labels ? labels->GetAllLabels() : std::vector<std::shared_ptr<Label>>{};
        std::sort(all.begin(), all.end(), [](const auto& a, const auto& b) {
            return a->address != b->address ? a->address < b->address : a->name < b->name;
        });
        for (const auto& label : all)
        {
            const std::shared_ptr<Label> end = labels->GetLabelByName(label->name + ".End");
            if (end)
                config.sections.push_back(Section{label->name, label->address, {end->address}});
        }
    }
    if (config.sections.empty())
    {
        error = "no section: give NAME or name=ENTRY:EXIT, or load labels with X and X.End pairs";
        return false;
    }
    if (config.sections.size() > 0x7FFF)
    {
        error = "too many sections";
        return false;
    }

    for (const std::string& raw : spec.extra)
    {
        uint16_t address = 0;
        if (!ResolveAddress(raw, address, error))
            return false;
        config.extra.push_back(address);
    }

    for (const std::string& raw : spec.breakIf)
    {
        std::string text = Trim(raw);
        BreakIf condition;
        condition.text = text;
        if (text.rfind("net:", 0) == 0)
        {
            condition.net = true;
            text = Trim(text.substr(4));
        }
        const size_t at = text.find_first_of("<>=!");
        if (at == std::string::npos || at == 0)
        {
            error = "a break condition is [net:]NAME<op>T, op one of > < >= <= == !=: " + raw;
            return false;
        }
        const std::string name = Trim(text.substr(0, at));
        std::string rest = text.substr(at);
        static const std::pair<const char*, Op> ops[] = {{">=", Op::GreaterEqual}, {"<=", Op::LessEqual},
                                                         {"==", Op::Equal},        {"!=", Op::NotEqual},
                                                         {">", Op::Greater},       {"<", Op::Less}};
        bool found = false;
        for (const auto& [symbol, op] : ops)
        {
            const std::string s = symbol;
            if (rest.rfind(s, 0) == 0)
            {
                condition.op = op;
                rest = Trim(rest.substr(s.size()));
                found = true;
                break;
            }
        }
        uint32_t value = 0;
        if (!found || !MemoryRead::ParseNumber(rest, value))
        {
            error = "a break condition is [net:]NAME<op>T, op one of > < >= <= == !=: " + raw;
            return false;
        }
        condition.value = value;
        auto section = std::find_if(config.sections.begin(), config.sections.end(),
                                    [&](const Section& s) { return s.name == name; });
        if (section == config.sections.end())
        {
            error = "a break condition names no section: " + name;
            return false;
        }
        condition.section = static_cast<uint16_t>(section - config.sections.begin());
        config.breakIf.push_back(std::move(condition));
    }
    return true;
}

void SectionTimerAnalyzer::configure(const Config& config)
{
    std::lock_guard<std::mutex> lock(_lock);
    _config = config;
    _markers.clear();
    auto marker = [this](uint16_t address) -> Marker& {
        for (Marker& m : _markers)
            if (m.address == address)
                return m;
        _markers.push_back(Marker{address, {}});
        return _markers.back();
    };
    for (size_t s = 0; s < config.sections.size(); s++)
        for (const uint16_t exit : config.sections[s].exits)
            marker(exit).actions.push_back(-static_cast<int32_t>(s + 1));
    for (size_t s = 0; s < config.sections.size(); s++)
        marker(config.sections[s].entry).actions.push_back(static_cast<int32_t>(s + 1));
    for (uint16_t address : config.extra)
        marker(address);
    Reset();
}

void SectionTimerAnalyzer::Reset()
{
    _open.clear();
    _pending.clear();
    _passes.clear();
    _stats.assign(_config.sections.size(), Stats{});
    _entered.assign(_config.sections.size(), false);
    _serial = 0;
    _passesTotal = 0;
    _hits = 0;
    _orphans = 0;
    _dropped = 0;
    _stop = BreakStop{};
    _stopPending = false;
}

void SectionTimerAnalyzer::onActivate(AnalyzerManager* manager)
{
    _manager = manager;
    if (_context == nullptr || _context->pDebugManager == nullptr)
        return;
    _breakpoints = _context->pDebugManager->GetBreakpointsManager();
    {
        std::lock_guard<std::mutex> lock(_lock);
        _frameT = _config.frameT != 0 ? _config.frameT : _context->GetFrameTStates();
        Reset();
        _active = true;
    }
    _sink = _breakpoints->RegisterTapSink(this);
    if (_sink == BRK_NO_TAP)
    {
        std::lock_guard<std::mutex> lock(_lock);
        _active = false;
        return;
    }
    std::string error;
    for (size_t i = 0; i < _markers.size(); i++)
    {
        BreakpointSpec spec;
        spec.type = BRK_MEMORY;
        spec.access = BRK_MEM_EXECUTE;
        spec.address = _markers[i].address;
        spec.tapSink = _sink;
        spec.tapCookie = static_cast<uint32_t>(i);
        spec.hidden = true;
        spec.shared = false;
        spec.owner = kId;
        _breakpoints->AddBreakpoint(spec, error);
    }
    for (const auto& [kind, cookie] : {std::pair{GSCardEvent::IntAccept, kCookieAccept},
                                      std::pair{GSCardEvent::IntReturn, kCookieReturn}})
    {
        BreakpointSpec spec;
        spec.type = BRK_EVENT;
        spec.address = static_cast<uint16_t>(kind);
        spec.tapSink = _sink;
        spec.tapCookie = cookie;
        spec.hidden = true;
        spec.shared = false;
        spec.owner = kId;
        _breakpoints->AddBreakpoint(spec, error);
    }
    Publish("started");
}

void SectionTimerAnalyzer::onDeactivate()
{
    if (_breakpoints != nullptr && _sink != BRK_NO_TAP)
    {
        // The sink's taps go with it
        _breakpoints->UnregisterTapSink(_sink);
        _sink = BRK_NO_TAP;
    }
    _breakpoints = nullptr;
    _manager = nullptr;
    bool wasActive = false;
    {
        std::lock_guard<std::mutex> lock(_lock);
        wasActive = _active;
        _active = false;
    }
    if (wasActive)
        Publish("stopped");
}

uint64_t SectionTimerAnalyzer::Now() const
{
    const Z80* z80 = _context->pCore->GetZ80();
    return _context->emulatorState.frame_counter * _frameT + z80->t;
}

void SectionTimerAnalyzer::OnTap(const BreakpointTapHit& hit)
{
    BreakStop stopped;
    {
        std::lock_guard<std::mutex> lock(_lock);
        if (hit.cookie == kCookieAccept)
        {
            Accept(hit.arg > 0 ? static_cast<uint32_t>(hit.arg) : 0);
            return;
        }
        if (hit.cookie == kCookieReturn)
        {
            Return();
            return;
        }
        if (hit.cookie >= _markers.size())
            return;
        _hits++;
        for (const int32_t action : _markers[hit.cookie].actions)
        {
            if (action > 0)
                Entry(static_cast<uint16_t>(action - 1));
            else
                Exit(static_cast<uint16_t>(-action - 1));
        }
        if (!_stopPending) [[likely]]
            return;
        _stopPending = false;
        stopped = _stop;
    }
    Publish("break", &stopped);
}

void SectionTimerAnalyzer::Entry(uint16_t section)
{
    // Entered again while open: the open pass left without its exit (recursive code nests instead)
    if (!_config.recursive)
    {
        for (size_t i = 0; i < _open.size(); i++)
        {
            if (_open[i].section == section)
            {
                _dropped += _open.size() - i;
                _open.resize(i);
                break;
            }
        }
    }
    const Z80* z80 = _context->pCore->GetZ80();
    Open o;
    o.section = section;
    o.serial = ++_serial;
    o.start = Now();
    o.frame = _context->emulatorState.frame_counter;
    o.t = z80->t;
    _open.push_back(o);
    _entered[section] = true;
}

void SectionTimerAnalyzer::Exit(uint16_t section)
{
    for (size_t i = _open.size(); i-- > 0;)
    {
        if (_open[i].section != section)
            continue;
        const Open o = _open[i];
        _dropped += _open.size() - 1 - i;
        _open.resize(i);
        const uint64_t elapsed = Now() - o.start;
        const uint64_t net = elapsed - std::min(o.interruptT, elapsed);
        const Pass pass{section, o.frame, o.t, elapsed, net, o.interrupts};
        _passesTotal++;
        if (_passes.size() < _config.capacity)
            _passes.push_back(pass);
        Stats& x = _stats[section];
        if (x.count == 0)
        {
            x.min = x.max = elapsed;
            x.netMin = x.netMax = net;
            x.maxFrame = o.frame;
            x.maxT = o.t;
        }
        x.count++;
        x.sum += elapsed;
        x.netSum += net;
        x.last = elapsed;
        x.min = std::min(x.min, elapsed);
        x.netMin = std::min(x.netMin, net);
        x.netMax = std::max(x.netMax, net);
        if (elapsed > x.max)
        {
            x.max = elapsed;
            x.maxFrame = o.frame;
            x.maxT = o.t;
        }
        if (o.interrupts)
            x.interrupted++;

        for (const BreakIf& condition : _config.breakIf)
        {
            if (condition.section != section)
                continue;
            const uint64_t v = condition.net ? net : elapsed;
            bool meets = false;
            switch (condition.op)
            {
                case Op::Greater: meets = v > condition.value; break;
                case Op::Less: meets = v < condition.value; break;
                case Op::GreaterEqual: meets = v >= condition.value; break;
                case Op::LessEqual: meets = v <= condition.value; break;
                case Op::Equal: meets = v == condition.value; break;
                case Op::NotEqual: meets = v != condition.value; break;
            }
            // Recorded and told only where the machine stops (not in a run that skips breakpoints)
            if (meets && _breakpoints && _breakpoints->TapStopsHere())
            {
                _stop = BreakStop{true, pass, condition.text};
                _stopPending = true;
                _breakpoints->StopAtThisTap();
                break;
            }
        }
        return;
    }
    if (_entered[section])
        _orphans++;
}

void SectionTimerAnalyzer::Accept(uint32_t acknowledgeT)
{
    Pending p;
    p.start = Now() - acknowledgeT;
    p.inside.reserve(_open.size());
    for (Open& o : _open)
    {
        o.interrupts++;
        p.inside.push_back(o.serial);
    }
    _pending.push_back(std::move(p));
}

void SectionTimerAnalyzer::Return()
{
    if (_pending.empty())
        return;
    const Pending p = std::move(_pending.back());
    _pending.pop_back();
    const uint64_t cost = Now() - p.start;
    // The passes still open of those the interrupt ran inside (a closed one has its net time already)
    for (Open& o : _open)
        if (std::find(p.inside.begin(), p.inside.end(), o.serial) != p.inside.end())
            o.interruptT += cost;
}

SectionTimerAnalyzer::Result SectionTimerAnalyzer::GetResult(size_t offset, size_t limit) const
{
    std::lock_guard<std::mutex> lock(_lock);
    Result r;
    r.active = _active;
    r.frameT = _frameT;
    r.sections = _config.sections;
    r.stats = _stats;
    if (offset < _passes.size())
    {
        const size_t count = std::min(limit, _passes.size() - offset);
        r.passes.assign(_passes.begin() + static_cast<std::ptrdiff_t>(offset),
                        _passes.begin() + static_cast<std::ptrdiff_t>(offset + count));
    }
    r.passesTotal = _passesTotal;
    r.passesKept = _passes.size();
    r.hits = _hits;
    r.orphans = _orphans;
    r.dropped = _dropped;
    r.open = _open.size();
    r.stop = _stop;
    return r;
}

std::string SectionTimerAnalyzer::ToJson(const Result& r)
{
    auto name = [&](uint16_t section) {
        return section < r.sections.size() ? JsonString(r.sections[section].name) : std::string("\"\"");
    };
    std::string out = "{\"active\":" + std::string(r.active ? "true" : "false") +
                      ",\"frame_t\":" + std::to_string(r.frameT) + ",\"sections\":[";
    for (size_t s = 0; s < r.sections.size(); s++)
    {
        const Section& section = r.sections[s];
        const Stats x = s < r.stats.size() ? r.stats[s] : Stats{};
        if (s)
            out += ',';
        out += "{\"name\":" + JsonString(section.name) + ",\"entry\":" + std::to_string(section.entry) + ",\"exits\":[";
        for (size_t e = 0; e < section.exits.size(); e++)
            out += (e ? "," : "") + std::to_string(section.exits[e]);
        out += "],\"count\":" + std::to_string(x.count) + ",\"min\":" + std::to_string(x.min) +
               ",\"max\":" + std::to_string(x.max) + ",\"avg\":" + std::to_string(x.count ? x.sum / x.count : 0) +
               ",\"net_min\":" + std::to_string(x.netMin) + ",\"net_max\":" + std::to_string(x.netMax) +
               ",\"net_avg\":" + std::to_string(x.count ? x.netSum / x.count : 0) +
               ",\"interrupted\":" + std::to_string(x.interrupted) + ",\"last\":" + std::to_string(x.last) +
               ",\"max_frame\":" + std::to_string(x.maxFrame) + ",\"max_t\":" + std::to_string(x.maxT) + "}";
    }
    out += "],\"passes\":[";
    for (size_t i = 0; i < r.passes.size(); i++)
    {
        const Pass& p = r.passes[i];
        if (i)
            out += ',';
        out += "[" + name(p.section) + "," + std::to_string(p.frame) + "," + std::to_string(p.t) + "," +
               std::to_string(p.elapsed) + "," + std::to_string(p.net) + "," + std::to_string(p.interrupts) + "]";
    }
    out += "],\"passes_total\":" + std::to_string(r.passesTotal) + ",\"passes_kept\":" + std::to_string(r.passesKept) +
           ",\"hits\":" + std::to_string(r.hits) + ",\"orphan\":" + std::to_string(r.orphans) +
           ",\"dropped\":" + std::to_string(r.dropped) + ",\"open\":" + std::to_string(r.open) + ",\"stopped\":";
    if (r.stop.hit)
    {
        const Pass& p = r.stop.pass;
        out += "{\"condition\":" + JsonString(r.stop.condition) + ",\"section\":" + name(p.section) +
               ",\"frame\":" + std::to_string(p.frame) + ",\"t\":" + std::to_string(p.t) +
               ",\"elapsed\":" + std::to_string(p.elapsed) + ",\"net\":" + std::to_string(p.net) +
               ",\"interrupts\":" + std::to_string(p.interrupts) + "}";
    }
    else
        out += "null";
    return out + "}";
}

std::string SectionTimerAnalyzer::ToText(const Result& r)
{
    std::string out = std::string("section-timer ") + (r.active ? "on" : "off") + ", " +
                      std::to_string(r.passesTotal) + " passes, " + std::to_string(r.hits) + " marker hits, frame " +
                      std::to_string(r.frameT) + " T\n";
    char line[256];
    std::snprintf(line, sizeof(line), "%-16s %-13s %8s %8s %8s %8s %8s %6s\n", "section", "entry..exit", "count", "min",
                  "avg", "max", "net max", "int");
    out += line;
    for (size_t s = 0; s < r.sections.size(); s++)
    {
        const Section& section = r.sections[s];
        const Stats x = s < r.stats.size() ? r.stats[s] : Stats{};
        const std::string range =
            Hex(section.entry) + ".." + (section.exits.empty() ? std::string("?") : Hex(section.exits[0])) +
            (section.exits.size() > 1 ? "+" : "");
        std::snprintf(line, sizeof(line), "%-16s %-13s %8llu %8llu %8llu %8llu %8llu %6llu\n", section.name.c_str(),
                      range.c_str(), static_cast<unsigned long long>(x.count), static_cast<unsigned long long>(x.min),
                      static_cast<unsigned long long>(x.count ? x.sum / x.count : 0),
                      static_cast<unsigned long long>(x.max), static_cast<unsigned long long>(x.netMax),
                      static_cast<unsigned long long>(x.interrupted));
        out += line;
    }
    if (r.orphans || r.dropped || r.open)
        out += "unpaired: " + std::to_string(r.orphans) + " exits with no entry, " + std::to_string(r.dropped) +
               " passes left without their exit, " + std::to_string(r.open) + " open now\n";
    if (r.passesKept < r.passesTotal)
        out += "kept " + std::to_string(r.passesKept) + " of " + std::to_string(r.passesTotal) +
               " passes one by one (capacity); the statistics count them all\n";
    if (r.stop.hit)
    {
        const Pass& p = r.stop.pass;
        out += "stopped on " + r.stop.condition + ": " +
               (p.section < r.sections.size() ? r.sections[p.section].name : std::string()) + " entered at frame " +
               std::to_string(p.frame) + " T " + std::to_string(p.t) + ", " + std::to_string(p.elapsed) + " T (net " +
               std::to_string(p.net) + ", " + std::to_string(p.interrupts) + " interrupts)\n";
    }
    return out;
}
