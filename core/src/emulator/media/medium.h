#pragma once

/// @file medium.h
/// @brief What is in a slot: contents built from a source and an access mode.
/// The MediaManager owns every Medium; slots hold a pointer between Attach and
/// Detach. Block media (M1), floppy disks (M2) and tapes (M3).

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "common/unicodehelper.h"
#include "emulator/io/fdc/diskimage.h"
#include "emulator/io/tape/tapetypes.h"
#include "emulator/io/storage/iblockdevice.h"
#include "emulator/media/mediatypes.h"

class CdImage;
struct CompositeInfo;
class HostWriteHold;
class MediaReadTap;
class SessionWriteMap;

struct MediaSource
{
    MediaSourceType type = MediaSourceType::File;
    std::string path;        ///< file or folder; empty for Blank
    std::string formatHint;  ///< optional: "raw", "folder-fat16", ...
    std::string inlineBody;  ///< Composite: the descriptor itself (YAML / JSON) instead of a file
};

class Medium
{
public:
    /// A block medium. `stack` is the whole block stack (source + access
    /// layer); `session` points into it when the access mode is Session.
    /// `kind` is Block (SD card, hard disk) or Optical (a CD: 2048-byte blocks
    /// as four sectors of the stack)
    Medium(MediaSource source, AccessMode access, std::string format, std::unique_ptr<IBlockDevice> stack,
           SessionWriteMap* session, MediaKind kind = MediaKind::Block);
    /// A floppy disk. The image itself is the session: guest writes change it
    /// in memory (its tracks turn dirty) until it is saved
    Medium(MediaSource source, AccessMode access, std::string format, std::unique_ptr<DiskImage> disk);
    /// A tape: the parsed image (blocks and descriptors). The deck plays a
    /// copy, so a rewind, a reset or a re-insert start from this one
    Medium(MediaSource source, AccessMode access, std::string format, std::unique_ptr<TapeImage> tape);

    Medium(const Medium&) = delete;
    Medium& operator=(const Medium&) = delete;

    MediaKind Kind() const { return _kind; }
    const MediaSource& Source() const { return _source; }
    AccessMode Access() const { return _access; }
    const std::string& Format() const { return _format; }

    /// The block stack (Block / Optical kinds), nullptr otherwise
    IBlockDevice* Block() { return _block.get(); }
    /// The disc (Optical kind): its tracks, raw frames and audio; it is also the
    /// base of the block stack. nullptr otherwise
    CdImage* Cd() { return _cd; }
    const CdImage* Cd() const { return _cd; }
    /// Set by the format registry when it builds an Optical medium (non-owning: the stack owns it)
    void SetCd(CdImage* cd) { _cd = cd; }
    /// The session change layer (block media with Session access), nullptr otherwise
    SessionWriteMap* Session() { return _session; }
    /// The replay hold on a write-through block stack (FR-20), nullptr otherwise
    HostWriteHold* Hold() { return _hold; }
    void SetHostWriteHold(HostWriteHold* hold) { _hold = hold; }
    /// The read tap on top of a block stack (time travel's media read journal), nullptr otherwise
    MediaReadTap* ReadTap() { return _readTap; }
    void SetReadTap(MediaReadTap* tap) { _readTap = tap; }
    /// The disk image (Floppy kind), nullptr otherwise
    DiskImage* Floppy() { return _disk.get(); }
    const DiskImage* Floppy() const { return _disk.get(); }
    /// The tape image (Tape kind), nullptr otherwise
    const TapeImage* Tape() const { return _tape.get(); }

    /// A save wrote the medium to `source` (a file): it now stands for that file
    void Rebase(MediaSource source);
    /// ... in that file's format (a block medium saved as another format)
    void SetFormat(std::string format) { _format = std::move(format); }
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

    /// A composite medium's layers and layout (`media layers`), nullptr for any other medium
    const CompositeInfo* Composite() const { return _composite.get(); }
    void SetComposite(std::shared_ptr<const CompositeInfo> info) { _composite = std::move(info); }

    /// A write-through floppy whose file format refused the guest's writes
    /// falls back to keeping them in memory
    void SetAccess(AccessMode access) { _access = access; }

    /// S2: the change layer as it is now is in a session delta file (saved or restored); a later
    /// write makes the medium dirty again
    void MarkPersisted();
    /// A delta met on insert was written over other sources: an S2 save over it needs `force`
    const std::string& DeltaConflict() const { return _deltaConflict; }
    void SetDeltaConflict(std::string reason) { _deltaConflict = std::move(reason); }

    /// Guest writes not saved anywhere
    bool IsDirty() const;
    /// How many units (block sectors / floppy tracks) differ from the source
    uint64_t ChangedUnits() const;
    uint64_t ContentId() const;
    /// The unsaved changes for people: "1 track: 3 sectors", "1 track: whole",
    /// "5 tracks: 20 sectors total", "48 sectors"; empty when clean.
    /// Emulation thread, or while the machine is not running
    std::string DescribeChanges() const;
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
    std::unique_ptr<TapeImage> _tape;
    SessionWriteMap* _session = nullptr;
    HostWriteHold* _hold = nullptr;   ///< in _block's stack (WriteThrough), not owned
    MediaReadTap* _readTap = nullptr; ///< the top of _block's stack, not owned
    CdImage* _cd = nullptr;
    std::string _sourceKey;
    std::vector<std::string> _report;
    OpenOptions _options;
    std::shared_ptr<const CompositeInfo> _composite;
    std::optional<uint64_t> _persistedGeneration;
    std::string _deltaConflict;
};

/// Write a block device to a raw image file, sector by sector
bool ExportBlockDevice(IBlockDevice& device, const std::string& path, std::string* error = nullptr);
