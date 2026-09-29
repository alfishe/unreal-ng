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

Medium::Medium(MediaSource source, AccessMode access, std::string format, std::unique_ptr<DiskImage> disk)
    : _kind(MediaKind::Floppy),
      _source(std::move(source)),
      _access(access),
      _format(std::move(format)),
      _disk(std::move(disk))
{
    _sourceKey = MakeSourceKey(_source);
}

Medium::Medium(MediaSource source, AccessMode access, std::string format, std::unique_ptr<TapeImage> tape)
    : _kind(MediaKind::Tape),
      _source(std::move(source)),
      _access(access),
      _format(std::move(format)),
      _tape(std::move(tape))
{
    _sourceKey = MakeSourceKey(_source);
}

void Medium::Rebase(MediaSource source)
{
    _source = std::move(source);
    _sourceKey = MakeSourceKey(_source);
    if (_disk)
        _disk->setFilePath(_source.path);
}

bool Medium::IsDirty() const
{
    return ChangedUnits() > 0;
}

uint64_t Medium::ChangedUnits() const
{
    if (_disk)
        return _disk->dirtyTrackCount();
    return _session ? _session->ChangedSectors() : 0;
}

namespace
{
    std::string Count(size_t n, const char* one, const char* many)
    {
        return std::to_string(n) + " " + (n == 1 ? one : many);
    }
}  // namespace

std::string Medium::DescribeChanges() const
{
    if (_disk)
    {
        const DiskImage::DirtySummary d = _disk->dirtySummary();
        if (d.tracks == 0)
            return {};
        std::string detail;
        const size_t sectorTracks = d.tracks - d.wholeTracks;
        if (sectorTracks > 0)
            detail = Count(d.sectors, "sector", "sectors") + (d.tracks > 1 && d.wholeTracks == 0 ? " total" : "");
        if (d.wholeTracks > 0)
        {
            if (sectorTracks == 0)
                detail = "whole";
            else
                detail += ", " + std::to_string(d.wholeTracks) + " whole";
        }
        return Count(d.tracks, "track", "tracks") + ": " + detail;
    }
    const uint64_t sectors = ChangedUnits();
    return sectors ? Count(static_cast<size_t>(sectors), "sector", "sectors") : std::string();
}

uint64_t Medium::ContentId() const
{
    return _block ? _block->ContentId() : 0;
}

std::string Medium::Describe() const
{
    if (_block)
        return _block->Describe();
    if (_disk)
        return _format + " disk " + std::to_string(static_cast<int>(_disk->getCylinders())) + "x" +
               std::to_string(static_cast<int>(_disk->getSides()));
    if (_tape)
        return _format + " tape, " + std::to_string(_tape->blocks.size()) + " blocks";
    return _source.path;
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
