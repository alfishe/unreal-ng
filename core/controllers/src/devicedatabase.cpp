#include "devicedatabase.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <mutex>

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
    {
        info.controls.clear();  // a user entry's list replaces the built-in one
        for (const JsonValue& control : controls->Items())
            if (control.IsString())
                info.controls.push_back(control.AsString());
    }
    if (const JsonValue* ignore = entry.Find("ignore"); ignore != nullptr && ignore->IsString())
        info.ignore = !ignore->AsString().empty();
}

std::mutex& ActiveMutex()
{
    static std::mutex mutex;
    return mutex;
}

struct ActiveState
{
    std::shared_ptr<const DeviceDatabase> database;
    std::string userText;
};

ActiveState& ActiveSlot()
{
    static ActiveState state;
    return state;
}

/// A devices.user.json text as its root object: an empty text is an empty database
bool UserRoot(const std::string& text, JsonValue& root, std::string& error)
{
    if (text.find_first_not_of(" \t\r\n") == std::string::npos)
    {
        root = JsonValue::Object({{"schema", JsonValue::Number(1)}, {"families", JsonValue::Array()}});
        return true;
    }
    if (!ParseJson(text, root, error))
        return false;
    const JsonValue* schema = root.Find("schema");
    if (!root.IsObject() || schema == nullptr || !schema->IsNumber() || schema->AsNumber() != 1)
    {
        error = "a device database is an object with 'schema': 1";
        return false;
    }
    return true;
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
            const std::string id = Text(entry, "id");
            if (id.empty())
            {
                error = "a family without an 'id'";
                return false;
            }
            auto same = std::find_if(families.begin(), families.end(),
                                     [&id](const Family& f) { return f.info.family == id; });
            // A user entry for a known family changes the fields it names (a photo, a name) and keeps the rest
            Family family = merge && same != families.end() ? *same : Family{};
            family.info.family = id;
            Info(entry, family.info);
            const JsonValue* matches = entry.Find("match");
            if (matches != nullptr && matches->IsArray())
                family.matches.clear();
            else if (same == families.end() || !merge)
            {
                error = "family '" + family.info.family + "': no 'match' list";
                return false;
            }
            static const std::vector<JsonValue> kNone;
            for (const JsonValue& m : matches != nullptr && matches->IsArray() ? matches->Items() : kNone)
            {
                Match match;
                match.bus = Text(m, "bus");
                match.backend = Text(m, "backend");
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

std::vector<std::string> DeviceDatabase::FamilyIds() const
{
    std::vector<std::string> ids;
    for (const Family& family : _families)
        ids.push_back(family.info.family);
    return ids;
}

std::shared_ptr<const DeviceDatabase> DeviceDatabase::Active()
{
    std::lock_guard<std::mutex> lock(ActiveMutex());
    ActiveState& state = ActiveSlot();
    if (!state.database)
        state.database = std::make_shared<const DeviceDatabase>(Builtin());
    return state.database;
}

bool DeviceDatabase::SetUser(const std::string& text, std::string& error)
{
    auto database = std::make_shared<DeviceDatabase>(Builtin());
    JsonValue root;
    if (!UserRoot(text, root, error) || !database->Merge(root.Dump(), error))
        return false;
    std::lock_guard<std::mutex> lock(ActiveMutex());
    ActiveState& state = ActiveSlot();
    state.database = std::move(database);
    state.userText = text;
    return true;
}

std::string DeviceDatabase::UserText()
{
    std::lock_guard<std::mutex> lock(ActiveMutex());
    return ActiveSlot().userText;
}

std::vector<std::string> DeviceDatabase::UserFamilies(const std::string& text)
{
    std::vector<std::string> ids;
    JsonValue root;
    std::string error;
    if (!UserRoot(text, root, error))
        return ids;
    if (const JsonValue* list = root.Find("families"); list != nullptr && list->IsArray())
        for (const JsonValue& entry : list->Items())
            if (const std::string id = Text(entry, "id"); !id.empty())
                ids.push_back(id);
    return ids;
}

bool DeviceDatabase::PutUserFamily(const std::string& text, const JsonValue& family, std::string& out,
                                   std::string& error)
{
    const std::string id = family.IsObject() ? Text(family, "id") : std::string{};
    if (id.empty())
    {
        error = "a device entry is an object with an 'id' (the family it adds or changes)";
        return false;
    }
    JsonValue root;
    if (!UserRoot(text, root, error))
        return false;
    std::vector<JsonValue> families;
    bool replaced = false;
    if (const JsonValue* list = root.Find("families"); list != nullptr && list->IsArray())
        for (const JsonValue& entry : list->Items())
        {
            if (Text(entry, "id") == id)
            {
                families.push_back(family);
                replaced = true;
            }
            else
                families.push_back(entry);
        }
    if (!replaced)
        families.push_back(family);
    root.Set("families", JsonValue::Array(std::move(families)));
    // The result must layer over the built-in database (a new family needs its 'match' list)
    DeviceDatabase check = Builtin();
    if (!check.Merge(root.Dump(), error))
        return false;
    out = root.Dump(2);
    return true;
}

bool DeviceDatabase::RemoveUserFamily(const std::string& text, const std::string& id, std::string& out,
                                      std::string& error)
{
    JsonValue root;
    if (!UserRoot(text, root, error))
        return false;
    std::vector<JsonValue> families;
    bool found = false;
    if (const JsonValue* list = root.Find("families"); list != nullptr && list->IsArray())
        for (const JsonValue& entry : list->Items())
        {
            if (Text(entry, "id") == id)
                found = true;
            else
                families.push_back(entry);
        }
    if (!found)
    {
        error = "devices.user.json has no entry '" + id + "'";
        return false;
    }
    root.Set("families", JsonValue::Array(std::move(families)));
    out = root.Dump(2);
    return true;
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
