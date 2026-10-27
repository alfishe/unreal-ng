#include "stdafx.h"

#include "blockformats.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>

#include "common/filehelper.h"
#include "common/stringhelper.h"
#include "emulator/io/storage/chd/chdimage.h"
#include "emulator/io/storage/compose/fatimagesource.h"
#include "emulator/io/storage/compose/sourcepool.h"
#include "emulator/io/storage/fat/fatsynthvolume.h"
#include "emulator/io/storage/fat/fatvolumereader.h"
#include "emulator/io/storage/chd/chdwriter.h"
#include "emulator/io/storage/hddimageformats.h"
#include "emulator/io/storage/hostwritehold.h"
#include "emulator/io/storage/mediareadtap.h"
#include "emulator/io/storage/readonlyguard.h"
#include "emulator/io/storage/sessionwritemap.h"
#include "emulator/io/storage/vhdimage.h"
#include "emulator/media/medium.h"

namespace
{
    std::string TempPathFor(const std::string& path)
    {
        return path + ".writing";
    }

    void RemoveFile(const std::string& path)
    {
        std::error_code ec;
        std::filesystem::remove(FileHelper::ToFsPath(path), ec);
    }

    bool RenameOver(const std::string& from, const std::string& to, std::string& error)
    {
        std::error_code ec;
        std::filesystem::rename(FileHelper::ToFsPath(from), FileHelper::ToFsPath(to), ec);
        if (ec)
            error = "cannot replace " + to + ": " + ec.message();
        return !ec;
    }

    /// The geometry a CHD of this device records: its own, else chdman's guess,
    /// else 16 heads × 63 sectors over as many whole cylinders as fit
    BlockGeometry GeometryFor(IBlockDevice& device)
    {
        if (auto native = device.NativeGeometry(); native && native->heads && native->sectors && native->cylinders)
            return *native;
        const uint64_t sectors = device.SectorCount();
        if (auto guess = chd::GuessGeometry(sectors))
            return *guess;
        BlockGeometry g{static_cast<uint32_t>(std::max<uint64_t>(1, sectors / (16 * 63))), 16, 63};
        return g;
    }

    /// Write the CHD form of `device` to `path`
    MediaResult WriteChdImage(IBlockDevice& device, const std::string& path, const BlockWriteOptions& options, ChdImage* source,
                              chd::ChdFile* inheritedParent, const std::function<bool(uint64_t, uint64_t)>& unchanged)
    {
        chd::WriteOptions w;
        std::string error;
        if (!options.compression.empty())
        {
            if (!chd::ParseCodecList(options.compression, w.codecs, &error))
                return MediaResult::Fail(MediaError::BadRequest, error);
        }
        else
        {
            w.codecs = source ? source->File().Codecs() : chd::kDefaultHardDiskCodecs;
        }
        if (source && source->File().HunkBytes() % IBlockDevice::kSectorSize == 0)
            w.hunkBytes = source->File().HunkBytes();

        // The source CHD's metadata (identify data, keys) travels along; a
        // geometry comes from the device when the source has none
        if (source)
            w.metadata = source->File().Metadata();
        const bool hasGeometry = std::any_of(w.metadata.begin(), w.metadata.end(),
                                             [](const chd::MetadataEntry& e) { return e.tag == chd::kTagHardDisk; });
        if (!hasGeometry)
            w.metadata.insert(w.metadata.begin(), chd::HardDiskMetadata(GeometryFor(device)));

        std::unique_ptr<chd::ChdFile> namedParent;
        if (!options.parent.empty())
        {
            namedParent = chd::ChdFile::Open(options.parent, &error);
            if (!namedParent)
                return MediaResult::Fail(MediaError::UnreadableSource, error);
            w.parent = namedParent.get();
        }
        else
        {
            w.parent = inheritedParent;
        }

        if (source && unchanged)
        {
            const uint64_t perHunk = w.hunkBytes / IBlockDevice::kSectorSize;
            w.reuse = &source->File();
            w.unchanged = [&unchanged, perHunk](uint32_t hunk) { return unchanged(static_cast<uint64_t>(hunk) * perHunk, perHunk); };
        }

        if (!chd::WriteChd(path, device, w, &error))
            return MediaResult::Fail(MediaError::IoError, error);
        return MediaResult::Success();
    }

    /// A fixed VHD: the raw data, then a 512-byte footer (Microsoft VHD specification 1.0)
    bool AppendVhdFooter(IBlockDevice& device, const std::string& path, std::string* error)
    {
        const uint64_t size = device.SectorCount() * IBlockDevice::kSectorSize;
        uint32_t cylinders = 0, heads = 0, sectors = 0;
        vhd::Geometry(device, cylinders, heads, sectors);
        const std::array<uint8_t, 512> footer = vhd::Footer(size, cylinders, heads, sectors, vhd::kFixed, ~0ULL, device.ContentId());
        std::fstream out(FileHelper::ToFsPath(path), std::ios::binary | std::ios::in | std::ios::out);
        out.seekp(static_cast<std::streamoff>(size));
        out.write(reinterpret_cast<const char*>(footer.data()), static_cast<std::streamsize>(footer.size()));
        out.flush();
        if (!out && error)
            *error = "cannot write the VHD footer of " + path;
        return static_cast<bool>(out);
    }

    MediaResult WriteImage(IBlockDevice& device, const std::string& path, const std::string& format, const BlockWriteOptions& options,
                           chd::ChdFile* inheritedParent, const std::function<bool(uint64_t, uint64_t)>& unchanged)
    {
        if (format != "chd" && (!options.compression.empty() || !options.parent.empty()))
            return MediaResult::Fail(MediaError::BadRequest, "compression and parent apply to a .chd target only");
        if (!options.vhd.empty() && options.vhd != "fixed" && options.vhd != "dynamic")
            return MediaResult::Fail(MediaError::BadRequest, "vhd '" + options.vhd + "': expected fixed or dynamic");
        if (!options.vhd.empty() && format != "vhd")
            return MediaResult::Fail(MediaError::BadRequest, "vhd applies to a .vhd target only");

        // Written next to the target, renamed into place when complete
        const std::string temp = TempPathFor(path);
        MediaResult result = MediaResult::Success();
        if (format == "chd")
        {
            result = WriteChdImage(device, temp, options, BlockFormats::FindChd(&device), inheritedParent, unchanged);
        }
        else
        {
            std::string error;
            if (format == "vhd" && options.vhd == "dynamic")
            {
                if (!vhd::WriteDynamic(device, temp, &error))
                    result = MediaResult::Fail(MediaError::IoError, error);
            }
            else if (!ExportBlockDevice(device, temp, &error) || (format == "vhd" && !AppendVhdFooter(device, temp, &error)))
            {
                result = MediaResult::Fail(MediaError::IoError, error);
            }
        }
        if (!result.Ok())
            RemoveFile(temp);
        return result;
    }
}  // namespace

std::string BlockFormats::WriterFor(const std::string& path)
{
    const std::string extension = StringHelper::ToLower(FileHelper::GetFileExtension(path));
    return extension == "chd" ? "chd" : extension == "vhd" ? "vhd" : "raw";
}

MediaResult BlockFormats::Compact(IBlockDevice& device, const BlockWriteOptions& options, std::unique_ptr<IBlockDevice>& volume)
{
    volume.reset();
    MediaResult result = MediaResult::Success();
    // The medium's stack outlives the compacted volume (written, then dropped): the pool borrows it
    auto pool = std::make_shared<SourcePool>();
    const uint16_t index = pool->AddDevice(std::shared_ptr<IBlockDevice>(&device, [](IBlockDevice*) {}));
    FatVolumeReader reader;
    std::string error;
    if (!reader.Open(device, CodePage::Cp866, &error))
        return MediaResult::Fail(MediaError::BadRequest, "compact needs a FAT volume: " + error);
    if (reader.VolumeStart() > 0)
    {
        uint8_t mbr[512];
        int used = 0;
        if (device.ReadSector(0, mbr))
            for (int e = 0; e < 4; e++)
                used += mbr[446 + 16 * e + 4] != 0 ? 1 : 0;
        if (used > 1)
            return MediaResult::Fail(MediaError::NotSupported, "compact re-synthesizes one FAT volume and this disk has " +
                                                                   std::to_string(used) + " partitions: export it as it is");
    }
    if (reader.Type() == FatReaderType::Fat12 && !options.fs)
        return MediaResult::Fail(MediaError::NotSupported,
                                 "compact writes FAT16 or FAT32 volumes and this one is FAT12: pass fs to convert it");
    auto tree = std::make_shared<FileTree>();
    std::vector<std::string> report;
    if (!FatImageSource::Enumerate(index, {}, *pool, *tree, &report, &error))
        return MediaResult::Fail(MediaError::UnreadableSource, "compact: " + error);
    for (const std::string& line : report)
        result.report.push_back("compact: " + line);

    FatVolumeOptions fat;
    fat.fs = options.fs.value_or(reader.Type() == FatReaderType::Fat32 ? FatType::Fat32 : FatType::Fat16);
    fat.mbr = reader.VolumeStart() > 0;
    if (fat.mbr)
        fat.partitionStart = static_cast<uint32_t>(reader.VolumeStart());
    if (!reader.Label().empty())
        fat.label = reader.Label();
    std::vector<std::string> carried;
    auto boot = std::make_shared<FatBootPlan>(FatBootPlan::FromVolume(pool->DevicePtr(index), std::nullopt, &carried));
    if (!boot->Empty())
        fat.boot = boot;

    std::vector<std::string> buildReport;
    std::unique_ptr<FatSynthVolume> built;
    const uint64_t size = options.size.value_or(device.SectorCount() * IBlockDevice::kSectorSize);
    built = FatSynthVolume::BuildToSize(tree, pool, fat, size, device.ContentId(), device.Describe() + ", compacted", &error,
                                        &buildReport);
    if (!built && options.size)
        return MediaResult::Fail(MediaError::DoesNotFit, "compact: " + error);
    if (!built)
    {
        // The medium's own size does not hold the content as asked (a FAT32 minimum, say): as small as it fits
        fat.freeBytes = 0;
        built = FatSynthVolume::Build(tree, pool, fat, device.ContentId(), device.Describe() + ", compacted", &error, &buildReport);
        if (!built)
            return MediaResult::Fail(MediaError::DoesNotFit, "compact: " + error);
        result.report.push_back("compact: the medium's size does not hold it as " + std::string(fat.fs == FatType::Fat32 ? "fat32" : "fat16") +
                                "; written at " + std::to_string(built->SectorCount() * IBlockDevice::kSectorSize) + " bytes");
    }
    for (const std::string& line : buildReport)
        result.report.push_back("compact: " + line + ": skipped");
    if (!carried.empty())
    {
        std::string list;
        for (const std::string& c : carried)
            list += (list.empty() ? "" : ", ") + c;
        result.report.push_back("compact: boot structures carried: " + list);
    }
    // The compacted volume keeps reading the pool's borrowed device; the pool lives as long as the volume
    volume = std::move(built);
    return result;
}

ChdImage* BlockFormats::FindChd(IBlockDevice* device)
{
    for (int depth = 0; device && depth < 8; depth++)
    {
        if (auto* chd = dynamic_cast<ChdImage*>(device))
            return chd;
        if (auto* session = dynamic_cast<SessionWriteMap*>(device))
            device = &session->Base();
        else if (auto* guard = dynamic_cast<ReadOnlyGuard*>(device))
            device = &guard->Base();
        else if (auto* tap = dynamic_cast<MediaReadTap*>(device))
            device = &tap->Base();
        else if (auto* hold = dynamic_cast<HostWriteHold*>(device))
            device = &hold->Base();
        else
            return nullptr;
    }
    return nullptr;
}

MediaResult BlockFormats::Write(IBlockDevice& device, const std::string& path, const BlockWriteOptions& options,
                                const std::function<bool(uint64_t, uint64_t)>& unchanged)
{
    const std::string format = WriterFor(path);
    if (options.compact)
    {
        std::unique_ptr<IBlockDevice> compacted;
        MediaResult compactResult = Compact(device, options, compacted);
        if (!compactResult.Ok())
            return compactResult;
        BlockWriteOptions plain = options;
        plain.compact = false;
        MediaResult written = Write(*compacted, path, plain);
        written.report.insert(written.report.begin(), compactResult.report.begin(), compactResult.report.end());
        return written;
    }
    MediaResult result = WriteImage(device, path, format, options, nullptr, unchanged);
    if (!result.Ok())
        return result;
    std::string error;
    if (!RenameOver(TempPathFor(path), path, error))
    {
        RemoveFile(TempPathFor(path));
        return MediaResult::Fail(MediaError::IoError, error);
    }
    return result;
}

MediaResult BlockFormats::Save(Medium& medium, const std::string& target, const BlockWriteOptions& options, std::string& savedPath)
{
    IBlockDevice* stack = medium.Block();
    if (!stack)
        return MediaResult::Fail(MediaError::NotSupported, "not a block medium");
    SessionWriteMap* session = medium.Session();

    const MediaSource& source = medium.Source();
    const bool hasOwnFile = source.type == MediaSourceType::File && !source.path.empty();
    if (target.empty() && !hasOwnFile)
        return MediaResult::Fail(MediaError::NotSupported, "the medium has no image file of its own: save it to a path, or export it");
    const std::string path = target.empty() ? source.path : target;
    const bool toOwnFile = hasOwnFile && FileHelper::AbsolutePath(path, /*resolveSymlinks*/ true) == medium.SourceKey();
    const std::string format = toOwnFile ? medium.Format() : WriterFor(path);

    if (!session)
    {
        // Read-only: nothing to save. Write-through: every write is in the file already
        if (toOwnFile)
        {
            savedPath = path;
            return MediaResult::Success();
        }
        return MediaResult::Fail(MediaError::NotSupported,
                                 "a read-only or write-through medium keeps its file: export it to '" + path + "' instead");
    }

    const bool rawFamily = format == "raw" || format == "hdf" || format == "vhd" || format == "hdi";
    // Compact: a new layout, so always a whole new file (never sectors written back in place)
    std::unique_ptr<IBlockDevice> compacted;
    MediaResult compactResult = MediaResult::Success();
    if (options.compact)
    {
        compactResult = Compact(*stack, options, compacted);
        if (!compactResult.Ok())
            return compactResult;
    }
    if (toOwnFile && !rawFamily && format != "chd")
        return MediaResult::Fail(MediaError::NotSupported, "a " + format + " medium cannot be written back: export it");

    // The raw family: the changed sectors go back into the file, in place
    if (toOwnFile && rawFamily && !compacted)
    {
        std::string error;
        {
            // A dynamic VHD allocates the blocks the changes need
            auto image = HddImageFormats::OpenBlock(path, format, RawImage::Access::ReadWrite, &error);
            if (!image)
                return MediaResult::Fail(MediaError::IoError, error);
            uint64_t failed = 0;
            bool writeFailed = false;
            if (!session->ForEachChange([&](uint64_t lba, const uint8_t* data) {
                    failed = lba;
                    writeFailed = !image->WriteSector(lba, data);
                    return !writeFailed;
                }))
            {
                return MediaResult::Fail(MediaError::IoError, writeFailed ? "cannot write sector " + std::to_string(failed) + " of " + path
                                                                          : "the session's spill file cannot be read");
            }
            if (auto* raw = dynamic_cast<RawImage*>(image.get()))
                raw->Flush();
        }
        // Read the file again through a fresh handle: the old one may hold stale buffers
        auto reopened = HddImageFormats::OpenBlock(path, format, RawImage::Access::ReadOnly, &error);
        if (!reopened)
            return MediaResult::Fail(MediaError::IoError, error);
        session->ReleaseBase().reset();
        session->SetBase(std::move(reopened));
        session->Discard();
        savedPath = path;
        return MediaResult::Success();
    }

    // A whole new file: a CHD written again (unchanged hunks reused, the same
    // parent), or the medium as another image file
    ChdImage* sourceChd = FindChd(stack);
    chd::ChdFile* inheritedParent = toOwnFile && sourceChd ? sourceChd->File().Parent() : nullptr;
    const auto unchanged = [session](uint64_t first, uint64_t count) { return !session->ChangedIn(first, count); };
    const std::string newFormat = WriterFor(path);
    BlockWriteOptions plain = options;
    plain.compact = false;
    MediaResult result = compacted ? WriteImage(*compacted, path, newFormat, plain, nullptr, {})
                                   : WriteImage(*stack, path, newFormat, options, inheritedParent, unchanged);
    compacted.reset();  // it reads the old stack, which goes away below
    result.report.insert(result.report.begin(), compactResult.report.begin(), compactResult.report.end());
    if (!result.Ok())
        return result;

    // Close the source before replacing it (Windows cannot replace an open file)
    std::unique_ptr<IBlockDevice> old = session->ReleaseBase();
    if (toOwnFile)
        old.reset();
    std::string error;
    if (!RenameOver(TempPathFor(path), path, error))
    {
        RemoveFile(TempPathFor(path));
        if (!old)
            old = HddImageFormats::OpenBlock(source.path, format, RawImage::Access::ReadOnly, &error);
        if (old)
            session->SetBase(std::move(old));
        return MediaResult::Fail(MediaError::IoError, error);
    }
    const std::string openFormat = newFormat == "chd" ? "chd" : HddImageFormats::Probe(path);
    auto reopened = HddImageFormats::OpenBlock(path, openFormat, RawImage::Access::ReadOnly, &error);
    if (!reopened)
    {
        // The file is written but cannot be read back: keep the old contents if they are still there
        if (old)
            session->SetBase(std::move(old));
        return MediaResult::Fail(MediaError::IoError, "saved to " + path + ", but cannot open it again: " + error);
    }
    session->SetBase(std::move(reopened));
    session->Discard();
    old.reset();
    if (!toOwnFile)
    {
        MediaSource saved;
        saved.type = MediaSourceType::File;
        saved.path = path;
        medium.Rebase(saved);
        medium.SetFormat(openFormat);
    }
    savedPath = path;
    MediaResult saved = MediaResult::Success();
    saved.report = result.report;
    return saved;
}
