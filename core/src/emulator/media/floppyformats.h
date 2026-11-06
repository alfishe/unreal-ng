#pragma once

/// @file floppyformats.h
/// @brief The floppy formats of unreal-disk (unrealdisk::FloppyFormats, core/src/3rdparty/unreal-disk) with the
/// emulator's context and result type (technical-design.md §4): probe, load, save with the UDI fallback, blank disks.

#include <memory>
#include <string>
#include <vector>

#include "emulator/io/fdc/diskimage.h"
#include "emulator/media/mediatypes.h"
#include "loaders/disk/diskloaderoptions.h"
#include "unrealdisk/floppyformats.h"

using unrealdisk::BlankFloppySpec;
using unrealdisk::FloppySaveResult;

class FloppyFormats
{
public:
    /// Extensions a floppy drive accepts (GUI filters, errors, automation)
    static std::vector<std::string> Extensions() { return unrealdisk::FloppyFormats::Extensions(); }

    /// The format of the file at `path` ("trd", "scl", "fdi", "udi", "dsk", "td0", "mgt", "rawpc", "mb02", "mdos",
    /// "pro", "hfe", "scp", "hobeta"), empty when nothing matches
    static std::string Probe(const std::string& path) { return unrealdisk::FloppyFormats::Probe(path); }

    /// Load the file at `path` into a new DiskImage; `context` gives the loaders the machine's settings
    static MediaResult Load(EmulatorContext* context, const std::string& path, std::unique_ptr<DiskImage>& disk, std::string& format)
    {
        return Convert(unrealdisk::FloppyFormats::Load(DiskLoaderOptions(context), path, disk, format));
    }

    /// Write `disk` to `path` in the format its extension names (see unrealdisk::FloppyFormats::Save)
    static FloppySaveResult Save(EmulatorContext* context, DiskImage& disk, const std::string& path, bool allowRetarget)
    {
        return unrealdisk::FloppyFormats::Save(DiskLoaderOptions(context), disk, path, allowRetarget);
    }

    /// Build a blank disk. `spec` comes back resolved (format, cylinders, sides)
    static MediaResult CreateBlank(bool plus3Machine, BlankFloppySpec& spec, std::unique_ptr<DiskImage>& disk)
    {
        return Convert(unrealdisk::FloppyFormats::CreateBlank(plus3Machine, spec, disk));
    }

private:
    static MediaResult Convert(const unrealdisk::FormatResult& result)
    {
        MediaResult out = MediaResult::Success();
        if (!result.Ok())
            out = MediaResult::Fail(result.error == unrealdisk::FormatResult::Error::BadRequest ? MediaError::BadRequest : MediaError::UnknownFormat,
                                    result.message);
        out.report = result.report;
        return out;
    }
};
