#include "stdafx.h"

#include "medium.h"

#include <atomic>
#include <fstream>

#include "common/filehelper.h"
#include "emulator/io/storage/sessionwritemap.h"

namespace
{
    std::atomic<uint64_t> g_nextAnonymousSource{1};

    std::string MakeSourceKey(const MediaSource& source)
    {
        if ((source.type == MediaSourceType::File || source.type == MediaSourceType::Folder ||
             source.type == MediaSourceType::Upload) &&
            !source.path.empty())
        {
            // Absolute, "~" expanded, symlinks resolved: two spellings of one file are one source
            return FileHelper::AbsolutePath(source.path, /*resolveSymlinks*/ true);
        }
        return "anonymous:" + std::to_string(g_nextAnonymousSource.fetch_add(1));
    }
}  // namespace

Medium::Medium(MediaSource source, AccessMode access, std::string format, std::unique_ptr<IBlockDevice> stack,
               SessionWriteMap* session)
    : _kind(MediaKind::Block),
      _source(std::move(source)),
      _access(access),
      _format(std::move(format)),
      _block(std::move(stack)),
      _session(session)
{
    _sourceKey = MakeSourceKey(_source);
}

bool Medium::IsDirty() const
{
    return ChangedUnits() > 0;
}

uint64_t Medium::ChangedUnits() const
{
    return _session ? _session->ChangedSectors() : 0;
}

uint64_t Medium::ContentId() const
{
    return _block ? _block->ContentId() : 0;
}

std::string Medium::Describe() const
{
    return _block ? _block->Describe() : _source.path;
}

bool ExportBlockDevice(IBlockDevice& device, const std::string& path, std::string* error)
{
    std::ofstream out(FileHelper::ToFsPath(path), std::ios::binary | std::ios::trunc);
    if (!out)
    {
        if (error)
            *error = "cannot create " + path;
        return false;
    }

    uint8_t sector[IBlockDevice::kSectorSize];
    for (uint64_t lba = 0; lba < device.SectorCount(); lba++)
    {
        if (!device.ReadSector(lba, sector))
        {
            if (error)
                *error = "cannot read sector " + std::to_string(lba);
            return false;
        }
        out.write(reinterpret_cast<const char*>(sector), static_cast<std::streamsize>(IBlockDevice::kSectorSize));
    }

    out.flush();
    if (!out && error)
        *error = "write error on " + path;
    return static_cast<bool>(out);
}
