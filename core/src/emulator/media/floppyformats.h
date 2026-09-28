#pragma once

/// @file floppyformats.h
/// @brief The floppy half of the format registry: which image formats a
/// floppy drive takes, how a file is recognized (content first, the extension
/// only when the content says nothing), how it is loaded into a DiskImage and
/// how a DiskImage is written back. The format loaders in loaders/disk do the
/// actual work; this is the one place that dispatches to them.
/// Design: docs/inprogress/2026-09-28-storage-manager/technical-design.md §4,
/// integration-floppy.md §3.

#include <memory>
#include <string>
#include <vector>

#include "emulator/io/fdc/diskimage.h"
#include "emulator/media/mediatypes.h"

class EmulatorContext;

/// Outcome of FloppyFormats::Save
struct FloppySaveResult
{
    bool saved = false;       ///< the image is on disk, at savedPath
    bool retargeted = false;  ///< the requested format refused the image; it went to <stem>.udi instead
    std::string savedPath;
    std::string reason;       ///< why a save was refused, or the first warning of a successful one
};

/// A blank floppy: "unformatted" (no sectors: for the machine's own FORMAT),
/// "plus3" (+3DOS: 9 x 512, formatted) or "auto" (plus3 on a +3, else unformatted).
/// Zero cylinders / sides take the format's default (unformatted 80 x 2, plus3 40 x 1)
struct BlankFloppySpec
{
    std::string format = "auto";
    uint8_t cylinders = 0;
    uint8_t sides = 0;
};

class FloppyFormats
{
public:
    /// Extensions a floppy drive accepts (GUI filters, errors, automation)
    static std::vector<std::string> Extensions();

    /// The format of the file at `path`: "trd", "scl", "fdi", "udi", "dsk",
    /// "td0", "mgt", "hfe", "scp" or "hobeta". Signatures decide; the
    /// extension only breaks the tie for formats without one (TRD, MGT).
    /// Empty when nothing matches
    static std::string Probe(const std::string& path);

    /// Load the file at `path` into a new DiskImage. `context` gives the loaders
    /// the machine's settings (TR-DOS interleave). On success `format` names
    /// the format; the report carries the loader's warnings
    static MediaResult Load(EmulatorContext* context, const std::string& path, std::unique_ptr<DiskImage>& disk,
                            std::string& format);

    /// Write `disk` to `path` in the format its extension names (TRD for an
    /// unknown one). When that format cannot hold the image (TRD / SCL take
    /// only TR-DOS tracks) and `allowRetarget` is set, the image goes to
    /// <path without extension>.udi instead and the original file is left alone
    static FloppySaveResult Save(EmulatorContext* context, DiskImage& disk, const std::string& path, bool allowRetarget);

    /// Build a blank disk. `spec` comes back resolved (format, cylinders, sides)
    static MediaResult CreateBlank(bool plus3Machine, BlankFloppySpec& spec, std::unique_ptr<DiskImage>& disk);
};
