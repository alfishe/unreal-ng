#include "stdafx.h"

#include "mediaformatregistry.h"

#include "common/filehelper.h"
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
        return MediaResult::Fail(MediaError::NotSupported, "folder volumes are not available yet: " + source.path);
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
