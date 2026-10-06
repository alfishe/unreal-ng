#include "stdafx.h"

#include "composedescriptor.h"

#include <cmath>
#include <cstdlib>
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
    /// rapidyaml's default error handler aborts the process; ours throws
    [[noreturn]] void ThrowOnError(const char* message, size_t length, ryml::Location, void*)
    {
        throw std::runtime_error(std::string(message, length));
    }

    void InstallErrorHandler()
    {
        static std::once_flag once;
        std::call_once(once, [] { ryml::set_callbacks(ryml::Callbacks(nullptr, nullptr, nullptr, &ThrowOnError)); });
    }

    std::string Text(ryml::csubstr s)
    {
        return std::string(s.str ? s.str : "", s.len);
    }

    std::string Lower(std::string text)
    {
        for (char& c : text)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return text;
    }

    std::filesystem::path Utf8Path(const std::string& text)
    {
        return std::filesystem::path(std::u8string(text.begin(), text.end()));
    }

    std::string PathText(const std::filesystem::path& path)
    {
        const auto u8 = path.generic_u8string();
        return std::string(u8.begin(), u8.end());
    }

    /// `~` -> home; relative -> under baseDir; lexically normal
    std::filesystem::path Resolve(const std::string& text, const std::filesystem::path& baseDir)
    {
        std::filesystem::path path;
        if (text == "~" || text.rfind("~/", 0) == 0 || text.rfind("~\\", 0) == 0)
        {
            const char* home = std::getenv("HOME");
#ifdef _WIN32
            if (!home)
                home = std::getenv("USERPROFILE");
#endif
            path = Utf8Path(home ? home : "");
            if (text.size() > 2)
                path /= Utf8Path(text.substr(2));
        }
        else
        {
            path = Utf8Path(text);
            if (path.is_relative())
                path = baseDir / path;
        }
        return path.lexically_normal();
    }

    /// "/A//b/" -> "/A/b"; "" -> "/" (target paths and `from` are compared after this)
    std::string NormalizeTargetPath(const std::string& path)
    {
        std::string out;
        size_t pos = 0;
        while (pos < path.size())
        {
            const size_t slash = path.find_first_of("/\\", pos);
            const std::string part = path.substr(pos, slash == std::string::npos ? std::string::npos : slash - pos);
            pos = slash == std::string::npos ? path.size() : slash + 1;
            if (part.empty() || part == ".")
                continue;
            out += "/" + part;
        }
        return out.empty() ? "/" : out;
    }

    std::string JsonString(const std::string& text)
    {
        std::string out = "\"";
        for (unsigned char c : text)
        {
            switch (c)
            {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (c < 0x20)
                    {
                        char buffer[8];
                        std::snprintf(buffer, sizeof buffer, "\\u%04x", c);
                        out += buffer;
                    }
                    else
                        out += static_cast<char>(c);
            }
        }
        return out + "\"";
    }

    std::string JsonList(const std::vector<std::string>& items)
    {
        std::string out = "[";
        for (size_t i = 0; i < items.size(); i++)
            out += (i ? "," : "") + JsonString(items[i]);
        return out + "]";
    }

    class Reader
    {
    public:
        Reader(ComposeDescriptor& d, std::string source) : _d(d), _source(std::move(source)) {}

        void Root(ryml::ConstNodeRef root)
        {
            if (!root.is_map())
            {
                _d.error = _source + ": a descriptor must be a map of settings";
                return;
            }
            bool sawLayers = false;
            for (ryml::ConstNodeRef child : root.children())
            {
                const std::string key = Text(child.key());
                if (key == "version")
                {
                    const auto v = Number(child, key, 1000);
                    _d.version = v ? static_cast<int>(*v) : 0;
                }
                else if (key == "target")
                    Target(child);
                else if (key == "layers")
                {
                    sawLayers = true;
                    Layers(child);
                }
                else if (key == "partitions")
                    _d.hasPartitions = true;
                else if (key == "boot")
                {
                    _d.hasBoot = true;
                    Boot(child);
                }
                else if (key == "writes")
                    Writes(child);
                else
                    Report(key, "unknown key, ignored");
            }
            if (_d.version != 1)
                _d.error = _source + ": 'version: 1' is required";
            else if (sawLayers && _d.hasPartitions)
                _d.error = _source + ": 'layers' and 'partitions' cannot both be given";
            else if (!_d.hasPartitions && _d.layers.empty())
                _d.error = _source + ": no layers";
        }

    private:
        void Report(const std::string& key, const std::string& what)
        {
            _d.report.push_back(_source + ": " + key + ": " + what);
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

        std::optional<uint64_t> Number(ryml::ConstNodeRef node, const std::string& key, uint64_t max)
        {
            const auto text = String(node, key);
            if (!text)
                return std::nullopt;
            try
            {
                size_t used = 0;
                const unsigned long long value = std::stoull(*text, &used, 0);
                if (used == text->size() && value <= max)
                    return value;
            }
            catch (const std::exception&)
            {
            }
            Report(key, "expected a number, got '" + *text + "'");
            return std::nullopt;
        }

        std::optional<bool> Bool(ryml::ConstNodeRef node, const std::string& key)
        {
            const auto text = String(node, key);
            if (!text)
                return std::nullopt;
            const std::string v = Lower(*text);
            if (v == "true" || v == "yes" || v == "on" || v == "1")
                return true;
            if (v == "false" || v == "no" || v == "off" || v == "0")
                return false;
            Report(key, "expected true or false, got '" + *text + "'");
            return std::nullopt;
        }

        std::optional<uint64_t> Size(ryml::ConstNodeRef node, const std::string& key)
        {
            const auto text = String(node, key);
            if (!text)
                return std::nullopt;
            uint64_t bytes = 0;
            if (ComposeDescriptor::ParseSize(*text, bytes))
                return bytes;
            Report(key, "expected a size (2GiB, 256MiB, 1048576), got '" + *text + "'");
            return std::nullopt;
        }

        std::vector<std::string> StringList(ryml::ConstNodeRef node, const std::string& key)
        {
            std::vector<std::string> list;
            if (node.has_val())
            {
                list.push_back(Text(node.val()));
                return list;
            }
            if (!node.is_seq())
            {
                Report(key, "expected a list");
                return list;
            }
            for (ryml::ConstNodeRef item : node.children())
            {
                if (item.has_val())
                    list.push_back(Text(item.val()));
                else
                    Report(key, "expected a list of values");
            }
            return list;
        }

        void Target(ryml::ConstNodeRef node)
        {
            if (!node.is_map())
            {
                Report("target", "expected a map");
                return;
            }
            ComposeTarget& t = _d.target;
            for (ryml::ConstNodeRef child : node.children())
            {
                const std::string key = Text(child.key());
                const std::string where = "target." + key;
                if (key == "kind")
                {
                    const auto v = String(child, where);
                    if (v && Lower(*v) == "block")
                        t.kind = MediaKind::Block;
                    else if (v && Lower(*v) == "optical")
                        t.kind = MediaKind::Optical;
                    else if (v)
                        Report(where, "expected block or optical, got '" + *v + "'");
                }
                else if (key == "fs")
                {
                    const auto v = String(child, where);
                    const std::string s = v ? Lower(*v) : "";
                    if (s == "auto")
                        t.fs = ComposeTarget::Fs::Auto;
                    else if (s == "fat16")
                        t.fs = ComposeTarget::Fs::Fat16;
                    else if (s == "fat32")
                        t.fs = ComposeTarget::Fs::Fat32;
                    else if (s == "iso9660" || s == "iso")
                        t.fs = ComposeTarget::Fs::Iso9660;
                    else if (v)
                        Report(where, "expected auto, fat16, fat32 or iso9660, got '" + *v + "'");
                }
                else if (key == "build")
                {
                    const auto v = String(child, where);
                    const std::string s = v ? Lower(*v) : "";
                    if (s == "auto")
                        t.build = ComposeTarget::Build::Auto;
                    else if (s == "rebuild")
                        t.build = ComposeTarget::Build::Rebuild;
                    else if (s == "graft")
                        t.build = ComposeTarget::Build::Graft;
                    else if (v)
                        Report(where, "expected auto, rebuild or graft, got '" + *v + "'");
                }
                else if (key == "size")
                    t.size = Size(child, where);
                else if (key == "free")
                    t.free = Size(child, where);
                else if (key == "label")
                    t.label = String(child, where);
                else if (key == "codepage")
                {
                    const auto v = String(child, where);
                    CodePage page = CodePage::Cp866;
                    if (v && UnicodeHelper::ParseCodePage(*v, page))
                        t.codePage = page;
                    else if (v)
                        Report(where, "expected cp866 or cp1251, got '" + *v + "'");
                }
                else if (key == "partition")
                {
                    const auto v = String(child, where);
                    if (v && Lower(*v) == "mbr")
                        t.mbr = true;
                    else if (v && Lower(*v) == "none")
                        t.mbr = false;
                    else if (v)
                        Report(where, "expected mbr or none, got '" + *v + "'");
                }
                else if (key == "fixedTime")
                {
                    const auto v = Number(child, where, 0x7FFFFFFFFFFFFFFFull);
                    if (v)
                        t.fixedTimeUtc = static_cast<int64_t>(*v);
                }
                else if (key == "onBadName")
                {
                    const auto v = String(child, where);
                    if (v && (Lower(*v) == "skip" || Lower(*v) == "replace"))
                        t.onBadName = Lower(*v);
                    else if (v)
                        Report(where, "expected skip or replace, got '" + *v + "'");
                }
                else if (key == "iso")
                {
                    if (!child.is_map())
                    {
                        Report(where, "expected a map such as {level: 1, joliet: true}");
                        continue;
                    }
                    for (ryml::ConstNodeRef option : child.children())
                    {
                        const std::string name = Text(option.key());
                        const std::string at = where + "." + name;
                        if (name == "level")
                        {
                            const auto v = Number(option, at, 2);
                            if (v && *v >= 1)
                                t.isoLevel = static_cast<int>(*v);
                            else if (v)
                                Report(at, "expected 1 or 2");
                        }
                        else if (name == "joliet")
                        {
                            if (const auto v = Bool(option, at))
                                t.joliet = *v;
                        }
                        else if (name == "relaxDepth")
                        {
                            if (const auto v = Bool(option, at))
                                t.relaxDepth = *v;
                        }
                        else
                            Report(at, "unknown key, ignored");
                    }
                }
                else
                    Report(where, "unknown key, ignored");
            }
        }

        /// A boot file: "/UNION/path" (a file of the union) or {host: path}
        ComposeBootFile BootFile(ryml::ConstNodeRef node, const std::string& where)
        {
            ComposeBootFile file;
            if (node.is_map())
            {
                for (ryml::ConstNodeRef child : node.children())
                {
                    if (Text(child.key()) == "host")
                    {
                        if (const auto v = String(child, where + ".host"))
                            file.host = Resolve(*v, _d.baseDir);
                    }
                    else
                        Report(where + "." + Text(child.key()), "unknown key, ignored (expected host)");
                }
            }
            else if (const auto v = String(node, where))
                file.unionPath = NormalizeTargetPath(*v);
            if (!file.Set())
                Report(where, "expected a target path or {host: path}");
            return file;
        }

        void Boot(ryml::ConstNodeRef node)
        {
            if (!node.is_map())
            {
                Report("boot", "expected a map");
                return;
            }
            for (ryml::ConstNodeRef child : node.children())
            {
                const std::string key = Text(child.key());
                const std::string where = "boot." + key;
                if (key == "mbrCode")
                    _d.boot.mbrCode = BootFile(child, where);
                else if (key == "volumeCode")
                    _d.boot.volumeCode = BootFile(child, where);
                else if (key == "reserved" && child.is_seq())
                {
                    int index = 0;
                    for (ryml::ConstNodeRef item : child.children())
                    {
                        const std::string at = where + "[" + std::to_string(index++) + "]";
                        std::optional<uint64_t> lba;
                        ComposeBootFile file;
                        for (ryml::ConstNodeRef field : item.children())
                        {
                            const std::string name = Text(field.key());
                            if (name == "lba")
                                lba = Number(field, at + ".lba", 0xFFFF);
                            else if (name == "file")
                                file = BootFile(field, at + ".file");
                            else
                                Report(at + "." + name, "unknown key, ignored");
                        }
                        if (!lba || *lba == 0 || !file.Set())
                            Report(at, "expected {lba: 1.., file: ...}; ignored");
                        else
                            _d.boot.reserved.push_back({static_cast<uint32_t>(*lba), file});
                    }
                }
                else if (key == "eltorito" && child.is_seq())
                {
                    int index = 0;
                    for (ryml::ConstNodeRef item : child.children())
                    {
                        const std::string at = where + "[" + std::to_string(index++) + "]";
                        ComposeElTorito entry;
                        for (ryml::ConstNodeRef field : item.children())
                        {
                            const std::string name = Text(field.key());
                            const std::string fat = at + "." + name;
                            if (name == "image")
                                entry.image = BootFile(field, fat);
                            else if (name == "emulation")
                            {
                                const std::string v = Lower(String(field, fat).value_or(""));
                                if (v == "none")
                                    entry.emulation = 0;
                                else if (v == "floppy" || v == "1.44m")
                                    entry.emulation = 2;
                                else if (v == "1.2m")
                                    entry.emulation = 1;
                                else if (v == "2.88m")
                                    entry.emulation = 3;
                                else if (v == "hdd")
                                    entry.emulation = 4;
                                else
                                    Report(fat, "expected none, floppy, 1.2m, 1.44m, 2.88m or hdd");
                            }
                            else if (name == "platform")
                            {
                                const std::string v = Lower(String(field, fat).value_or(""));
                                if (v == "x86")
                                    entry.platform = 0;
                                else if (v == "ppc")
                                    entry.platform = 1;
                                else if (v == "mac")
                                    entry.platform = 2;
                                else if (v == "efi")
                                    entry.platform = 0xEF;
                                else
                                    Report(fat, "expected x86, ppc, mac or efi");
                            }
                            else if (name == "loadSegment")
                            {
                                if (const auto v = Number(field, fat, 0xFFFF))
                                    entry.loadSegment = static_cast<uint16_t>(*v);
                            }
                            else if (name == "sectors")
                            {
                                if (const auto v = Number(field, fat, 0xFFFF))
                                    entry.sectors = static_cast<uint16_t>(*v);
                            }
                            else
                                Report(fat, "unknown key, ignored");
                        }
                        if (entry.image.Set())
                            _d.boot.eltorito.push_back(entry);
                        else
                            Report(at, "an El Torito entry needs an image; ignored");
                    }
                }
                else
                    Report(where, "unknown key, ignored");
            }
        }

        void Source(ryml::ConstNodeRef node, ComposeLayer& layer, const std::string& where)
        {
            if (!node.is_map())
            {
                Report(where, "expected a map such as {folder: path}");
                return;
            }
            bool seen = false;
            for (ryml::ConstNodeRef child : node.children())
            {
                const std::string key = Text(child.key());
                const std::string at = where + "." + key;
                if (key == "folder" || key == "image" || key == "iso")
                {
                    if (seen)
                        Report(at, "a source has one of folder, image, iso; ignored");
                    const auto v = String(child, at);
                    if (!v)
                        continue;
                    seen = true;
                    layer.source.kind = key == "folder" ? ComposeSource::Kind::Folder
                                        : key == "image" ? ComposeSource::Kind::Image
                                                         : ComposeSource::Kind::Iso;
                    layer.source.path = Resolve(*v, _d.baseDir);
                }
                else if (key == "partition")
                {
                    const auto v = Number(child, at, 128);
                    if (v)
                        layer.source.partition = static_cast<uint32_t>(*v);
                }
                else if (key == "codepage")
                {
                    const auto v = String(child, at);
                    CodePage page = CodePage::Cp866;
                    if (v && UnicodeHelper::ParseCodePage(*v, page))
                        layer.source.codePage = page;
                    else if (v)
                        Report(at, "expected cp866 or cp1251, got '" + *v + "'");
                }
                else
                    Report(at, "unknown key, ignored");
            }
            if (!seen)
                _d.error = _source + ": " + where + ": a source needs folder, image or iso";
        }

        void Layers(ryml::ConstNodeRef node)
        {
            if (!node.is_seq())
            {
                _d.error = _source + ": layers: expected a list";
                return;
            }
            size_t index = 0;
            for (ryml::ConstNodeRef item : node.children())
            {
                ComposeLayer layer;
                layer.name = "layer" + std::to_string(index);
                const std::string where = "layers[" + std::to_string(index) + "]";
                index++;
                if (!item.is_map())
                {
                    _d.error = _source + ": " + where + ": expected a map";
                    return;
                }
                bool hasSource = false;
                for (ryml::ConstNodeRef child : item.children())
                {
                    const std::string key = Text(child.key());
                    const std::string at = where + "." + key;
                    if (key == "name")
                    {
                        if (auto v = String(child, at))
                            layer.name = *v;
                    }
                    else if (key == "source")
                    {
                        hasSource = true;
                        Source(child, layer, at);
                    }
                    else if (key == "mount")
                    {
                        if (auto v = String(child, at))
                            layer.mount = *v;
                    }
                    else if (key == "from")
                    {
                        if (auto v = String(child, at))
                            layer.from = *v;
                    }
                    else if (key == "include")
                        layer.include = StringList(child, at);
                    else if (key == "exclude")
                        layer.exclude = StringList(child, at);
                    else if (key == "opaque")
                        layer.opaque = StringList(child, at);
                    else if (key == "whiteout")
                        layer.whiteout = StringList(child, at);
                    else if (key == "conflict")
                    {
                        const auto v = String(child, at);
                        const std::string s = v ? Lower(*v) : "";
                        if (s == "shadow")
                            layer.conflict = ConflictPolicy::Shadow;
                        else if (s == "keep-lower")
                            layer.conflict = ConflictPolicy::KeepLower;
                        else if (s == "error")
                            layer.conflict = ConflictPolicy::Error;
                        else if (v)
                            Report(at, "expected shadow, keep-lower or error, got '" + *v + "'");
                    }
                    else if (key == "writable")
                    {
                        if (auto v = Bool(child, at))
                            layer.writable = *v;
                    }
                    else if (key == "onDelete")
                    {
                        const auto v = String(child, at);
                        const std::string s = v ? Lower(*v) : "";
                        if (s == "keep")
                            layer.onDelete = DeletePolicy::Keep;
                        else if (s == "trash")
                            layer.onDelete = DeletePolicy::Trash;
                        else if (s == "move")
                            layer.onDelete = DeletePolicy::Move;
                        else if (s == "delete")
                            layer.onDelete = DeletePolicy::Delete;
                        else if (s == "ignore")
                            layer.onDelete = DeletePolicy::Ignore;
                        else if (v)
                            Report(at, "expected keep, trash, move, delete or ignore, got '" + *v + "'");
                    }
                    else if (key == "deletedFolder")
                    {
                        if (auto v = String(child, at))
                            layer.deletedFolder = Resolve(*v, _d.baseDir);
                    }
                    else if (key == "order")
                        ;  // per-directory order: phase C2b
                    else
                        Report(at, "unknown key, ignored");
                }
                if (!hasSource)
                {
                    _d.error = _source + ": " + where + ": no source";
                    return;
                }
                for (const ComposeLayer& other : _d.layers)
                {
                    if (other.name == layer.name)
                        Report(where + ".name", "'" + layer.name + "' is used twice");
                }
                layer.mount = NormalizeTargetPath(layer.mount);
                layer.from = NormalizeTargetPath(layer.from);
                for (std::string& p : layer.opaque)
                    p = NormalizeTargetPath(p);
                for (std::string& p : layer.whiteout)
                    p = NormalizeTargetPath(p);
                if (layer.deletedFolder.empty())
                    layer.deletedFolder = (_d.baseDir / ".deleted").lexically_normal();
                _d.layers.push_back(std::move(layer));
            }
        }

        void Writes(ryml::ConstNodeRef node)
        {
            if (!node.is_map())
            {
                Report("writes", "expected a map");
                return;
            }
            for (ryml::ConstNodeRef child : node.children())
            {
                const std::string key = Text(child.key());
                const std::string at = "writes." + key;
                if (key == "access")
                {
                    AccessMode mode = AccessMode::Session;
                    const auto v = String(child, at);
                    if (v && ParseAccessMode(*v, mode) && mode != AccessMode::WriteThrough)
                        _d.writes.access = mode;
                    else if (v)
                        Report(at, "expected readonly or session, got '" + *v + "'");
                }
                else if (key == "save")
                {
                    const auto v = String(child, at);
                    const std::string s = v ? Lower(*v) : "";
                    if (s == "flat" || s == "delta" || s == "commit" || s == "write-back")
                        _d.writes.save = s;
                    else if (v)
                        Report(at, "expected flat, delta, commit or write-back, got '" + *v + "'");
                }
                else if (key == "upper")
                {
                    if (auto v = String(child, at))
                        _d.writes.upper = *v;
                }
                else if (key == "delta")
                {
                    if (auto v = String(child, at))
                        _d.writes.delta = Resolve(*v, _d.baseDir);
                }
                else
                    Report(at, "unknown key, ignored");
            }
        }

        ComposeDescriptor& _d;
        std::string _source;
    };
}  // namespace

bool ComposeDescriptor::ParseSize(const std::string& text, uint64_t& bytes)
{
    size_t used = 0;
    double value = 0;
    try
    {
        value = std::stod(text, &used);
    }
    catch (const std::exception&)
    {
        return false;
    }
    if (value < 0 || !std::isfinite(value))
        return false;
    std::string unit = Lower(text.substr(used));
    while (!unit.empty() && unit.front() == ' ')
        unit.erase(unit.begin());
    double scale = 1;
    if (unit.empty() || unit == "b")
        scale = 1;
    else if (unit == "k" || unit == "kib")
        scale = 1024.0;
    else if (unit == "m" || unit == "mib")
        scale = 1024.0 * 1024;
    else if (unit == "g" || unit == "gib")
        scale = 1024.0 * 1024 * 1024;
    else if (unit == "t" || unit == "tib")
        scale = 1024.0 * 1024 * 1024 * 1024;
    else if (unit == "kb")
        scale = 1000.0;
    else if (unit == "mb")
        scale = 1000.0 * 1000;
    else if (unit == "gb")
        scale = 1000.0 * 1000 * 1000;
    else
        return false;
    bytes = static_cast<uint64_t>(std::llround(value * scale));
    return true;
}

bool ComposeDescriptor::IsDescriptorName(const std::string& fileName)
{
    const std::string name = Lower(fileName);
    for (const char* suffix : {".ucompose.yaml", ".ucompose.yml", ".ucompose.json"})
    {
        const std::string s = suffix;
        if (name.size() > s.size() && name.compare(name.size() - s.size(), s.size(), s) == 0)
            return true;
    }
    return false;
}

ComposeDescriptor ComposeDescriptor::Parse(const std::string& text, const std::filesystem::path& baseDir,
                                           const std::string& sourceName)
{
    InstallErrorHandler();
    ComposeDescriptor d;
    d.baseDir = baseDir.lexically_normal();
    const std::string lower = Lower(sourceName);
    const bool json = lower.size() >= 5 && lower.compare(lower.size() - 5, 5, ".json") == 0;
    try
    {
        const ryml::Tree tree = json ? ryml::parse_json_in_arena(ryml::to_csubstr(text))
                                     : ryml::parse_in_arena(ryml::to_csubstr(text));
        ryml::ConstNodeRef root = tree.crootref();
        if (root.is_stream())
        {
            if (root.num_children() == 0)
            {
                d.error = sourceName + ": empty";
                return d;
            }
            root = root.first_child();
        }
        Reader(d, sourceName).Root(root);
    }
    catch (const std::exception& e)
    {
        ComposeDescriptor failed;
        failed.baseDir = d.baseDir;
        failed.error = sourceName + ": cannot be read: " + e.what();
        return failed;
    }
    // S2: the session delta sits next to its descriptor unless writes.delta says otherwise; an inline
    // descriptor has none unless it names one
    if (d.writes.delta.empty() && !sourceName.empty() && sourceName != kInlineName)
        d.writes.delta = (d.baseDir / Utf8Path(sourceName + ".delta")).lexically_normal();
    return d;
}

ComposeDescriptor ComposeDescriptor::Load(const std::filesystem::path& file)
{
    std::ifstream in(file, std::ios::binary);
    const auto name = file.filename().u8string();
    const std::string sourceName(name.begin(), name.end());
    if (!in)
    {
        ComposeDescriptor d;
        d.file = file;
        d.error = PathText(file) + ": cannot be opened";
        return d;
    }
    std::stringstream text;
    text << in.rdbuf();
    std::error_code ec;
    const std::filesystem::path absolute = std::filesystem::absolute(file, ec);
    ComposeDescriptor d = Parse(text.str(), (ec ? file : absolute).parent_path(), sourceName);
    d.file = (ec ? file : absolute).lexically_normal();
    return d;
}

std::string ComposeDescriptor::Normalized() const
{
    auto fsName = [](ComposeTarget::Fs fs) {
        switch (fs)
        {
            case ComposeTarget::Fs::Fat16: return "fat16";
            case ComposeTarget::Fs::Fat32: return "fat32";
            case ComposeTarget::Fs::Iso9660: return "iso9660";
            default: return "auto";
        }
    };
    auto buildName = [](ComposeTarget::Build b) {
        switch (b)
        {
            case ComposeTarget::Build::Rebuild: return "rebuild";
            case ComposeTarget::Build::Graft: return "graft";
            default: return "auto";
        }
    };
    auto conflictName = [](ConflictPolicy p) {
        switch (p)
        {
            case ConflictPolicy::KeepLower: return "keep-lower";
            case ConflictPolicy::Error: return "error";
            default: return "shadow";
        }
    };
    auto sourceKind = [](ComposeSource::Kind k) {
        switch (k)
        {
            case ComposeSource::Kind::Image: return "image";
            case ComposeSource::Kind::Iso: return "iso";
            default: return "folder";
        }
    };

    std::ostringstream o;
    o << "{\"version\":" << version << ",\"target\":{";
    o << "\"kind\":" << JsonString(target.kind ? MediaKindName(*target.kind) : "auto");
    o << ",\"fs\":\"" << fsName(target.fs) << "\",\"build\":\"" << buildName(target.build) << "\"";
    o << ",\"size\":" << (target.size ? std::to_string(*target.size) : "null");
    o << ",\"free\":" << (target.free ? std::to_string(*target.free) : "null");
    o << ",\"label\":" << (target.label ? JsonString(*target.label) : "null");
    o << ",\"codepage\":" << (target.codePage ? JsonString(UnicodeHelper::CodePageName(*target.codePage)) : "null");
    o << ",\"partition\":" << (target.mbr ? (*target.mbr ? "\"mbr\"" : "\"none\"") : "null");
    o << ",\"fixedTime\":" << (target.fixedTimeUtc ? std::to_string(*target.fixedTimeUtc) : "null");
    o << ",\"onBadName\":" << JsonString(target.onBadName);
    o << ",\"iso\":{\"level\":" << target.isoLevel << ",\"joliet\":" << (target.joliet ? "true" : "false")
      << ",\"relaxDepth\":" << (target.relaxDepth ? "true" : "false") << "}},\"layers\":[";
    for (size_t i = 0; i < layers.size(); i++)
    {
        const ComposeLayer& l = layers[i];
        o << (i ? "," : "") << "{\"name\":" << JsonString(l.name) << ",\"source\":{\"" << sourceKind(l.source.kind)
          << "\":" << JsonString(PathText(l.source.path));
        o << ",\"partition\":" << (l.source.partition ? std::to_string(*l.source.partition) : "null");
        o << ",\"codepage\":" << (l.source.codePage ? JsonString(UnicodeHelper::CodePageName(*l.source.codePage)) : "null");
        o << "},\"mount\":" << JsonString(l.mount) << ",\"from\":" << JsonString(l.from);
        o << ",\"include\":" << JsonList(l.include) << ",\"exclude\":" << JsonList(l.exclude);
        o << ",\"opaque\":" << JsonList(l.opaque) << ",\"whiteout\":" << JsonList(l.whiteout);
        o << ",\"conflict\":\"" << conflictName(l.conflict) << "\"}";
    }
    o << "]";
    if (hasBoot)
    {
        auto file = [](const ComposeBootFile& f) {
            return f.host.empty() ? JsonString(f.unionPath) : "{\"host\":" + JsonString(PathText(f.host)) + "}";
        };
        o << ",\"boot\":{\"mbrCode\":" << (boot.mbrCode.Set() ? file(boot.mbrCode) : "null")
          << ",\"volumeCode\":" << (boot.volumeCode.Set() ? file(boot.volumeCode) : "null") << ",\"reserved\":[";
        for (size_t i = 0; i < boot.reserved.size(); i++)
            o << (i ? "," : "") << "{\"lba\":" << boot.reserved[i].first << ",\"file\":" << file(boot.reserved[i].second) << "}";
        o << "],\"eltorito\":[";
        for (size_t i = 0; i < boot.eltorito.size(); i++)
        {
            const ComposeElTorito& e = boot.eltorito[i];
            o << (i ? "," : "") << "{\"image\":" << file(e.image) << ",\"emulation\":" << int(e.emulation)
              << ",\"platform\":" << int(e.platform) << ",\"loadSegment\":" << e.loadSegment << ",\"sectors\":" << e.sectors << "}";
        }
        o << "]}";
    }
    o << "}";
    return o.str();
}
