#include "diskfiles.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <memory>

#include "unrealdisk/floppyformats.h"
#include "unrealdisk/loaders/loader_trd.h"
#include "unrealdisk/trdosfs.h"

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
    unrealdisk::TrdosFileSystem fs(*disk);
    std::vector<unrealdisk::TrdosFile> entries;
    if (!fs.List(entries, error))
        return false;
    for (const unrealdisk::TrdosFile& entry : entries)
    {
        containers::TrdosFile file;
        file.name = entry.name;
        file.type = entry.type;
        file.start = entry.start;
        file.length = entry.length;
        file.sectors = entry.sectors;
        if (!fs.Read(entry, file.data, &file.tail, error))
            return false;
        out.push_back(std::move(file));
    }
    return true;
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
    unrealdisk::TrdosFileSystem::WriteOptions options;
    options.start = file.start;
    options.lengthField = file.length != 0 || file.data.empty();
    std::vector<uint8_t> body = file.data;
    body.insert(body.end(), file.tail.begin(), file.tail.end());
    std::string name = file.name;
    while (!name.empty() && name.back() == ' ')
        name.pop_back();
    if (!unrealdisk::TrdosFileSystem(*disk).Write(name, file.type, file.length ? file.data : body, options, error))
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
