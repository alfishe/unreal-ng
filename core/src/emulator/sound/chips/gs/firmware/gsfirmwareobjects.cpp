#include "emulator/sound/chips/gs/firmware/gsfirmwareobjects.h"

#include <array>

#include <cstdio>

#include "3rdparty/unreal-z80/z80cpu.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/chips/gs/firmware/gsfirmwareprofile.h"
#include "emulator/sound/chips/gs/generalsoundcard.h"
#include "emulator/sound/chips/gs/gscommandlog.h"
#include "emulator/sound/chips/gs/gsslotstats.h"
#include "emulator/sound/chips/gs/soundchip_gslw.h"
#include "emulator/sound/soundmanager.h"

namespace
{

constexpr uint32_t kPageSize = 0x4000;

std::string Hex(uint32_t value, int digits)
{
    char text[16];
    std::snprintf(text, sizeof(text), "#%0*X", digits, value);
    return text;
}

std::string PlaceText(GSPlace place, uint16_t page, uint32_t address)
{
    switch (place)
    {
        case GSPlace::Ram: return "ram " + std::to_string(page) + ":" + Hex(address, 4);
        case GSPlace::Rom: return "rom " + std::to_string(page) + ":" + Hex(address, 4);
        case GSPlace::Window: break;
    }
    return Hex(address & 0xFFFF, 4);
}

/// Bytes of the card's memory at a place: a page and an offset that may run into the next page, or a CPU address
/// through its mapping now
class CardBytes
{
public:
    explicit CardBytes(const GeneralSoundCard& card) : _card(card) {}

    bool Byte(GSPlace place, uint16_t page, uint32_t address, uint8_t& out) const
    {
        if (place == GSPlace::Window)
        {
            const GeneralSoundCard::CardPage where = _card.mapCardAddress(static_cast<uint16_t>(address));
            if (where.kind == GeneralSoundCard::CardPage::None)
                return false;
            place = where.kind == GeneralSoundCard::CardPage::Rom ? GSPlace::Rom : GSPlace::Ram;
            page = where.page;
            address = where.offset;
        }
        const uint32_t linear = page * kPageSize + address;
        const uint32_t p = linear / kPageSize;
        const uint8_t* base = place == GSPlace::Ram ? _card.cardRamPage(p) : _card.cardRomPage(p);
        if (!base)
            return false;
        out = base[linear % kPageSize];
        return true;
    }

    /// A little-endian value of 1-4 bytes
    bool Value(GSPlace place, uint16_t page, uint32_t address, uint32_t size, uint32_t& out) const
    {
        out = 0;
        for (uint32_t i = 0; i < size; i++)
        {
            uint8_t byte = 0;
            if (!Byte(place, page, address + i, byte))
                return false;
            out |= static_cast<uint32_t>(byte) << (8 * i);
        }
        return true;
    }

    /// A little-endian value of 1-2 bytes at `offset` of a 32K firmware image: the firmware the command code runs
    /// (`baseline` false: GeneralSoundCard::firmwarePage) or as it started (true: firmwareBaselinePage)
    bool FirmwareValue(bool baseline, uint32_t offset, uint32_t size, uint32_t& out) const
    {
        out = 0;
        for (uint32_t i = 0; i < size; i++)
        {
            const uint32_t at = offset + i;
            const uint32_t page = at / kPageSize;
            const uint8_t* base = baseline ? _card.firmwareBaselinePage(page) : _card.firmwarePage(page);
            if (page > 1 || !base)
                return false;
            out |= static_cast<uint32_t>(base[at % kPageSize]) << (8 * i);
        }
        return true;
    }

private:
    const GeneralSoundCard& _card;
};

StateNode ReadStruct(const CardBytes& bytes, const GSFirmwareProfile& profile, const GSStructInfo& info, GSPlace place,
                     uint16_t page, uint32_t base, int depth);

StateNode ReadField(const CardBytes& bytes, const GSFirmwareProfile& profile, const GSStructField& field, GSPlace place,
                    uint16_t page, uint32_t address, int depth)
{
    switch (field.type)
    {
        case GSFieldType::Struct:
            if (const GSStructInfo* inner = profile.Struct(field.structName); inner && depth < 8)
                return ReadStruct(bytes, profile, *inner, place, page, address, depth + 1);
            return StateNode();
        case GSFieldType::Bytes:
        {
            std::string text;
            for (uint32_t i = 0; i < field.size; i++)
            {
                uint8_t byte = 0;
                if (!bytes.Byte(place, page, address + i, byte))
                    return StateNode();
                char hex[4];
                std::snprintf(hex, sizeof(hex), "%02X", byte);
                text += (i ? " " : "") + std::string(hex);
            }
            return StateNode(text);
        }
        default:
        {
            uint32_t value = 0;
            if (!bytes.Value(place, page, address, field.size, value))
                return StateNode();
            if (field.type == GSFieldType::S8)
                return StateNode(static_cast<int64_t>(static_cast<int8_t>(value)));
            if (field.type == GSFieldType::S16)
                return StateNode(static_cast<int64_t>(static_cast<int16_t>(value)));
            return StateNode(static_cast<int64_t>(value));
        }
    }
}

StateNode ReadStruct(const CardBytes& bytes, const GSFirmwareProfile& profile, const GSStructInfo& info, GSPlace place,
                     uint16_t page, uint32_t base, int depth)
{
    StateNode fields = StateNode::Object();
    for (const GSStructField& field : info.fields)
    {
        const uint32_t step = field.stride ? field.stride : field.size;
        if (field.count <= 1)
        {
            fields[field.name] = ReadField(bytes, profile, field, place, page, base + field.offset, depth);
            continue;
        }
        StateNode list = StateNode::Array();
        for (uint32_t i = 0; i < field.count; i++)
            list.push(ReadField(bytes, profile, field, place, page, base + field.offset + i * step, depth));
        fields[field.name] = list;
    }
    return fields;
}

bool RegisterValue(const Z80CpuRegisters& r, const std::string& name, uint32_t& out)
{
    const struct
    {
        const char* name;
        uint32_t value;
    } table[] = {
        {"A", static_cast<uint32_t>(r.af >> 8)}, {"F", static_cast<uint32_t>(r.af & 0xFF)},
        {"B", static_cast<uint32_t>(r.bc >> 8)}, {"C", static_cast<uint32_t>(r.bc & 0xFF)},
        {"D", static_cast<uint32_t>(r.de >> 8)}, {"E", static_cast<uint32_t>(r.de & 0xFF)},
        {"H", static_cast<uint32_t>(r.hl >> 8)}, {"L", static_cast<uint32_t>(r.hl & 0xFF)},
        {"AF", r.af}, {"BC", r.bc}, {"DE", r.de}, {"HL", r.hl}, {"AF'", r.afAlt}, {"BC'", r.bcAlt},
        {"DE'", r.deAlt}, {"HL'", r.hlAlt}, {"IX", r.ix}, {"IY", r.iy}, {"SP", r.sp}, {"PC", r.pc},
    };
    for (const auto& entry : table)
        if (name == entry.name)
        {
            out = entry.value;
            return true;
        }
    return false;
}

StateNode ReadVariable(const GeneralSoundCard& card, const CardBytes& bytes, const GSProfileVariable& v)
{
    StateNode node = StateNode::Object();
    node["name"] = v.name;
    if (!v.meaning.empty())
        node["meaning"] = v.meaning;
    uint32_t value = 0;
    switch (v.kind)
    {
        case GSVariableKind::Memory:
            node["kind"] = std::string("memory");
            node["at"] = PlaceText(v.place, v.page, v.address);
            node["valid"] = bytes.Value(v.place, v.page, v.address, v.size, value);
            break;
        case GSVariableKind::SmcOperand:
            // The operand follows the opcode at the symbol (`label = $+1` names the operand itself: then `at` is it)
            node["kind"] = std::string("smc_operand");
            node["at"] = PlaceText(v.place, v.page, v.address);
            node["valid"] = bytes.Value(v.place, v.page, v.address, v.size, value);
            break;
        case GSVariableKind::Register:
        {
            node["kind"] = std::string("register");
            node["register"] = v.reg;
            node["pc_range"] = Hex(v.pcFrom, 4) + "-" + Hex(v.pcTo, 4);
            Z80CpuRegisters r{};
            const bool cpu = card.cardRegisters(r);
            const bool inRange = cpu && r.pc >= v.pcFrom && r.pc <= v.pcTo;
            node["valid"] = inRange && RegisterValue(r, v.reg, value);
            if (!inRange)
                node["reason"] = std::string(cpu ? "the card PC " + Hex(r.pc, 4) + " is outside the range" : "no card CPU");
            break;
        }
    }
    if (node.find("valid")->b)
        node["value"] = static_cast<int64_t>(value);
    return node;
}

/// The extension profile that names `code` (the code that patched the entry, M4); "" when none
std::string PatchedBy(uint8_t code)
{
    for (const GSFirmwareProfile* p : GSFirmwareProfiles())
        if (p->extends && p->Command(code).name)
            return p->id;
    return {};
}

/// The command dispatch as the card holds it now (M4): every entry's handler, the baseline it has in the ROM, the
/// entries that differ ("patched") and the ones that go to the "unknown command" stub
StateNode ReadDispatch(const GSFirmwareProfile& profile, const CardBytes& bytes)
{
    StateNode node = StateNode::Object();
    StateNode entries = StateNode::Array();
    int64_t patched = 0;
    int64_t unknown = 0;
    const auto add = [&](uint8_t code, bool ok, uint32_t handler, bool hasBaseline, uint32_t baseline) {
        StateNode e = StateNode::Object();
        e["command"] = static_cast<int64_t>(code);
        if (const char* name = profile.Command(code).name)
            e["name"] = std::string(name);
        if (!ok)
        {
            e["readable"] = false;
            entries.push(e);
            return;
        }
        e["handler"] = Hex(handler, 4);
        // The "unknown command" stub, or a handler the profile names no command for
        const bool stub = profile.dispatch.hasUnknownStub && handler == profile.dispatch.unknownStub;
        if (stub || !profile.Command(code).name)
        {
            e["unknown"] = true;
            unknown++;
        }
        if (hasBaseline)
        {
            e["baseline"] = Hex(baseline, 4);
            if (baseline != handler)
            {
                e["patched"] = true;
                patched++;
                if (const std::string by = PatchedBy(code); !by.empty())
                    e["patched_by"] = by;
            }
        }
        entries.push(e);
    };
    if (profile.dispatch.kind == GSDispatchKind::Words)
    {
        // A word per entry; entries from #20 on are the commands from `highFrom` (NeoTracker: #E0-#FF)
        const GSDispatchInfo& d = profile.dispatch;
        node["table"] = PlaceText(d.place, d.page, d.table);
        for (uint32_t i = 0; i < d.entries; i++)
        {
            const uint32_t code = i < 0x20 ? i : i - 0x20 + d.highFrom;
            if (code > 0xFF)
                break;
            uint32_t handler = 0;
            const bool ok = bytes.Value(d.place, d.page, d.table + 2 * i, 2, handler);
            add(static_cast<uint8_t>(code), ok, handler, false, 0);
        }
    }
    else if (profile.comtab)
    {
        // The stock split (COMINT): COMTAB words for #00-#1F and #F0-#FF, COMTABH low bytes for #20-#EF with the high
        // bytes 256 later. COMHIGH maps page 0 (OUT (MPAG),0) before it reads COMTABH, so the tables are read where
        // the firmware keeps them, not through window 3 as mapped now: the firmware's 32K image (the ROM on a GS, the
        // RAM copy on a NeoGS), against the image as it started
        node["table"] = Hex(profile.comtab, 4);
        node["table_high"] = Hex(profile.comtabh, 4);
        // COMTAB in window 0, COMTABH in the high half of page 0 (#C000-#FFFF): offsets into the 32K image
        const bool inImage = profile.comtab < kPageSize && profile.comtabh >= 0xC000;
        const auto read = [&](bool baseline, uint32_t address, uint32_t size, uint32_t& out) {
            if (inImage)
                return bytes.FirmwareValue(baseline, address < kPageSize ? address : address - 0x8000, size, out);
            return !baseline && bytes.Value(GSPlace::Window, 0, address, size, out);
        };
        for (uint32_t code = 0; code < 0x100; code++)
        {
            uint32_t handler = 0;
            uint32_t baseline = 0;
            bool ok = false;
            bool base = false;
            if (code < 0x20 || code >= 0xF0)
            {
                const uint32_t index = code < 0x20 ? code : code - 0xD0;
                ok = read(false, profile.comtab + 2 * index, 2, handler);
                base = read(true, profile.comtab + 2 * index, 2, baseline);
            }
            else
            {
                const uint32_t low = profile.comtabh + (code - 0x20);
                uint32_t lo = 0, hi = 0, blo = 0, bhi = 0;
                ok = read(false, low, 1, lo) && read(false, low + 0x100, 1, hi);
                handler = lo | (hi << 8);
                base = read(true, low, 1, blo) && read(true, low + 0x100, 1, bhi);
                baseline = blo | (bhi << 8);
            }
            add(static_cast<uint8_t>(code), ok, handler, base, baseline);
        }
    }
    node["entries"] = entries;
    node["patched"] = patched;
    node["unknown"] = unknown;
    return node;
}

StateNode Error(const std::string& text)
{
    StateNode node = StateNode::Object();
    node["error"] = text;
    return node;
}

} // namespace

StateNode GSReadFirmwareObjects(const GeneralSoundCard& card, const GSFirmwareProfile& profile, const std::string& object)
{
    const CardBytes bytes(card);
    StateNode body = StateNode::Object();
    body["profile"] = std::string(profile.id);
    body["title"] = std::string(profile.title);
    body["source"] = profile.source;

    StateNode instances = StateNode::Array();
    for (const GSInstanceInfo& instance : profile.instances)
    {
        if (!object.empty() && object != instance.name)
            continue;
        const GSStructInfo* info = profile.Struct(instance.structName);
        if (!info)
            continue;
        StateNode node = StateNode::Object();
        node["name"] = instance.name;
        node["struct"] = instance.structName;
        node["at"] = PlaceText(instance.place, instance.page, instance.address);
        node["count"] = static_cast<int64_t>(instance.count);
        node["size"] = static_cast<int64_t>(info->size);
        StateNode rows = StateNode::Array();
        const uint32_t step = instance.stride ? instance.stride : info->size;
        for (uint32_t i = 0; i < instance.count; i++)
        {
            const uint32_t address = instance.address + i * step;
            StateNode row = StateNode::Object();
            row["index"] = static_cast<int64_t>(i);
            row["at"] = PlaceText(instance.place, instance.page, address);
            row["fields"] = ReadStruct(bytes, profile, *info, instance.place, instance.page, address, 0);
            rows.push(row);
        }
        node["rows"] = rows;
        instances.push(node);
    }
    body["instances"] = instances;

    StateNode variables = StateNode::Array();
    for (const GSProfileVariable& v : profile.objectVariables)
        if (object.empty() || object == v.name)
            variables.push(ReadVariable(card, bytes, v));
    body["variables"] = variables;

    // The built-in tables' variables of the fixed RAM window; the lightweight card has no RAM, its interpreter mirrors
    // the variables it keeps (`source` mirror)
    const auto* lightweight = dynamic_cast<const SoundChip_GSLightweight*>(&card);
    StateNode window = StateNode::Array();
    for (size_t i = 0; i < profile.variableCount; i++)
    {
        const GSVariableInfo& v = profile.variables[i];
        if (!object.empty() && object != v.name)
            continue;
        StateNode node = StateNode::Object();
        node["name"] = std::string(v.name);
        node["at"] = Hex(v.address, 4);
        node["size"] = static_cast<int64_t>(v.size);
        node["meaning"] = std::string(v.meaning);
        uint32_t value = 0;
        if (bytes.Value(GSPlace::Window, 0, v.address, v.size <= 4 ? v.size : 4, value))
            node["value"] = static_cast<int64_t>(value);
        else if (lightweight && lightweight->mirroredVariable(v.name, value))
        {
            node["value"] = static_cast<int64_t>(value);
            node["source"] = std::string("mirror");
        }
        window.push(node);
    }
    body["window_variables"] = window;

    if ((object.empty() || object == "dispatch") && (profile.dispatch.kind != GSDispatchKind::None || profile.comtab))
        body["dispatch"] = ReadDispatch(profile, bytes);
    // The commands the host sent that the decoding profile does not name (AU6), from the command log while it records
    if (const GSCommandLog* log = card.commandLog(); log && (object.empty() || object == "unknown_commands"))
    {
        StateNode list = StateNode::Array();
        const std::array<uint32_t, 256> counts = log->UnknownCommands();
        for (size_t code = 0; code < counts.size(); code++)
            if (counts[code])
            {
                StateNode n = StateNode::Object();
                n["command"] = static_cast<int64_t>(code);
                n["count"] = static_cast<int64_t>(counts[code]);
                list.push(n);
            }
        body["unknown_commands"] = list;
    }
    return body;
}

GSFirmwareObjectsReply GSFirmwareObjectsRead(EmulatorContext* context, const GSFirmwareObjectsQuery& query)
{
    GSFirmwareObjectsReply reply;
    bool known = true;
    const GeneralSoundCard* card = GSCardForCpu(context, query.cpu, nullptr, &known);
    if (!card)
    {
        reply.status = 404;
        reply.body = Error(!known             ? "No card CPU '" + query.cpu + "' (gs: the GS slot's card, ms: the ZX-MultiSound's)"
                           : query.cpu == "ms" ? std::string("No ZX-MultiSound with a General Sound fitted")
                                               : std::string("No General Sound card is fitted"));
        reply.text = reply.body.find("error")->s;
        return reply;
    }
    // The profile the code on the card decodes by now (the command log's stack top, M5), else the ROM's
    const GSFirmwareProfile* profile = nullptr;
    if (!query.profile.empty())
        profile = FindGSFirmwareProfileById(query.profile);
    else if (const GSCommandLog* log = card->commandLog(); log && log->On() && log->Profile())
        profile = log->Profile();
    else
        profile = GSFirmwareProfileOf(*card);
    if (!profile)
    {
        reply.status = 404;
        reply.body = Error(query.profile.empty() ? "The card's firmware has no profile"
                                                 : "No firmware profile '" + query.profile + "'");
        reply.text = reply.body.find("error")->s;
        return reply;
    }
    reply.body = GSReadFirmwareObjects(*card, *profile, query.object);
    reply.body["cpu"] = std::string(query.cpu.empty() ? "gs" : query.cpu);
    if (!query.object.empty() && reply.body.find("instances")->items.empty() &&
        reply.body.find("variables")->items.empty() && reply.body.find("window_variables")->items.empty() &&
        !reply.body.find("dispatch") && !reply.body.find("unknown_commands"))
    {
        reply.status = 404;
        reply.body = Error("Profile " + std::string(profile->id) + " has no object '" + query.object + "'");
        reply.text = reply.body.find("error")->s;
        return reply;
    }
    reply.text = GSFirmwareObjectsText(reply.body);
    return reply;
}

StateNode GSFirmwareProfilesState(std::string* text)
{
    StateNode list = StateNode::Array();
    for (const GSFirmwareProfile* profile : GSFirmwareProfiles())
    {
        StateNode node = StateNode::Object();
        node["id"] = std::string(profile->id);
        node["title"] = std::string(profile->title);
        node["source"] = profile->source;
        if (profile->sha256 && *profile->sha256)
            node["sha256"] = std::string(profile->sha256);
        StateNode matches = StateNode::Array();
        for (const GSProfileMatch& match : profile->matches)
        {
            StateNode m = StateNode::Object();
            m["kind"] = std::string(match.kind == GSMatchKind::Upload ? "upload" : "rom");
            m["sha256"] = match.sha256;
            if (match.kind == GSMatchKind::Upload)
            {
                m["addr"] = Hex(match.address, 4);
                m["length"] = static_cast<int64_t>(match.length);
            }
            matches.push(m);
        }
        node["matches"] = matches;
        int64_t commands = 0;
        for (const GSCommandInfo& info : *profile->commands)
            commands += info.name ? 1 : 0;
        node["commands"] = commands;
        node["structs"] = static_cast<int64_t>(profile->structs.size());
        node["instances"] = static_cast<int64_t>(profile->instances.size());
        node["variables"] = static_cast<int64_t>(profile->objectVariables.size() + profile->variableCount);
        if (profile->dispatch.kind != GSDispatchKind::None)
        {
            StateNode d = StateNode::Object();
            d["pc"] = Hex(profile->dispatch.pc, 4);
            d["register"] = profile->dispatch.reg;
            d["table"] = PlaceText(profile->dispatch.place, profile->dispatch.page, profile->dispatch.table);
            d["entries"] = static_cast<int64_t>(profile->dispatch.entries);
            node["dispatch"] = d;
        }
        if (text)
            *text += std::string(profile->id) + ": " + profile->title + " (" + profile->source + "), " +
                     std::to_string(commands) + " commands, " + std::to_string(profile->structs.size()) + " structs, " +
                     std::to_string(profile->instances.size()) + " instances\n";
        list.push(node);
    }
    StateNode body = StateNode::Object();
    body["profiles"] = list;
    return body;
}

namespace
{

std::string ValueText(const StateNode& value)
{
    switch (value.kind)
    {
        case StateNode::Kind::Int: return std::to_string(value.i);
        case StateNode::Kind::String: return value.s;
        case StateNode::Kind::Array:
        {
            std::string text = "[";
            for (size_t i = 0; i < value.items.size(); i++)
                text += (i ? ", " : "") + ValueText(value.items[i]);
            return text + "]";
        }
        case StateNode::Kind::Object:
        {
            std::string text = "{";
            for (size_t i = 0; i < value.members.size(); i++)
                text += (i ? ", " : "") + value.members[i].first + " " + ValueText(value.members[i].second);
            return text + "}";
        }
        default: return "?";
    }
}

} // namespace

std::string GSFirmwareObjectsText(const StateNode& body)
{
    std::string text;
    if (const StateNode* error = body.find("error"))
        return error->s;
    text += "profile " + body.find("profile")->s + " (" + body.find("title")->s + ")\n";
    for (const StateNode& instance : body.find("instances")->items)
        for (const StateNode& row : instance.find("rows")->items)
        {
            text += instance.find("name")->s + "[" + std::to_string(row.find("index")->i) + "] @" + row.find("at")->s + ":";
            const StateNode* fields = row.find("fields");
            for (size_t i = 0; i < fields->members.size(); i++)
                text += (i ? ", " : " ") + fields->members[i].first + " " + ValueText(fields->members[i].second);
            text += "\n";
        }
    for (const char* list : {"variables", "window_variables"})
        for (const StateNode& v : body.find(list)->items)
        {
            text += v.find("name")->s + " @" + (v.find("at") ? v.find("at")->s : v.find("register")->s) + ": ";
            if (const StateNode* value = v.find("value"))
                text += std::to_string(value->i) + " (" + Hex(static_cast<uint32_t>(value->i), 2) + ")";
            else
                text += v.find("reason") ? v.find("reason")->s : std::string("unreadable");
            text += "\n";
        }
    if (const StateNode* dispatch = body.find("dispatch"))
    {
        text += "dispatch " + dispatch->find("table")->s + ": " + std::to_string(dispatch->find("patched")->i) +
                " patched, " + std::to_string(dispatch->find("unknown")->i) + " unknown\n";
        for (const StateNode& e : dispatch->find("entries")->items)
            if (e.find("patched"))
                text += "  " + Hex(static_cast<uint32_t>(e.find("command")->i), 2) + " -> " + e.find("handler")->s +
                        " (ROM " + e.find("baseline")->s + ")" +
                        (e.find("patched_by") ? ", by " + e.find("patched_by")->s : std::string()) + "\n";
    }
    if (const StateNode* unknown = body.find("unknown_commands"); unknown && !unknown->items.empty())
    {
        text += "unknown commands:";
        for (const StateNode& u : unknown->items)
            text += " " + Hex(static_cast<uint32_t>(u.find("command")->i), 2) + " x" + std::to_string(u.find("count")->i);
        text += "\n";
    }
    return text;
}
