#include "stdafx.h"

#include "foldermanifest.h"

#include <fstream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <system_error>

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#endif
#ifndef _RYML_SINGLE_HEADER_AMALGAMATED_HPP_
#include "3rdparty/rapidyaml/ryml_all.hpp"
#endif
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

namespace
{
    /// rapidyaml's default error handler aborts the process; ours throws, so a
    /// malformed metafile becomes a report line. It must never return
    [[noreturn]] void ThrowOnError(const char* message, size_t length, ryml::Location, void*)
    {
        throw std::runtime_error(std::string(message, length));
    }

    void InstallErrorHandler()
    {
        static std::once_flag once;
        std::call_once(once, [] { ryml::set_callbacks(ryml::Callbacks(nullptr, nullptr, nullptr, &ThrowOnError)); });
    }

    std::string Utf8Name(const std::filesystem::path& path)
    {
        const auto text = path.filename().u8string();
        return std::string(text.begin(), text.end());
    }

    std::string Text(ryml::csubstr s)
    {
        return std::string(s.str ? s.str : "", s.len);
    }

    class Reader
    {
    public:
        explicit Reader(FolderManifest& manifest) : _m(manifest) {}

        void Root(ryml::ConstNodeRef root)
        {
            if (!root.is_map())
            {
                Report("", "the metafile must be a map of settings");
                return;
            }
            for (ryml::ConstNodeRef child : root.children())
            {
                const std::string key = Text(child.key());
                if (key == "label")
                    _m.label = String(child, key);
                else if (key == "order")
                    _m.order = StringList(child, key);
                else if (key == "exclude")
                    _m.exclude = StringList(child, key);
                else if (key == "files")
                    Files(child);
                else if (key == "disk")
                    Disk(child);
                else if (key == "tape")
                    Tape(child);
                else
                    Report(key, "unknown key, ignored");
            }
        }

    private:
        void Report(const std::string& key, const std::string& what)
        {
            _m.report.push_back(_m.sourceFile + (key.empty() ? "" : ": " + key) + ": " + what);
        }

        std::optional<std::string> String(ryml::ConstNodeRef node, const std::string& key)
        {
            if (!node.has_val())
            {
                Report(key, "expected a single value");
                return std::nullopt;
            }
            return Text(node.val());
        }

        std::optional<uint32_t> Number(ryml::ConstNodeRef node, const std::string& key, uint32_t max)
        {
            const std::optional<std::string> text = String(node, key);
            if (!text)
                return std::nullopt;
            try
            {
                size_t used = 0;
                const unsigned long value = std::stoul(*text, &used, 0);  // 0: decimal or 0x hex
                if (used == text->size() && value <= max)
                    return static_cast<uint32_t>(value);
            }
            catch (const std::exception&)
            {
            }
            Report(key, "expected a number from 0 to " + std::to_string(max) + ", got '" + *text + "'");
            return std::nullopt;
        }

        std::vector<std::string> StringList(ryml::ConstNodeRef node, const std::string& key)
        {
            std::vector<std::string> list;
            if (node.has_val())  // a single name is a list of one
            {
                list.push_back(Text(node.val()));
                return list;
            }
            if (!node.is_seq())
            {
                Report(key, "expected a list of names");
                return list;
            }
            for (ryml::ConstNodeRef item : node.children())
            {
                if (item.has_val())
                    list.push_back(Text(item.val()));
                else
                    Report(key, "list items must be names");
            }
            return list;
        }

        void Files(ryml::ConstNodeRef node)
        {
            if (!node.is_map())
            {
                Report("files", "expected a map of file name → settings");
                return;
            }
            for (ryml::ConstNodeRef file : node.children())
            {
                const std::string fileName = Text(file.key());
                const std::string where = "files." + fileName;
                if (!file.is_map())
                {
                    Report(where, "expected {name, type, start, line}");
                    continue;
                }
                ManifestFileOverride& o = _m.files[fileName];
                for (ryml::ConstNodeRef field : file.children())
                {
                    const std::string key = Text(field.key());
                    if (key == "name")
                        o.name = String(field, where + ".name");
                    else if (key == "type")
                    {
                        const auto type = String(field, where + ".type");
                        if (type && type->size() == 1)
                            o.type = (*type)[0];
                        else if (type)
                            Report(where + ".type", "expected one letter (B, C, D, #)");
                    }
                    else if (key == "start")
                    {
                        if (auto v = Number(field, where + ".start", 0xFFFF))
                            o.start = static_cast<uint16_t>(*v);
                    }
                    else if (key == "line")
                    {
                        if (auto v = Number(field, where + ".line", 9999))
                            o.line = static_cast<uint16_t>(*v);
                    }
                    else
                        Report(where + "." + key, "unknown key, ignored");
                }
            }
        }

        void Disk(ryml::ConstNodeRef node)
        {
            if (!node.is_map())
            {
                Report("disk", "expected {format, tracks, sides}");
                return;
            }
            for (ryml::ConstNodeRef field : node.children())
            {
                const std::string key = Text(field.key());
                if (key == "format")
                    _m.diskFormat = String(field, "disk.format");
                else if (key == "tracks")
                {
                    if (auto v = Number(field, "disk.tracks", 255))
                        _m.diskTracks = static_cast<uint8_t>(*v);
                }
                else if (key == "sides")
                {
                    if (auto v = Number(field, "disk.sides", 2); v && *v >= 1)
                        _m.diskSides = static_cast<uint8_t>(*v);
                }
                else
                    Report("disk." + key, "unknown key, ignored");
            }
        }

        void Tape(ryml::ConstNodeRef node)
        {
            if (!node.is_map())
            {
                Report("tape", "expected {format, pause}");
                return;
            }
            for (ryml::ConstNodeRef field : node.children())
            {
                const std::string key = Text(field.key());
                if (key == "format")
                    _m.tapeFormat = String(field, "tape.format");
                else if (key == "pause")
                    _m.tapePauseMs = Number(field, "tape.pause", 60000);
                else
                    Report("tape." + key, "unknown key, ignored");
            }
        }

        FolderManifest& _m;
    };

    bool ReadFile(const std::filesystem::path& path, std::string& text)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in)
            return false;
        std::ostringstream buffer;
        buffer << in.rdbuf();
        text = buffer.str();
        return true;
    }
}  // namespace

FolderManifest FolderManifest::Parse(const std::string& text, const std::string& sourceName)
{
    InstallErrorHandler();

    FolderManifest manifest;
    manifest.sourceFile = sourceName;

    const bool json = sourceName.size() >= 5 && sourceName.compare(sourceName.size() - 5, 5, ".json") == 0;
    try
    {
        if (text.find_first_not_of(" \t\r\n") == std::string::npos)
            return manifest;  // an empty metafile sets nothing

        const ryml::Tree tree = json ? ryml::parse_json_in_arena(ryml::to_csubstr(text))
                                     : ryml::parse_in_arena(ryml::to_csubstr(text));
        ryml::ConstNodeRef root = tree.crootref();
        if (root.is_stream())
        {
            if (root.num_children() == 0)
                return manifest;
            root = root.first_child();
        }
        Reader(manifest).Root(root);
    }
    catch (const std::exception& e)
    {
        FolderManifest failed;
        failed.sourceFile = sourceName;
        failed.report.push_back(sourceName + ": cannot be read, ignored: " + e.what());
        return failed;
    }
    return manifest;
}

FolderManifest FolderManifest::Load(const std::filesystem::path& folder)
{
    std::error_code ec;
    const std::filesystem::path yaml = folder / kYamlName;
    const std::filesystem::path json = folder / kJsonName;
    const bool hasYaml = std::filesystem::is_regular_file(yaml, ec);
    const bool hasJson = std::filesystem::is_regular_file(json, ec);
    if (!hasYaml && !hasJson)
        return {};

    const std::filesystem::path& chosen = hasYaml ? yaml : json;
    std::string text;
    FolderManifest manifest;
    if (!ReadFile(chosen, text))
    {
        manifest.sourceFile = Utf8Name(chosen);
        manifest.report.push_back(manifest.sourceFile + ": cannot be opened, ignored");
        return manifest;
    }

    manifest = Parse(text, Utf8Name(chosen));
    if (hasYaml && hasJson)
        manifest.report.push_back(std::string(kJsonName) + ": ignored, " + kYamlName + " takes precedence");
    return manifest;
}
