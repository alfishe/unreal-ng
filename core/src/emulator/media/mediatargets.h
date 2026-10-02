#pragma once

/// @file mediatargets.h
/// @brief Where a file can go on a machine: one analysis for every entry point
/// (drag and drop, File > Open, the command line, `media insert auto`).
/// Design: docs/inprogress/2026-09-29-media-drop-targets/design.md §4.
///
/// Classify looks at the file alone (content first, the extension as the
/// tie-breaker); Plan lists the targets a machine offers for it, in the order a
/// chooser shows them, with the default when there is no question to ask;
/// Apply performs a chosen slot target through MediaControl.

#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include "emulator/media/mediacontrol.h"
#include "emulator/media/mediatypes.h"

class EmulatorContext;

/// What a file is, independent of any machine
enum class FileKind : uint8_t
{
    Floppy,    ///< a floppy disk image (TRD, SCL, UDI, FDI, DSK, ...)
    Tape,      ///< a tape image
    Hdd,       ///< a hard-disk image (IDE unit)
    SdCard,    ///< a memory card image (SD slot)
    Optical,   ///< a CD image (ISO 9660)
    Snapshot,  ///< a machine state (SNA, Z80, SZX, SPG)
    Rzx,       ///< an input recording
    ZxPoly,    ///< a ZX-Poly group file (.zxp, .prom)
    Symbols,   ///< a label file (.map, .sym)
    Rom,       ///< a ROM image
};

const char* FileKindName(FileKind kind);

struct FileClass
{
    std::string path;
    bool folder = false;
    std::vector<FileKind> kinds;        ///< candidates, most likely first; empty: not a file this emulator knows
    std::string format;                 ///< the format the loader would use ("trd", "iso", "hdf", "fat", "raw", "szx", ...)
    std::vector<std::string> evidence;  ///< why, for people ("CD001 at #8001", "extension .tap")

    bool Is(FileKind kind) const;
};

/// One way to take the file on this machine
struct MediaTarget
{
    enum class Action : uint8_t
    {
        Insert,      ///< into `slotId`
        Load,        ///< a snapshot, a recording, a ZX-Poly group or labels: the caller loads it
        NewMachine,  ///< no machine yet: start `model` and insert into its drive A
    };

    Action action = Action::Insert;
    FileKind as = FileKind::Floppy;  ///< what the file is taken as
    std::string slotId;              ///< Insert
    std::string model;               ///< NewMachine
    std::string label;               ///< for people: "Drive A", "IDE slave (CD-ROM)", "SD card (NeoGS)"
    std::string occupiedBy;          ///< the medium the insert replaces; empty: the slot is empty
    bool dirty = false;              ///< that medium has unsaved writes
    bool autostart = false;          ///< drive A of a TR-DOS machine: the disk can boot at once
};

struct MediaPlan
{
    FileClass file;
    std::vector<MediaTarget> targets;  ///< in the order a chooser lists them
    /// The target used without asking: the only one, or drive A for a floppy
    /// image (the drop shortcut). -1: the caller asks (several targets) or
    /// refuses (none)
    int defaultTarget = -1;
    std::string refusal;  ///< no targets: why, in the user's words

    bool Refused() const { return targets.empty(); }
    /// The slot ids of the insert targets, comma separated (for messages)
    std::string SlotList() const;
};

class MediaTargets
{
public:
    /// What the file is. Never fails: an unknown file has no kinds
    static FileClass Classify(const std::string& path);

    /// The targets `context`'s machine offers for the file; `context` null:
    /// no machine runs (only a file that names its machine, or a floppy image
    /// with the Pentagon default, gets a target)
    static MediaPlan Plan(EmulatorContext* context, const FileClass& file);

    /// Performs targets[index] when it is an insert, through MediaControl
    /// (the slot's access, the TTD guard, the unsaved-writes disposition from
    /// `options`). Load and NewMachine targets belong to the caller:
    /// NotSupported
    static MediaReply Apply(EmulatorContext* context, const MediaPlan& plan, size_t index,
                            const std::map<std::string, std::string>& options = {});
};
