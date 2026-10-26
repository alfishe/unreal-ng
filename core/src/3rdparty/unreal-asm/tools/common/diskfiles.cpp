#include "diskfiles.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <memory>

#include "unrealdisk/diskfs.h"
#include "unrealdisk/diskimage.h"
#include "unrealdisk/floppyformats.h"
#include "unrealdisk/loaders/loader_trd.h"

namespace unrealasm::tools
{
namespace
{
std::string Lower(std::string text)
{
    for (char& c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

bool IsTape(const std::string& extension)
{
    return extension == "tap" || extension == "tzx";
}

std::string Extension(const std::string& path)
{
    const size_t slash = path.find_last_of("/\\");
    const size_t dot = path.find_last_of('.');
    return dot == std::string::npos || (slash != std::string::npos && dot < slash) ? std::string() : Lower(path.substr(dot + 1));
}

/// TR-DOS name bytes as DiskFs names them: printable ASCII as is, any other byte as \\xNN
std::string Escape(const std::string& raw)
{
    static const char* hex = "0123456789abcdef";
    std::string out;
    for (const char ch : raw)
    {
        const uint8_t c = static_cast<uint8_t>(ch);
        if (c >= 0x20 && c < 0x7F)
            out += ch;
        else
            out += std::string("\\x") + hex[c >> 4] + hex[c & 0x0F];
    }
    return out;
}

/// The reverse of Escape: the bytes as stored
std::string Unescape(const std::string& text)
{
    std::string out;
    for (size_t i = 0; i < text.size(); i++)
    {
        if (text[i] == '\\' && i + 3 < text.size() && text[i + 1] == 'x' && std::isxdigit(static_cast<unsigned char>(text[i + 2])) &&
            std::isxdigit(static_cast<unsigned char>(text[i + 3])))
        {
            out += static_cast<char>(std::stoi(text.substr(i + 2, 2), nullptr, 16));
            i += 3;
        }
        else
            out += text[i];
    }
    return out;
}
}  // namespace

bool IsImage(const std::string& extension)
{
    if (IsTape(extension))
        return true;
    if (extension.size() == 2 && extension[0] == '$')
        return false;   // a hobeta file is one file, read as such
    const std::vector<std::string> disks = unrealdisk::FloppyFormats::Extensions();
    return std::find(disks.begin(), disks.end(), extension) != disks.end();
}

bool ReadImageFiles(const std::string& name, const std::vector<uint8_t>& bytes, std::vector<containers::TrdosFile>& out, std::string& error)
{
    out.clear();
    if (IsTape(Extension(name)))
        return containers::ReadTape(bytes, out, error);
    std::unique_ptr<unrealdisk::DiskImage> disk;
    std::string format;
    const unrealdisk::FormatResult loaded = unrealdisk::FloppyFormats::LoadData({}, bytes.data(), bytes.size(), name, disk, format);
    if (!loaded.Ok())
    {
        error = loaded.message;
        return false;
    }
    return ReadDiskFiles(*disk, out, error);
}

bool ReadDiskFiles(unrealdisk::DiskImage& disk, std::vector<containers::TrdosFile>& out, std::string& error)
{
    out.clear();
    unrealdisk::DiskFs fs(disk);
    std::vector<unrealdisk::DiskFsEntry> entries;
    if (!fs.List("", "", true, entries, error))
        return false;
    const bool trdos = fs.Volumes().front().fs == "trdos";
    for (const unrealdisk::DiskFsEntry& entry : entries)
    {
        if (entry.directory)
            continue;
        containers::TrdosFile file;
        const size_t dot = entry.path.rfind('.');
        const bool hasExtension = dot != std::string::npos && entry.path.find('/', dot) == std::string::npos;
        file.name = hasExtension ? entry.path.substr(0, dot) : entry.path;
        file.start = static_cast<uint16_t>(entry.start);
        if (trdos)
        {
            file.name = Unescape(file.name);
            // TR-DOS: the type letter, the catalog's length and sectors, the rest of the last sector kept
            file.type = static_cast<char>(entry.Field("type"));
            file.length = static_cast<uint16_t>(entry.size);
            file.sectors = static_cast<uint8_t>(entry.Field("sectors"));
            if (!fs.Read("", entry.path, file.data, error, &file.tail))
                return false;
        }
        else
        {
            // +3DOS, iS-DOS, FAT: the type as an extension, the data's size as the length
            file.extension = hasExtension ? Lower(entry.path.substr(dot + 1)) : std::string();
            if (!fs.Read("", entry.path, file.data, error))
                return false;
            file.length = static_cast<uint16_t>(std::min<size_t>(file.data.size(), 0xFFFF));
            file.sectors = static_cast<uint8_t>(std::min<size_t>((file.data.size() + 255) / 256, 255));
        }
        if (file.name.size() < 8)
            file.name.resize(8, ' ');
        out.push_back(std::move(file));
    }
    return true;
}

bool PutDiskFile(unrealdisk::DiskImage& disk, const containers::TrdosFile& file, std::string& error)
{
    unrealdisk::DiskFs fs(disk);
    std::string name = file.name;
    while (!name.empty() && name.back() == ' ')
        name.pop_back();
    if (fs.Volumes().empty())
    {
        error = "no file system found on the disk";
        return false;
    }
    unrealdisk::DiskFsWriteOptions options;
    options.start = file.start;
    if (fs.Volumes().front().fs == "trdos")
    {
        // NAME.T: the length field 0 when the file has none (as XAS saves), whole sectors then
        options.lengthField = file.length != 0 || file.data.empty();
        std::vector<uint8_t> body = file.data;
        body.insert(body.end(), file.tail.begin(), file.tail.end());
        return fs.Write("", Escape(name) + "." + Escape(std::string(1, file.type)), file.length ? file.data : body, options, error);
    }
    // +3DOS: NAME.TYP (the type letter for a TR-DOS one) with a +3DOS header, CODE at its start
    const std::string type = !file.extension.empty() ? file.extension : file.type ? std::string(1, file.type) : std::string();
    return fs.Write("", type.empty() ? name : name + "." + type, file.data, options, error);
}

bool PutImageFile(const std::string& path, const std::string& output, const containers::TrdosFile& file, std::string& error)
{
    std::unique_ptr<unrealdisk::DiskImage> disk;
    std::error_code ec;
    if (std::filesystem::exists(std::filesystem::path(std::u8string(path.begin(), path.end())), ec))
    {
        std::string format;
        const unrealdisk::FormatResult loaded = unrealdisk::FloppyFormats::Load({}, path, disk, format);
        if (!loaded.Ok())
        {
            error = loaded.message;
            return false;
        }
    }
    else
    {
        disk = std::make_unique<unrealdisk::DiskImage>(uint8_t{80}, uint8_t{2});
        unrealdisk::LoaderTRD trd({}, path);
        if (!trd.format(disk.get()))
        {
            error = "cannot format a blank TR-DOS disk";
            return false;
        }
    }
    if (!PutDiskFile(*disk, file, error))
        return false;
    const unrealdisk::FloppySaveResult saved = unrealdisk::FloppyFormats::Save({}, *disk, output, false);
    if (!saved.saved)
    {
        error = "cannot save " + output + (saved.reason.empty() ? std::string() : ": " + saved.reason);
        return false;
    }
    return true;
}
}  // namespace unrealasm::tools
