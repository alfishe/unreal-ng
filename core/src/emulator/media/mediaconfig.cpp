#include "stdafx.h"

#include "mediaconfig.h"

#include <algorithm>
#include <cctype>
#include <map>

#include "common/filehelper.h"
#include "common/inifile.h"

namespace
{
    constexpr const char* kOptions[] = {"access", "fs", "codepage", "free", "wp", "swapdelay"};

    std::string Lower(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text;
    }

    std::string Trim(const std::string& text)
    {
        const size_t first = text.find_first_not_of(" \t");
        if (first == std::string::npos)
            return {};
        const size_t last = text.find_last_not_of(" \t");
        return text.substr(first, last - first + 1);
    }

    bool ParseUnsigned(const std::string& text, uint64_t& value)
    {
        if (text.empty() || !std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isdigit(c); }))
            return false;
        try
        {
            value = std::stoull(text);
            return true;
        }
        catch (const std::exception&)
        {
            return false;
        }
    }

    /// Split "sd.zc.access" into ("sd.zc", "access"); "sd.zc" into ("sd.zc", "")
    std::pair<std::string, std::string> SplitKey(const std::string& key)
    {
        const size_t dot = key.rfind('.');
        if (dot != std::string::npos)
        {
            const std::string suffix = Lower(key.substr(dot + 1));
            if (std::find(std::begin(kOptions), std::end(kOptions), suffix) != std::end(kOptions))
                return {Lower(key.substr(0, dot)), suffix};
        }
        return {Lower(key), ""};
    }

    MediaSetEntry& EntryFor(std::vector<MediaSetEntry>& entries, const std::string& slotId)
    {
        auto it = std::find_if(entries.begin(), entries.end(), [&slotId](const MediaSetEntry& e) { return e.slotId == slotId; });
        if (it != entries.end())
            return *it;
        entries.push_back({});
        entries.back().slotId = slotId;
        return entries.back();
    }
}  // namespace

std::string MediaConfig::ResolvePath(const std::string& path, const std::string& configFolder)
{
    if (path.empty())
        return path;
    std::string expanded = FileHelper::ExpandPath(path);
    if (!FileHelper::IsAbsolutePath(expanded) && !configFolder.empty())
        expanded = FileHelper::PathCombine(configFolder, expanded);
    return FileHelper::LexicallyNormalPath(expanded);
}

std::vector<MediaSetEntry> MediaConfig::FromIni(const IniFile& ini, const std::string& configFolder, std::vector<std::string>* report)
{
    std::vector<MediaSetEntry> entries;
    auto note = [report](const std::string& text) {
        if (report)
            report->push_back(text);
    };

    // --- [MEDIA] ---
    for (const auto& [rawKey, rawValue] : ini.GetSectionEntries("MEDIA"))
    {
        const auto [slotId, option] = SplitKey(Trim(rawKey));
        const std::string value = Trim(rawValue);
        if (slotId.empty())
            continue;
        MediaSetEntry& entry = EntryFor(entries, slotId);
        const std::string where = "[MEDIA] " + rawKey;

        if (option.empty())
        {
            entry.source.path = ResolvePath(value, configFolder);
            entry.source.type = FileHelper::IsFolder(entry.source.path) ? MediaSourceType::Folder : MediaSourceType::File;
        }
        else if (option == "access")
        {
            AccessMode access;
            if (ParseAccessMode(value, access))
                entry.access = access;
            else
                note(where + ": expected readonly, session or writethrough, got '" + value + "'");
        }
        else if (option == "fs")
        {
            const std::string fs = Lower(value);
            if (fs == "fat16")
                entry.fs = FatType::Fat16;
            else if (fs == "fat32")
                entry.fs = FatType::Fat32;
            else
                note(where + ": expected fat16 or fat32, got '" + value + "'");
        }
        else if (option == "codepage")
        {
            CodePage page;
            if (UnicodeHelper::ParseCodePage(value, page))
                entry.codePage = page;
            else
                note(where + ": expected cp866 or cp1251, got '" + value + "'");
        }
        else
        {
            uint64_t number = 0;
            if (!ParseUnsigned(value, number))
            {
                note(where + ": expected a number, got '" + value + "'");
                continue;
            }
            if (option == "free")
                entry.freeBytes = number;
            else if (option == "wp")
                entry.writeProtect = number != 0;
            else if (option == "swapdelay")
                entry.swapDelayMs = static_cast<uint32_t>(std::min<uint64_t>(number, 60000));
        }
    }

    // --- Legacy sections: only for slots [MEDIA] did not set ---
    auto legacy = [&](const char* slotId, const char* section, const char* key, const char* alias) -> MediaSetEntry* {
        auto it = std::find_if(entries.begin(), entries.end(), [slotId](const MediaSetEntry& e) { return e.slotId == slotId; });
        if (it != entries.end() && !it->source.path.empty())
            return nullptr;
        const char* value = ini.GetValue(section, key, nullptr);
        if ((!value || !*value) && alias)
            value = ini.GetValue(section, alias, nullptr);
        if (!value || Trim(value).empty())
            return nullptr;
        MediaSetEntry& entry = EntryFor(entries, slotId);
        entry.source.path = ResolvePath(Trim(value), configFolder);
        entry.source.type = FileHelper::IsFolder(entry.source.path) ? MediaSourceType::Folder : MediaSourceType::File;
        entry.legacy = true;
        return &entry;
    };

    if (MediaSetEntry* zc = legacy("sd.zc", "ZC", "SDCardImage", "SDCARD"))
    {
        AccessMode access;
        if (const char* write = ini.GetValue("ZC", "SDWrite", nullptr); write && ParseAccessMode(Trim(write), access))
            zc->access = access;
        if (!zc->writeProtect)
            zc->writeProtect = ini.GetLongValue("ZC", "SDWriteProtect", 0) != 0;
    }
    if (MediaSetEntry* ngs = legacy("sd.ngs", "NGS", "SDCardImage", "SDCARD"))
    {
        // [NGS] SDWrite = session | persist | off
        if (const char* write = ini.GetValue("NGS", "SDWrite", nullptr); write)
        {
            const std::string mode = Lower(Trim(write));
            if (mode == "persist")
                ngs->access = AccessMode::WriteThrough;
            else if (mode == "off")
                ngs->access = AccessMode::ReadOnly;
            else if (mode == "session")
                ngs->access = AccessMode::Session;
        }
        if (!ngs->writeProtect)
            ngs->writeProtect = ini.GetLongValue("NGS", "SDWriteProtect", 0) != 0;
    }
    if (MediaSetEntry* master = legacy("ide0.master", "HDD", "Image0", nullptr))
    {
        if (ini.GetLongValue("HDD", "HD0RO", 0) != 0)
            master->access = AccessMode::ReadOnly;
    }
    if (MediaSetEntry* slave = legacy("ide0.slave", "HDD", "Image1", nullptr))
    {
        if (ini.GetLongValue("HDD", "HD1RO", 0) != 0)
            slave->access = AccessMode::ReadOnly;
    }

    // Options without a source (e.g. only "sd.zc.wp = 1") describe the slot, not a medium
    return entries;
}
