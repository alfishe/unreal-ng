#include "stdafx.h"

#include "folderdiskbuilder.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iterator>
#include <set>

#include "common/unicodehelper.h"
#include "emulator/io/fdc/trdos.h"
#include "emulator/io/storage/hostfolder/foldermanifest.h"
#include "emulator/io/storage/hostfolder/foldersnapshot.h"
#include "loaders/disk/loader_hobeta.h"
#include "loaders/disk/loader_trd.h"

namespace
{
    constexpr size_t kSector = 256;
    constexpr size_t kMaxFileSectors = 255;
    constexpr size_t kScreenBytes = 6912;
    constexpr uint16_t kCodeStart = 32768;
    constexpr uint16_t kScreenStart = 16384;
    constexpr size_t kLabelOffset = 0xF5;  ///< TRDVolumeInfo::label in the volume sector

    std::string Lower(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text;
    }

    /// "intro.scr" -> ("intro", "scr"); ".profile" -> (".profile", "")
    std::pair<std::string, std::string> SplitName(const std::string& name)
    {
        const size_t dot = name.find_last_of('.');
        if (dot == std::string::npos || dot == 0)
            return {name, ""};
        return {name.substr(0, dot), name.substr(dot + 1)};
    }

    bool ReadHostFile(const std::filesystem::path& path, std::vector<uint8_t>& data)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in)
            return false;
        data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        return !in.bad();
    }

    /// One file ready for the catalog
    struct Candidate
    {
        std::string hostName;
        TRDOSDirectoryEntryBase entry{};
        std::vector<uint8_t> body;  ///< padded to whole sectors
    };

    void SetName(TRDOSDirectoryEntryBase& entry, const std::string& name8)
    {
        std::memcpy(entry.Name, name8.data(), 8);
    }

    std::string NameOf(const TRDOSDirectoryEntryBase& entry)
    {
        return std::string(entry.Name, 8) + static_cast<char>(entry.Type);
    }

    /// Build the catalog entry and body for one host file; false with `why`
    /// when it cannot go on a TR-DOS disk at all
    bool Prepare(const FolderEntry& file, const FolderManifest& manifest, Candidate& out, std::string& why)
    {
        std::vector<uint8_t> data;
        if (!ReadHostFile(file.hostPath, data))
        {
            why = "cannot be read";
            return false;
        }

        out.hostName = file.name;
        const auto [stem, ext] = SplitName(file.name);
        LoaderHobeta::Header header;
        std::vector<std::string> ignored;
        const bool hobeta = LoaderHobeta::detect(data.data(), data.size()) &&
                            LoaderHobeta::parseHeader(data.data(), data.size(), header, ignored);

        if (hobeta)
        {
            std::memcpy(out.entry.Name, header.name, 8);
            out.entry.Type = header.type;
            out.entry.Start = header.start;
            out.entry.Length = header.length;
            out.entry.SizeInSectors = header.sectors;
            out.body.assign(data.begin() + LoaderHobeta::HEADER_SIZE, data.end());
        }
        else
        {
            SetName(out.entry, FolderDiskBuilder::CompatibleName(stem));
            out.entry.Type = static_cast<uint8_t>(DiskTypeMap::TypeFor(ext));
            out.entry.Length = static_cast<uint16_t>(std::min<size_t>(data.size(), 0xFFFF));
            out.entry.Start = (Lower(ext) == "scr" && data.size() == kScreenBytes) ? kScreenStart : kCodeStart;
            if (out.entry.Type == 'B')
                out.entry.Start = out.entry.Length;  // BASIC: program + variables length
            out.body = std::move(data);
        }

        // The manifest overrides what the file says
        auto override = manifest.files.find(file.name);
        if (override != manifest.files.end())
        {
            const ManifestFileOverride& o = override->second;
            if (o.name)
                SetName(out.entry, FolderDiskBuilder::CompatibleName(*o.name));
            if (o.type)
                out.entry.Type = static_cast<uint8_t>(*o.type);
            if (o.start)
                out.entry.Start = *o.start;
            if (o.line)
            {
                // TR-DOS keeps a BASIC program's autorun line after its data: #80 #AA line
                out.body.resize(out.entry.Length);
                out.body.push_back(0x80);
                out.body.push_back(0xAA);
                out.body.push_back(static_cast<uint8_t>(*o.line & 0xFF));
                out.body.push_back(static_cast<uint8_t>(*o.line >> 8));
            }
        }

        const size_t sectors = (out.body.size() + kSector - 1) / kSector;
        if (!hobeta || (override != manifest.files.end() && override->second.line))
        {
            if (sectors > kMaxFileSectors || out.body.size() > 0xFFFF + 4)
            {
                why = "larger than a TR-DOS file (65 280 bytes)";
                return false;
            }
            out.entry.SizeInSectors = static_cast<uint8_t>(sectors);
        }
        out.body.resize(static_cast<size_t>(out.entry.SizeInSectors) * kSector, 0);
        return true;
    }

    /// Same name and type as a file already placed: the last character
    /// becomes 1...9; false when all nine are taken
    bool MakeUnique(TRDOSDirectoryEntryBase& entry, const std::set<std::string>& taken)
    {
        if (!taken.count(NameOf(entry)))
            return true;
        for (char digit = '1'; digit <= '9'; digit++)
        {
            entry.Name[7] = digit;
            if (!taken.count(NameOf(entry)))
                return true;
        }
        return false;
    }

    bool WriteLabel(DiskImage& disk, const std::string& label8)
    {
        DiskImage::Track* track0 = disk.getTrackForCylinderAndSide(0, 0);
        DiskImage::Sector* volume = track0 ? track0->getSector(TRD_VOLUME_SECTOR) : nullptr;
        if (!volume)
            return false;
        uint8_t data[kSector];
        std::memcpy(data, volume->data, kSector);
        std::memcpy(data + kLabelOffset, label8.data(), 8);
        track0->writeSectorData(TRD_VOLUME_SECTOR, data, kSector);
        volume->recalculateDataCRC();
        return true;
    }
}  // namespace

/// region <DiskTypeMap>

const std::vector<DiskTypeMap::Rule>& DiskTypeMap::Rules()
{
    static const std::vector<Rule> rules = {
        {"b", 'B'},    {"bas", 'B'},
        {"c", 'C'},    {"bin", 'C'},  {"cod", 'C'}, {"code", 'C'}, {"scr", 'C'}, {"rom", 'C'},
        {"d", 'D'},
        {"#", '#'},
    };
    return rules;
}

char DiskTypeMap::TypeFor(const std::string& extension)
{
    const std::string ext = Lower(extension);
    for (const Rule& rule : Rules())
    {
        if (ext == rule.extension)
            return rule.type;
    }
    return 'C';
}

/// endregion </DiskTypeMap>

std::string FolderDiskBuilder::CompatibleName(const std::string& utf8Name)
{
    std::string name;
    for (char32_t codepoint : UnicodeHelper::DecodeUtf8(utf8Name))
    {
        if (name.size() == 8)
            break;
        const bool printable = codepoint >= 0x20 && codepoint < 0x7F && codepoint != U'"';
        name.push_back(printable ? static_cast<char>(codepoint) : '_');
    }
    name.resize(8, ' ');
    return name;
}

MediaResult FolderDiskBuilder::BuildTrd(EmulatorContext* context, const std::filesystem::path& folder,
                                        std::unique_ptr<DiskImage>& disk)
{
    disk.reset();
    MediaResult result = MediaResult::Success();

    const FolderManifest manifest = FolderManifest::Load(folder);
    result.report.insert(result.report.end(), manifest.report.begin(), manifest.report.end());
    if (manifest.diskFormat && Lower(*manifest.diskFormat) != "trd")
        result.report.push_back("disk.format '" + *manifest.diskFormat + "': only trd is built from a folder");

    FolderScanOptions scan;
    scan.recursive = false;
    scan.excludePatterns = manifest.exclude;
    FolderSnapshot snapshot;
    std::string error;
    if (!FolderSnapshot::Scan(folder, scan, snapshot, &error))
        return MediaResult::Fail(MediaError::UnreadableSource, error);
    for (const SkippedEntry& skipped : snapshot.Skipped())
        result.report.push_back(skipped.path + ": skipped, " + skipped.reason);

    // Geometry: DS 80 unless the manifest picks 40 tracks or one side
    const uint8_t cylinders = manifest.diskTracks.value_or(80);
    const uint8_t sides = manifest.diskSides.value_or(2);
    if ((cylinders != 40 && cylinders != 80) || (sides != 1 && sides != 2))
        return MediaResult::Fail(MediaError::KindMismatch, "a TR-DOS disk has 40 or 80 tracks and 1 or 2 sides");

    auto image = std::make_unique<DiskImage>(cylinders, sides);
    LoaderTRD formatter(context, folder.string());
    if (!formatter.format(image.get()))
        return MediaResult::Fail(MediaError::IoError, "cannot format a blank TR-DOS disk");

    // Order: the manifest's list first, then everything else as scanned (byte-wise sorted)
    std::vector<const FolderEntry*> files;
    for (const FolderEntry& entry : snapshot.Root().children)
    {
        if (!entry.isDirectory)
            files.push_back(&entry);
    }
    std::vector<const FolderEntry*> ordered;
    for (const std::string& wanted : manifest.order)
    {
        auto it = std::find_if(files.begin(), files.end(), [&wanted](const FolderEntry* f) { return f && f->name == wanted; });
        if (it == files.end())
        {
            result.report.push_back("order: '" + wanted + "' is not in the folder");
            continue;
        }
        ordered.push_back(*it);
        *it = nullptr;
    }
    for (const FolderEntry* file : files)
    {
        if (file)
            ordered.push_back(file);
    }

    std::set<std::string> taken;
    for (const FolderEntry* file : ordered)
    {
        Candidate candidate;
        std::string why;
        if (!Prepare(*file, manifest, candidate, why))
        {
            result.report.push_back(file->name + ": skipped, " + why);
            continue;
        }
        if (!MakeUnique(candidate.entry, taken))
        {
            result.report.push_back(file->name + ": skipped, its name and type are taken, with 1...9 as well");
            continue;
        }

        std::vector<std::string> warnings;
        if (!LoaderHobeta::addFile(image.get(), candidate.entry, candidate.body.data(), warnings))
        {
            result.report.push_back(file->name + ": skipped, does not fit (" +
                                    (warnings.empty() ? std::string("no room") : warnings.back()) + ")");
            continue;
        }
        taken.insert(NameOf(candidate.entry));
    }

    std::string label;
    if (manifest.label)
        label = *manifest.label;
    else
    {
        const std::filesystem::path name = folder.has_filename() ? folder.filename() : folder.parent_path().filename();
        const auto u8 = name.u8string();
        label.assign(u8.begin(), u8.end());
    }
    WriteLabel(*image, CompatibleName(label));

    image->setFortyTrack(cylinders == 40);  // 48 tpi: an 80-track drive steps twice per track
    image->markClean();  // a freshly built disk has nothing to save back
    image->setLoaded(true);
    disk = std::move(image);
    return result;
}
