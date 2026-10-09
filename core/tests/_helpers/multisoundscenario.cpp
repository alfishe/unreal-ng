#include "multisoundscenario.h"

#include <algorithm>
#include <iterator>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace
{
    using Op = MultiSoundCycle::Op;

    struct OpName
    {
        Op op;
        const char* name;
        int operands;       // hex operands after the name
        bool optionalValue; // the last operand may be omitted
    };

    constexpr OpName OpNames[] = {
        { Op::M1, "m1", 1, false },
        { Op::MemRead, "mr", 1, false },
        { Op::MemWrite, "mw", 2, false },
        { Op::Out, "out", 2, false },
        { Op::In, "in", 1, false },
        { Op::GsOut, "gout", 2, false },
        { Op::GsIn, "gin", 1, false },
        { Op::GsMemRead, "gmr", 2, false },
        { Op::GsMemWrite, "gmw", 2, false },
        { Op::Reset, "reset", 0, false },
    };

    const OpName* FindOp(const std::string& name)
    {
        for (const OpName& entry : OpNames)
        {
            if (name == entry.name)
                return &entry;
        }
        return nullptr;
    }

    const OpName* FindOp(Op op)
    {
        for (const OpName& entry : OpNames)
        {
            if (entry.op == op)
                return &entry;
        }
        return nullptr;
    }

    bool ParseHex(const std::string& token, uint32_t limit, uint32_t& value)
    {
        if (token.empty() || token.size() > 4)
            return false;
        char* end = nullptr;
        const unsigned long parsed = std::strtoul(token.c_str(), &end, 16);
        if (end == nullptr || *end != '\0' || parsed > limit)
            return false;
        value = static_cast<uint32_t>(parsed);
        return true;
    }

    bool ParseDip(const std::string& list, uint8_t& bits)
    {
        bits = 0;
        if (list == "none")
            return true;
        std::stringstream stream(list);
        std::string item;
        while (std::getline(stream, item, ','))
        {
            if (item == "ym") bits |= 0x01;
            else if (item == "saa") bits |= 0x02;
            else if (item == "gs") bits |= 0x04;
            else if (item == "sd") bits |= 0x08;
            else return false;
        }
        return true;
    }

    std::string DipText(uint8_t bits)
    {
        std::string text;
        static constexpr const char* names[] = { "ym", "saa", "gs", "sd" };
        for (int i = 0; i < 4; i++)
        {
            if (bits & (1 << i))
            {
                if (!text.empty())
                    text += ',';
                text += names[i];
            }
        }
        return text.empty() ? "none" : text;
    }

    std::string Hex2(uint8_t value)
    {
        char buffer[4];
        std::snprintf(buffer, sizeof(buffer), "%02X", value);
        return buffer;
    }

    std::string Hex4(uint16_t value)
    {
        char buffer[8];
        std::snprintf(buffer, sizeof(buffer), "%04X", value);
        return buffer;
    }

    /// Parses "<op> <hex>..." from tokens[first..]; returns the number of tokens used, 0 on error.
    size_t ParseCycle(const std::vector<std::string>& tokens, size_t first, Op& op, uint16_t& address, uint8_t& value)
    {
        const OpName* entry = FindOp(tokens[first]);
        if (entry == nullptr)
            return 0;
        op = entry->op;
        address = 0;
        value = 0;
        size_t used = 1;
        for (int i = 0; i < entry->operands; i++)
        {
            const size_t index = first + 1 + static_cast<size_t>(i);
            if (index >= tokens.size() || tokens[index] == "|" || tokens[index][0] == '@')
                return 0;
            uint32_t parsed = 0;
            if (!ParseHex(tokens[index], i == 0 ? 0xFFFFu : 0xFFu, parsed))
                return 0;
            if (i == 0)
                address = static_cast<uint16_t>(parsed);
            else
                value = static_cast<uint8_t>(parsed);
            used++;
        }
        return used;
    }
}

std::array<uint8_t, MultiSoundCycleRecord::ByteCount> MultiSoundCycleRecord::Bytes() const
{
    return { iorqge, ymWrite, ymA0, ymValue, saaWrite, saaA0, saaValue, sdWrite, readDriven, readValue, gsMap,
             ymChip, ymReadStatus, fmMuted, saaClock, romLock, gsData, gsCommand, gsPage, gsOutput, dataFlag,
             commandFlag, dacSample[0], dacSample[1], dacSample[2], dacSample[3],
             dacVolume[0], dacVolume[1], dacVolume[2], dacVolume[3] };
}

namespace MultiSoundTiming
{
    uint32_t HostTick(int halfT, double cpuMHz)
    {
        // 64 ticks per microsecond, two half T-states per T-state
        return static_cast<uint32_t>(std::lround(static_cast<double>(halfT) * 32.0 / cpuMHz));
    }

    int HalfStates(MultiSoundCycle::Op op)
    {
        switch (op)
        {
            case Op::M1: return 8;
            case Op::MemRead: case Op::MemWrite: case Op::GsMemRead: case Op::GsMemWrite: return 6;
            case Op::Out: case Op::In: case Op::GsOut: case Op::GsIn: return 8;
            default: return 0;
        }
    }

    uint32_t StrobeEnd(MultiSoundCycle::Op op)
    {
        switch (op)
        {
            case Op::M1: return 4;
            case Op::MemRead: case Op::MemWrite: case Op::GsMemRead: case Op::GsMemWrite: return 5;
            case Op::Out: case Op::In: case Op::GsOut: case Op::GsIn: return 7;
            default: return 0;
        }
    }
}

uint8_t MultiSoundDipBits(const MultiSoundOptions& options)
{
    return static_cast<uint8_t>((options.ym ? 0x01 : 0) | (options.saa ? 0x02 : 0) | (options.gs ? 0x04 : 0) |
                                (options.sd ? 0x08 : 0));
}

void ApplyMultiSoundDipBits(MultiSoundOptions& options, uint8_t bits)
{
    options.ym = (bits & 0x01) != 0;
    options.saa = (bits & 0x02) != 0;
    options.gs = (bits & 0x04) != 0;
    options.sd = (bits & 0x08) != 0;
}

std::string FormatMultiSoundDip(const MultiSoundOptions& options)
{
    return DipText(MultiSoundDipBits(options));
}

bool ParseMultiSoundScenario(const std::string& text, MultiSoundScenario& scenario, std::string& error)
{
    scenario = MultiSoundScenario{};
    std::stringstream lines(text);
    std::string line;
    int lineNumber = 0;
    bool headerOpen = true;
    uint64_t clock = 0;     // the sum of the '+D' deltas so far
    std::string pendingFrames;

    auto fail = [&](const std::string& message)
    {
        error = "line " + std::to_string(lineNumber) + ": " + message + ": '" + line + "'";
        return false;
    };

    while (std::getline(lines, line))
    {
        lineNumber++;
        const size_t comment = line.find('#');
        std::string body = comment == std::string::npos ? line : line.substr(0, comment);
        std::stringstream words(body);
        std::vector<std::string> tokens;
        std::string word;
        while (words >> word)
            tokens.push_back(word);
        if (tokens.empty())
            continue;

        const std::string& head = tokens[0];
        if (head == "mask" || head == "ram" || head == "cpu" || head == "frame" || head == "rate" ||
            head == "length")
        {
            if (!headerOpen)
                return fail("'" + head + "' is a header directive (before the first cycle)");
            if (tokens.size() != 2)
                return fail("expected one argument");
            if (head == "mask")
            {
                if (tokens[1] == "pro") scenario.options.ctrlMask = MultiSoundCtrlMask::Pro;
                else if (tokens[1] == "classic") scenario.options.ctrlMask = MultiSoundCtrlMask::Classic;
                else return fail("mask is pro or classic");
            }
            else if (head == "ram")
            {
                if (tokens[1] == "1m") scenario.options.gsRam = MultiSoundGsRam::OneMb;
                else if (tokens[1] == "2m") scenario.options.gsRam = MultiSoundGsRam::TwoMb;
                else return fail("ram is 1m or 2m");
            }
            else if (head == "frame" || head == "rate" || head == "length")
            {
                char* end = nullptr;
                const unsigned long long parsed = std::strtoull(tokens[1].c_str(), &end, 10);
                if (end == nullptr || *end != '\0' || parsed == 0 || (head != "length" && parsed > 0xFFFFFFFFull))
                    return fail(head + " takes a positive decimal tick count");
                if (head == "frame")
                    scenario.frameTicks = parsed;
                else if (head == "length")
                    scenario.lengthTicks = parsed;
                else
                    scenario.tickRate = static_cast<uint32_t>(parsed);
            }
            else
            {
                scenario.cpuMHz = std::atof(tokens[1].c_str());
                if (scenario.cpuMHz < 1.0 || scenario.cpuMHz > 28.0)
                    return fail("cpu is the host clock in MHz (1-28)");
            }
            continue;
        }

        if (head == "frame-end" || head == "frame-start")
        {
            if (tokens.size() != 1)
                return fail(head + " takes no operands");
            pendingFrames += head == "frame-end" ? 'E' : 'S';
            headerOpen = false;
            continue;
        }

        MultiSoundCycle cycle;
        cycle.sourceLine = lineNumber;
        cycle.framesBefore.swap(pendingFrames);
        pendingFrames.clear();
        if (head == "dip")
        {
            uint8_t bits = 0;
            if (tokens.size() != 2 || !ParseDip(tokens[1], bits))
                return fail("dip takes a list of ym,saa,gs,sd or 'none'");
            if (headerOpen)
            {
                ApplyMultiSoundDipBits(scenario.options, bits);
                continue;
            }
            cycle.op = Op::Dip;
            cycle.value = bits;
        }
        else if (head == "par")
        {
            // par <host cycle> | <gs cycle> @<ticks>
            Op hostOp = Op::Reset;
            const size_t hostUsed = tokens.size() > 1 ? ParseCycle(tokens, 1, hostOp, cycle.address, cycle.value) : 0;
            if (hostUsed == 0 || !(hostOp == Op::Out || hostOp == Op::In || hostOp == Op::MemRead ||
                                   hostOp == Op::MemWrite || hostOp == Op::M1))
                return fail("par needs a host cycle first");
            size_t next = 1 + hostUsed;
            if (next >= tokens.size() || tokens[next] != "|")
                return fail("par needs '|' between the host and the GS cycle");
            next++;
            Op gsOp = Op::Reset;
            const size_t gsUsed = next < tokens.size() ? ParseCycle(tokens, next, gsOp, cycle.gsAddress, cycle.gsValue) : 0;
            if (gsUsed == 0 || !(gsOp == Op::GsOut || gsOp == Op::GsIn || gsOp == Op::GsMemRead || gsOp == Op::GsMemWrite))
                return fail("par needs a GS cycle after '|'");
            next += gsUsed;
            if (next + 1 != tokens.size() || tokens[next].size() < 2 || tokens[next][0] != '@')
                return fail("par ends with @<offset in ticks>");
            char* end = nullptr;
            const unsigned long offset = std::strtoul(tokens[next].c_str() + 1, &end, 10);
            if (end == nullptr || *end != '\0' || offset > 1000)
                return fail("bad @offset (decimal ticks, 0-1000)");
            cycle.op = Op::Par;
            cycle.hostOp = hostOp;
            cycle.gsOp = gsOp;
            cycle.gsOffset = static_cast<uint32_t>(offset);
        }
        else
        {
            Op op = Op::Reset;
            uint16_t address = 0;
            uint8_t value = 0;
            const size_t used = ParseCycle(tokens, 0, op, address, value);
            if (used == 0)
                return fail("unknown cycle or wrong operands");
            cycle.op = op;
            cycle.address = address;
            cycle.value = value;
            // Trace annotations: '=XX' / '=--' (reads), '*N' (reads), '+D' (any cycle)
            const bool read = op == Op::In || op == Op::GsIn;
            for (size_t i = used; i < tokens.size(); i++)
            {
                const std::string& token = tokens[i];
                char* end = nullptr;
                if (token[0] == '=' && read && !cycle.observed)
                {
                    cycle.observed = true;
                    uint32_t parsed = 0;
                    if (token == "=--")
                        cycle.observedDriven = false;
                    else if (token.size() == 3 && ParseHex(token.substr(1), 0xFF, parsed))
                    {
                        cycle.observedDriven = true;
                        cycle.observedValue = static_cast<uint8_t>(parsed);
                    }
                    else
                        return fail("a read's observed value is =XX or =--");
                }
                else if (token[0] == '*' && read && cycle.repeat == 1)
                {
                    const unsigned long long parsed = std::strtoull(token.c_str() + 1, &end, 10);
                    if (token.size() < 2 || end == nullptr || *end != '\0' || parsed < 2 || parsed > 0xFFFFFFFFull)
                        return fail("a repeat count is *N with N >= 2");
                    cycle.repeat = static_cast<uint32_t>(parsed);
                }
                else if (token[0] == '+' && !cycle.timed)
                {
                    const unsigned long long parsed = std::strtoull(token.c_str() + 1, &end, 10);
                    if (token.size() < 2 || end == nullptr || *end != '\0')
                        return fail("a time delta is +D (decimal host ticks)");
                    cycle.timed = true;
                    clock += parsed;
                    cycle.time = clock;
                }
                else
                    return fail("unknown annotation");
            }
        }
        headerOpen = false;
        scenario.cycles.push_back(cycle);
    }
    scenario.trailingFrames = pendingFrames;
    return true;
}

std::string FormatMultiSoundCycle(const MultiSoundCycle& cycle)
{
    auto one = [](Op op, uint16_t address, uint8_t value)
    {
        const OpName* entry = FindOp(op);
        std::string text = entry ? entry->name : "?";
        if (entry && entry->operands >= 1)
            text += " " + Hex4(address);
        if (entry && entry->operands >= 2)
            text += " " + Hex2(value);
        return text;
    };

    switch (cycle.op)
    {
        case Op::Dip: return "dip " + DipText(cycle.value);
        case Op::Par:
        {
            return "par " + one(cycle.hostOp, cycle.address, cycle.value) + " | " +
                   one(cycle.gsOp, cycle.gsAddress, cycle.gsValue) + " @" + std::to_string(cycle.gsOffset);
        }
        default: return one(cycle.op, cycle.address, cycle.value);
    }
}

std::string FormatMultiSoundRecord(const MultiSoundCycleRecord& r)
{
    std::string text = "ge=";
    text += r.iorqge == 2 ? "-" : (r.iorqge ? "1" : "0");
    text += " yw=";
    if (r.ymWrite)
        text += std::to_string(r.ymWrite) + ":" + std::to_string(r.ymA0) + ":" + Hex2(r.ymValue);
    else
        text += "-";
    text += " sw=";
    text += r.saaWrite ? std::to_string(r.saaA0) + ":" + Hex2(r.saaValue) : std::string("-");
    text += " sd=";
    text += r.sdWrite ? Hex2(r.sdWrite) : std::string("-");
    text += " rd=";
    text += r.readDriven ? Hex2(r.readValue) : std::string("-");
    text += " map=";
    text += r.gsMap == 0xFF ? std::string("-") : Hex2(r.gsMap);
    text += " | sel=" + std::to_string(r.ymChip) + " st=" + std::to_string(r.ymReadStatus) +
            " fm=" + std::string(r.fmMuted ? "mute" : "on") + " saa=" + std::string(r.saaClock ? "on" : "off") +
            " lock=" + std::to_string(r.romLock) +
            " gs=" + Hex2(r.gsData) + "," + Hex2(r.gsCommand) + "," + Hex2(r.gsPage) + "," + Hex2(r.gsOutput) +
            " fl=" + std::to_string(r.dataFlag) + std::to_string(r.commandFlag) + " dac=";
    for (int i = 0; i < 4; i++)
    {
        if (i)
            text += ",";
        text += Hex2(r.dacSample[static_cast<size_t>(i)]) + "/" + Hex2(r.dacVolume[static_cast<size_t>(i)]);
    }
    return text;
}

namespace
{
    /// The SounDrive decode (L9) without the DIP and lock terms: a write here may change a DAC register
    bool TraceSoundrivePort(uint16_t port) { return (port & 0x00AFu) == 0x000Fu; }
    int TraceSoundriveChannel(uint16_t port) { return static_cast<int>(((port >> 5) & 0x02u) | ((port >> 4) & 0x01u)); }

    /// Reads that change nothing in the CPLD (a repeat is the same cycle again): all host reads but #B3, GS reads of
    /// ports other than 2, 3, 5, #0A, #0B
    bool TraceReadIsIdempotent(const MultiSoundBusEvent& event)
    {
        if (event.kind == MultiSoundBusEvent::Kind::HostIn)
            return (event.address & 0x00FFu) != 0x00B3u;
        const uint8_t port = event.address & 0x0Fu;
        return !(port == 0x2 || port == 0x3 || port == 0x5 || port == 0xA || port == 0xB);
    }
}

void MultiSoundTraceWriter::OnMultiSoundBus(const MultiSoundBusEvent& event)
{
    using Kind = MultiSoundBusEvent::Kind;
    switch (event.kind)
    {
        case Kind::GsDacFetch:
        {
            _stats.dacFetches++;
            const int channel = (event.address >> 8) & 0x03;
            if (_dacByte[channel] == event.value)
            {
                _stats.dacUnchanged++;
                return;
            }
            if (_stats.dacLines >= _dacFetchBudget)
            {
                _stats.dacOverBudget++;
                if (!_budgetNoted)
                {
                    Flush();
                    _body += "# DAC fetch budget reached: the GS DAC fetches after this line are not in the trace\n";
                    _budgetNoted = true;
                }
                return;
            }
            _dacByte[channel] = event.value;
            _stats.dacLines++;
            Flush();
            Emit(event, 1);
            return;
        }
        case Kind::HostOut:
            _stats.hostWrites++;
            if (TraceSoundrivePort(event.address))
                _dacByte[TraceSoundriveChannel(event.address)] = -1;
            break;
        case Kind::BusReset:
            _stats.resets++;
            std::fill(std::begin(_dacByte), std::end(_dacByte), -1);
            break;
        case Kind::HostIn:
            _stats.hostReads++;
            break;
        case Kind::GsIn:
        case Kind::GsOut:
            _stats.gsPortCycles++;
            break;
        case Kind::FrameStart:
        case Kind::FrameEnd:
            Flush();
            _body += event.kind == Kind::FrameEnd ? "frame-end\n" : "frame-start\n";
            _stats.lines++;
            _stats.frames += event.kind == Kind::FrameEnd ? 1 : 0;
            return;
    }

    const bool read = event.kind == Kind::HostIn || event.kind == Kind::GsIn;
    if (read && TraceReadIsIdempotent(event))
    {
        const MultiSoundBusEvent& p = _pending.event;
        if (_pending.active && p.kind == event.kind && p.address == event.address && p.value == event.value &&
            p.drives == event.drives && (event.kind != Kind::HostIn || p.m1 == event.m1))
        {
            _pending.count++;
            return;
        }
        Flush();
        _pending.active = true;
        _pending.event = event;
        _pending.count = 1;
        return;
    }
    Flush();
    Emit(event, 1);
}

void MultiSoundTraceWriter::Flush()
{
    if (!_pending.active)
        return;
    _pending.active = false;
    _stats.longestRun = std::max(_stats.longestRun, _pending.count);
    Emit(_pending.event, _pending.count);
}

void MultiSoundTraceWriter::Emit(const MultiSoundBusEvent& event, uint32_t count)
{
    using Kind = MultiSoundBusEvent::Kind;
    char line[96];
    const bool host = event.kind == Kind::HostOut || event.kind == Kind::HostIn || event.kind == Kind::BusReset;
    if (host && event.kind != Kind::BusReset && (!_haveM1 || event.m1 != _m1))
    {
        std::snprintf(line, sizeof(line), "m1 %04X\n", event.m1);
        _body += line;
        _stats.lines++;
        _haveM1 = true;
        _m1 = event.m1;
    }
    std::string observed;
    if (event.kind == Kind::HostIn || event.kind == Kind::GsIn)
    {
        observed = event.drives ? "=" + Hex2(event.value) : std::string("=--");
        if (count > 1)
            observed += " *" + std::to_string(count);
    }
    std::string delta;
    if (host)
    {
        const uint64_t d = event.time > _lastTime ? event.time - _lastTime : 0;
        _lastTime = std::max(_lastTime, event.time);
        delta = " +" + std::to_string(d);
    }
    switch (event.kind)
    {
        case Kind::HostOut: std::snprintf(line, sizeof(line), "out %04X %02X%s\n", event.address, event.value, delta.c_str()); break;
        case Kind::HostIn: std::snprintf(line, sizeof(line), "in %04X %s%s\n", event.address, observed.c_str(), delta.c_str()); break;
        case Kind::BusReset: std::snprintf(line, sizeof(line), "reset%s\n", delta.c_str()); break;
        case Kind::GsOut: std::snprintf(line, sizeof(line), "gout %04X %02X\n", event.address & 0xFFu, event.value); break;
        case Kind::GsIn: std::snprintf(line, sizeof(line), "gin %04X %s\n", event.address & 0xFFu, observed.c_str()); break;
        case Kind::GsDacFetch: std::snprintf(line, sizeof(line), "gmr %04X %02X\n", event.address, event.value); break;
        case Kind::FrameStart:
        case Kind::FrameEnd: line[0] = '\0'; break;
    }
    _body += line;
    _stats.lines++;
    if (event.kind == Kind::HostIn)
        _stats.hostReadLines++;
    else if (event.kind == Kind::GsIn || event.kind == Kind::GsOut)
        _stats.gsPortLines++;
}

void MultiSoundTraceWriter::StartUp(uint64_t resetTime)
{
    MultiSoundBusEvent reset;
    reset.kind = MultiSoundBusEvent::Kind::BusReset;
    reset.time = resetTime;
    OnMultiSoundBus(reset);
    MultiSoundBusEvent start;
    start.kind = MultiSoundBusEvent::Kind::FrameStart;
    OnMultiSoundBus(start);
}

std::string MultiSoundTraceWriter::Text(const std::string& header)
{
    Flush();
    return header + _body;
}

bool ParseMultiSoundTraceRtl(const std::string& text, MultiSoundTraceRtl& rtl)
{
    rtl = MultiSoundTraceRtl{};
    std::stringstream lines(text);
    std::string line;
    bool haveHash = false;
    while (std::getline(lines, line))
    {
        if (line.empty() || line[0] == '#')
            continue;
        std::stringstream words(line);
        std::string key;
        words >> key;
        if (key == "cycles")
            words >> rtl.cycles;
        else if (key == "hash")
        {
            std::string value;
            words >> value;
            rtl.hash = std::strtoull(value.c_str(), nullptr, 16);
            haveHash = true;
        }
        else if (key == "reads")
            words >> rtl.reads;
        else if (key == "values")
            words >> rtl.values;
        else if (key == "at")
        {
            size_t at = 0;
            std::string value;
            words >> at >> value;
            rtl.checkpoints.emplace_back(at, std::strtoull(value.c_str(), nullptr, 16));
        }
        else
            return false;
        if (words.fail())
            return false;
    }
    return haveHash && rtl.cycles > 0;
}

bool MultiSoundTraceReadIsYm(const MultiSoundLogic& logic, uint16_t port)
{
    const MultiSoundReadResult::Source source = logic.Peek(port).source;
    return source == MultiSoundReadResult::Source::YmStatus || source == MultiSoundReadResult::Source::YmRegister;
}

bool MultiSoundTraceReadMatches(const MultiSoundCycle& cycle, const MultiSoundCycleRecord& record, bool ymRead,
                                std::string& why)
{
    if (!cycle.observed)
        return true;
    if ((record.readDriven != 0) != cycle.observedDriven)
    {
        why = std::string("the emulator saw the bus ") + (cycle.observedDriven ? "driven" : "not driven") +
              ", the model " + (record.readDriven ? "drives it" : "does not");
        return false;
    }
    if (cycle.observedDriven && !ymRead && record.readValue != cycle.observedValue)
    {
        why = "the emulator read " + Hex2(cycle.observedValue) + ", the model drives " + Hex2(record.readValue);
        return false;
    }
    return true;
}

uint64_t HashMultiSoundRecord(uint64_t hash, const MultiSoundCycleRecord& record)
{
    for (uint8_t byte : record.Bytes())
    {
        hash ^= byte;
        hash *= 0x100000001B3ull;
    }
    return hash;
}

uint16_t MultiSoundEventCode(const MultiSoundCycleRecord& r)
{
    uint16_t code = 0;
    if (r.iorqge == 1) code |= 0x0001;
    code |= static_cast<uint16_t>((r.ymWrite & 0x03) << 1);
    if (r.ymWrite && r.ymA0) code |= 0x0008;
    if (r.saaWrite) code |= 0x0010;
    if (r.saaWrite && r.saaA0) code |= 0x0020;
    code |= static_cast<uint16_t>((r.sdWrite & 0x0F) << 6);
    if (r.readDriven) code |= 0x0400;
    return code;
}

MultiSoundLogicBus::MultiSoundLogicBus(const MultiSoundScenario& scenario) : _logic(scenario.options), _header(scenario)
{
    _logic.Reset();
}

void MultiSoundLogicBus::ExecuteOne(Op op, uint16_t address, uint8_t value, MultiSoundCycleRecord& record)
{
    switch (op)
    {
        case Op::M1:
            _logic.OnM1(address);
            break;
        case Op::MemRead:
        case Op::MemWrite:
            break;
        case Op::Out:
        {
            record.iorqge = _logic.Iorqge(address) ? 1 : 0;
            for (const MultiSoundBusAction& action : _logic.Write(address, value))
            {
                using Kind = MultiSoundBusAction::Kind;
                switch (action.kind)
                {
                    case Kind::YmAddress:
                    case Kind::YmData:
                        record.ymWrite |= static_cast<uint8_t>(1u << action.chip);
                        record.ymA0 = action.kind == Kind::YmData ? 1 : 0;
                        record.ymValue = action.value;
                        break;
                    case Kind::SaaAddress:
                    case Kind::SaaData:
                        record.saaWrite = 1;
                        record.saaA0 = action.kind == Kind::SaaAddress ? 1 : 0;
                        record.saaValue = action.value;
                        break;
                    case Kind::SoundriveSample:
                        record.sdWrite |= static_cast<uint8_t>(1u << action.chip);
                        break;
                    default:
                        break;
                }
            }
            break;
        }
        case Op::In:
        {
            record.iorqge = _logic.Iorqge(address) ? 1 : 0;
            const MultiSoundReadResult result = _logic.Read(address);
            using Source = MultiSoundReadResult::Source;
            if (result.Drives())
            {
                record.readDriven = 1;
                if (result.source == Source::YmStatus)
                    record.readValue = MultiSoundFakeYmValue(result.chip, 0);
                else if (result.source == Source::YmRegister)
                    record.readValue = MultiSoundFakeYmValue(result.chip, 1);
                else
                    record.readValue = result.value;
            }
            break;
        }
        case Op::GsOut:
            _logic.GsPortWrite(static_cast<uint8_t>(address), value);
            break;
        case Op::GsIn:
            record.readDriven = 1;
            record.readValue = _logic.GsPortRead(static_cast<uint8_t>(address));
            break;
        case Op::GsMemRead:
        case Op::GsMemWrite:
        {
            const MultiSoundGsMapping mapping = _logic.GsMemoryMap(address);
            record.gsMap = static_cast<uint8_t>((static_cast<uint8_t>(mapping.chip) << 4) | mapping.gma);
            if (op == Op::GsMemRead)
                _logic.GsMemoryRead(address, value);
            break;
        }
        case Op::Reset:
            _logic.Reset();
            break;
        case Op::Dip:
        {
            MultiSoundOptions options = _logic.Options();
            ApplyMultiSoundDipBits(options, value);
            _logic.Configure(options);
            break;
        }
        case Op::Par:
            break;
    }
}

MultiSoundCycleRecord MultiSoundLogicBus::Execute(const MultiSoundCycle& cycle)
{
    MultiSoundCycleRecord record;
    if (cycle.op != Op::Par)
    {
        ExecuteOne(cycle.op, cycle.address, cycle.value, record);
    }
    else
    {
        // Two strobes overlapping in time: every DAC register is rewritten on each 32 MHz edge while a strobe is
        // active, so the one whose last active edge comes later wins. On the same last edge the RTL's priority
        // decides: the GS sample beats the SounDrive sample, the SounDrive volume beats the GS volume.
        const Op hostOp = cycle.hostOp;
        const uint32_t offset = cycle.gsOffset;
        const uint32_t hostEnd = MultiSoundTiming::HostTick(static_cast<int>(MultiSoundTiming::StrobeEnd(hostOp)), _header.cpuMHz);
        const uint32_t gsEnd = offset + MultiSoundTiming::GsTick(static_cast<int>(MultiSoundTiming::StrobeEnd(cycle.gsOp)));
        const uint32_t hostLastEdge = (hostEnd - 1) & ~1u;     // posedges are on even ticks
        const uint32_t gsLastEdge = (gsEnd - 1) & ~1u;
        bool gsFirst = gsLastEdge < hostLastEdge;
        if (gsLastEdge == hostLastEdge)
            gsFirst = cycle.gsOp != Op::GsMemRead;

        MultiSoundCycleRecord gsRecord;
        if (gsFirst)
        {
            ExecuteOne(cycle.gsOp, cycle.gsAddress, cycle.gsValue, gsRecord);
            ExecuteOne(hostOp, cycle.address, cycle.value, record);
        }
        else
        {
            ExecuteOne(hostOp, cycle.address, cycle.value, record);
            ExecuteOne(cycle.gsOp, cycle.gsAddress, cycle.gsValue, gsRecord);
        }
        record.gsMap = gsRecord.gsMap;
        if (gsRecord.readDriven && !record.readDriven)
        {
            record.readDriven = gsRecord.readDriven;
            record.readValue = gsRecord.readValue;
        }
    }
    CaptureState(record);
    return record;
}

void MultiSoundLogicBus::CaptureState(MultiSoundCycleRecord& record) const
{
    const MultiSoundLatches& latches = _logic.Latches();
    record.ymChip = latches.ymChip;
    record.ymReadStatus = latches.ymReadStatus ? 1 : 0;
    record.fmMuted = latches.fmMuted ? 1 : 0;
    record.saaClock = latches.saaClock ? 1 : 0;
    record.romLock = latches.romLock ? 1 : 0;
    record.gsData = latches.gsData;
    record.gsCommand = latches.gsCommand;
    record.gsPage = latches.gsPage;
    record.gsOutput = latches.gsOutput;
    record.dataFlag = latches.dataFlag ? 1 : 0;
    record.commandFlag = latches.commandFlag ? 1 : 0;
    for (int i = 0; i < 4; i++)
    {
        const MultiSoundDacState dac = _logic.Dac(i);
        record.dacSample[static_cast<size_t>(i)] = dac.sample;
        record.dacVolume[static_cast<size_t>(i)] = dac.volume;
    }
}
