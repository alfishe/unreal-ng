#pragma once

/// @file medium.h
/// @brief What is in a slot: contents built from a source and an access mode.
/// The MediaManager owns every Medium; slots hold a pointer between Attach and
/// Detach. Block media (M1) and floppy disks (M2); tape joins in M3.

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "common/unicodehelper.h"
#include "emulator/io/fdc/diskimage.h"
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
    /// A floppy disk. The image itself is the session: guest writes change it
    /// in memory (its tracks turn dirty) until it is saved
    Medium(MediaSource source, AccessMode access, std::string format, std::unique_ptr<DiskImage> disk);

    Medium(const Medium&) = delete;
    Medium& operator=(const Medium&) = delete;

    MediaKind Kind() const { return _kind; }
    const MediaSource& Source() const { return _source; }
    AccessMode Access() const { return _access; }
    const std::string& Format() const { return _format; }

    /// The block stack (Block / Optical kinds), nullptr otherwise
    IBlockDevice* Block() { return _block.get(); }
    /// The session change layer (block media with Session access), nullptr otherwise
    SessionWriteMap* Session() { return _session; }
    /// The disk image (Floppy kind), nullptr otherwise
    DiskImage* Floppy() { return _disk.get(); }
    const DiskImage* Floppy() const { return _disk.get(); }

    /// A save wrote the medium to `source` (a file): it now stands for that file
    void Rebase(MediaSource source);
    /// How a folder medium was built, so it can be built again (rescan, a
    /// floppy's discard): the options the registry used
    struct OpenOptions
    {
        FatType fs = FatType::Fat16;
        std::optional<CodePage> codePage;
        std::optional<uint64_t> freeBytes;
    };
    const OpenOptions& Options() const { return _options; }
    void SetOptions(const OpenOptions& options) { _options = options; }

    /// A write-through floppy whose file format refused the guest's writes
    /// falls back to keeping them in memory
    void SetAccess(AccessMode access) { _access = access; }

    /// Guest writes not saved anywhere
    bool IsDirty() const;
    /// How many units (block sectors / floppy tracks) differ from the source
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
    std::unique_ptr<DiskImage> _disk;
    SessionWriteMap* _session = nullptr;
    std::string _sourceKey;
    std::vector<std::string> _report;
    OpenOptions _options;
};

/// Write a block device to a raw image file, sector by sector
bool ExportBlockDevice(IBlockDevice& device, const std::string& path, std::string* error = nullptr);
