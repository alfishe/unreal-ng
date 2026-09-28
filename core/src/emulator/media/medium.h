#pragma once

/// @file medium.h
/// @brief What is in a slot: contents built from a source and an access mode.
/// The MediaManager owns every Medium; slots hold a pointer between Attach and
/// Detach. M1 carries block media; floppy and tape payloads join in M2 / M3.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "emulator/io/storage/iblockdevice.h"
#include "emulator/media/mediatypes.h"

class SessionWriteMap;

struct MediaSource
{
    MediaSourceType type = MediaSourceType::File;
    std::string path;        ///< file or folder; empty for Blank
    std::string formatHint;  ///< optional: "raw", "folder-fat16", ...
};

class Medium
{
public:
    /// A block medium. `stack` is the whole block stack (source + access
    /// layer); `session` points into it when the access mode is Session
    Medium(MediaSource source, AccessMode access, std::string format, std::unique_ptr<IBlockDevice> stack,
           SessionWriteMap* session);

    Medium(const Medium&) = delete;
    Medium& operator=(const Medium&) = delete;

    MediaKind Kind() const { return _kind; }
    const MediaSource& Source() const { return _source; }
    AccessMode Access() const { return _access; }
    const std::string& Format() const { return _format; }

    /// The block stack (Block / Optical kinds), nullptr otherwise
    IBlockDevice* Block() { return _block.get(); }
    /// The session change layer (Session access), nullptr otherwise
    SessionWriteMap* Session() { return _session; }

    /// Guest writes not saved anywhere
    bool IsDirty() const;
    /// How many units (sectors / tracks) differ from the source
    uint64_t ChangedUnits() const;
    uint64_t ContentId() const;
    std::string Describe() const;

    /// Identity of the source for the "one source, one slot" rule: the
    /// canonical path of a file or folder, a unique token for anything else
    const std::string& SourceKey() const { return _sourceKey; }

    /// Notes gathered while building the medium (skipped entries, guessed
    /// format); the manager copies them into the insert result
    std::vector<std::string>& Report() { return _report; }

private:
    MediaKind _kind = MediaKind::Block;
    MediaSource _source;
    AccessMode _access;
    std::string _format;
    std::unique_ptr<IBlockDevice> _block;
    SessionWriteMap* _session = nullptr;
    std::string _sourceKey;
    std::vector<std::string> _report;
};

/// Write a block device to a raw image file, sector by sector
bool ExportBlockDevice(IBlockDevice& device, const std::string& path, std::string* error = nullptr);
