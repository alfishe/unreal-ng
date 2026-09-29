#pragma once

/// @file mediatypes.h
/// @brief Vocabulary of the media manager: what a slot holds, how guest writes
/// are treated, where contents come from, and the result every operation
/// returns. Design: docs/inprogress/2026-09-28-storage-manager/technical-design.md §2.

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

/// What a slot holds
enum class MediaKind : uint8_t
{
    Floppy,   ///< a DiskImage (TRD, SCL, UDI, DSK, ...)
    Tape,     ///< a tape image
    Block,    ///< 512-byte sectors: SD cards, IDE hard disks
    Optical,  ///< 2048-byte blocks: CD-ROM
};

/// What happens to guest writes
enum class AccessMode : uint8_t
{
    ReadOnly,     ///< refused with the peripheral's own error
    Session,      ///< kept in the change layer; the source never changes
    WriteThrough  ///< written to the source image file (never to a folder)
};

/// Where the contents come from
enum class MediaSourceType : uint8_t
{
    File,    ///< an image file
    Folder,  ///< a host folder, presented as a medium of the slot's kind
    Blank,   ///< created in memory
    Upload,  ///< a staged upload: a File the manager deletes on eject
};

/// File system of a folder volume
enum class FatType : uint8_t
{
    Fat16,
    Fat32,
};

/// Error codes shared by every surface (technical design, automation §1)
enum class MediaError : uint8_t
{
    None,
    UnknownSlot,       ///< no such slot on this machine
    KindMismatch,      ///< the source cannot go into this slot
    UnreadableSource,  ///< missing file or folder, no permission
    UnknownFormat,     ///< no format matched
    Dirty,             ///< unsaved changes; retry with force
    Recording,         ///< refused while TTD records; retry with endRecording
    InUse,             ///< the same source is in another slot
    NotSupported,      ///< a valid request this build cannot do yet
    DoesNotFit,        ///< the source does not fit the requested medium (FAT16 size, disk capacity)
    IoError,           ///< host I/O failed
    AmbiguousSlot,     ///< a selector names several slots
    BadRequest,        ///< an unknown verb or option, a malformed value
};

/// What an eject (or an insert over a medium) does with unsaved writes
enum class Disposition : uint8_t
{
    None,     ///< refuse a dirty medium ("dirty")
    Save,     ///< write it into its own file first
    Export,   ///< write it into a new file first
    Discard,  ///< drop the writes
};

/// Stable text codes: "unknown-slot", "kind-mismatch", ...
const char* MediaErrorCode(MediaError error);
/// The HTTP status every surface that speaks HTTP uses for an error (200 for None)
int MediaErrorHttpStatus(MediaError error);
const char* MediaKindName(MediaKind kind);
const char* AccessModeName(AccessMode access);
/// "readonly" | "session" | "writethrough" (case-insensitive); false if unknown
bool ParseAccessMode(const std::string& text, AccessMode& out);

/// The result of every manager operation. `report` lists what the caller should
/// see even on success: skipped folder entries, a format guessed from the
/// extension, media that could not follow a model switch
struct MediaResult
{
    MediaError error = MediaError::None;
    std::string message;
    std::vector<std::string> report;

    bool Ok() const { return error == MediaError::None; }

    static MediaResult Success() { return {}; }
    static MediaResult Fail(MediaError code, std::string text)
    {
        MediaResult result;
        result.error = code;
        result.message = std::move(text);
        return result;
    }
};
