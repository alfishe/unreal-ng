#include "profiles/memoryprofile.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace controllers
{

namespace
{
/// "0x5c3a", "23610", "#5C3A", "$5C3A"
bool ParseNumber(const std::string& text, unsigned long& out)
{
    if (text.empty())
        return false;
    const char* begin = text.c_str();
    int base = 10;
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
    {
        begin += 2;
        base = 16;
    }
    else if (text[0] == '#' || text[0] == '$')
    {
        begin += 1;
        base = 16;
    }
    if (*begin == '\0' || *begin == '-' || *begin == '+' || std::isspace(static_cast<unsigned char>(*begin)))
        return false;
    char* end = nullptr;
    out = std::strtoul(begin, &end, base);
    return *end == '\0';
}

std::string Trim(const std::string& text)
{
    const size_t first = text.find_first_not_of(" \t");
    if (first == std::string::npos)
        return {};
    return text.substr(first, text.find_last_not_of(" \t") - first + 1);
}

std::string At(const char* list, size_t index)
{
    return std::string("memory.") + list + "[" + std::to_string(index) + "]";
}

bool CheckFields(const JsonValue& entry, std::initializer_list<const char*> known, const std::string& where,
                 std::string& error)
{
    if (!entry.IsObject())
    {
        error = where + ": an object expected";
        return false;
    }
    for (const auto& [field, value] : entry.GetMembers())
        if (std::none_of(known.begin(), known.end(), [&field](const char* name) { return field == name; }))
        {
            std::string names;
            for (const char* name : known)
                names += std::string(names.empty() ? "" : ", ") + name;
            error = where + ": unknown field '" + field + "' (known: " + names + ")";
            return false;
        }
    return true;
}

bool OptionalString(const JsonValue& entry, const char* field, std::string& out, const std::string& where,
                    std::string& error)
{
    const JsonValue* value = entry.Find(field);
    if (value == nullptr)
        return true;
    if (!value->IsString())
    {
        error = where + ": '" + field + "' is a string";
        return false;
    }
    out = value->AsString();
    return true;
}

bool RequiredString(const JsonValue& entry, const char* field, std::string& out, const std::string& where,
                    std::string& error)
{
    if (entry.Find(field) == nullptr)
    {
        error = where + ": '" + field + "' is required";
        return false;
    }
    if (!OptionalString(entry, field, out, where, error))
        return false;
    if (Trim(out).empty())
    {
        error = where + ": '" + field + "' is empty";
        return false;
    }
    return true;
}

bool Place(const JsonValue& entry, bool range, MemoryPlace& out, const std::string& where, std::string& error)
{
    const JsonValue* at = entry.Find("at");
    if (at == nullptr)
    {
        error = where + ": 'at' is required";
        return false;
    }
    if (!ParseMemoryPlace(*at, range, out, error))
    {
        error = where + ": " + error;
        return false;
    }
    return true;
}

/// A list of entries: absent is empty, anything else is a list
const JsonValue* List(const JsonValue& section, const char* field, std::string& error)
{
    const JsonValue* list = section.Find(field);
    if (list != nullptr && !list->IsArray())
    {
        error = std::string("memory.") + field + ": a list expected";
        return nullptr;
    }
    return list;
}

bool Labels(const JsonValue& section, const char* field, std::vector<MemoryLabelSpec>& out, std::string& error)
{
    const JsonValue* list = List(section, field, error);
    if (!error.empty())
        return false;
    if (list == nullptr)
        return true;
    for (size_t i = 0; i < list->Items().size(); i++)
    {
        const JsonValue& entry = list->Items()[i];
        const std::string where = At(field, i);
        MemoryLabelSpec label;
        if (!CheckFields(entry, {"name", "at", "comment"}, where, error) ||
            !RequiredString(entry, "name", label.name, where, error) || !Place(entry, false, label.place, where, error) ||
            !OptionalString(entry, "comment", label.comment, where, error))
            return false;
        if (std::any_of(label.name.begin(), label.name.end(),
                        [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; }))
        {
            error = where + ": the name '" + label.name + "' has a space";
            return false;
        }
        out.push_back(std::move(label));
    }
    return true;
}
}  // namespace

const std::vector<std::string>& MemoryRegionKinds()
{
    static const std::vector<std::string> kinds = {
        "code",     "data",          "text",          "font", "sprite", "tile",   "screen",    "buffer", "init_table",
        "records",  "variable",      "pointer_table", "handler_table", "rng", "music", "packed", "level_map", "unused",
        "untouched"};
    return kinds;
}

bool ParseMemoryPlace(const JsonValue& value, bool range, MemoryPlace& out, std::string& error)
{
    out = MemoryPlace{};
    if (value.IsNumber())
    {
        const double number = value.AsNumber();
        if (number < 0 || number > 0xFFFF || number != static_cast<double>(static_cast<unsigned long>(number)))
        {
            error = "the address " + std::to_string(number) + " is not in 0-#FFFF";
            return false;
        }
        out.lo = out.hi = static_cast<uint16_t>(number);
        return true;
    }
    if (!value.IsString())
    {
        error = "an address (a number or a string) expected";
        return false;
    }
    const std::string written = value.AsString();
    std::string text = Trim(written);
    for (const auto& [prefix, space] : {std::pair<const char*, MemorySpace>{"page:", MemorySpace::Ram},
                                        std::pair<const char*, MemorySpace>{"rom:", MemorySpace::Rom}})
    {
        const std::string head(prefix);
        if (text.rfind(head, 0) != 0)
            continue;
        const size_t colon = text.find(':', head.size());
        unsigned long page = 0;
        if (colon == std::string::npos || !ParseNumber(text.substr(head.size(), colon - head.size()), page) ||
            page > 0xFFFE)
        {
            error = "'" + written + "': " + head + "<n>:<addr> expected";
            return false;
        }
        out.space = space;
        out.page = static_cast<uint16_t>(page);
        text = text.substr(colon + 1);
    }
    const size_t dash = text.find('-');
    std::string loText = Trim(text.substr(0, dash));
    std::string hiText = dash == std::string::npos ? loText : Trim(text.substr(dash + 1));
    if (dash != std::string::npos && !range)
    {
        error = "'" + written + "': one address expected, not a range";
        return false;
    }
    unsigned long lo = 0;
    unsigned long hi = 0;
    if (!ParseNumber(loText, lo) || !ParseNumber(hiText, hi))
    {
        error = "'" + written + "' is not an address";
        return false;
    }
    if (lo > 0xFFFF || hi > 0xFFFF)
    {
        error = "'" + written + "' is above #FFFF";
        return false;
    }
    if (hi < lo)
    {
        error = "'" + written + "': the range ends before it starts";
        return false;
    }
    if (out.space != MemorySpace::Cpu)
    {
        // Offsets inside the page, whichever window the address was written for; a range stays in one page
        if ((lo >> 14) != (hi >> 14))
        {
            error = "'" + written + "': a page range stays inside its 16K page";
            return false;
        }
        lo &= 0x3FFF;
        hi &= 0x3FFF;
    }
    out.lo = static_cast<uint16_t>(lo);
    out.hi = static_cast<uint16_t>(hi);
    return true;
}

bool ParseMemoryProfile(const JsonValue& section, MemoryProfile& out, std::string& error)
{
    out = MemoryProfile{};
    error.clear();
    if (!CheckFields(section, {"regions", "labels", "symbols", "entries", "comments"}, "memory", error))
        return false;

    const JsonValue* regions = List(section, "regions", error);
    if (!error.empty())
        return false;
    if (regions != nullptr)
        for (size_t i = 0; i < regions->Items().size(); i++)
        {
            const JsonValue& entry = regions->Items()[i];
            const std::string where = At("regions", i);
            MemoryRegionSpec region;
            if (!CheckFields(entry, {"at", "size", "kind", "title", "sha256", "confidence", "geometry"}, where, error) ||
                !Place(entry, true, region.place, where, error) ||
                !RequiredString(entry, "kind", region.kind, where, error) ||
                !OptionalString(entry, "title", region.title, where, error) ||
                !OptionalString(entry, "sha256", region.sha256, where, error))
                return false;
            const std::vector<std::string>& kinds = MemoryRegionKinds();
            if (std::find(kinds.begin(), kinds.end(), region.kind) == kinds.end())
            {
                std::string names;
                for (const std::string& kind : kinds)
                    names += (names.empty() ? "" : ", ") + kind;
                error = where + ": unknown kind '" + region.kind + "' (known: " + names + ")";
                return false;
            }
            if (const JsonValue* size = entry.Find("size"))
            {
                unsigned long bytes = 0;
                if (size->IsNumber() && size->AsNumber() >= 1)
                    bytes = static_cast<unsigned long>(size->AsNumber());
                else if (!size->IsString() || !ParseNumber(size->AsString(), bytes) || bytes == 0)
                {
                    error = where + ": 'size' is a positive number";
                    return false;
                }
                const unsigned long limit = region.place.space == MemorySpace::Cpu ? 0x10000 : 0x4000;
                if (region.place.lo != region.place.hi)
                {
                    error = where + ": 'size' goes with one address in 'at', not a range";
                    return false;
                }
                if (region.place.lo + bytes > limit)
                {
                    error = where + ": 'size' runs past the end of " +
                            (region.place.space == MemorySpace::Cpu ? "the 64K" : "the page");
                    return false;
                }
                region.place.hi = static_cast<uint16_t>(region.place.lo + bytes - 1);
            }
            if (!region.sha256.empty())
            {
                const bool hex = std::all_of(region.sha256.begin(), region.sha256.end(), [](char c) {
                    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
                });
                if (region.sha256.size() != 64 || !hex)
                {
                    error = where + ": 'sha256' is 64 lower-case hex digits";
                    return false;
                }
            }
            if (const JsonValue* confidence = entry.Find("confidence"))
            {
                if (!confidence->IsNumber() || confidence->AsNumber() < 0 || confidence->AsNumber() > 1000)
                {
                    error = where + ": 'confidence' is 0-1000";
                    return false;
                }
                region.confidence = static_cast<uint16_t>(confidence->AsNumber());
            }
            if (const JsonValue* geometry = entry.Find("geometry"))
            {
                if (!geometry->IsObject())
                {
                    error = where + ": 'geometry' is an object";
                    return false;
                }
                region.geometry = *geometry;
            }
            out.regions.push_back(std::move(region));
        }

    if (!Labels(section, "labels", out.labels, error) || !Labels(section, "entries", out.entries, error))
        return false;

    const JsonValue* symbols = List(section, "symbols", error);
    if (!error.empty())
        return false;
    if (symbols != nullptr)
        for (size_t i = 0; i < symbols->Items().size(); i++)
        {
            const JsonValue& file = symbols->Items()[i];
            const std::string where = At("symbols", i);
            if (!file.IsString() || Trim(file.AsString()).empty())
            {
                error = where + ": a file name expected";
                return false;
            }
            const std::string& path = file.AsString();
            // Next to the profile: no absolute path, no way up out of the profiles' folder
            if (path[0] == '/' || path[0] == '\\' || (path.size() > 1 && path[1] == ':') ||
                path.find("..") != std::string::npos)
            {
                error = where + ": '" + path + "' is not a path next to the profile";
                return false;
            }
            out.symbols.push_back(path);
        }

    const JsonValue* comments = List(section, "comments", error);
    if (!error.empty())
        return false;
    if (comments != nullptr)
        for (size_t i = 0; i < comments->Items().size(); i++)
        {
            const JsonValue& entry = comments->Items()[i];
            const std::string where = At("comments", i);
            MemoryCommentSpec comment;
            if (!CheckFields(entry, {"at", "text"}, where, error) || !Place(entry, false, comment.place, where, error) ||
                !RequiredString(entry, "text", comment.text, where, error))
                return false;
            out.comments.push_back(std::move(comment));
        }
    return true;
}

std::string FormatMemoryPlace(const MemoryPlace& place)
{
    char lo[8];
    char hi[8];
    std::snprintf(lo, sizeof(lo), "0x%04X", place.lo);
    std::snprintf(hi, sizeof(hi), "0x%04X", place.hi);
    std::string text = place.space == MemorySpace::Ram   ? "page:" + std::to_string(place.page) + ":"
                       : place.space == MemorySpace::Rom ? "rom:" + std::to_string(place.page) + ":"
                                                         : std::string();
    text += lo;
    if (place.hi != place.lo)
        text += std::string("-") + hi;
    return text;
}

JsonValue MemoryProfileToJson(const MemoryProfile& profile)
{
    JsonValue section = JsonValue::Object();
    auto labels = [](const std::vector<MemoryLabelSpec>& list) {
        std::vector<JsonValue> items;
        for (const MemoryLabelSpec& label : list)
        {
            JsonValue entry = JsonValue::Object();
            entry.Set("name", JsonValue::String(label.name));
            entry.Set("at", JsonValue::String(FormatMemoryPlace(label.place)));
            if (!label.comment.empty())
                entry.Set("comment", JsonValue::String(label.comment));
            items.push_back(std::move(entry));
        }
        return JsonValue::Array(std::move(items));
    };
    if (!profile.regions.empty())
    {
        std::vector<JsonValue> items;
        for (const MemoryRegionSpec& region : profile.regions)
        {
            JsonValue entry = JsonValue::Object();
            entry.Set("at", JsonValue::String(FormatMemoryPlace(region.place)));
            entry.Set("kind", JsonValue::String(region.kind));
            if (!region.title.empty())
                entry.Set("title", JsonValue::String(region.title));
            if (!region.sha256.empty())
                entry.Set("sha256", JsonValue::String(region.sha256));
            if (region.confidence != 1000)
                entry.Set("confidence", JsonValue::Number(region.confidence));
            if (region.geometry.IsObject())
                entry.Set("geometry", region.geometry);
            items.push_back(std::move(entry));
        }
        section.Set("regions", JsonValue::Array(std::move(items)));
    }
    if (!profile.labels.empty())
        section.Set("labels", labels(profile.labels));
    if (!profile.symbols.empty())
    {
        std::vector<JsonValue> items;
        for (const std::string& file : profile.symbols)
            items.push_back(JsonValue::String(file));
        section.Set("symbols", JsonValue::Array(std::move(items)));
    }
    if (!profile.entries.empty())
        section.Set("entries", labels(profile.entries));
    if (!profile.comments.empty())
    {
        std::vector<JsonValue> items;
        for (const MemoryCommentSpec& comment : profile.comments)
        {
            JsonValue entry = JsonValue::Object();
            entry.Set("at", JsonValue::String(FormatMemoryPlace(comment.place)));
            entry.Set("text", JsonValue::String(comment.text));
            items.push_back(std::move(entry));
        }
        section.Set("comments", JsonValue::Array(std::move(items)));
    }
    return section;
}

}  // namespace controllers
