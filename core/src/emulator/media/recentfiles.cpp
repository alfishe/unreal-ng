#include "stdafx.h"

#include "emulator/media/recentfiles.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <system_error>

#include "common/filehelper.h"
#include "emulator/state/statenodejson.h"

namespace
{
std::string BaseName(const std::string& path)
{
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

int64_t Now()
{
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

bool AllKinds(const std::string& kind)
{
    return kind.empty() || kind == "all";
}
}  // namespace

RecentFiles& RecentFiles::Instance()
{
    static RecentFiles* files = new RecentFiles();  // never destroyed: noted from any thread until the process ends
    return *files;
}

const std::vector<std::string>& RecentFiles::Kinds()
{
    static const std::vector<std::string> kinds = {"snapshot", "tape", "disk", "rzx", "image", "cd"};
    return kinds;
}

bool RecentFiles::IsKind(const std::string& kind)
{
    const std::vector<std::string>& kinds = Kinds();
    return std::find(kinds.begin(), kinds.end(), kind) != kinds.end();
}

std::string RecentFiles::KindOfSlot(const std::string& slotKind)
{
    if (slotKind == "floppy")
        return "disk";
    if (slotKind == "tape")
        return "tape";
    if (slotKind == "block")
        return "image";
    if (slotKind == "optical")
        return "cd";
    return {};
}

void RecentFiles::Note(const std::string& kind, const std::string& path)
{
    if (!IsKind(kind) || path.empty())
        return;
    const std::string absolute = FileHelper::AbsolutePath(path, /*resolveSymlinks*/ false);
    std::lock_guard<std::mutex> lock(_mutex);
    _entries.erase(std::remove_if(_entries.begin(), _entries.end(),
                                  [&](const Entry& e) { return e.kind == kind && e.path == absolute; }),
                   _entries.end());
    _entries.insert(_entries.begin(), Entry{kind, absolute, BaseName(absolute), Now()});
    size_t count = 0;
    _entries.erase(std::remove_if(_entries.begin(), _entries.end(),
                                  [&](const Entry& e) { return e.kind == kind && ++count > kPerKind; }),
                   _entries.end());
    _revision++;
    SaveLocked();
}

std::vector<RecentFiles::Entry> RecentFiles::List(const std::string& kind) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (AllKinds(kind))
        return _entries;
    std::vector<Entry> out;
    for (const Entry& entry : _entries)
        if (entry.kind == kind)
            out.push_back(entry);
    return out;
}

void RecentFiles::Clear(const std::string& kind)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (AllKinds(kind))
        _entries.clear();
    else
        _entries.erase(std::remove_if(_entries.begin(), _entries.end(), [&](const Entry& e) { return e.kind == kind; }),
                       _entries.end());
    _revision++;
    SaveLocked();
}

uint64_t RecentFiles::Revision() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _revision;
}

bool RecentFiles::SetStoreFile(const std::string& path, std::string* error)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _storeFile = path;
    if (path.empty() || !FileHelper::FileExists(path))
        return true;
    std::string text(FileHelper::GetFileSize(path), '\0');
    if (!text.empty() &&
        FileHelper::ReadFileToBuffer(path, reinterpret_cast<uint8_t*>(text.data()), text.size()) != text.size())
    {
        if (error)
            *error = "cannot read " + path;
        return false;
    }
    StateNode root;
    std::string parseError;
    const StateNode* files = nullptr;
    if (StateNodeFromJsonText(text, root, &parseError))
        files = root.find("files");
    if (files == nullptr || !files->isArray())
    {
        if (error)
            *error = path + " is not a recent-files list" + (parseError.empty() ? "" : ": " + parseError);
        return false;
    }
    std::vector<Entry> loaded;
    for (const StateNode& item : files->items)
    {
        const StateNode* kind = item.find("kind");
        const StateNode* file = item.find("path");
        const StateNode* time = item.find("time");
        if (kind == nullptr || file == nullptr || kind->kind != StateNode::Kind::String ||
            file->kind != StateNode::Kind::String || !IsKind(kind->s) || file->s.empty())
            continue;
        const size_t ofKind = static_cast<size_t>(
            std::count_if(loaded.begin(), loaded.end(), [&](const Entry& e) { return e.kind == kind->s; }));
        if (ofKind < kPerKind)
            loaded.push_back(Entry{kind->s, file->s, BaseName(file->s),
                                   time != nullptr && time->kind == StateNode::Kind::Int ? time->i : 0});
    }
    _entries = std::move(loaded);
    _revision++;
    return true;
}

void RecentFiles::SaveLocked() const
{
    if (_storeFile.empty())
        return;
    StateNode root = StateNode::Object();
    root["format"] = "unreal-ng-recent-files";
    root["version"] = 1;
    StateNode& files = root["files"];
    files = StateNode::Array();
    for (const Entry& entry : _entries)
    {
        StateNode item = StateNode::Object();
        item["kind"] = entry.kind;
        item["path"] = entry.path;
        item["time"] = entry.time;
        files.push(std::move(item));
    }
    std::string text = StateNodeToJsonText(root) + "\n";
    // Through a temp file: a crash mid-write never leaves half a list
    const std::string temp = _storeFile + ".tmp";
    if (FileHelper::SaveBufferToFile(temp, reinterpret_cast<uint8_t*>(text.data()), text.size()))
    {
        std::error_code ec;
        std::filesystem::rename(FileHelper::ToFsPath(temp), FileHelper::ToFsPath(_storeFile), ec);
    }
}

StateNode RecentFiles::EntryValue(const Entry& entry, size_t index)
{
    StateNode node = StateNode::Object();
    node["index"] = static_cast<uint64_t>(index);
    node["kind"] = entry.kind;
    node["name"] = entry.name;
    node["path"] = entry.path;
    node["time"] = entry.time;
    return node;
}

StateNode RecentFiles::ToValue(const std::string& kind) const
{
    StateNode node = StateNode::Object();
    node["revision"] = Revision();
    if (!AllKinds(kind))
    {
        node["kind"] = kind;
        StateNode& files = node["files"];
        files = StateNode::Array();
        size_t index = 0;
        for (const Entry& entry : List(kind))
            files.push(EntryValue(entry, index++));
        return node;
    }
    const std::vector<Entry> all = List();
    StateNode& kinds = node["kinds"];
    kinds = StateNode::Object();
    for (const std::string& name : Kinds())
    {
        StateNode& files = kinds[name];
        files = StateNode::Array();
        size_t index = 0;
        for (const Entry& entry : all)
            if (entry.kind == name)
                files.push(EntryValue(entry, index++));
    }
    StateNode& merged = node["all"];
    merged = StateNode::Array();
    size_t index = 0;
    for (const Entry& entry : all)
        merged.push(EntryValue(entry, index++));
    return node;
}
