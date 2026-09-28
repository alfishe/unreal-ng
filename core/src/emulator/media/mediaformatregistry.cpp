#include "stdafx.h"

#include "mediaformatregistry.h"

#include "common/filehelper.h"
#include "emulator/io/storage/hostfolder/foldermanifest.h"
#include "emulator/io/storage/hostfolder/foldersnapshot.h"
#include "emulator/io/storage/hostfolder/hostfolderfat.h"
#include "emulator/io/storage/rawimage.h"
#include "emulator/io/storage/readonlyguard.h"
#include "emulator/io/storage/sessionwritemap.h"

std::unique_ptr<Medium> MediaFormatRegistry::WrapBlock(MediaSource source, AccessMode access, std::string format,
                                                       std::unique_ptr<IBlockDevice> base)
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
    return std::make_unique<Medium>(std::move(source), access, std::move(format), std::move(stack), session);
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
    FolderSnapshot snapshot;
    std::string error;
    if (!FolderSnapshot::Scan(folder, scan, snapshot, &error))
        return MediaResult::Fail(MediaError::UnreadableSource, error);
    for (const SkippedEntry& skipped : snapshot.Skipped())
        result.report.push_back(skipped.path + ": skipped, " + skipped.reason);

    FatVolumeOptions options;
    options.fs = request.fs;
    options.codePage = request.codePage.value_or(manifest.codePage.value_or(CodePage::Cp866));
    if (manifest.label)
        options.label = *manifest.label;
    if (request.freeBytes)
        options.freeBytes = *request.freeBytes;

    std::vector<std::string> buildReport;
    auto volume = HostFolderFat::Build(snapshot, options, &error, &buildReport);
    if (!volume)
        return MediaResult::Fail(MediaError::DoesNotFit, error);
    for (const std::string& line : buildReport)
        result.report.push_back(line + ": skipped");

    MediaSource resolved = source;
    resolved.type = MediaSourceType::Folder;
    medium = MediaFormatRegistry::WrapBlock(resolved, request.access,
                                            request.fs == FatType::Fat32 ? "folder-fat32" : "folder-fat16", std::move(volume));
    medium->Report() = result.report;
    return result;
}

MediaResult MediaFormatRegistry::Open(const OpenRequest& request, std::unique_ptr<Medium>& medium)
{
    medium.reset();

    if (request.kind != MediaKind::Block)
        return MediaResult::Fail(MediaError::NotSupported,
                                 std::string(MediaKindName(request.kind)) + " media are not served by the media manager yet");

    const MediaSource& source = request.source;
    const bool isFolder = FileHelper::IsFolder(source.path);

    if (source.type == MediaSourceType::Folder || isFolder)
    {
        if (request.access == AccessMode::WriteThrough)
            return MediaResult::Fail(MediaError::KindMismatch, "a folder is never written: use session or readonly access");
        return OpenFolderVolume(request, medium);
    }

    if (source.type == MediaSourceType::Blank)
        return MediaResult::Fail(MediaError::NotSupported, "blank block media are created with CreateBlank");

    if (!FileHelper::FileExists(source.path))
        return MediaResult::Fail(MediaError::UnreadableSource, "no such file: " + source.path);

    std::string error;
    auto image = RawImage::Open(source.path,
                                request.access == AccessMode::WriteThrough ? RawImage::Access::ReadWrite : RawImage::Access::ReadOnly,
                                &error);
    if (!image)
        return MediaResult::Fail(MediaError::UnreadableSource, error);

    MediaSource resolved = source;
    resolved.type = source.type == MediaSourceType::Upload ? MediaSourceType::Upload : MediaSourceType::File;
    medium = WrapBlock(resolved, request.access, "raw", std::move(image));
    return MediaResult::Success();
}

std::vector<std::string> MediaFormatRegistry::Extensions(MediaKind kind)
{
    switch (kind)
    {
        case MediaKind::Block: return {"img", "ima", "hdd", "hd", "bin", "mmc", "sd"};
        default: return {};
    }
}
