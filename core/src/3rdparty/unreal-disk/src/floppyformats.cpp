#include "unrealdisk/floppyformats.h"

#include <algorithm>
#include <cstring>

#include "unrealdisk/detail/helpers.h"
#include "unrealdisk/loaders/loader_dsk.h"
#include "unrealdisk/loaders/loader_fdi.h"
#include "unrealdisk/loaders/loader_hfe.h"
#include "unrealdisk/loaders/loader_hobeta.h"
#include "unrealdisk/loaders/loader_mgt.h"
#include "unrealdisk/loaders/loader_rawpc.h"
#include "unrealdisk/loaders/loader_scl.h"
#include "unrealdisk/loaders/loader_scp.h"
#include "unrealdisk/loaders/loader_td0.h"
#include "unrealdisk/loaders/loader_trd.h"
#include "unrealdisk/loaders/loader_udi.h"

namespace unrealdisk
{
namespace
{
    /// The largest Hobeta file: a 17-byte header and 255 sectors
    constexpr size_t kProbeBytes = 17 + 255 * 256;
    /// TR-DOS volume sector (track 0, sector 9) byte #E7: the TR-DOS id #10
    constexpr size_t kTrdosIdOffset = 8 * 256 + 0xE7;
    /// The largest TR-DOS image: 86 cylinders, 2 sides, 16 x 256-byte sectors. A bigger file with #10
    /// at the TR-DOS id byte is something else (a CD or hard-disk image)
    constexpr uint64_t kMaxTrdBytes = 2ull * 86 * 16 * 256;

    std::string Extension(const std::string& path)
    {
        return StringHelper::ToLower(FileHelper::GetFileExtension(path));
    }

    bool IsHobetaExtension(const std::string& ext)
    {
        return ext.size() == 2 && ext[0] == '$';
    }

    /// Run one loader that follows the loadImage / getImage / lastWarnings shape
    template <typename Loader>
    FormatResult LoadWith(Loader& loader, const char* name, std::unique_ptr<DiskImage>& disk)
    {
        if (!loader.loadImage() || !loader.getImage())
        {
            const std::string why = loader.lastWarnings().empty() ? std::string("not a valid ") + name + " image"
                                                                  : loader.lastWarnings().back();
            return FormatResult::Fail(FormatResult::Error::UnknownFormat, why);
        }
        disk.reset(loader.getImage());
        FormatResult result = FormatResult::Success();
        result.report = loader.lastWarnings();
        return result;
    }

    /// HFE / SCP keep their image under other accessor names
    template <typename Loader>
    FormatResult LoadFlux(Loader& loader, const char* name, std::unique_ptr<DiskImage>& disk)
    {
        if (!loader.loadImage() || !loader.getDiskImage())
            return FormatResult::Fail(FormatResult::Error::UnknownFormat, std::string("not a valid ") + name + " image");
        disk.reset(loader.getDiskImage());
        return FormatResult::Success();
    }

    template <typename Loader>
    bool WriteWith(Loader& loader, DiskImage& disk, std::vector<std::string>& warnings)
    {
        loader.setImage(&disk);
        const bool saved = loader.writeImage();
        warnings = loader.lastWarnings();
        return saved;
    }

    template <typename Loader>
    bool WriteFlux(Loader& loader, DiskImage& disk)
    {
        loader.setDiskImage(&disk);
        return loader.writeImage();
    }

    bool WriteByExtension(const LoaderOptions& options, DiskImage& disk, const std::string& path,
                          std::vector<std::string>& warnings)
    {
        const std::string ext = Extension(path);
        if (ext == "udi")
        {
            LoaderUDI loader(options, path);
            return WriteWith(loader, disk, warnings);
        }
        if (ext == "fdi")
        {
            LoaderFDI loader(options, path);
            return WriteWith(loader, disk, warnings);
        }
        if (ext == "dsk")
        {
            LoaderDSK loader(options, path);
            return WriteWith(loader, disk, warnings);
        }
        if (ext == "td0")
        {
            LoaderTD0 loader(options, path);
            return WriteWith(loader, disk, warnings);
        }
        // .img is shared: an MGT (+D, 10 x 512) disk stays MGT, anything else is a raw PC floppy, whose writer
        // either takes it (9 / 18 x 512 on 80 x 2) or says why not
        if (ext == "mgt" || (ext == "img" && LoaderMGT::isPlusDGeometry(&disk)))
        {
            LoaderMGT loader(options, path);
            return WriteWith(loader, disk, warnings);
        }
        if (ext == "img" || ext == "ima")
        {
            LoaderRawPcFloppy loader(options, path);
            return WriteWith(loader, disk, warnings);
        }
        if (ext == "scl")
        {
            LoaderSCL loader(options, path);
            return WriteWith(loader, disk, warnings);
        }
        if (ext == "hfe")
        {
            LoaderHFE loader(options, path);
            return WriteFlux(loader, disk);
        }
        if (ext == "scp")
        {
            LoaderSCP loader(options, path);
            return WriteFlux(loader, disk);
        }
        LoaderTRD loader(options, path);
        return WriteWith(loader, disk, warnings);
    }
}  // namespace

std::vector<std::string> FloppyFormats::Extensions()
{
    return {"trd", "scl", "fdi", "udi", "dsk", "td0", "mgt", "img", "ima", "hfe", "scp", "$b", "$c", "$d", "$#"};
}

std::string FloppyFormats::Probe(const std::string& path)
{
    const uint64_t size = FileHelper::GetFileSize(path);
    std::vector<uint8_t> head(static_cast<size_t>(std::min<uint64_t>(size, kProbeBytes)));
    if (!head.empty() && FileHelper::ReadFileToBuffer(path, head.data(), head.size()) != head.size())
        return {};
    return ProbeHead(head.data(), head.size(), size, Extension(path));
}

std::string FloppyFormats::ProbeData(const uint8_t* data, size_t size, const std::string& name)
{
    return ProbeHead(data, std::min<size_t>(size, kProbeBytes), size, Extension(name));
}

std::string FloppyFormats::ProbeHead(const uint8_t* data, size_t length, uint64_t size, const std::string& ext)
{
    // Signatures first: they cannot be mistaken for anything else
    if (LoaderUDI::detect(data, length))
        return "udi";
    if (LoaderFDI::detect(data, length))
        return "fdi";
    if (LoaderDSK::detect(data, length))
        return "dsk";
    if (LoaderSCP::detect(data, length))
        return "scp";
    if (length >= 8 && (std::memcmp(data, "HXCPICFE", 8) == 0 || std::memcmp(data, "HXCHFEV3", 8) == 0))
        return "hfe";
    if (length >= 8 && std::memcmp(data, "SINCLAIR", 8) == 0)
        return "scl";
    if (length == size && LoaderHobeta::detect(data, length))
        return "hobeta";
    // TD0's two-letter mark is weak: only with its own extension or nothing better
    if (LoaderTD0::detect(data, length) && (ext == "td0" || ext.empty()))
        return "td0";

    // No signature. A raw PC floppy is recognized by its size alone (720 KB, 1.44 MB): neither is a TR-DOS
    // image size (a TRD is at most 655 360 bytes) nor an MGT one, and a PC boot sector or FAT may well carry
    // #10 where the TR-DOS id would be, so the size rule goes first
    if (LoaderRawPcFloppy::detect(static_cast<size_t>(size)))
        return "rawpc";

    // A TR-DOS volume sector, then the size rules
    if (size % 256 == 0 && size <= kMaxTrdBytes && length > kTrdosIdOffset && data[kTrdosIdOffset] == 0x10)
        return "trd";
    if (LoaderMGT::detect(static_cast<size_t>(size), ext))
        return "mgt";
    if (ext == "trd" && size % 256 == 0 && size > 0 && size <= kMaxTrdBytes)
        return "trd";  // an unformatted TR-DOS image: no volume sector yet
    if (ext == "td0" && LoaderTD0::detect(data, length))
        return "td0";
    return {};
}

namespace
{
    /// A disk image of at most 42 cylinders is a 40-track (48 tpi) medium
    FormatResult MarkDensity(FormatResult result, std::unique_ptr<DiskImage>& disk)
    {
        if (result.Ok() && disk)
            disk->setFortyTrack(disk->getCylinders() <= 42);
        return result;
    }
}  // namespace

FormatResult FloppyFormats::Load(const LoaderOptions& options, const std::string& path, std::unique_ptr<DiskImage>& disk,
                                std::string& format)
{
    return MarkDensity(LoadAny(options, path, disk, format), disk);
}

namespace
{
    /// The drivers' parse step on bytes: the image named `name` (an MGT's sector order comes from its extension)
    template <typename Loader>
    FormatResult ParseWith(Loader& loader, const uint8_t* data, size_t size, const char* title, std::unique_ptr<DiskImage>& disk)
    {
        std::vector<std::string> warnings;
        DiskImage* image = loader.parse(data, size, warnings);
        if (!image)
            return FormatResult::Fail(FormatResult::Error::UnknownFormat,
                                      warnings.empty() ? std::string("not a valid ") + title + " image" : warnings.back());
        image->setLoaded(true);
        disk.reset(image);
        FormatResult result = FormatResult::Success();
        result.report = warnings;
        return result;
    }

    template <typename Loader>
    FormatResult DataWith(Loader& loader, const uint8_t* data, size_t size, const char* title, std::unique_ptr<DiskImage>& disk)
    {
        if (!loader.loadData(data, size) || !loader.getImage())
        {
            const std::string why = loader.lastWarnings().empty() ? std::string("not a valid ") + title + " image" : loader.lastWarnings().back();
            return FormatResult::Fail(FormatResult::Error::UnknownFormat, why);
        }
        disk.reset(loader.getImage());
        FormatResult result = FormatResult::Success();
        result.report = loader.lastWarnings();
        return result;
    }
}  // namespace

FormatResult FloppyFormats::LoadData(const LoaderOptions& options, const uint8_t* data, size_t size, const std::string& name,
                                     std::unique_ptr<DiskImage>& disk, std::string& format)
{
    disk.reset();
    format = ProbeData(data, size, name);
    FormatResult result;
    if (format.empty())
        result = FormatResult::Fail(FormatResult::Error::UnknownFormat, "'" + name + "' is not a disk image this library reads");
    else if (format == "trd")
    {
        LoaderTRD loader(options, name);
        result = ParseWith(loader, data, size, "TRD", disk);
    }
    else if (format == "scl")
    {
        LoaderSCL loader(options, name);
        result = DataWith(loader, data, size, "SCL", disk);
    }
    else if (format == "hobeta")
    {
        LoaderHobeta loader(options, name);
        result = DataWith(loader, data, size, "Hobeta", disk);
    }
    else if (format == "udi")
    {
        LoaderUDI loader(options, name);
        result = ParseWith(loader, data, size, "UDI", disk);
    }
    else if (format == "fdi")
    {
        LoaderFDI loader(options, name);
        result = ParseWith(loader, data, size, "FDI", disk);
    }
    else if (format == "dsk")
    {
        LoaderDSK loader(options, name);
        result = ParseWith(loader, data, size, "DSK", disk);
    }
    else if (format == "td0")
    {
        LoaderTD0 loader(options, name);
        result = ParseWith(loader, data, size, "TD0", disk);
    }
    else if (format == "mgt")
    {
        LoaderMGT loader(options, name);
        result = ParseWith(loader, data, size, "MGT", disk);
    }
    else if (format == "rawpc")
    {
        LoaderRawPcFloppy loader(options, name);
        result = ParseWith(loader, data, size, "raw PC floppy", disk);
    }
    else if (format == "hfe")
    {
        LoaderHFE loader(options, name);
        result = ParseWith(loader, data, size, "HFE", disk);
    }
    else if (format == "scp")
    {
        LoaderSCP loader(options, name);
        result = ParseWith(loader, data, size, "SCP", disk);
    }
    else
        result = FormatResult::Fail(FormatResult::Error::UnknownFormat, "unknown format " + format);
    return MarkDensity(result, disk);
}

FormatResult FloppyFormats::LoadAny(const LoaderOptions& options, const std::string& path, std::unique_ptr<DiskImage>& disk,
                                   std::string& format)
{
    disk.reset();
    format = Probe(path);
    if (format.empty())
    {
        const std::string ext = Extension(path);
        std::string known;
        for (const std::string& e : Extensions())
            known += (known.empty() ? "." : " .") + e;
        if (IsHobetaExtension(ext))
            return FormatResult::Fail(FormatResult::Error::UnknownFormat, "'" + path + "' has no valid Hobeta header");
        return FormatResult::Fail(FormatResult::Error::UnknownFormat,
                                 "unsupported disk image '" + path + "' (supported: " + known + ")");
    }

    if (format == "trd")
    {
        LoaderTRD loader(options, path);
        return LoadWith(loader, "TRD", disk);
    }
    if (format == "scl")
    {
        LoaderSCL loader(options, path);
        return LoadWith(loader, "SCL", disk);
    }
    if (format == "udi")
    {
        LoaderUDI loader(options, path);
        return LoadWith(loader, "UDI", disk);
    }
    if (format == "fdi")
    {
        LoaderFDI loader(options, path);
        return LoadWith(loader, "FDI", disk);
    }
    if (format == "dsk")
    {
        LoaderDSK loader(options, path);
        return LoadWith(loader, "DSK", disk);
    }
    if (format == "td0")
    {
        LoaderTD0 loader(options, path);
        return LoadWith(loader, "TD0", disk);
    }
    if (format == "mgt")
    {
        LoaderMGT loader(options, path);
        return LoadWith(loader, "MGT", disk);
    }
    if (format == "rawpc")
    {
        LoaderRawPcFloppy loader(options, path);
        return LoadWith(loader, "raw PC floppy", disk);
    }
    if (format == "hobeta")
    {
        LoaderHobeta loader(options, path);
        return LoadWith(loader, "Hobeta", disk);
    }
    if (format == "hfe")
    {
        LoaderHFE loader(options, path);
        return LoadFlux(loader, "HFE", disk);
    }
    LoaderSCP loader(options, path);
    return LoadFlux(loader, "SCP", disk);
}

FloppySaveResult FloppyFormats::Save(const LoaderOptions& options, DiskImage& disk, const std::string& path,
                                     bool allowRetarget)
{
    FloppySaveResult result;
    const std::string ext = Extension(path);
    if (IsHobetaExtension(ext))
    {
        result.reason = "a Hobeta file holds one TR-DOS file, not a disk: save the disk as .trd, .scl or .udi";
        return result;
    }

    std::vector<std::string> warnings;
    if (WriteByExtension(options, disk, path, warnings))
    {
        result.saved = true;
        result.savedPath = path;
        if (!warnings.empty())
            result.reason = warnings.front();
        return result;
    }

    const std::string reason = warnings.empty() ? "the image no longer fits the " + ext + " format" : warnings.front();
    if (!allowRetarget || ext == "udi")
    {
        result.reason = warnings.empty() ? "save failed" : warnings.front();
        return result;
    }

    // Keep the original untouched and save losslessly as UDI
    std::string udiPath = path;
    const size_t dot = udiPath.find_last_of('.');
    const size_t slash = udiPath.find_last_of("/\\");
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
        udiPath.erase(dot);
    udiPath += ".udi";

    LoaderUDI udi(options, udiPath);
    std::vector<std::string> udiWarnings;
    if (WriteWith(udi, disk, udiWarnings))
    {
        result.saved = true;
        result.retargeted = true;
        result.savedPath = udiPath;
        result.reason = reason;
    }
    else
    {
        result.reason = udiWarnings.empty() ? reason : udiWarnings.front();
    }
    return result;
}

FormatResult FloppyFormats::CreateBlank(bool plus3Machine, BlankFloppySpec& spec, std::unique_ptr<DiskImage>& disk)
{
    disk.reset();
    std::string format = StringHelper::ToLower(spec.format);
    if (format.empty() || format == "auto")
        format = plus3Machine ? "plus3" : "unformatted";
    else if (format == "raw")
        format = "unformatted";
    else if (format == "+3" || format == "+3dos")
        format = "plus3";
    else if (format == "tr-dos" || format == "trd")
        format = "trdos";
    if (format != "unformatted" && format != "plus3" && format != "trdos")
        return FormatResult::Fail(FormatResult::Error::BadRequest,
                                  "blank format '" + spec.format + "': expected auto, trdos, plus3 or unformatted (raw)");

    const bool plus3 = format == "plus3";
    const uint8_t cylinders = spec.cylinders ? spec.cylinders : (plus3 ? 40 : 80);
    const uint8_t sides = spec.sides ? spec.sides : (plus3 ? 1 : 2);
    if (cylinders != 40 && cylinders != 80)
        return FormatResult::Fail(FormatResult::Error::BadRequest, "cylinders must be 40 or 80");
    if (sides != 1 && sides != 2)
        return FormatResult::Fail(FormatResult::Error::BadRequest, "sides must be 1 or 2");

    disk = plus3 ? std::make_unique<DiskImage>(cylinders, sides, DiskImage::TrackFormatSpec::plus3())
                 : std::make_unique<DiskImage>(cylinders, sides);
    disk->setFortyTrack(cylinders == 40);
    // A TR-DOS disk as FORMAT leaves it: 16 x 256-byte sectors on every track, an empty catalogue and the system
    // sector (track 0, sector 9) naming the geometry and the free space
    if (format == "trdos")
    {
        LoaderTRD trd(LoaderOptions{}, "");
        if (!trd.format(disk.get()))
        {
            disk.reset();
            return FormatResult::Fail(FormatResult::Error::BadRequest, "cannot format a TR-DOS disk of that shape");
        }
    }
    spec.format = format;
    spec.cylinders = cylinders;
    spec.sides = sides;
    return FormatResult::Success();
}
}  // namespace unrealdisk
