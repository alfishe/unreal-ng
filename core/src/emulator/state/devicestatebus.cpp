// DeviceState::Bus* - the bus analyzer's reports every automation interface renders (declared in
// emulator/state/devicestate.h). Design: docs/inprogress/2026-10-11-bus-analyzer/technical-design.md §7

#include "stdafx.h"

#include "emulator/state/devicestate.h"

#include <cstdio>
#include <sstream>

#include "common/stringhelper.h"
#include "emulator/bus/busexport.h"
#include "emulator/bus/busmanager.h"
#include "emulator/bus/bustap.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/sdcard/sdcardspi.h"

namespace
{
std::string Hex8(unsigned value) { return StringHelper::Format("0x%02X", value & 0xFFu); }
std::string Hex16(unsigned value) { return StringHelper::Format("0x%04X", value & 0xFFFFu); }

BusManager* Manager(EmulatorContext* context)
{
    return context ? context->pBusManager : nullptr;
}

StateNode Unavailable(const std::string& description)
{
    StateNode n = StateNode::Object();
    n["available"] = false;
    n["description"] = description;
    return n;
}

StateNode NoBus(BusManager& manager, const std::string& bus)
{
    std::string ids;
    for (const std::string& id : manager.Ids())
        ids += (ids.empty() ? "" : ", ") + id;
    return Unavailable("no bus '" + bus + "'; this machine has: " + (ids.empty() ? std::string("none") : ids));
}

StateNode CardNode(SdCardSpi& card)
{
    StateNode n = StateNode::Object();
    n["present"] = card.present();
    if (card.present())
    {
        n["type"] = card.isSdhc() ? "SDHC" : "SDSC";
        n["state"] = card.stateName();
        n["in_transfer"] = card.InTransfer();
    }
    n["timing"] = SdCardSpi::TimingName(card.timing());
    n["seed"] = card.timingSeed();
    if (card.TimingRequestPending())
        n["timing_pending"] = true;
    n["blocks_read"] = card.blocksRead();
    n["blocks_written"] = card.blocksWritten();
    return n;
}

/// The bus header every report starts with
StateNode Header(BusTap& tap)
{
    const BusTapStatus st = tap.Status();
    StateNode n = StateNode::Object();
    n["available"] = true;
    n["id"] = st.id;
    n["protocol"] = BusProtocolName(st.protocol);
    n["label"] = st.label;
    n["byte_time_ns"] = st.byteTimeNs;
    StateNode devices = StateNode::Array();
    for (const BusDevice& d : st.devices)
    {
        StateNode dev = StateNode::Object();
        dev["slot"] = int(d.slot);
        dev["name"] = d.name;
        tap.WithSdCard(d.slot, [&](SdCardSpi& card) { dev["sd"] = CardNode(card); });
        devices.push(std::move(dev));
    }
    n["devices"] = std::move(devices);
    StateNode trace = StateNode::Object();
    trace["enabled"] = st.tracing;
    trace["protocol_only"] = st.protocolOnly;
    trace["size"] = static_cast<uint64_t>(st.size);
    trace["capacity"] = static_cast<uint64_t>(st.capacity);
    trace["evicted"] = st.evicted;
    trace["rewound"] = st.rewound;
    trace["last_seq"] = st.lastSeq;
    n["trace"] = std::move(trace);
    StateNode checks = StateNode::Object();
    checks["enabled"] = st.checking;
    checks["findings"] = st.findings;
    StateNode byRule = StateNode::Object();
    for (size_t i = 0; i < st.byRule.size(); i++)
        if (st.byRule[i])
            byRule[BusRuleName(static_cast<BusRule>(i))] = st.byRule[i];
    checks["by_rule"] = std::move(byRule);
    n["checks"] = std::move(checks);
    return n;
}

StateNode EventNode(const BusTraceEvent& e)
{
    StateNode ev = StateNode::Object();
    ev["seq"] = e.seq;
    ev["frame"] = int64_t(e.frame);
    ev["t"] = int64_t(e.t);
    ev["pc"] = Hex16(e.pc);
    ev["source"] = BusSourceName(e.source);
    ev["slot"] = e.slot == BusTap::kNoSlot ? StateNode("none") : StateNode(int(e.slot));
    ev["kind"] = BusEventKindName(e.kind);
    switch (e.kind)
    {
        case BusEventKind::SpiByte:
            ev["mosi"] = Hex8(e.a);
            ev["miso"] = Hex8(e.b);
            ev["read"] = (e.flags & BusEventFlag::Read) != 0;
            break;
        case BusEventKind::SpiEarly:
            ev["write"] = (e.flags & BusEventFlag::Write) != 0;
            if (e.flags & BusEventFlag::Write)
                ev["value"] = Hex8(e.a);
            break;
        case BusEventKind::SdCommand:
            ev["command"] = SdCommandName(e.a, (e.flags & BusEventFlag::App) != 0);
            ev["argument"] = StringHelper::Format("0x%08X", e.arg);
            ev["r1"] = e.b == 0xFF ? StateNode("none") : StateNode(Hex8(e.b));
            ev["crc_valid"] = (e.flags & BusEventFlag::CrcValid) != 0;
            break;
        case BusEventKind::SdReadBlock:
            ev["block"] = e.block;
            break;
        case BusEventKind::SdWriteBlock:
            ev["block"] = e.block;
            ev["ok"] = (e.flags & BusEventFlag::Ok) != 0;
            break;
        case BusEventKind::AtaRegRead:
        case BusEventKind::AtaRegWrite:
            ev["register"] = AtaRegisterName(e.a, e.kind == BusEventKind::AtaRegWrite);
            ev["value"] = Hex8(e.b);
            break;
        case BusEventKind::AtaCommand:
            ev["command"] = AtaCommandName(e.a);
            break;
        case BusEventKind::AtaData:
            ev["word"] = Hex16(e.arg);
            ev["write"] = (e.flags & BusEventFlag::Write) != 0;
            break;
        default:
            break;
    }
    return ev;
}

StateNode FindingNode(const BusFinding& f)
{
    StateNode n = StateNode::Object();
    n["seq"] = f.seq;
    n["event_seq"] = f.eventSeq;
    n["frame"] = int64_t(f.frame);
    n["t"] = int64_t(f.t);
    n["pc"] = Hex16(f.pc);
    n["source"] = BusSourceName(f.source);
    n["slot"] = f.slot == BusTap::kNoSlot ? StateNode("none") : StateNode(int(f.slot));
    n["rule"] = BusRuleName(f.rule);
    n["severity"] = BusSeverityName(BusRuleSeverity(f.rule));
    n["value"] = Hex8(f.value);
    n["detail"] = BusRuleDetail(f.rule);
    return n;
}
}  // namespace

namespace
{
/// region <Text (the CLI's rendering; the MCP tool renders the same lines from the JSON)>
std::string Str(const StateNode& n, const char* key)
{
    const StateNode* v = n.find(key);
    if (!v)
        return {};
    switch (v->kind)
    {
        case StateNode::Kind::String: return v->s;
        case StateNode::Kind::Int: return std::to_string(v->i);
        case StateNode::Kind::Bool: return v->b ? "true" : "false";
        case StateNode::Kind::Double:
        {
            std::ostringstream out;
            out << v->d;
            return out.str();
        }
        default: return {};
    }
}

bool Flag(const StateNode& n, const char* key)
{
    const StateNode* v = n.find(key);
    return v && v->kind == StateNode::Kind::Bool && v->b;
}

const StateNode& Child(const StateNode& n, const char* key)
{
    static const StateNode empty;
    const StateNode* v = n.find(key);
    return v ? *v : empty;
}

void BusLineText(std::ostringstream& out, const StateNode& bus)
{
    const StateNode& trace = Child(bus, "trace");
    const StateNode& checks = Child(bus, "checks");
    out << Str(bus, "id") << " (" << Str(bus, "protocol") << "): " << Str(bus, "label") << "\n";
    out << "  trace " << (Flag(trace, "enabled") ? (Flag(trace, "protocol_only") ? "on (protocol only)" : "on") : "off") << " size=" << Str(trace, "size") << "/" << Str(trace, "capacity")
        << " evicted=" << Str(trace, "evicted") << " rewound=" << Str(trace, "rewound") << " last_seq=" << Str(trace, "last_seq")
        << "; checks " << (Flag(checks, "enabled") ? "on" : "off") << " findings=" << Str(checks, "findings");
    const StateNode& byRule = Child(checks, "by_rule");
    if (!byRule.members.empty())
    {
        out << " (";
        for (size_t i = 0; i < byRule.members.size(); i++)
            out << (i ? ", " : "") << byRule.members[i].first << "=" << byRule.members[i].second.i;
        out << ")";
    }
    out << "; byte_time_ns=" << Str(bus, "byte_time_ns");
    for (const StateNode& dev : Child(bus, "devices").items)
    {
        out << "\n  slot " << Str(dev, "slot") << ": " << Str(dev, "name");
        if (const StateNode* sd = dev.find("sd"))
        {
            out << " sd present=" << (Flag(*sd, "present") ? "yes" : "no");
            if (Flag(*sd, "present"))
                out << " type=" << Str(*sd, "type") << " state=" << Str(*sd, "state");
            out << " timing=" << Str(*sd, "timing") << " seed=" << Str(*sd, "seed");
            if (Flag(*sd, "timing_pending"))
                out << " (pending: applied at the card's next access)";
            out << " blocks_read=" << Str(*sd, "blocks_read") << " blocks_written=" << Str(*sd, "blocks_written");
        }
    }
}

void EventLineText(std::ostringstream& out, const StateNode& e)
{
    const std::string kind = Str(e, "kind");
    out << "#" << Str(e, "seq") << " f" << Str(e, "frame") << " t" << Str(e, "t") << " pc=" << Str(e, "pc") << " " << Str(e, "source")
        << " slot=" << Str(e, "slot") << " " << kind;
    if (kind == "byte")
        out << " mosi=" << Str(e, "mosi") << " miso=" << Str(e, "miso") << (Flag(e, "read") ? " (read)" : "");
    else if (kind == "early")
        out << (Flag(e, "write") ? " write " + Str(e, "value") : std::string(" read"));
    else if (kind == "command")
        out << " " << Str(e, "command") << " arg=" << Str(e, "argument") << " r1=" << Str(e, "r1") << (Flag(e, "crc_valid") ? "" : " crc=bad");
    else if (kind == "read")
        out << " block=" << Str(e, "block");
    else if (kind == "write")
        out << " block=" << Str(e, "block") << (Flag(e, "ok") ? " stored" : " refused");
    else if (kind == "reg_read" || kind == "reg_write")
        out << " " << Str(e, "register") << "=" << Str(e, "value");
    else if (kind == "ata_command")
        out << " " << Str(e, "command");
    else if (kind == "data")
        out << " word=" << Str(e, "word") << (Flag(e, "write") ? " (write)" : "");
}

void FindingLineText(std::ostringstream& out, const StateNode& f)
{
    out << "#" << Str(f, "seq") << " " << Str(f, "severity") << " " << Str(f, "rule") << " f" << Str(f, "frame") << " t" << Str(f, "t")
        << " pc=" << Str(f, "pc") << " " << Str(f, "source") << " slot=" << Str(f, "slot") << " value=" << Str(f, "value") << " event=#"
        << Str(f, "event_seq") << " - " << Str(f, "detail");
}
/// endregion
}  // namespace

namespace DeviceState
{
std::string BusText(const StateNode& report)
{
    std::ostringstream out;
    if (!Flag(report, "available"))
        return "Bus analyzer: " + Str(report, "description");
    if (const StateNode* buses = report.find("buses"))
    {
        if (buses->items.empty())
            return "No buses on this machine";
        for (size_t i = 0; i < buses->items.size(); i++)
        {
            if (i)
                out << "\n";
            BusLineText(out, buses->items[i]);
        }
        return out.str();
    }
    BusLineText(out, report);
    if (const StateNode* events = report.find("events"))
    {
        out << "\n" << events->items.size() << " event(s)";
        if (!Flag(Child(report, "trace"), "enabled"))
            out << " - tracing is off: bus <id> on";
        for (const StateNode& e : events->items)
        {
            out << "\n";
            EventLineText(out, e);
        }
    }
    if (const StateNode* findings = report.find("findings"))
    {
        out << "\n" << findings->items.size() << " finding(s)";
        if (!Flag(Child(report, "checks"), "enabled"))
            out << " - checks are off: bus <id> on checks";
        for (const StateNode& f : findings->items)
        {
            out << "\n";
            FindingLineText(out, f);
        }
    }
    if (report.find("slot"))
        out << "\nslot " << Str(report, "slot");
    return out.str();
}

StateNode BusList(EmulatorContext* context)
{
    BusManager* manager = Manager(context);
    if (!manager)
        return Unavailable("no bus manager (no emulator instance)");
    StateNode n = StateNode::Object();
    n["available"] = true;
    StateNode buses = StateNode::Array();
    manager->ForEach([&](BusTap& tap) { buses.push(Header(tap)); });
    n["buses"] = std::move(buses);
    return n;
}

StateNode BusEvents(EmulatorContext* context, const std::string& bus, const BusEventQuery& query)
{
    BusManager* manager = Manager(context);
    if (!manager)
        return Unavailable("no bus manager (no emulator instance)");
    StateNode n;
    const bool found = manager->With(bus, [&](BusTap& tap) {
        n = Header(tap);
        StateNode events = StateNode::Array();
        for (const BusTraceEvent& e : tap.Events(query))
            events.push(EventNode(e));
        n["events"] = std::move(events);
    });
    return found ? n : NoBus(*manager, bus);
}

StateNode BusFindings(EmulatorContext* context, const std::string& bus, const BusFindingQuery& query)
{
    BusManager* manager = Manager(context);
    if (!manager)
        return Unavailable("no bus manager (no emulator instance)");
    StateNode n;
    const bool found = manager->With(bus, [&](BusTap& tap) {
        n = Header(tap);
        StateNode findings = StateNode::Array();
        for (const BusFinding& f : tap.Findings(query))
            findings.push(FindingNode(f));
        n["findings"] = std::move(findings);
    });
    return found ? n : NoBus(*manager, bus);
}

StateNode BusControl(EmulatorContext* context, const std::string& bus, int enable, int checks, bool clear, size_t capacity,
                     int protocolOnly)
{
    BusManager* manager = Manager(context);
    if (!manager)
        return Unavailable("no bus manager (no emulator instance)");
    if (bus == "all")
    {
        manager->ForEach([&](BusTap& tap) { tap.Control(enable, checks, clear, capacity, protocolOnly); });
        return BusList(context);
    }
    StateNode n;
    const bool found = manager->With(bus, [&](BusTap& tap) {
        tap.Control(enable, checks, clear, capacity, protocolOnly);
        n = Header(tap);
    });
    return found ? n : NoBus(*manager, bus);
}

StateNode BusSdTiming(EmulatorContext* context, const std::string& bus, int slot, const std::string& profile, int64_t seed)
{
    BusManager* manager = Manager(context);
    if (!manager)
        return Unavailable("no bus manager (no emulator instance)");
    SdCardSpi::Timing timing = SdCardSpi::Timing::Ideal;
    if (!profile.empty() && !SdCardSpi::TimingFromName(profile, timing))
        return Unavailable("profile: '" + profile + "' is not one of ideal, realistic, slow");
    StateNode n;
    std::string error;
    const bool found = manager->With(bus, [&](BusTap& tap) {
        const std::vector<uint8_t> slots = tap.SdCardSlots();
        if (slots.empty())
        {
            error = "bus '" + bus + "' has no SD card";
            return;
        }
        const uint8_t target = slot >= 0 ? static_cast<uint8_t>(slot) : slots.front();
        const bool hasCard = tap.WithSdCard(target, [&](SdCardSpi& card) {
            if (!profile.empty())
                card.RequestTiming(timing, seed > 0 ? static_cast<uint64_t>(seed) : card.timingSeed());
        });
        if (!hasCard)
        {
            error = "bus '" + bus + "' has no SD card in slot " + std::to_string(slot);
            return;
        }
        n = Header(tap);
        n["slot"] = int(target);
    });
    if (!found)
        return NoBus(*manager, bus);
    return error.empty() ? n : Unavailable(error);
}

std::string BusExport(EmulatorContext* context, const std::string& bus, const std::string& format, const BusEventQuery& query,
                      std::string& error)
{
    BusManager* manager = Manager(context);
    if (!manager)
    {
        error = "no bus manager (no emulator instance)";
        return {};
    }
    std::string out;
    const bool found = manager->With(bus, [&](BusTap& tap) {
        const BusTapStatus status = tap.Status();
        const std::vector<BusTraceEvent> events = tap.Events(query);
        BusExportTiming timing;
        timing.frameTStates = context->config.frame;
        timing.cpuHz = static_cast<double>(context->config.frame) * 1e6 / (context->config.frame_duration_us ? context->config.frame_duration_us : 20000);
        if (format == "jsonl")
            out = BusExportJsonLines(status, events);
        else if (format == "vcd")
            out = BusExportVcd(status, events, timing);
        else
            error = "format: '" + format + "' is not one of vcd, jsonl";
    });
    if (!found)
        error = "no bus '" + bus + "'";
    return out;
}
}  // namespace DeviceState
