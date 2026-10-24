#include "devicedatabase.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

#include "profiles/json.h"

namespace controllers
{

namespace
{
std::string Lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

bool Hex(const JsonValue* value, uint16_t& out)
{
    if (value == nullptr || !value->IsString() || value->AsString().empty() || value->AsString().size() > 4)
        return false;
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(value->AsString().c_str(), &end, 16);
    if (*end != '\0')
        return false;
    out = static_cast<uint16_t>(parsed);
    return true;
}

std::string Text(const JsonValue& object, const char* field)
{
    const JsonValue* value = object.Find(field);
    return value != nullptr && value->IsString() ? value->AsString() : std::string{};
}

/// A `hid` object (device-database.md §2); false with the reason for a malformed one
bool ReadHid(const JsonValue& hid, DeviceMatch::HidMap& out, std::string& error)
{
    if (!hid.IsObject())
    {
        error = "'hid' is an object";
        return false;
    }
    if (const JsonValue* buttons = hid.Find("buttons"); buttons != nullptr)
    {
        if (!buttons->IsArray())
        {
            error = "'hid.buttons' is a list of control names";
            return false;
        }
        for (const JsonValue& button : buttons->Items())
            out.buttons.push_back(button.IsString() ? button.AsString() : std::string{});
    }
    if (const JsonValue* triggers = hid.Find("rxRyTriggers"); triggers != nullptr && triggers->IsBool())
        out.rxRyTriggers = triggers->AsBool() ? 1 : 0;
    if (const JsonValue* dpad = hid.Find("dpadAxes"); dpad != nullptr && dpad->IsBool())
        out.dpadAxes = dpad->AsBool();
    out.hat = Text(hid, "hat");
    if (const JsonValue* consumer = hid.Find("consumer"); consumer != nullptr && consumer->IsObject())
        for (const auto& [usage, control] : consumer->GetMembers())
        {
            JsonValue key = JsonValue::String(usage);
            uint16_t code = 0;
            if (!Hex(&key, code) || !control.IsString())
            {
                error = "'hid.consumer' maps a hex usage to a control name";
                return false;
            }
            out.consumer.emplace_back(code, control.AsString());
        }
    if (const JsonValue* bits = hid.Find("bits"); bits != nullptr && bits->IsArray())
        for (const JsonValue& entry : bits->Items())
        {
            const JsonValue* report = entry.Find("report");
            const JsonValue* byte = entry.Find("byte");
            const JsonValue* bit = entry.Find("bit");
            const std::string control = Text(entry, "control");
            if (report == nullptr || byte == nullptr || bit == nullptr || !report->IsNumber() || !byte->IsNumber() ||
                !bit->IsNumber() || bit->AsNumber() < 0 || bit->AsNumber() > 7 || byte->AsNumber() < 0 ||
                byte->AsNumber() > 1024 || report->AsNumber() < 0 || report->AsNumber() > 255 || control.empty())
            {
                error = "a 'hid.bits' entry is {report, byte, bit 0-7, control}";
                return false;
            }
            out.bits.push_back({static_cast<uint8_t>(report->AsNumber()), static_cast<uint32_t>(byte->AsNumber()),
                                static_cast<uint8_t>(bit->AsNumber()), control});
        }
    return true;
}

void Info(const JsonValue& entry, DeviceMatch& info)
{
    for (const auto& [field, target] : {std::pair<const char*, std::string*>{"name", &info.name},
                                        {"vendor", &info.vendor},
                                        {"layout", &info.layout},
                                        {"images", &info.images},
                                        {"glyphs", &info.glyphs}})
    {
        const std::string value = Text(entry, field);
        if (!value.empty())
            *target = value;
    }
    if (const JsonValue* player = entry.Find("player"); player != nullptr && player->IsBool())
        info.player = player->AsBool();
    if (const JsonValue* role = entry.Find("role"); role != nullptr && role->IsString())
        DeviceRoleFromName(role->AsString(), info.role);  // an unknown name keeps the player role
    if (const JsonValue* controls = entry.Find("controls"); controls != nullptr && controls->IsArray())
        for (const JsonValue& control : controls->Items())
            if (control.IsString())
                info.controls.push_back(control.AsString());
}
}  // namespace

bool DeviceDatabase::Load(const std::string& text, std::string& error)
{
    return Read(text, false, error);
}

bool DeviceDatabase::Merge(const std::string& text, std::string& error)
{
    return Read(text, true, error);
}

bool DeviceDatabase::Read(const std::string& text, bool merge, std::string& error)
{
    JsonValue root;
    if (!ParseJson(text, root, error))
        return false;
    const JsonValue* schema = root.Find("schema");
    if (!root.IsObject() || schema == nullptr || !schema->IsNumber() || schema->AsNumber() != 1)
    {
        error = "a device database is an object with 'schema': 1";
        return false;
    }
    std::vector<Family> families = merge ? _families : std::vector<Family>{};
    std::vector<Template> templates = merge ? _templates : std::vector<Template>{};
    auto glyphs = merge ? _glyphs : decltype(_glyphs){};

    if (const JsonValue* list = root.Find("families"); list != nullptr && list->IsArray())
    {
        for (const JsonValue& entry : list->Items())
        {
            Family family;
            family.info.family = Text(entry, "id");
            if (family.info.family.empty())
            {
                error = "a family without an 'id'";
                return false;
            }
            Info(entry, family.info);
            if (const JsonValue* hid = entry.Find("hid"); hid != nullptr && !ReadHid(*hid, family.info.hid, error))
            {
                error = "family '" + family.info.family + "': " + error;
                return false;
            }
            family.info.ignore = !Text(entry, "ignore").empty();
            const JsonValue* matches = entry.Find("match");
            if (matches == nullptr || !matches->IsArray())
            {
                error = "family '" + family.info.family + "': no 'match' list";
                return false;
            }
            for (const JsonValue& m : matches->Items())
            {
                Match match;
                match.bus = Text(m, "bus");
                match.backend = Text(m, "backend");
                if (const JsonValue* hid = m.Find("hid"); hid != nullptr)
                {
                    if (!ReadHid(*hid, match.hid, error))
                    {
                        error = "family '" + family.info.family + "': " + error;
                        return false;
                    }
                    match.hasHid = true;
                }
                uint16_t version = 0;
                if (Hex(m.Find("version"), version))
                    match.version = version;
                const JsonValue* pids = m.Find("pid");
                if (!Hex(m.Find("vid"), match.vendor) || pids == nullptr || !pids->IsArray())
                {
                    error = "family '" + family.info.family + "': a match needs 'vid' and a 'pid' list (hex)";
                    return false;
                }
                for (const JsonValue& pid : pids->Items())
                {
                    uint16_t product = 0;
                    if (!Hex(&pid, product))
                    {
                        error = "family '" + family.info.family + "': a pid is 4 hex digits";
                        return false;
                    }
                    match.products.push_back(product);
                }
                family.matches.push_back(match);
            }
            if (const JsonValue* modes = entry.Find("modes"); modes != nullptr && modes->IsObject())
                for (const auto& [pid, mode] : modes->GetMembers())
                {
                    JsonValue key = JsonValue::String(pid);
                    uint16_t product = 0;
                    if (Hex(&key, product) && mode.IsString())
                        family.modes[product] = mode.AsString();
                }
            auto same = std::find_if(families.begin(), families.end(),
                                     [&family](const Family& f) { return f.info.family == family.info.family; });
            if (same != families.end())
                *same = std::move(family);
            else
                families.push_back(std::move(family));
        }
    }
    if (const JsonValue* list = root.Find("templates"); list != nullptr && list->IsArray())
        for (const JsonValue& entry : list->Items())
        {
            Template t;
            t.name = Lower(Text(entry, "name"));
            t.info.family = Text(entry, "family");
            Info(entry, t.info);
            t.info.name.clear();  // a template's "name" is the substring it matches; the device keeps its own name
            if (!t.name.empty() && !t.info.family.empty())
                templates.push_back(std::move(t));
        }
    if (const JsonValue* sets = root.Find("glyphs"); sets != nullptr && sets->IsObject())
        for (const auto& [set, labels] : sets->GetMembers())
            for (const auto& [control, label] : labels.GetMembers())
                if (label.IsString())
                    glyphs[set][control] = label.AsString();

    _families = std::move(families);
    _templates = std::move(templates);
    _glyphs = std::move(glyphs);
    return true;
}

DeviceMatch DeviceDatabase::Lookup(const DeviceIdentity& device) const
{
    const Family* best = nullptr;
    const Match* bestMatch = nullptr;
    int bestScore = 0;
    for (const Family& family : _families)
    {
        for (const Match& match : family.matches)
        {
            if (match.vendor != device.vendor ||
                std::find(match.products.begin(), match.products.end(), device.product) == match.products.end())
                continue;
            if (!match.backend.empty() && !device.backend.empty() && match.backend != device.backend)
                continue;
            if (match.version >= 0 && match.version != device.version)
                continue;
            if (!match.bus.empty() && !device.bus.empty() && match.bus != device.bus)
                continue;
            // A version or a bus that had to match makes the entry more specific (§3: exact before VID + PID)
            const int score = match.version >= 0 || !match.bus.empty() ? 100 : 80;
            if (score > bestScore)
            {
                best = &family;
                bestMatch = &match;
                bestScore = score;
            }
        }
    }
    DeviceMatch result;
    if (best != nullptr)
    {
        result = best->info;
        result.score = bestScore;
        result.step = bestScore == 100 ? "exact" : "vid-pid";
        if (bestMatch->hasHid)
            result.hid = bestMatch->hid;
        if (auto mode = best->modes.find(device.product); mode != best->modes.end())
            result.mode = mode->second;
    }
    else
    {
        const std::string name = Lower(device.name);
        auto t = std::find_if(_templates.begin(), _templates.end(),
                              [&name](const Template& t) { return name.find(t.name) != std::string::npos; });
        if (t != _templates.end())
        {
            result = t->info;
            result.score = 20;
            result.step = "name";
        }
        else
        {
            result.family = "generic";
            result.step = "unknown";
        }
    }
    if (result.name.empty())
        result.name = device.name;
    return result;
}

DeviceMatch DeviceDatabase::ForFamily(const std::string& family, const std::string& layout) const
{
    for (const auto& known : _families)
        if (known.info.family == family)
            return known.info;
    DeviceMatch match;
    match.family = family.empty() ? "generic" : family;
    if (!layout.empty())
        match.layout = layout;
    if (match.layout == "xbox" || match.layout == "playstation" || match.layout == "nintendo")
    {
        match.images = "generic-" + match.layout + "-like";
        match.glyphs = match.layout;
    }
    else if (match.family == "keyboard")
        match.images = "generic-keyboard";
    return match;
}

std::string DeviceDatabase::Glyph(const std::string& glyphs, const std::string& control) const
{
    auto set = _glyphs.find(glyphs);
    if (set != _glyphs.end())
        if (auto label = set->second.find(control); label != set->second.end())
            return label->second;
    return control;
}

const DeviceDatabase& DeviceDatabase::Builtin()
{
    static const DeviceDatabase database = [] {
        DeviceDatabase db;
        std::string error;
        db.Load(BuiltinDevicesJson(), error);  // checked by a test
        return db;
    }();
    return database;
}

}  // namespace controllers
