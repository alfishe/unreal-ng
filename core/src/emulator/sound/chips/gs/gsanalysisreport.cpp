#include "emulator/sound/chips/gs/gsanalysisreport.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>

#include "emulator/emulatorcontext.h"
#include "emulator/sound/chips/gs/firmware/gsfirmwareobjects.h"
#include "emulator/sound/chips/gs/generalsoundcard.h"
#include "emulator/sound/chips/gs/gsslotstats.h"
#include "emulator/video/screenshotter.h"

namespace
{
constexpr uint32_t kPage = 0x4000;

std::string Hex(uint32_t value, int digits)
{
    char text[16];
    std::snprintf(text, sizeof text, "#%0*X", digits, value);
    return text;
}

bool ParseUnsigned(const std::string& text, const char* name, uint64_t max, uint64_t& out, std::string& error)
{
    if (text.empty())
        return true;
    char* end = nullptr;
    const unsigned long long value = std::strtoull(text.c_str(), &end, 10);
    if (text[0] == '-' || !end || *end != '\0' || value > max)
    {
        error = std::string("'") + name + "' is a number from 0 to " + std::to_string(max);
        return false;
    }
    out = value;
    return true;
}

bool ParseChannels(const std::string& text, const char* name, std::vector<GSCardAnalysis::Channel>& out,
                   std::string& error)
{
    out.clear();
    size_t start = 0;
    while (start <= text.size())
    {
        const size_t comma = text.find(',', start);
        const std::string item = text.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        if (item == "all")
        {
            out.clear();
            for (uint8_t c = 0; c < GSCardAnalysis::ChannelCount; c++)
                out.push_back(static_cast<GSCardAnalysis::Channel>(c));
            return true;
        }
        GSCardAnalysis::Channel channel;
        if (!item.empty())
        {
            if (!GSCardAnalysis::ParseChannel(item, channel))
            {
                error = std::string("'") + name + "': unknown channel '" + item +
                        "' (fetch, read, write, dac, sd_dma, zx_dma, all)";
                return false;
            }
            out.push_back(channel);
        }
        if (comma == std::string::npos)
            break;
        start = comma + 1;
    }
    return true;
}

const char* SpaceKey(GSCardAnalysis::Space space)
{
    return space == GSCardAnalysis::Rom ? "rom" : "ram";
}

/// The card a request names; @p status 404 with @p error when there is none
GeneralSoundCard* CardFor(EmulatorContext* context, const std::string& cpu, std::string& error)
{
    bool known = true;
    GeneralSoundCard* card = GSCardForCpu(context, cpu, nullptr, &known);
    if (!known)
        error = "No card CPU '" + cpu + "' (gs: the GS slot's card, ms: the ZX-MultiSound's)";
    else if (!card)
        error = cpu == "ms" ? "No ZX-MultiSound with a General Sound fitted" : "No General Sound card is fitted";
    return card;
}

/// The common head of every report; false (with `available` false) when the card has no analysis to show
bool Head(EmulatorContext* context, const std::string& cpu, StateNode& n, GeneralSoundCard*& cardOut,
          std::shared_ptr<GSCardAnalysis>& analysisOut)
{
    n = StateNode::Object();
    std::string error;
    GeneralSoundCard* card = CardFor(context, cpu, error);
    cardOut = card;
    if (!card)
    {
        n["available"] = false;
        n["description"] = error;
        return false;
    }
    if (!card->hasCoprocessor() || card->cardPageCount(GeneralSoundCard::CardPage::Ram) == 0)
    {
        n["available"] = false;
        n["description"] = "not available: the " + card->deviceDescription() +
                           " has no card CPU (coverage and the heat map need the classic GS, the ZX-MultiSound's GS or "
                           "NeoGS)";
        return false;
    }
    std::shared_ptr<GSCardAnalysis> analysis = card->analysis();
    analysisOut = analysis;
    n["available"] = true;
    n["card"] = card->deviceDescription();
    n["cpu"] = std::string(cpu.empty() ? "gs" : cpu);
    n["enabled"] = card->analysisOn();
    n["recording"] = card->analysisBound();
    n["allocated"] = analysis != nullptr;
    n["memory_bytes"] = uint64_t(analysis ? analysis->MemoryBytes() : 0);
    n["rom_name"] = std::string(card->implementation() == GSCardImplementation::NGS ? "flash" : "ROM");
    if (analysis)
    {
        n["rom_bytes"] = uint64_t(analysis->RomBytes());
        n["ram_bytes"] = uint64_t(analysis->RamBytes());
        n["cell_bytes"] = uint64_t(analysis->CellBytes());
        n["cells"] = uint64_t(analysis->Cells());
        n["decay"] = int(analysis->Decay());
        n["frame"] = analysis->Frame();
        n["created_frame"] = analysis->CreatedFrame();
        n["card_resets"] = analysis->CardResets();
        n["last_reset_frame"] = analysis->LastResetFrame();
    }
    return analysis != nullptr;
}

std::string StatusLine(const StateNode& n)
{
    const StateNode* enabled = n.find("enabled");
    const StateNode* recording = n.find("recording");
    const StateNode* memory = n.find("memory_bytes");
    std::string line = n.find("card")->s + " (" + n.find("cpu")->s + "): analysis " +
                       (enabled && enabled->b ? (recording && recording->b ? "on" : "on (binds at the card's next run)")
                                              : "off");
    if (memory && memory->i > 0)
        line += ", " + std::to_string(memory->i / 1024) + " KB";
    return line + "\n";
}

constexpr const char* kHowToStart =
    "switch it on: POST control/audio/gs/analysis {action: on}, CLI 'gs analysis on', Lua / Python "
    "gs_analysis('on'), MCP gs_analysis action=on";

/// Ranges of the bytes with any of `mask` in [from, from + count), offsets relative to `base`
StateNode Ranges(const uint8_t* flags, uint32_t from, uint32_t count, uint8_t mask, uint32_t base, size_t max,
                 bool& truncated, std::string* text)
{
    StateNode list = StateNode::Array();
    uint32_t i = 0;
    while (i < count)
    {
        if (!(flags[from + i] & mask))
        {
            i++;
            continue;
        }
        const uint32_t start = i;
        while (i < count && (flags[from + i] & mask))
            i++;
        if (list.size() >= max)
        {
            truncated = true;
            break;
        }
        StateNode range = StateNode::Array();
        range.push(int64_t(from + start - base));
        range.push(int64_t(from + i - 1 - base));
        list.push(std::move(range));
        if (text)
            *text += " " + Hex(from + start - base, 4) + (i - 1 > start ? "-" + Hex(from + i - 1 - base, 4) : "");
    }
    return list;
}

struct Counts
{
    uint64_t exec = 0, code = 0, read = 0, write = 0, jump = 0, sub = 0, dac = 0, written = 0;
    void Add(uint8_t f)
    {
        exec += (f & GSCardAnalysis::Exec) != 0;
        code += (f & GSCardAnalysis::kCodeFlags) != 0;
        read += (f & GSCardAnalysis::Read) != 0;
        write += (f & GSCardAnalysis::Write) != 0;
        jump += (f & GSCardAnalysis::Jump) != 0;
        sub += (f & GSCardAnalysis::Sub) != 0;
        dac += (f & GSCardAnalysis::Dac) != 0;
        written += (f & GSCardAnalysis::Written) != 0;
    }
    bool Any() const { return exec || code || read || write || dac || written; }
    void Into(StateNode& n, bool ram) const
    {
        n["exec"] = exec;
        n["code"] = code;
        n["read"] = read;
        n["write"] = write;
        n["jump"] = jump;
        n["sub"] = sub;
        n["dac"] = dac;
        if (ram)
            n["written"] = written;
    }
    std::string Text() const
    {
        char line[200];
        std::snprintf(line, sizeof line, "exec %llu, code %llu, read %llu, write %llu, jump %llu, sub %llu, dac %llu",
                      static_cast<unsigned long long>(exec), static_cast<unsigned long long>(code),
                      static_cast<unsigned long long>(read), static_cast<unsigned long long>(write),
                      static_cast<unsigned long long>(jump), static_cast<unsigned long long>(sub),
                      static_cast<unsigned long long>(dac));
        return line;
    }
};

/// The command handlers of the card's firmware profile that ran (their first byte has Exec where the card maps the
/// handler's address now)
StateNode Handlers(EmulatorContext* context, const std::string& cpu, const GeneralSoundCard& card,
                   const GSCardAnalysis& analysis, std::string& text)
{
    GSFirmwareObjectsQuery query;
    query.cpu = cpu;
    query.object = "dispatch";
    const GSFirmwareObjectsReply reply = GSFirmwareObjectsRead(context, query);
    StateNode node = StateNode::Object();
    const StateNode* dispatch = reply.status == 200 ? reply.body.find("dispatch") : nullptr;
    const StateNode* entries = dispatch ? dispatch->find("entries") : nullptr;
    if (!entries)
    {
        node["available"] = false;
        node["description"] = reply.status != 200 ? reply.text : std::string("the firmware profile has no dispatch table");
        text += "handlers: " + node.find("description")->s + "\n";
        return node;
    }
    node["available"] = true;
    if (const StateNode* profile = reply.body.find("profile"))
        node["profile"] = profile->s;
    if (const StateNode* table = dispatch->find("table"))
        node["table"] = table->s;
    const bool stockSplit = dispatch->find("table_high") != nullptr;
    StateNode executed = StateNode::Array();
    StateNode notExecuted = StateNode::Array();
    int64_t total = 0;
    std::string lines;
    for (const StateNode& e : entries->items)
    {
        const StateNode* handler = e.find("handler");
        const StateNode* unknown = e.find("unknown");
        if (!handler || handler->s.size() < 2 || (unknown && unknown->b))
            continue;
        const uint16_t address = static_cast<uint16_t>(std::strtoul(handler->s.c_str() + 1, nullptr, 16));
        // The stock split calls its handlers with the firmware paged in (COMHIGH: OUT (MPAG),0), so #8000-#FFFF is the
        // firmware's 32K image there, not what window 2-3 maps now
        GeneralSoundCard::CardPage where = card.mapCardAddress(address);
        if (stockSplit && address >= 0x8000)
        {
            where = card.firmwareCardPage((address - 0x8000u) / kPage);
            where.offset = static_cast<uint16_t>(address % kPage);
        }
        if (where.kind == GeneralSoundCard::CardPage::None)
            continue;
        total++;
        const GSCardAnalysis::Space space =
            where.kind == GeneralSoundCard::CardPage::Rom ? GSCardAnalysis::Rom : GSCardAnalysis::Ram;
        const size_t offset = static_cast<size_t>(where.page) * kPage + where.offset;
        const bool ran = offset < analysis.SpaceBytes(space) && (analysis.Flags(space)[offset] & GSCardAnalysis::Exec);
        const int64_t command = e.find("command") ? e.find("command")->i : 0;
        if (!ran)
        {
            notExecuted.push(command);
            continue;
        }
        StateNode row = StateNode::Object();
        row["command"] = command;
        if (const StateNode* name = e.find("name"))
            row["name"] = name->s;
        row["handler"] = handler->s;
        row["space"] = SpaceKey(space);
        row["page"] = int(where.page);
        executed.push(row);
        lines += "  " + Hex(static_cast<uint32_t>(command), 2) + " " +
                 (e.find("name") ? e.find("name")->s : std::string("?")) + " @" + handler->s + "\n";
    }
    node["entries"] = total;
    node["executed_count"] = int64_t(executed.size());
    node["executed"] = std::move(executed);
    node["not_executed"] = std::move(notExecuted);
    text += "handlers executed (" + (node.find("profile") ? node.find("profile")->s : std::string("profile")) + "): " +
            std::to_string(node.find("executed_count")->i) + " of " + std::to_string(total) + "\n" + lines;
    return node;
}
}  // namespace

bool GSAnalysisQueryFromParams(const std::function<std::string(const std::string&)>& get, GSAnalysisQuery& query,
                               std::string& error)
{
    query.cpu = get("cpu");
    query.space = get("space");
    if (!query.space.empty() && query.space != "rom" && query.space != "ram" && query.space != "flash")
    {
        error = "'space' is rom (flash on NeoGS) or ram";
        return false;
    }
    if (query.space == "flash")
        query.space = "rom";
    uint64_t value = 0;
    const std::string page = get("page");
    if (!page.empty())
    {
        if (!ParseUnsigned(page, "page", 255, value, error))
            return false;
        query.page = static_cast<int>(value);
        if (query.space.empty())
        {
            error = "'page' needs 'space' (rom or ram)";
            return false;
        }
    }
    value = query.ranges;
    if (!ParseUnsigned(get("ranges"), "ranges", 100000, value, error))
        return false;
    query.ranges = static_cast<size_t>(value);
    const std::string raw = get("raw");
    query.raw = raw == "1" || raw == "true" || raw == "yes";
    if (!ParseChannels(get("channels"), "channels", query.channels, error))
        return false;
    const std::string recent = get("recent");
    if (!recent.empty() && !ParseChannels(recent, "recent", query.recent, error))
        return false;
    value = query.frames;
    if (!ParseUnsigned(get("frames"), "frames", 1000000, value, error))
        return false;
    query.frames = static_cast<uint32_t>(value);
    value = query.cells;
    if (!ParseUnsigned(get("cells"), "cells", 1u << 20, value, error))
        return false;
    query.cells = static_cast<uint32_t>(value);
    const std::string format = get("format");
    if (!format.empty())
    {
        if (format != "base64" && format != "hex" && format != "none")
        {
            error = "'format' is base64, hex or none";
            return false;
        }
        query.format = format;
    }
    return true;
}

GSAnalysisReply GSAnalysisControl(EmulatorContext* context, const GSAnalysisControlRequest& request)
{
    GSAnalysisReply reply;
    const std::string action = request.action.empty() ? std::string("status") : request.action;
    auto fail = [&reply](int status, const std::string& message) {
        reply.status = status;
        reply.body = StateNode::Object();
        reply.body["error"] = message;
        return reply;
    };
    if (action != "status" && action != "on" && action != "off" && action != "reset" && action != "release")
        return fail(400, "unknown action '" + action + "' (status, on, off, reset, release)");
    if (request.decay > 255)
        return fail(400, "'decay' is 0-255 (heat lost per frame)");
    std::string error;
    GeneralSoundCard* card = CardFor(context, request.cpu, error);
    if (!card)
        return fail(404, error);
    const uint64_t* frames = context ? &context->emulatorState.frame_counter : nullptr;
    if (action == "on")
    {
        if (!card->setAnalysisOn(true, frames, &error))
            return fail(400, error);
    }
    else if (action == "off")
    {
        if (!card->setAnalysisOn(false, frames, &error))
            return fail(400, error);
    }
    else if (action == "release")
        card->releaseAnalysis();
    else if (std::shared_ptr<GSCardAnalysis> analysis = card->analysis(); analysis && action == "reset")
        analysis->Clear();
    if (request.decay >= 0)
        if (std::shared_ptr<GSCardAnalysis> analysis = card->analysis())
            analysis->SetDecay(static_cast<uint8_t>(request.decay));
    reply.body = DeviceState::GsAnalysisStatus(context, request.cpu);
    reply.body["action"] = action;
    const StateNode* available = reply.body.find("available");
    if (available && !available->b)
        return fail(400, reply.body.find("description")->s);
    return reply;
}

namespace DeviceState
{
StateNode GsAnalysisStatus(EmulatorContext* context, const std::string& cpu)
{
    StateNode n;
    GeneralSoundCard* card = nullptr;
    std::shared_ptr<GSCardAnalysis> analysis;
    Head(context, cpu, n, card, analysis);
    const StateNode* available = n.find("available");
    if (!available || !available->b)
        return n;
    std::string text = StatusLine(n);
    if (!analysis)
    {
        text += std::string("no data: ") + kHowToStart + "\n";
        n["how_to_start"] = kHowToStart;
    }
    else
    {
        char line[200];
        std::snprintf(line, sizeof line, "ROM %zu KB, RAM %zu KB; heat map %u cells of %u bytes, decay %d per frame; "
                      "%llu card reset(s) since created at frame %llu\n",
                      analysis->RomBytes() / 1024, analysis->RamBytes() / 1024, analysis->Cells(),
                      analysis->CellBytes(), int(analysis->Decay()),
                      static_cast<unsigned long long>(analysis->CardResets()),
                      static_cast<unsigned long long>(analysis->CreatedFrame()));
        text += line;
    }
    n["text"] = text;
    return n;
}

StateNode GsCoverage(EmulatorContext* context, const GSAnalysisQuery& query)
{
    StateNode n;
    GeneralSoundCard* card = nullptr;
    std::shared_ptr<GSCardAnalysis> analysis;
    const bool have = Head(context, query.cpu, n, card, analysis);
    const StateNode* available = n.find("available");
    if (!available || !available->b)
        return n;
    std::string text = StatusLine(n);
    if (!have)
    {
        n["how_to_start"] = kHowToStart;
        n["text"] = text + "no data: " + kHowToStart + "\n";
        return n;
    }

    StateNode legend = StateNode::Object();
    legend["exec"] = int(GSCardAnalysis::Exec);
    legend["operand"] = int(GSCardAnalysis::Operand);
    legend["read"] = int(GSCardAnalysis::Read);
    legend["write"] = int(GSCardAnalysis::Write);
    legend["jump"] = int(GSCardAnalysis::Jump);
    legend["sub"] = int(GSCardAnalysis::Sub);
    legend["dac"] = int(GSCardAnalysis::Dac);
    legend["written"] = int(GSCardAnalysis::Written);
    n["legend"] = std::move(legend);

    const std::string romName = n.find("rom_name")->s;
    StateNode spaces = StateNode::Array();
    for (GSCardAnalysis::Space space : {GSCardAnalysis::Rom, GSCardAnalysis::Ram})
    {
        if (!query.space.empty() && query.space != SpaceKey(space))
            continue;
        const bool ram = space == GSCardAnalysis::Ram;
        const uint8_t* flags = analysis->Flags(space);
        const uint32_t pages = static_cast<uint32_t>(analysis->SpaceBytes(space) / kPage);
        StateNode s = StateNode::Object();
        s["space"] = SpaceKey(space);
        s["name"] = ram ? std::string("RAM") : romName;
        s["bytes"] = uint64_t(analysis->SpaceBytes(space));
        s["pages"] = int64_t(pages);
        Counts totals;
        StateNode pageList = StateNode::Array();
        std::string pageText;
        for (uint32_t page = 0; page < pages; page++)
        {
            if (query.page >= 0 && static_cast<uint32_t>(query.page) != page)
                continue;
            const uint32_t base = page * kPage;
            Counts c;
            for (uint32_t i = 0; i < kPage; i++)
                c.Add(flags[base + i]);
            totals.exec += c.exec;
            totals.code += c.code;
            totals.read += c.read;
            totals.write += c.write;
            totals.jump += c.jump;
            totals.sub += c.sub;
            totals.dac += c.dac;
            totals.written += c.written;
            if (!c.Any() && query.page < 0)
                continue;
            StateNode p = StateNode::Object();
            p["page"] = int64_t(page);
            c.Into(p, ram);
            pageText += std::string("  ") + SpaceKey(space) + " " + std::to_string(page) + ": " + c.Text() + "\n";
            if (query.ranges > 0)
            {
                bool truncated = false;
                std::string line;
                p["code_ranges"] = Ranges(flags, base, kPage, GSCardAnalysis::kCodeFlags, base, query.ranges, truncated,
                                          &line);
                if (!line.empty())
                    pageText += "    code:" + line + "\n";
                line.clear();
                p["data_ranges"] = Ranges(flags, base, kPage, GSCardAnalysis::Read | GSCardAnalysis::Write, base,
                                          query.ranges, truncated, &line);
                if (!line.empty())
                    pageText += "    data:" + line + "\n";
                line.clear();
                p["dac_ranges"] = Ranges(flags, base, kPage, GSCardAnalysis::Dac, base, query.ranges, truncated, &line);
                if (!line.empty())
                    pageText += "    dac:" + line + "\n";
                if (truncated)
                    p["ranges_truncated"] = true;
            }
            if (query.page >= 0)
            {
                StateNode entries = StateNode::Array();
                std::string line;
                for (uint32_t i = 0; i < kPage && entries.size() < 256; i++)
                    if (flags[base + i] & GSCardAnalysis::Sub)
                    {
                        entries.push(int64_t(i));
                        line += " " + Hex(i, 4);
                    }
                p["sub_entries"] = std::move(entries);
                if (!line.empty())
                    pageText += "    sub entries:" + line + "\n";
                if (query.raw)
                    p["flags"] = Screenshotter::Base64Encode(std::vector<uint8_t>(flags + base, flags + base + kPage));
            }
            pageList.push(std::move(p));
        }
        StateNode t = StateNode::Object();
        totals.Into(t, ram);
        s["totals"] = std::move(t);
        s["page_list"] = std::move(pageList);
        text += (ram ? std::string("RAM") : romName) + " (" + std::to_string(pages) + " pages): " + totals.Text() + "\n" +
                pageText;
        spaces.push(std::move(s));
    }
    n["spaces"] = std::move(spaces);
    n["handlers"] = Handlers(context, query.cpu, *card, *analysis, text);

    // Reads of RAM never written since the card reset (or since the analysis was created)
    uint64_t dropped = 0;
    const std::vector<GSCardAnalysis::UninitRead> rows = analysis->UninitRows(dropped);
    StateNode nw = StateNode::Object();
    nw["count"] = uint64_t(rows.size());
    nw["dropped"] = dropped;
    nw["since_frame"] = analysis->LastResetFrame();
    StateNode list = StateNode::Array();
    text += "never-written reads: " + std::to_string(rows.size()) +
            (dropped ? " (+" + std::to_string(dropped) + " not kept)" : std::string()) + "\n";
    size_t shown = 0;
    for (const GSCardAnalysis::UninitRead& r : rows)
    {
        StateNode row = StateNode::Object();
        row["address"] = int(r.address);
        row["space"] = "ram";
        row["page"] = int64_t(r.offset / kPage);
        row["offset"] = int64_t(r.offset % kPage);
        row["pc"] = int(r.pc);
        row["frame"] = r.frame;
        row["kind"] = r.fetch ? "fetch" : "read";
        list.push(std::move(row));
        if (shown++ < 32)
        {
            char line[128];
            std::snprintf(line, sizeof line, "  #%04X (ram %u:#%04X) %s by PC #%04X, frame %llu\n", unsigned(r.address),
                          unsigned(r.offset / kPage), unsigned(r.offset % kPage), r.fetch ? "fetched" : "read",
                          unsigned(r.pc), static_cast<unsigned long long>(r.frame));
            text += line;
        }
    }
    nw["rows"] = std::move(list);
    n["never_written"] = std::move(nw);
    n["text"] = text;
    return n;
}

StateNode GsHeatmap(EmulatorContext* context, const GSAnalysisQuery& query)
{
    StateNode n;
    GeneralSoundCard* card = nullptr;
    std::shared_ptr<GSCardAnalysis> analysis;
    const bool have = Head(context, query.cpu, n, card, analysis);
    const StateNode* available = n.find("available");
    if (!available || !available->b)
        return n;
    std::string text = StatusLine(n);
    if (!have)
    {
        n["how_to_start"] = kHowToStart;
        n["text"] = text + "no data: " + kHowToStart + "\n";
        return n;
    }
    const uint64_t now = analysis->Frame();
    const uint32_t cells = analysis->Cells();
    uint32_t group = 1;
    while (query.cells > 0 && (cells + group - 1) / group > query.cells)
        group <<= 1;
    const uint32_t dataCells = (cells + group - 1) / group;
    uint32_t columns = 1;
    while (static_cast<uint64_t>(columns) * columns < dataCells)
        columns <<= 1;
    n["bump"] = int(GSCardAnalysis::kHeatBump);
    n["data_cells"] = int64_t(dataCells);
    n["group"] = int64_t(group);
    n["columns"] = int64_t(columns);
    n["format"] = query.format;

    std::vector<GSCardAnalysis::Channel> channels = query.channels;
    if (channels.empty())
        for (uint8_t c = 0; c < GSCardAnalysis::ChannelCount; c++)
            channels.push_back(static_cast<GSCardAnalysis::Channel>(c));
    char line[256];
    std::snprintf(line, sizeof line, "heat map: %u cells of %u bytes (grid %u cells of %u, %u columns), frame %llu, "
                  "decay %d per frame\n",
                  cells, analysis->CellBytes(), dataCells, analysis->CellBytes() * group, columns,
                  static_cast<unsigned long long>(now), int(analysis->Decay()));
    text += line;
    StateNode list = StateNode::Array();
    std::vector<uint8_t> grid(dataCells);
    for (GSCardAnalysis::Channel channel : channels)
    {
        std::fill(grid.begin(), grid.end(), 0);
        uint64_t hot = 0;
        uint8_t max = 0;
        for (uint32_t cell = 0; cell < cells; cell++)
        {
            const uint8_t heat = analysis->HeatAt(channel, cell, now);
            if (!heat)
                continue;
            hot++;
            max = std::max(max, heat);
            uint8_t& g = grid[cell / group];
            g = std::max(g, heat);
        }
        StateNode c = StateNode::Object();
        c["name"] = GSCardAnalysis::ChannelName(channel);
        c["accesses"] = analysis->Accesses(channel);
        c["hot_cells"] = hot;
        c["max"] = int(max);
        if (query.format == "base64")
            c["data"] = Screenshotter::Base64Encode(grid);
        else if (query.format == "hex")
        {
            StateNode rows = StateNode::Array();
            static const char* kDigits = "0123456789ABCDEF";
            for (uint32_t at = 0; at < dataCells; at += columns)
            {
                std::string row;
                for (uint32_t i = at; i < std::min(dataCells, at + columns); i++)
                {
                    row += kDigits[grid[i] >> 4];
                    row += kDigits[grid[i] & 15];
                }
                rows.push(row);
            }
            c["rows"] = std::move(rows);
        }
        std::snprintf(line, sizeof line, "  %-6s %llu accesses, %llu hot cells, max %d\n",
                      GSCardAnalysis::ChannelName(channel), static_cast<unsigned long long>(analysis->Accesses(channel)),
                      static_cast<unsigned long long>(hot), int(max));
        text += line;
        list.push(std::move(c));
    }
    n["channels"] = std::move(list);

    // The ranges each recent channel touched in the last `frames` frames, trimmed to the bytes the CDL marks
    std::vector<GSCardAnalysis::Channel> recent = query.recent;
    if (recent.empty())
        recent.push_back(GSCardAnalysis::DacFetch);
    StateNode r = StateNode::Object();
    r["frames"] = int64_t(query.frames);
    StateNode recentList = StateNode::Array();
    const uint8_t* ramFlags = analysis->Flags(GSCardAnalysis::Ram);
    const uint32_t cellBytes = analysis->CellBytes();
    const uint64_t ramBytes = analysis->RamBytes();
    for (GSCardAnalysis::Channel channel : recent)
    {
        const uint8_t mask = channel == GSCardAnalysis::DacFetch ? uint8_t(GSCardAnalysis::Dac)
                             : channel == GSCardAnalysis::Fetch  ? uint8_t(GSCardAnalysis::kCodeFlags)
                             : channel == GSCardAnalysis::DataRead ? uint8_t(GSCardAnalysis::Read)
                                                                   : uint8_t(GSCardAnalysis::Write);
        StateNode ranges = StateNode::Array();
        bool truncated = false;
        std::string rangeText;
        uint32_t cell = 0;
        auto recentCell = [&](uint32_t at) {
            uint64_t last = 0;
            return analysis->LastTouch(channel, at, last) && last + query.frames >= now + 1;
        };
        while (cell < cells)
        {
            if (!recentCell(cell))
            {
                cell++;
                continue;
            }
            const uint32_t first = cell;
            while (cell < cells && recentCell(cell))
                cell++;
            uint64_t start = static_cast<uint64_t>(first) * cellBytes;
            uint64_t end = std::min<uint64_t>(static_cast<uint64_t>(cell) * cellBytes, ramBytes) - 1;
            // Trimmed to the bytes the CDL marks for the channel, when it marks any (a DMA read marks none)
            uint64_t marked = start;
            while (marked <= end && !(ramFlags[marked] & mask))
                marked++;
            if (marked <= end)
            {
                start = marked;
                while (end > start && !(ramFlags[end] & mask))
                    end--;
            }
            const size_t max = query.ranges ? query.ranges : 16;
            if (ranges.size() >= max * 4)
            {
                truncated = true;
                break;
            }
            StateNode range = StateNode::Object();
            range["start"] = start;
            range["end"] = end;
            range["length"] = end - start + 1;
            range["page"] = int64_t(start / kPage);
            range["offset"] = int64_t(start % kPage);
            ranges.push(std::move(range));
            std::snprintf(line, sizeof line, " #%06llX-#%06llX (ram %llu:#%04llX, %llu B)",
                          static_cast<unsigned long long>(start), static_cast<unsigned long long>(end),
                          static_cast<unsigned long long>(start / kPage), static_cast<unsigned long long>(start % kPage),
                          static_cast<unsigned long long>(end - start + 1));
            rangeText += line;
        }
        StateNode c = StateNode::Object();
        c["name"] = GSCardAnalysis::ChannelName(channel);
        c["ranges"] = std::move(ranges);
        if (truncated)
            c["truncated"] = true;
        recentList.push(std::move(c));
        text += std::string("  ") + GSCardAnalysis::ChannelName(channel) + " in the last " +
                std::to_string(query.frames) + " frames:" + (rangeText.empty() ? std::string(" none") : rangeText) +
                "\n";
    }
    r["channels"] = std::move(recentList);
    n["recent"] = std::move(r);
    n["text"] = text;
    return n;
}
}  // namespace DeviceState
