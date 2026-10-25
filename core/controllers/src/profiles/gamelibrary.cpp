#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

#include "match.h"  // core/swsignature: header-only, no core symbols
#include "profiles/profilestore.h"
#include "profiles/yaml.h"

namespace controllers
{

namespace
{
namespace fs = std::filesystem;

constexpr int kIndexVersion = 1;
const char* const kIndexName = "index.yaml";

std::string Lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

bool ReadFile(const fs::path& path, std::string& out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    std::stringstream text;
    text << in.rdbuf();
    out = text.str();
    return true;
}

/// The game files and bundles: `*.yaml` under @p dir, not the index, sorted by their `/` separated relative path
std::vector<std::pair<std::string, fs::path>> GameFiles(const fs::path& dir)
{
    std::vector<std::pair<std::string, fs::path>> files;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
    {
        if (!it->is_regular_file(ec) || it->path().extension() != ".yaml")
            continue;
        const std::string relative = it->path().lexically_relative(dir).generic_string();
        if (relative != kIndexName)
            files.emplace_back(relative, it->path());
    }
    std::sort(files.begin(), files.end());
    return files;
}

JsonValue Strings(const std::vector<std::string>& values)
{
    std::vector<JsonValue> items;
    for (const std::string& value : values)
        items.push_back(JsonValue::String(value));
    return JsonValue::Array(std::move(items));
}

bool ReadStrings(const JsonValue* value, std::vector<std::string>& out)
{
    if (value == nullptr)
        return true;
    if (!value->IsArray())
        return false;
    for (const JsonValue& item : value->Items())
    {
        if (!item.IsString())
            return false;
        out.push_back(item.AsString());
    }
    return true;
}

bool ReadSize(const JsonValue* value, size_t& out)
{
    if (value == nullptr || !value->IsNumber() || value->AsNumber() < 0 ||
        std::floor(value->AsNumber()) != value->AsNumber())
        return false;
    out = static_cast<size_t>(value->AsNumber());
    return true;
}
}  // namespace

std::vector<std::string> GameLibrary::Keys(const Profile& profile, const JsonValue& root)
{
    std::vector<std::string> keys;
    for (const std::string& sha : profile.fileSha256)
        keys.push_back("sha256:" + Lower(sha));
    for (const std::string& signature : profile.signatures)
        keys.push_back("sig:" + Lower(signature));
    // match.zxdb_id: a number, a string or a list of them (the profile itself does not use it yet)
    const JsonValue* match = root.Find("match");
    const JsonValue* zxdb = match != nullptr ? match->Find("zxdb_id") : nullptr;
    std::vector<JsonValue> ids;
    if (zxdb != nullptr)
        ids = zxdb->IsArray() ? zxdb->Items() : std::vector<JsonValue>{*zxdb};
    for (const JsonValue& id : ids)
    {
        if (id.IsNumber())
            keys.push_back("zxdb:" + std::to_string(static_cast<long long>(id.AsNumber())));
        else if (id.IsString())
            keys.push_back("zxdb:" + Lower(id.AsString()));
    }
    return keys;
}

bool GameLibrary::Build(const std::string& gamesDir, std::vector<GameEntry>& out, std::vector<std::string>& errors)
{
    out.clear();
    std::error_code ec;
    const fs::path dir(gamesDir);
    if (!fs::is_directory(dir, ec))
        return false;
    std::map<std::string, std::string> ids;   // id -> where
    std::map<std::string, std::string> keys;  // key -> id
    for (const auto& [relative, path] : GameFiles(dir))
    {
        std::string text;
        if (!ReadFile(path, text))
        {
            errors.push_back(path.string() + ": cannot be read");
            continue;
        }
        for (const auto& [offset, length] : SplitYamlDocuments(text))
        {
            const std::string where = relative + (offset != 0 ? " at byte " + std::to_string(offset) : "");
            JsonValue root;
            Profile profile;
            std::string error;
            if (!ParseYaml(text.substr(offset, length), root, error) || !ParseProfile(root, profile, error))
            {
                errors.push_back(where + ": " + error);
                continue;
            }
            if (profile.kind != ProfileKind::Play || profile.scheme || !profile.IsGame())
            {
                errors.push_back(where + ": not a game profile (a play profile with match.file_sha256, signatures or file_set)");
                continue;
            }
            if (!ids.emplace(profile.id, where).second)
            {
                errors.push_back(where + ": '" + profile.id + "' is also " + ids[profile.id]);
                continue;
            }
            GameEntry entry;
            entry.id = profile.id;
            entry.label = profile.label;
            entry.machines = profile.machines;
            entry.file = relative;
            entry.offset = offset;
            entry.length = length;
            entry.keys = Keys(profile, root);
            entry.fileSet = profile.fileSet;
            bool duplicate = false;
            for (const std::string& key : entry.keys)
            {
                const auto [it, added] = keys.emplace(key, profile.id);
                if (!added && it->second != profile.id)
                {
                    errors.push_back(where + ": " + key + " also names '" + it->second + "'");
                    duplicate = true;
                }
            }
            if (!duplicate)
                out.push_back(std::move(entry));
        }
    }
    return true;
}

std::string GameLibrary::DumpIndex(const std::vector<GameEntry>& entries)
{
    std::vector<JsonValue> profiles;
    for (const GameEntry& entry : entries)
    {
        JsonValue item = JsonValue::Object({{"id", JsonValue::String(entry.id)}});
        if (!entry.label.empty())
            item.Set("label", JsonValue::String(entry.label));
        if (!entry.machines.empty())
            item.Set("machines", Strings(entry.machines));
        item.Set("file", JsonValue::String(entry.file));
        item.Set("offset", JsonValue::Number(static_cast<double>(entry.offset)));
        item.Set("length", JsonValue::Number(static_cast<double>(entry.length)));
        item.Set("keys", Strings(entry.keys));
        if (!entry.fileSet.empty())
            item.Set("file_set", Strings(entry.fileSet));
        profiles.push_back(std::move(item));
    }
    const JsonValue root = JsonValue::Object(
        {{"version", JsonValue::Number(kIndexVersion)}, {"profiles", JsonValue::Array(std::move(profiles))}});
    return "# games/index.yaml - generated by tools/controllers/profile-index.py: do not edit\n" + DumpYaml(root, 2);
}

bool GameLibrary::ParseIndex(const std::string& text, std::vector<GameEntry>& out, std::string& error)
{
    JsonValue root;
    if (!ParseYaml(text, root, error))
        return false;
    const JsonValue* version = root.IsObject() ? root.Find("version") : nullptr;
    const JsonValue* profiles = root.IsObject() ? root.Find("profiles") : nullptr;
    if (version == nullptr || !version->IsNumber() || version->AsNumber() != kIndexVersion || profiles == nullptr ||
        !profiles->IsArray())
    {
        error = "an index is {version: " + std::to_string(kIndexVersion) + ", profiles: [...]}";
        return false;
    }
    out.clear();
    for (const JsonValue& item : profiles->Items())
    {
        GameEntry entry;
        const JsonValue* id = item.IsObject() ? item.Find("id") : nullptr;
        const JsonValue* file = item.IsObject() ? item.Find("file") : nullptr;
        const JsonValue* label = item.IsObject() ? item.Find("label") : nullptr;
        if (id == nullptr || !id->IsString() || file == nullptr || !file->IsString() ||
            (label != nullptr && !label->IsString()) || !ReadSize(item.Find("offset"), entry.offset) ||
            !ReadSize(item.Find("length"), entry.length) || !ReadStrings(item.Find("machines"), entry.machines) ||
            !ReadStrings(item.Find("keys"), entry.keys) || !ReadStrings(item.Find("file_set"), entry.fileSet))
        {
            error = "profiles[" + std::to_string(out.size()) +
                    "]: {id, label, machines, file, offset, length, keys, file_set}";
            return false;
        }
        entry.id = id->AsString();
        entry.file = file->AsString();
        entry.label = label != nullptr ? label->AsString() : std::string();
        out.push_back(std::move(entry));
    }
    return true;
}

bool GameLibrary::Open(const std::string& gamesDir, std::vector<std::string>& errors)
{
    _dir = gamesDir;
    _entries.clear();
    std::error_code ec;
    const fs::path dir(gamesDir);
    if (!fs::is_directory(dir, ec))
    {
        Reindex();
        return false;
    }
    // The index serves while it is newer than every profile file
    const fs::path index = dir / kIndexName;
    bool fresh = fs::is_regular_file(index, ec);
    const fs::file_time_type indexTime = fresh ? fs::last_write_time(index, ec) : fs::file_time_type{};
    for (const auto& [relative, path] : GameFiles(dir))
        if (fresh && fs::last_write_time(path, ec) > indexTime)
            fresh = false;
    std::string text;
    std::string error;
    if (!fresh || !ReadFile(index, text) || !ParseIndex(text, _entries, error))
    {
        if (!error.empty())
            errors.push_back(index.string() + ": " + error + " (rebuilt in memory)");
        Build(gamesDir, _entries, errors);
    }
    Reindex();
    return true;
}

void GameLibrary::Reindex()
{
    _byKey.clear();
    _byToken.clear();
    _byId.clear();
    for (size_t i = 0; i < _entries.size(); i++)
    {
        _byId.emplace(_entries[i].id, i);
        for (const std::string& key : _entries[i].keys)
            _byKey.emplace(Lower(key), i);
        for (const std::string& token : _entries[i].fileSet)
            _byToken.emplace(Lower(token), i);
    }
}

const GameEntry* GameLibrary::FindId(const std::string& id) const
{
    const auto it = _byId.find(id);
    return it != _byId.end() ? &_entries[it->second] : nullptr;
}

std::vector<const GameEntry*> GameLibrary::Lookup(const std::vector<std::string>& keys) const
{
    std::set<size_t> found;
    for (const std::string& key : keys)
    {
        const auto [first, last] = _byKey.equal_range(Lower(key));
        for (auto it = first; it != last; ++it)
            found.insert(it->second);
    }
    std::vector<const GameEntry*> out;
    for (size_t i : found)
        out.push_back(&_entries[i]);
    return out;
}

std::vector<const GameEntry*> GameLibrary::LookupFileSet(const std::vector<std::string>& fileSet) const
{
    // The candidates share a token; the rule decides
    std::set<size_t> candidates;
    for (const std::string& token : fileSet)
    {
        const auto [first, last] = _byToken.equal_range(Lower(token));
        for (auto it = first; it != last; ++it)
            candidates.insert(it->second);
    }
    std::vector<const GameEntry*> out;
    for (size_t i : candidates)
        if (unrealng::swsignature::CompareFileSets(_entries[i].fileSet, fileSet).matches)
            out.push_back(&_entries[i]);
    return out;
}

bool GameLibrary::Load(const std::string& id, Profile& out, std::string& error)
{
    for (int attempt = 0; attempt < 2; attempt++)
    {
        error.clear();
        const GameEntry* entry = FindId(id);
        if (entry == nullptr)
        {
            error = "'" + id + "' is not in " + (fs::path(_dir) / kIndexName).string();
            return false;
        }
        const fs::path path = fs::path(_dir) / fs::path(entry->file);
        std::string text;
        Profile profile;
        if (ReadFile(path, text) && entry->offset + entry->length <= text.size() &&
            ParseProfile(text.substr(entry->offset, entry->length), profile, error) && profile.id == id)
        {
            profile.path = path.string();
            out = std::move(profile);
            return true;
        }
        if (attempt == 0)
        {
            // A stale index (or line ends a checkout changed): the files themselves are the truth
            std::vector<std::string> errors;
            Build(_dir, _entries, errors);
            Reindex();
        }
        else
            error = path.string() + ": " + (error.empty() ? "does not hold '" + id + "' where the index says" : error);
    }
    return false;
}

}  // namespace controllers
