#include "stdafx.h"

#include "blockformats.h"

#include <algorithm>
#include <filesystem>
#include <system_error>

#include "common/filehelper.h"
#include "common/stringhelper.h"
#include "emulator/io/storage/chd/chdimage.h"
#include "emulator/io/storage/chd/chdwriter.h"
#include "emulator/io/storage/hddimageformats.h"
#include "emulator/io/storage/readonlyguard.h"
#include "emulator/io/storage/sessionwritemap.h"
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

    MediaResult WriteImage(IBlockDevice& device, const std::string& path, const std::string& format, const BlockWriteOptions& options,
                           chd::ChdFile* inheritedParent, const std::function<bool(uint64_t, uint64_t)>& unchanged)
    {
        if (format != "chd" && (!options.compression.empty() || !options.parent.empty()))
            return MediaResult::Fail(MediaError::BadRequest, "compression and parent apply to a .chd target only");

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
            if (!ExportBlockDevice(device, temp, &error))
                result = MediaResult::Fail(MediaError::IoError, error);
        }
        if (!result.Ok())
            RemoveFile(temp);
        return result;
    }
}  // namespace

std::string BlockFormats::WriterFor(const std::string& path)
{
    return StringHelper::ToLower(FileHelper::GetFileExtension(path)) == "chd" ? "chd" : "raw";
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
        else
            return nullptr;
    }
    return nullptr;
}

MediaResult BlockFormats::Write(IBlockDevice& device, const std::string& path, const BlockWriteOptions& options,
                                const std::function<bool(uint64_t, uint64_t)>& unchanged)
{
    const std::string format = WriterFor(path);
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
    if (toOwnFile && !rawFamily && format != "chd")
        return MediaResult::Fail(MediaError::NotSupported, "a " + format + " medium cannot be written back: export it");

    // The raw family: the changed sectors go back into the file, in place
    if (toOwnFile && rawFamily)
    {
        std::string error;
        {
            auto image = HddImageFormats::Open(path, format, RawImage::Access::ReadWrite, &error);
            if (!image)
                return MediaResult::Fail(MediaError::IoError, error);
            for (const auto& [lba, data] : session->Changes())
            {
                if (!image->WriteSector(lba, data.data()))
                    return MediaResult::Fail(MediaError::IoError, "cannot write sector " + std::to_string(lba) + " of " + path);
            }
            image->Flush();
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
    MediaResult result = WriteImage(*stack, path, newFormat, options, inheritedParent, unchanged);
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
    return MediaResult::Success();
}
