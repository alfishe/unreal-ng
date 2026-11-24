#include "emulator/sound/chips/gs/gsprofilereport.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "debugger/debugmanager.h"
#include "debugger/labels/labelmanager.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/chips/gs/firmware/gsfirmwareprofile.h"
#include "emulator/sound/chips/gs/generalsoundcard.h"
#include "emulator/sound/chips/gs/gscardprofiler.h"
#include "emulator/sound/chips/gs/gsporttrace.h"
#include "emulator/sound/chips/gs/gsslotstats.h"

namespace
{
bool ParseCount(const std::string& text, const char* name, size_t& out, std::string& error)
{
    if (text.empty())
        return true;
    char* end = nullptr;
    const unsigned long long value = std::strtoull(text.c_str(), &end, 10);
    if (text[0] == '-' || !end || *end != '\0')
    {
        error = std::string("'") + name + "' is a non-negative number";
        return false;
    }
    out = static_cast<size_t>(value);
    return true;
}

constexpr const char* kStartHint =
    "start the profiler: POST control/audio/gs {action: profile_start}, CLI 'gs profile start', MCP emulator_manage "
    "gs_profile_start";

StateNode NotAvailable(const std::string& why)
{
    StateNode n = StateNode::Object();
    n["available"] = false;
    n["description"] = why;
    return n;
}

/// The card of a CPU name with a CPU; else the reason
GeneralSoundCard* CardOf(EmulatorContext* context, const std::string& cpu, std::string& why)
{
    bool known = false;
    GeneralSoundCard* card = GSCardForCpu(context, cpu, nullptr, &known);
    if (!known)
        why = "No card CPU '" + cpu + "' (gs: the GS slot's card, ms: the ZX-MultiSound's)";
    else if (!card)
        why = cpu == "ms" ? "No ZX-MultiSound with a General Sound fitted" : "General Sound card not fitted";
    else if (!card->hasCoprocessor())
        why = "not available: the " + card->deviceDescription() + " has no card CPU";
    else
        return card;
    return nullptr;
}

std::string CpuName(const std::string& cpu)
{
    return cpu.empty() ? std::string("gs") : cpu;
}

LabelManager* LabelsOf(EmulatorContext* context, const std::string& cpu)
{
    if (!context || !context->pDebugManager)
        return nullptr;
    return context->pDebugManager->GetLabelManager(cpu == "ms" ? DebugCpuId::MultiSoundGS : DebugCpuId::GS);
}

/// The label at a routine's entry: the one bound to its page, else one bound to none, else none
std::string LabelAt(LabelManager* labels, uint16_t pc, uint8_t kind, uint8_t page)
{
    if (!labels)
        return std::string();
    std::string any;
    for (const std::shared_ptr<Label>& label : labels->GetAllLabelsAtAddress(pc))
    {
        if (!label)
            continue;
        if (label->bank == UINT16_MAX)
        {
            if (any.empty())
                any = label->name;
            continue;
        }
        const bool rom = label->bankType == BANK_ROM;
        if (label->bank == page && rom == (kind == 0))
            return label->name;
    }
    return any;
}

const char* PageKind(uint8_t kind)
{
    return kind == 0 ? "rom" : kind == 1 ? "ram" : "none";
}

double Pct(double part, double whole)
{
    return whole > 0 ? 100.0 * part / whole : 0.0;
}

StateNode IsrNode(const GSProfileIsrStats& s, int64_t periodCycles)
{
    StateNode n = StateNode::Object();
    const double avg = s.count ? static_cast<double>(s.cycles) / static_cast<double>(s.count) : 0.0;
    n["count"] = s.count;
    n["min"] = int64_t(s.count ? s.min : 0);
    n["avg"] = avg;
    n["max"] = int64_t(s.max);
    n["used_pct"] = Pct(avg, static_cast<double>(periodCycles));
    n["headroom_pct"] = periodCycles > 0 ? 100.0 - Pct(avg, static_cast<double>(periodCycles)) : 0.0;
    n["overruns"] = s.overruns;
    n["latency_avg"] = s.count ? static_cast<double>(s.latencySum) / static_cast<double>(s.count) : 0.0;
    n["latency_max"] = int64_t(s.latencyMax);
    n["nmis"] = s.nmis;
    n["lost"] = s.lost;
    return n;
}

std::string IsrLine(const char* what, const GSProfileIsrStats& s, int64_t periodCycles)
{
    char line[256];
    if (s.count == 0)
    {
        std::snprintf(line, sizeof line, "ISR %s: no interrupt ended%s\n", what,
                      s.nmis ? " (NMIs only)" : "");
        return line;
    }
    const double avg = static_cast<double>(s.cycles) / static_cast<double>(s.count);
    std::snprintf(line, sizeof line,
                  "ISR %s: %.0f / %lld avg · %u max · %u min · %.0f%% headroom · %llu ISRs · %llu overrun(s) · latency "
                  "%.1f avg %u max · %llu lost\n",
                  what, avg, static_cast<long long>(periodCycles), s.max, s.min,
                  periodCycles > 0 ? 100.0 - Pct(avg, static_cast<double>(periodCycles)) : 0.0,
                  static_cast<unsigned long long>(s.count), static_cast<unsigned long long>(s.overruns),
                  static_cast<double>(s.latencySum) / static_cast<double>(s.count), s.latencyMax,
                  static_cast<unsigned long long>(s.lost));
    return line;
}

int64_t FloorDiv(int64_t a, int64_t b)
{
    int64_t q = a / b;
    if ((a % b) != 0 && ((a < 0) != (b < 0)))
        q--;
    return q;
}
}  // namespace

bool GSProfileQueryFromStrings(const std::string& cpu, const std::string& routines, const std::string& activity,
                               GSProfileQuery& query, std::string& error)
{
    query.cpu = cpu;
    return ParseCount(routines, "routines", query.routines, error) && ParseCount(activity, "activity", query.activity, error);
}

bool GSEventsQueryFromStrings(const std::string& cpu, const std::string& first, const std::string& count,
                              const std::string& rows, GSEventsQuery& query, std::string& error)
{
    query.cpu = cpu;
    return ParseCount(first, "first", query.first, error) && ParseCount(count, "count", query.count, error) &&
           ParseCount(rows, "rows", query.rows, error);
}

GSProfileControlReply GSProfileControl(EmulatorContext* context, const std::string& action, const std::string& cpu)
{
    GSProfileControlReply reply;
    reply.body = StateNode::Object();
    if (action != "start" && action != "stop" && action != "clear")
    {
        reply.status = 400;
        reply.body["error"] = "the profiler's action is start, stop or clear (got '" + action + "')";
        reply.text = reply.body["error"].s;
        return reply;
    }
    std::string why;
    GeneralSoundCard* card = CardOf(context, cpu, why);
    if (!card)
    {
        reply.status = 404;
        reply.body["error"] = why;
        reply.text = why;
        return reply;
    }
    GSCardProfiler& profiler = card->profiler();
    if (action == "start")
        profiler.Start();
    else if (action == "stop")
        profiler.Stop();
    else
        profiler.Clear();
    reply.body["action"] = action;
    reply.body["cpu"] = CpuName(cpu);
    reply.body["card"] = card->deviceDescription();
    reply.body["on"] = profiler.On();
    reply.text = "GS profiler (" + CpuName(cpu) + ", " + card->deviceDescription() + "): " +
                 (action == "start" ? "started" : action == "stop" ? "stopped" : "cleared") + ", " +
                 (profiler.On() ? "on" : "off");
    return reply;
}

namespace DeviceState
{
StateNode GsProfile(EmulatorContext* context, const GSProfileQuery& query)
{
    std::string why;
    GeneralSoundCard* card = CardOf(context, query.cpu, why);
    if (!card)
        return NotAvailable(why);
    return GsProfileOf(*card, query.cpu, LabelsOf(context, query.cpu), query);
}

StateNode GsProfileOf(GeneralSoundCard& cardRef, const std::string& cpu, LabelManager* labels, const GSProfileQuery& query)
{
    GeneralSoundCard* card = &cardRef;
    if (!card->hasCoprocessor())
        return NotAvailable("not available: the " + card->deviceDescription() + " has no card CPU");
    const GSProfileSnapshot s = card->profiler().Snapshot();
    const int64_t upc = s.unitsPerCycle > 0 ? s.unitsPerCycle : 1;
    const int64_t periodCycles = s.periodUnits / upc;
    const GSFirmwareProfile* fw = GSFirmwareProfileOf(*card);

    StateNode n = StateNode::Object();
    n["available"] = true;
    n["card"] = card->deviceDescription();
    n["cpu"] = CpuName(cpu);
    n["implementation"] = gsImplementationLabel(card->implementation());
    n["on"] = s.on;
    n["frames"] = s.frames;
    n["cycles"] = s.cycles;
    n["clock_hz"] = int64_t(card->cardCpuClockHz());
    n["units_per_cycle"] = upc;
    n["period_cycles"] = periodCycles;

    std::string text;
    char line[320];
    std::snprintf(line, sizeof line, "GS profiler (%s, %s): %s, %llu frame(s), period %lld cycles\n",
                  CpuName(cpu).c_str(), card->deviceDescription().c_str(), s.on ? "on" : "off",
                  static_cast<unsigned long long>(s.frames), static_cast<long long>(periodCycles));
    text += line;

    StateNode& isr = n["isr"];
    isr = StateNode::Object();
    isr["last_frame"] = IsrNode(s.isrLastFrame, periodCycles);
    isr["since_start"] = IsrNode(s.isrTotal, periodCycles);
    text += IsrLine("last frame", s.isrLastFrame, periodCycles);
    text += IsrLine("since start", s.isrTotal, periodCycles);

    // Commands, by code
    StateNode commands = StateNode::Array();
    std::string commandText;
    for (int code = 0; code < 256; code++)
    {
        const GSProfileCommand& c = s.commands[static_cast<size_t>(code)];
        if (c.count == 0)
            continue;
        const GSCommandInfo* info = fw ? &fw->Command(static_cast<uint8_t>(code)) : nullptr;
        StateNode r = StateNode::Object();
        r["code"] = code;
        r["name"] = info && info->name ? std::string(info->name) : std::string();
        r["handler"] = info && info->handler ? std::string(info->handler) : std::string();
        r["count"] = c.count;
        const double avg = static_cast<double>(c.cycles) / static_cast<double>(c.count);
        r["avg"] = avg;
        r["min"] = int64_t(c.min);
        r["max"] = int64_t(c.max);
        r["total"] = c.cycles;
        commands.push(std::move(r));
        std::snprintf(line, sizeof line, "  #%02X %-28s x%-6llu avg %8.0f  max %8u  min %8u\n", code,
                      info && info->name ? info->name : "?", static_cast<unsigned long long>(c.count), avg, c.max,
                      c.min);
        commandText += line;
    }
    text += "Commands (" + std::to_string(commands.size()) + " codes; cycles from the card's read to its next status poll, "
            "interrupts apart):\n" + commandText;
    n["commands"] = std::move(commands);
    if (s.commandOpen)
    {
        const GSCommandInfo* info = fw ? &fw->Command(s.commandCode) : nullptr;
        StateNode c = StateNode::Object();
        c["code"] = int(s.commandCode);
        c["name"] = info && info->name ? std::string(info->name) : std::string();
        c["cycles"] = s.commandCycles;
        n["current_command"] = std::move(c);
        std::snprintf(line, sizeof line, "  current: #%02X %s, %llu cycles so far\n", s.commandCode,
                      info && info->name ? info->name : "?", static_cast<unsigned long long>(s.commandCycles));
        text += line;
    }
    else
        n["current_command"] = StateNode();

    // Routines by inclusive time
    std::vector<GSProfileRoutine> routines = s.routines;
    std::sort(routines.begin(), routines.end(), [](const GSProfileRoutine& a, const GSProfileRoutine& b) {
        return a.inclusive != b.inclusive ? a.inclusive > b.inclusive : a.pc < b.pc;
    });
    const size_t shown = query.routines == 0 ? routines.size() : std::min(query.routines, routines.size());
    StateNode rows = StateNode::Array();
    text += "Routines (" + std::to_string(routines.size()) + ", top " + std::to_string(shown) +
            " by inclusive cycles; interrupts excluded):\n";
    for (size_t i = 0; i < shown; i++)
    {
        const GSProfileRoutine& r = routines[i];
        const std::string label = LabelAt(labels, r.pc, r.kind, r.page);
        StateNode row = StateNode::Object();
        row["address"] = int(r.pc);
        row["page_kind"] = PageKind(r.kind);
        row["page"] = int(r.page);
        row["label"] = label;
        row["calls"] = r.calls;
        row["interrupts"] = r.interrupts;
        row["inclusive"] = r.inclusive;
        row["exclusive"] = r.exclusive;
        const double avg = r.calls ? static_cast<double>(r.inclusive) / static_cast<double>(r.calls) : 0.0;
        row["avg"] = avg;
        row["min"] = int64_t(r.min);
        row["max"] = int64_t(r.max);
        row["inclusive_pct"] = Pct(static_cast<double>(r.inclusive), static_cast<double>(s.cycles));
        rows.push(std::move(row));
        std::snprintf(line, sizeof line, "  #%04X %s%u %-20s calls %-7llu incl %10llu excl %10llu avg %7.0f max %7u%s\n",
                      r.pc, r.kind == 0 ? "ROM" : r.kind == 1 ? "RAM" : "--", unsigned(r.page),
                      label.empty() ? "" : label.c_str(), static_cast<unsigned long long>(r.calls),
                      static_cast<unsigned long long>(r.inclusive), static_cast<unsigned long long>(r.exclusive), avg,
                      r.max, r.interrupts ? " (ISR)" : "");
        text += line;
    }
    n["routines"] = std::move(rows);
    n["routines_total"] = uint64_t(routines.size());

    // Activity, the newest N frames
    const size_t from = query.activity == 0 || query.activity >= s.activity.size() ? 0 : s.activity.size() - query.activity;
    StateNode activity = StateNode::Array();
    if (query.activity > 0)
        for (size_t i = from; i < s.activity.size(); i++)
        {
            const GSProfileActivity& a = s.activity[i];
            StateNode row = StateNode::Object();
            const double cycles = static_cast<double>(a.cycles);
            row["frame"] = a.hostFrame;
            row["cycles"] = int64_t(a.cycles);
            row["isr_cycles"] = int64_t(a.isrCycles);
            row["isr_pct"] = Pct(a.isrCycles, cycles);
            row["idle_pct"] = Pct(a.idleCycles, cycles);
            row["halt_pct"] = Pct(a.haltCycles, cycles);
            row["busy_pct"] = std::max(0.0, 100.0 - Pct(static_cast<double>(a.idleCycles) + a.haltCycles, cycles));
            row["commands"] = int(a.commands);
            row["interrupts"] = int(a.interrupts);
            row["overruns"] = int(a.overruns);
            row["lost"] = int(a.lost);
            row["isr_max"] = int64_t(a.isrMax);
            activity.push(std::move(row));
        }
    n["activity"] = std::move(activity);
    if (!s.activity.empty())
    {
        text += "Activity (newest frames: ISR % · idle % · halted % · commands):\n";
        for (size_t i = s.activity.size() > 4 ? s.activity.size() - 4 : 0; i < s.activity.size(); i++)
        {
            const GSProfileActivity& a = s.activity[i];
            const double cycles = static_cast<double>(a.cycles);
            std::snprintf(line, sizeof line, "  frame %llu: ISR %.1f%% · idle %.1f%% · halted %.1f%% · %u command(s), %u INT, %u overrun(s)\n",
                          static_cast<unsigned long long>(a.hostFrame), Pct(a.isrCycles, cycles),
                          Pct(a.idleCycles, cycles), Pct(a.haltCycles, cycles), unsigned(a.commands),
                          unsigned(a.interrupts), unsigned(a.overruns));
            text += line;
        }
    }
    n["stack_overflows"] = s.stackOverflows;
    n["routines_dropped"] = s.routinesDropped;
    n["resyncs"] = s.resyncs;
    if (!s.on)
    {
        n["how_to_start"] = kStartHint;
        if (s.frames == 0)
            text += std::string("not profiling: ") + kStartHint + "\n";
    }
    n["text"] = text;
    return n;
}

StateNode GsEvents(EmulatorContext* context, const GSEventsQuery& query)
{
    std::string why;
    GeneralSoundCard* card = CardOf(context, query.cpu, why);
    if (!card)
        return NotAvailable(why);
    return GsEventsOf(*card, query.cpu, query);
}

StateNode GsEventsOf(GeneralSoundCard& cardRef, const std::string& cpu, const GSEventsQuery& query)
{
    GeneralSoundCard* card = &cardRef;
    if (!card->hasCoprocessor())
        return NotAvailable("not available: the " + card->deviceDescription() + " has no card CPU");

    const GSProfileSnapshot s = card->profiler().Snapshot();
    const GSProfileFrameMap& map = s.map;
    const bool capturing = card->isPortTraceCapturing();
    const bool neogs = card->implementation() == GSCardImplementation::NGS;
    const bool multisound = cpu == "ms";

    StateNode n = StateNode::Object();
    n["available"] = true;
    n["card"] = card->deviceDescription();
    n["cpu"] = CpuName(cpu);
    n["on"] = s.on;
    n["capturing"] = capturing;
    StateNode columns = StateNode::Array();
    for (const char* c : {"cycle", "kind", "value", "detail"})
        columns.push(c);
    n["columns"] = std::move(columns);

    std::string text;
    char line[320];
    if (!map.valid || map.period <= 0 || map.end <= map.start)
    {
        n["frame"] = StateNode();
        n["periods"] = 0;
        n["rows"] = StateNode::Array();
        n["how_to_start"] = kStartHint;
        n["text"] = std::string("GS events (") + CpuName(cpu) + "): no profiled frame yet: " + kStartHint +
                    "; the host / DAC / port dots need the GS port trace too ('gsporttrace start')\n";
        return n;
    }

    const int64_t upc = map.unitsPerCycle > 0 ? map.unitsPerCycle : 1;
    const int64_t firstStart = map.anchor + FloorDiv(map.start - map.anchor, map.period) * map.period;
    const size_t periods = static_cast<size_t>(FloorDiv(map.end - 1 - firstStart, map.period) + 1);
    const size_t first = std::min(query.first, periods);
    const size_t count = query.count == 0 ? periods - first : std::min(query.count, periods - first);
    n["frame"] = map.hostFrame;
    n["card_start"] = map.start / upc;
    n["cycles"] = (map.end - map.start) / upc;
    n["period_cycles"] = map.period / upc;
    n["periods"] = uint64_t(periods);
    n["first"] = uint64_t(first);
    n["count"] = uint64_t(count);
    n["truncated"] = map.truncated;

    struct Row
    {
        std::vector<const GSProfileIsrMark*> isrs;
        std::vector<std::array<int64_t, 4>> events;  // cycle, kind index, value, detail
    };
    std::vector<Row> rows(count);
    auto rowOf = [&](int64_t t, int64_t& cycle) -> Row* {
        if (t < map.start || t >= map.end)
            return nullptr;
        const int64_t index = FloorDiv(t - firstStart, map.period);
        if (index < static_cast<int64_t>(first) || index >= static_cast<int64_t>(first + count))
            return nullptr;
        cycle = (t - (firstStart + index * map.period)) / upc;
        return &rows[static_cast<size_t>(index) - first];
    };
    int64_t isrCount = 0;
    for (const GSProfileIsrMark& m : map.isrs)
    {
        int64_t cycle = 0;
        if (Row* r = rowOf(m.accept, cycle))
        {
            r->isrs.push_back(&m);
            isrCount++;
        }
    }

    static const char* const kKinds[] = {"host_data_write", "host_data_read", "host_command", "host_status",
                                         "host_control", "dac", "page", "volume", "zxdma_read", "zxdma_write"};
    uint64_t totals[10] = {};
    constexpr size_t kMaxEvents = 65536;
    size_t events = 0;
    bool eventsTruncated = false;
    if (capturing || card->getPortTraceEventCount() > 0)
    {
        for (const GSTraceEvent& e : card->getPortTraceEvents())
        {
            int kind = -1;
            int64_t detail = 0;
            switch (e.side)
            {
                case GSTraceSide::Host:
                    if (e.port == GeneralSoundCard::PORT_DATA)
                        kind = e.isOut() ? 0 : 1;
                    else if (e.port == GeneralSoundCard::PORT_COMMAND)
                        kind = e.isOut() ? 2 : 3;
                    else if (e.port == GeneralSoundCard::PORT_CONTROL)
                        kind = 4;
                    break;
                case GSTraceSide::DacFetch:
                    kind = 5;
                    detail = e.channel;
                    break;
                case GSTraceSide::GsInternal:
                {
                    if (!e.isOut())
                        break;
                    const uint16_t p = multisound ? (e.port & 0x0F) : (e.port & 0xFF);
                    if (p == 0x00 || (neogs && (p == 0x10 || (p >= 0x20 && p <= 0x23))))
                    {
                        kind = 6;
                        detail = p;
                    }
                    else if (p >= 0x06 && p <= 0x09)
                    {
                        kind = 7;
                        detail = p - 0x06;
                    }
                    else if (neogs && p >= 0x16 && p <= 0x19)
                    {
                        kind = 7;
                        detail = 4 + (p - 0x16);
                    }
                    break;
                }
                case GSTraceSide::ZxDma:
                    kind = e.isOut() ? 9 : 8;
                    detail = e.cardAddress();
                    break;
                default:
                    break;
            }
            if (kind < 0)
                continue;
            int64_t cycle = 0;
            Row* r = rowOf(e.timestamp, cycle);
            if (!r)
                continue;
            if (events >= kMaxEvents)
            {
                eventsTruncated = true;
                break;
            }
            r->events.push_back({cycle, kind, int64_t(e.value), detail});
            totals[kind]++;
            events++;
        }
    }
    n["events_truncated"] = eventsTruncated;

    std::snprintf(line, sizeof line,
                  "GS events (%s, %s): host frame %llu, %lld cycles, %zu periods of %lld cycles, %lld ISR(s), %zu "
                  "event(s)%s\n",
                  CpuName(cpu).c_str(), card->deviceDescription().c_str(),
                  static_cast<unsigned long long>(map.hostFrame), static_cast<long long>((map.end - map.start) / upc),
                  periods, static_cast<long long>(map.period / upc), static_cast<long long>(isrCount), events,
                  capturing ? "" : " (port trace not capturing: ISR bars only; 'gsporttrace start' for the dots)");
    text += line;

    StateNode out = StateNode::Array();
    size_t textRows = 0;
    for (size_t i = 0; i < count; i++)
    {
        const Row& r = rows[i];
        const size_t index = first + i;
        StateNode row = StateNode::Object();
        row["period"] = uint64_t(index);
        row["start"] = (firstStart + static_cast<int64_t>(index) * map.period - map.start) / upc;
        StateNode isrs = StateNode::Array();
        std::string isrText;
        for (const GSProfileIsrMark* m : r.isrs)
        {
            const int64_t periodStart = firstStart + static_cast<int64_t>(index) * map.period;
            StateNode b = StateNode::Object();
            b["accept"] = (m->accept - periodStart) / upc;
            if (m->end >= 0)
            {
                b["end"] = (m->end - periodStart) / upc;
                b["length"] = (m->end - m->accept) / upc;
            }
            else
            {
                b["end"] = StateNode();
                b["length"] = StateNode();
            }
            b["entry"] = int(m->entry);
            b["nmi"] = m->nmi;
            isrs.push(std::move(b));
            char part[96];
            if (m->end >= 0)
                std::snprintf(part, sizeof part, " %s %lld-%lld", m->nmi ? "NMI" : "INT",
                              static_cast<long long>((m->accept - periodStart) / upc),
                              static_cast<long long>((m->end - periodStart) / upc));
            else
                std::snprintf(part, sizeof part, " %s %lld-(running)", m->nmi ? "NMI" : "INT",
                              static_cast<long long>((m->accept - periodStart) / upc));
            isrText += part;
        }
        row["isr"] = std::move(isrs);
        StateNode ev = StateNode::Array();
        std::string evText;
        for (const auto& e : r.events)
        {
            StateNode item = StateNode::Array();
            item.push(e[0]);
            item.push(kKinds[e[1]]);
            item.push(e[2]);
            item.push(e[3]);
            ev.push(std::move(item));
            if (evText.size() < 160)
            {
                char part[64];
                if (e[1] == 5 || e[1] == 7)
                    std::snprintf(part, sizeof part, " %s%lld@%lld=#%02X", kKinds[e[1]], static_cast<long long>(e[3]),
                                  static_cast<long long>(e[0]), unsigned(e[2]));
                else
                    std::snprintf(part, sizeof part, " %s@%lld=#%02X", kKinds[e[1]], static_cast<long long>(e[0]),
                                  unsigned(e[2]));
                evText += part;
            }
        }
        row["events"] = std::move(ev);
        out.push(std::move(row));
        if (textRows < query.rows)
        {
            textRows++;
            std::snprintf(line, sizeof line, "  p%-4zu%s |", index, isrText.empty() ? " (no ISR)" : isrText.c_str());
            text += line + evText + "\n";
        }
    }
    if (textRows < count)
        text += "  ... " + std::to_string(count - textRows) + " more period(s) in rows\n";
    n["rows"] = std::move(out);
    StateNode& t = n["totals"];
    t = StateNode::Object();
    t["isr"] = isrCount;
    for (int k = 0; k < 10; k++)
        t[kKinds[k]] = totals[k];
    if (!s.on)
        n["how_to_start"] = kStartHint;
    n["text"] = text;
    return n;
}
}  // namespace DeviceState
