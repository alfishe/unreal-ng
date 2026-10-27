#pragma once

// The floppy image formats as one registry: which format a file is, load it, save a disk in the format a path names
// (with a lossless UDI fallback), make a blank disk. The emulator's media manager wraps it with its own result type.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "unrealdisk/diskimage.h"
#include "unrealdisk/options.h"

namespace unrealdisk
{
/// Outcome of FloppyFormats::Load / CreateBlank
struct FormatResult
{
    enum class Error : uint8_t
    {
        None,
        UnknownFormat,   ///< not a disk image this library reads, or a broken one
        BadRequest,      ///< a parameter out of range (CreateBlank)
    };
    Error error = Error::None;
    std::string message;
    std::vector<std::string> report;   ///< the loader's warnings

    bool Ok() const { return error == Error::None; }
    static FormatResult Success() { return {}; }
    static FormatResult Fail(Error code, std::string text)
    {
        FormatResult result;
        result.error = code;
        result.message = std::move(text);
        return result;
    }
};

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
    /// "td0", "mgt", "rawpc", "mb02", "hfe", "scp" or "hobeta". Signatures decide; then
    /// the size (a raw PC floppy is 737 280 or 1 474 560 bytes) and the MB-02 boot sector; the extension
    /// only breaks the tie for formats without either (TRD, MGT).
    /// Empty when nothing matches
    static std::string Probe(const std::string& path);

    /// The same for an image in memory; `name` gives the extension where the format has no mark (TRD, MGT)
    static std::string ProbeData(const uint8_t* data, size_t size, const std::string& name);

    /// Load the file at `path` into a new DiskImage. `options` gives the loaders
    /// the host's settings (TR-DOS interleave). On success `format` names
    /// the format; the report carries the loader's warnings
    static FormatResult Load(const LoaderOptions& options, const std::string& path, std::unique_ptr<DiskImage>& disk,
                            std::string& format);

    /// An image in memory (a file read already, an archive member, base64 data) into a new DiskImage
    static FormatResult LoadData(const LoaderOptions& options, const uint8_t* data, size_t size, const std::string& name,
                                 std::unique_ptr<DiskImage>& disk, std::string& format);

    /// Write `disk` to `path` in the format its extension names (TRD for an
    /// unknown one; .img is MGT for a +D disk, else a raw PC floppy). When
    /// that format cannot hold the image (TRD / SCL take only TR-DOS tracks,
    /// a raw PC floppy only its regular layout) and `allowRetarget` is set, the image goes to
    /// <path without extension>.udi instead and the original file is left alone
    static FloppySaveResult Save(const LoaderOptions& options, DiskImage& disk, const std::string& path, bool allowRetarget);

    /// Build a blank disk. `spec` comes back resolved (format, cylinders, sides)
    static FormatResult CreateBlank(bool plus3Machine, BlankFloppySpec& spec, std::unique_ptr<DiskImage>& disk);

private:
    static std::string ProbeHead(const uint8_t* data, size_t length, uint64_t size, const std::string& ext);
    static FormatResult LoadAny(const LoaderOptions& options, const std::string& path, std::unique_ptr<DiskImage>& disk,
                               std::string& format);
};
}  // namespace unrealdisk
