#include "stdafx.h"

#include "floppyformats.h"

#include <algorithm>
#include <cstring>

#include "common/filehelper.h"
#include "common/stringhelper.h"
#include "loaders/disk/loader_dsk.h"
#include "loaders/disk/loader_fdi.h"
#include "loaders/disk/loader_hfe.h"
#include "loaders/disk/loader_hobeta.h"
#include "loaders/disk/loader_mgt.h"
#include "loaders/disk/loader_scl.h"
#include "loaders/disk/loader_scp.h"
#include "loaders/disk/loader_td0.h"
#include "loaders/disk/loader_trd.h"
#include "loaders/disk/loader_udi.h"

namespace
{
    /// The largest Hobeta file: a 17-byte header and 255 sectors
    constexpr size_t kProbeBytes = 17 + 255 * 256;
    /// TR-DOS volume sector (track 0, sector 9) byte #E7: the TR-DOS id #10
    constexpr size_t kTrdosIdOffset = 8 * 256 + 0xE7;

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
    MediaResult LoadWith(Loader& loader, const char* name, std::unique_ptr<DiskImage>& disk)
    {
        if (!loader.loadImage() || !loader.getImage())
        {
            const std::string why = loader.lastWarnings().empty() ? std::string("not a valid ") + name + " image"
                                                                  : loader.lastWarnings().back();
            return MediaResult::Fail(MediaError::UnknownFormat, why);
        }
        disk.reset(loader.getImage());
        MediaResult result = MediaResult::Success();
        result.report = loader.lastWarnings();
        return result;
    }

    /// HFE / SCP keep their image under other accessor names
    template <typename Loader>
    MediaResult LoadFlux(Loader& loader, const char* name, std::unique_ptr<DiskImage>& disk)
    {
        if (!loader.loadImage() || !loader.getDiskImage())
            return MediaResult::Fail(MediaError::UnknownFormat, std::string("not a valid ") + name + " image");
        disk.reset(loader.getDiskImage());
        return MediaResult::Success();
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

    bool WriteByExtension(EmulatorContext* context, DiskImage& disk, const std::string& path,
                          std::vector<std::string>& warnings)
    {
        const std::string ext = Extension(path);
        if (ext == "udi")
        {
            LoaderUDI loader(context, path);
            return WriteWith(loader, disk, warnings);
        }
        if (ext == "fdi")
        {
            LoaderFDI loader(context, path);
            return WriteWith(loader, disk, warnings);
        }
        if (ext == "dsk")
        {
            LoaderDSK loader(context, path);
            return WriteWith(loader, disk, warnings);
        }
        if (ext == "td0")
        {
            LoaderTD0 loader(context, path);
            return WriteWith(loader, disk, warnings);
        }
        if (ext == "mgt" || ext == "img")
        {
            LoaderMGT loader(context, path);
            return WriteWith(loader, disk, warnings);
        }
        if (ext == "scl")
        {
            LoaderSCL loader(context, path);
            return WriteWith(loader, disk, warnings);
        }
        if (ext == "hfe")
        {
            LoaderHFE loader(context, path);
            return WriteFlux(loader, disk);
        }
        if (ext == "scp")
        {
            LoaderSCP loader(context, path);
            return WriteFlux(loader, disk);
        }
        LoaderTRD loader(context, path);
        return WriteWith(loader, disk, warnings);
    }
}  // namespace

std::vector<std::string> FloppyFormats::Extensions()
{
    return {"trd", "scl", "fdi", "udi", "dsk", "td0", "mgt", "img", "hfe", "scp", "$b", "$c", "$d", "$#"};
}

std::string FloppyFormats::Probe(const std::string& path)
{
    const uint64_t size = FileHelper::GetFileSize(path);
    std::vector<uint8_t> head(static_cast<size_t>(std::min<uint64_t>(size, kProbeBytes)));
    if (!head.empty() && FileHelper::ReadFileToBuffer(path, head.data(), head.size()) != head.size())
        return {};
    const uint8_t* data = head.data();
    const size_t length = head.size();
    const std::string ext = Extension(path);

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

    // No signature: a TR-DOS volume sector, then the size rules
    if (size % 256 == 0 && length > kTrdosIdOffset && data[kTrdosIdOffset] == 0x10)
        return "trd";
    if (LoaderMGT::detect(static_cast<size_t>(size), ext))
        return "mgt";
    if (ext == "trd" && size % 256 == 0 && size > 0)
        return "trd";  // an unformatted TR-DOS image: no volume sector yet
    if (ext == "td0" && LoaderTD0::detect(data, length))
        return "td0";
    return {};
}

MediaResult FloppyFormats::Load(EmulatorContext* context, const std::string& path, std::unique_ptr<DiskImage>& disk,
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
            return MediaResult::Fail(MediaError::UnknownFormat, "'" + path + "' has no valid Hobeta header");
        return MediaResult::Fail(MediaError::UnknownFormat,
                                 "unsupported disk image '" + path + "' (supported: " + known + ")");
    }

    if (format == "trd")
    {
        LoaderTRD loader(context, path);
        return LoadWith(loader, "TRD", disk);
    }
    if (format == "scl")
    {
        LoaderSCL loader(context, path);
        return LoadWith(loader, "SCL", disk);
    }
    if (format == "udi")
    {
        LoaderUDI loader(context, path);
        return LoadWith(loader, "UDI", disk);
    }
    if (format == "fdi")
    {
        LoaderFDI loader(context, path);
        return LoadWith(loader, "FDI", disk);
    }
    if (format == "dsk")
    {
        LoaderDSK loader(context, path);
        return LoadWith(loader, "DSK", disk);
    }
    if (format == "td0")
    {
        LoaderTD0 loader(context, path);
        return LoadWith(loader, "TD0", disk);
    }
    if (format == "mgt")
    {
        LoaderMGT loader(context, path);
        return LoadWith(loader, "MGT", disk);
    }
    if (format == "hobeta")
    {
        LoaderHobeta loader(context, path);
        return LoadWith(loader, "Hobeta", disk);
    }
    if (format == "hfe")
    {
        LoaderHFE loader(context, path);
        return LoadFlux(loader, "HFE", disk);
    }
    LoaderSCP loader(context, path);
    return LoadFlux(loader, "SCP", disk);
}

FloppySaveResult FloppyFormats::Save(EmulatorContext* context, DiskImage& disk, const std::string& path,
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
    if (WriteByExtension(context, disk, path, warnings))
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

    LoaderUDI udi(context, udiPath);
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

MediaResult FloppyFormats::CreateBlank(bool plus3Machine, BlankFloppySpec& spec, std::unique_ptr<DiskImage>& disk)
{
    disk.reset();
    std::string format = StringHelper::ToLower(spec.format);
    if (format.empty() || format == "auto")
        format = plus3Machine ? "plus3" : "unformatted";
    else if (format == "raw")
        format = "unformatted";
    else if (format == "+3" || format == "+3dos")
        format = "plus3";
    if (format != "unformatted" && format != "plus3")
        return MediaResult::Fail(MediaError::BadRequest, "blank format '" + spec.format + "': expected auto, unformatted or plus3");

    const bool plus3 = format == "plus3";
    const uint8_t cylinders = spec.cylinders ? spec.cylinders : (plus3 ? 40 : 80);
    const uint8_t sides = spec.sides ? spec.sides : (plus3 ? 1 : 2);
    if (cylinders != 40 && cylinders != 80)
        return MediaResult::Fail(MediaError::BadRequest, "cylinders must be 40 or 80");
    if (sides != 1 && sides != 2)
        return MediaResult::Fail(MediaError::BadRequest, "sides must be 1 or 2");

    disk = plus3 ? std::make_unique<DiskImage>(cylinders, sides, DiskImage::TrackFormatSpec::plus3())
                 : std::make_unique<DiskImage>(cylinders, sides);
    spec.format = format;
    spec.cylinders = cylinders;
    spec.sides = sides;
    return MediaResult::Success();
}
