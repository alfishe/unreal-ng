#include "stdafx.h"

#include "mediaformatregistry.h"

#include "common/filehelper.h"
#include "common/stringhelper.h"
#include "emulator/io/storage/hostfolder/folderdiskbuilder.h"
#include "emulator/io/storage/hostfolder/foldertapebuilder.h"
#include "emulator/media/floppyformats.h"
#include "emulator/io/storage/hostfolder/foldermanifest.h"
#include "emulator/io/storage/hostfolder/foldersnapshot.h"
#include "emulator/io/storage/hddimageformats.h"
#include "emulator/io/storage/hostfolder/hostfolderfat.h"
#include "emulator/io/storage/rawimage.h"
#include "emulator/io/storage/readonlyguard.h"
#include "emulator/io/storage/sessionwritemap.h"
#include "loaders/tape/loader_tape.h"

std::unique_ptr<Medium> MediaFormatRegistry::WrapBlock(MediaSource source, AccessMode access, std::string format,
                                                       std::unique_ptr<IBlockDevice> base, MediaKind kind)
{
    SessionWriteMap* session = nullptr;
    std::unique_ptr<IBlockDevice> stack;
    switch (access)
    {
        case AccessMode::ReadOnly:
            stack = std::make_unique<ReadOnlyGuard>(std::move(base));
            break;
        case AccessMode::Session:
        {
            auto map = std::make_unique<SessionWriteMap>(std::move(base));
            session = map.get();
            stack = std::move(map);
            break;
        }
        case AccessMode::WriteThrough:
            stack = std::move(base);
            break;
    }
    return std::make_unique<Medium>(std::move(source), access, std::move(format), std::move(stack), session, kind);
}

/// A host folder as a FAT volume: manifest, snapshot, volume, access layer
static MediaResult OpenFolderVolume(const OpenRequest& request, std::unique_ptr<Medium>& medium)
{
    const MediaSource& source = request.source;
    const std::filesystem::path folder = FileHelper::ToFsPath(source.path);
    MediaResult result = MediaResult::Success();

    const FolderManifest manifest = FolderManifest::Load(folder);
    result.report.insert(result.report.end(), manifest.report.begin(), manifest.report.end());

    FolderScanOptions scan;
    scan.excludePatterns = manifest.exclude;
    scan.cancelRequested = request.cancelRequested;
    scan.onProgress = request.onProgress;
    FolderSnapshot snapshot;
    std::string error;
    if (!FolderSnapshot::Scan(folder, scan, snapshot, &error))
    {
        return MediaResult::Fail(error == FolderSnapshot::kCancelledError ? MediaError::Cancelled : MediaError::UnreadableSource,
                                 error);
    }
    for (const SkippedEntry& skipped : snapshot.Skipped())
        result.report.push_back(skipped.path + ": skipped, " + skipped.reason);

    FatVolumeOptions options;
    options.fs = request.fs;
    options.mbr = request.mbr;
    options.codePage = request.codePage.value_or(manifest.codePage.value_or(CodePage::Cp866));
    if (manifest.label)
        options.label = *manifest.label;
    if (request.freeBytes)
        options.freeBytes = *request.freeBytes;

    std::vector<std::string> buildReport;
    auto volume = HostFolderFat::Build(snapshot, options, &error, &buildReport);
    if (!volume && options.fs == FatType::Fat16)
    {
        // BUGS.md #2: a folder over the FAT16 ceiling (2 GiB) does not fail
        // the insert when the slot's controller also reads FAT32 - the volume
        // is built as FAT32 instead, whatever chose FAT16
        const bool fat32Allowed = request.allowedFs.empty()
            || std::find(request.allowedFs.begin(), request.allowedFs.end(), FatType::Fat32) != request.allowedFs.end();
        if (fat32Allowed)
        {
            options.fs = FatType::Fat32;
            error.clear();
            volume = HostFolderFat::Build(snapshot, options, &error, &buildReport);
            if (volume)
                result.report.push_back("the folder is over the FAT16 ceiling: the volume is built as FAT32");
        }
    }
    if (!volume)
        return MediaResult::Fail(MediaError::DoesNotFit, error);
    for (const std::string& line : buildReport)
        result.report.push_back(line + ": skipped");

    MediaSource resolved = source;
    resolved.type = MediaSourceType::Folder;
    medium = MediaFormatRegistry::WrapBlock(resolved, request.access,
                                            options.fs == FatType::Fat32 ? "folder-fat32" : "folder-fat16", std::move(volume));
    medium->Report() = result.report;
    medium->SetOptions({options.fs, request.codePage, request.freeBytes});
    return result;
}

/// A floppy: a disk image file, or a folder built into a TR-DOS disk
static MediaResult OpenFloppy(const OpenRequest& request, std::unique_ptr<Medium>& medium)
{
    const MediaSource& source = request.source;
    MediaSource resolved = source;
    std::unique_ptr<DiskImage> disk;
    std::string format;
    MediaResult result = MediaResult::Success();

    if (source.type == MediaSourceType::Folder || FileHelper::IsFolder(source.path))
    {
        if (request.access == AccessMode::WriteThrough)
            return MediaResult::Fail(MediaError::KindMismatch, "a folder is never written: use session or readonly access");
        result = FolderDiskBuilder::BuildTrd(request.context, FileHelper::ToFsPath(source.path), disk,
                                             request.cancelRequested, request.onProgress);
        resolved.type = MediaSourceType::Folder;
        format = "folder-trd";
    }
    else if (source.type == MediaSourceType::Blank)
    {
        return MediaResult::Fail(MediaError::NotSupported, "a blank disk is built by the caller and inserted as a Medium");
    }
    else
    {
        if (!FileHelper::FileExists(source.path))
            return MediaResult::Fail(MediaError::UnreadableSource, "file not found: '" + source.path + "'");
        result = FloppyFormats::Load(request.context, source.path, disk, format);
        resolved.type = source.type == MediaSourceType::Upload ? MediaSourceType::Upload : MediaSourceType::File;
        // The disk stands for its file, unless the file is not a disk (Hobeta)
        if (disk && format != "hobeta")
            disk->setFilePath(source.path);
    }
    if (!result.Ok())
        return result;

    // A Hobeta file stands for one file, not a disk: it is never written back
    AccessMode access = request.access;
    if (format == "hobeta" && access == AccessMode::WriteThrough)
    {
        access = AccessMode::Session;
        result.report.push_back("a Hobeta file is never written back: the disk is kept in memory (session)");
    }
    medium = std::make_unique<Medium>(resolved, access, format, std::move(disk));
    medium->Report() = result.report;
    medium->SetOptions({request.fs, request.codePage, request.freeBytes});
    return result;
}

static bool ReadWholeFile(const std::string& path, std::vector<uint8_t>& bytes)
{
    const size_t size = FileHelper::GetFileSize(path);
    if (size == 0)
        return false;
    bytes.resize(size);
    return FileHelper::ReadFileToBuffer(path, bytes.data(), size) == size;
}

/// A tape: a file any tape loader claims (content probe, extension as the
/// tie-break), or a folder built into a TZX. A tape is only ever read
static MediaResult OpenTape(const OpenRequest& request, std::unique_ptr<Medium>& medium)
{
    const MediaSource& source = request.source;
    MediaSource resolved = source;
    std::unique_ptr<TapeImage> image;
    std::string format;
    MediaResult result = MediaResult::Success();

    if (source.type == MediaSourceType::Folder || FileHelper::IsFolder(source.path))
    {
        result = FolderTapeBuilder::Build(FileHelper::ToFsPath(source.path), image);
        if (!result.Ok())
            return result;
        resolved.type = MediaSourceType::Folder;
        format = "folder-tzx";
    }
    else if (source.type == MediaSourceType::Blank)
    {
        return MediaResult::Fail(MediaError::NotSupported, "there is no blank tape: the emulator does not record to tape");
    }
    else
    {
        std::vector<uint8_t> bytes;
        if (!FileHelper::FileExists(source.path) || !ReadWholeFile(source.path, bytes))
            return MediaResult::Fail(MediaError::UnreadableSource, "cannot read '" + source.path + "'");
        LoaderTapeBase* loader = TapeLoaderRegistry::Instance().Select(bytes, source.path);
        if (!loader)
            return MediaResult::Fail(MediaError::UnknownFormat, "'" + source.path + "' is no tape format this build reads");
        image = std::make_unique<TapeImage>(loader->Load(bytes, source.path));
        if (!image->IsUsable())
            return MediaResult::Fail(MediaError::UnknownFormat,
                                     "'" + source.path + "': " + (image->errorText.empty() ? "no playable blocks" : image->errorText));
        result.report = image->parseWarnings;
        format = loader->Format().id;
        resolved.type = source.type == MediaSourceType::Upload ? MediaSourceType::Upload : MediaSourceType::File;
    }
    if (image->formatId.empty())
        image->formatId = format;

    medium = std::make_unique<Medium>(resolved, AccessMode::ReadOnly, format, std::move(image));
    medium->Report() = result.report;
    return result;
}

/// A CD: an ISO 9660 image, always read-only
static MediaResult OpenOptical(const OpenRequest& request, std::unique_ptr<Medium>& medium)
{
    const MediaSource& source = request.source;
    if (source.type == MediaSourceType::Folder || FileHelper::IsFolder(source.path))
        return MediaResult::Fail(MediaError::NotSupported, "a folder cannot be a CD yet: make an ISO of it");
    if (source.type == MediaSourceType::Blank)
        return MediaResult::Fail(MediaError::NotSupported, "there is no blank CD: the drive only reads");
    if (!FileHelper::FileExists(source.path))
        return MediaResult::Fail(MediaError::UnreadableSource, "no such file: " + source.path);

    std::string error;
    const std::string format = HddImageFormats::Probe(source.path, &error);
    if (format.empty())
        return MediaResult::Fail(MediaError::UnreadableSource, error);
    if (format == "chd")
        return MediaResult::Fail(MediaError::NotSupported, "'" + source.path + "': CD-ROM CHDs are not supported yet (extract an ISO with chdman extractcd)");
    const bool isoName = StringHelper::ToLower(FileHelper::GetFileExtension(source.path)) == "iso";
    if (format != "iso" && !(format == "raw" && isoName))
        return MediaResult::Fail(MediaError::UnknownFormat, "'" + source.path + "' is no CD image (no ISO 9660 volume)");
    auto image = HddImageFormats::Open(source.path, "iso", RawImage::Access::ReadOnly, &error);
    if (!image)
        return MediaResult::Fail(MediaError::UnreadableSource, error);

    MediaSource resolved = source;
    resolved.type = source.type == MediaSourceType::Upload ? MediaSourceType::Upload : MediaSourceType::File;
    medium = MediaFormatRegistry::WrapBlock(resolved, AccessMode::ReadOnly, "iso", std::move(image), MediaKind::Optical);
    return MediaResult::Success();
}

MediaResult MediaFormatRegistry::Open(const OpenRequest& request, std::unique_ptr<Medium>& medium)
{
    medium.reset();

    if (request.kind == MediaKind::Floppy)
        return OpenFloppy(request, medium);
    if (request.kind == MediaKind::Tape)
        return OpenTape(request, medium);
    if (request.kind == MediaKind::Optical)
        return OpenOptical(request, medium);

    const MediaSource& source = request.source;
    const bool isFolder = FileHelper::IsFolder(source.path);

    if (source.type == MediaSourceType::Folder || isFolder)
    {
        if (request.access == AccessMode::WriteThrough)
            return MediaResult::Fail(MediaError::KindMismatch, "a folder is never written: use session or readonly access");
        return OpenFolderVolume(request, medium);
    }

    if (source.type == MediaSourceType::Blank)
        return MediaResult::Fail(MediaError::NotSupported, "a blank block medium is built by the caller and inserted as a Medium (WrapBlock)");

    if (!FileHelper::FileExists(source.path))
        return MediaResult::Fail(MediaError::UnreadableSource, "no such file: " + source.path);

    // A raw image, a headered hard-disk format (HDF, HDI, fixed VHD) or a CHD
    std::string error;
    const std::string format = HddImageFormats::Probe(source.path, &error);
    if (format.empty())
        return MediaResult::Fail(MediaError::UnreadableSource, error);
    if (format == "iso")
        return MediaResult::Fail(MediaError::KindMismatch, "'" + source.path + "' is a CD image: it goes into a CD drive (an IDE unit becomes one with device=cdrom)");

    // A CHD is never written in place: guest writes stay in the change layer
    // until a save writes the CHD again (docs/inprogress/2026-10-02-media-chd/)
    AccessMode access = request.access;
    MediaResult result = MediaResult::Success();
    if (format == "chd" && access == AccessMode::WriteThrough)
    {
        access = AccessMode::Session;
        result.report.push_back("a CHD is not written in place: guest writes stay in memory until 'save' (session)");
    }
    auto image = HddImageFormats::OpenBlock(
        source.path, format, access == AccessMode::WriteThrough ? RawImage::Access::ReadWrite : RawImage::Access::ReadOnly, &error);
    if (!image)
        return MediaResult::Fail(format == "raw" ? MediaError::UnreadableSource : MediaError::UnknownFormat, error);

    MediaSource resolved = source;
    resolved.type = source.type == MediaSourceType::Upload ? MediaSourceType::Upload : MediaSourceType::File;
    medium = WrapBlock(resolved, access, format, std::move(image));
    medium->Report() = result.report;
    return result;
}

std::vector<std::string> MediaFormatRegistry::Extensions(MediaKind kind)
{
    switch (kind)
    {
        case MediaKind::Block: return {"img", "ima", "hdd", "hd", "hdf", "hdi", "vhd", "chd", "bin", "mmc", "sd"};
        case MediaKind::Optical: return {"iso"};
        case MediaKind::Floppy: return FloppyFormats::Extensions();
        case MediaKind::Tape: return TapeLoaderRegistry::Instance().SupportedExtensions();
        default: return {};
    }
}
