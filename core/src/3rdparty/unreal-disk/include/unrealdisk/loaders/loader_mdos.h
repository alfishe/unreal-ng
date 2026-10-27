#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "unrealdisk/detail/std.h"
#include "unrealdisk/diskimage.h"
#include "unrealdisk/options.h"

namespace unrealdisk
{

/// Didaktik D40 / D80 raw image loader / writer (.d40, .d80 as Fuse keeps them): the 512-byte sectors of an MDOS
/// disk in logical order, no header: LSN = (cylinder * sides + side) * SPT + (sector - 1), sector ids 1..SPT.
///
/// Recognized by its boot sector (LSN 0), whatever the extension: "SDOS" at 204 and the geometry it gives (sides 2
/// when bit 4 of #B1 is set, cylinders at #B2, sectors a track at #B3) matching the file size. Typical: D80 80 x 2 x 9
/// (737 280 bytes, the size of a 720 KB PC dump: the boot sector tells them apart), D40 40 x 1 or 2 x 9.
///
/// Every track is a DD MFM track (6 250 bytes) of 512-byte sectors 1..SPT, HD (12 500) past 10 sectors. Saving
/// writes the sectors back while the layout is still regular. Layout: docs/emulator/design/disk-file-systems.md
/// (MDOS).
class LoaderMDOS
{
    /// region <Constants>
public:
    static constexpr const size_t SECTOR_SIZE = 512;
    static constexpr const size_t MARK_OFFSET = 204;  ///< "SDOS"
    /// endregion </Constants>

    /// region <Fields>
protected:
    LoaderOptions _options;
    std::string _filepath;

    DiskImage* _diskImage = nullptr;
    std::vector<std::string> _warnings;
    /// endregion </Fields>

    /// region <Constructors / destructors>
public:
    LoaderMDOS(const LoaderOptions& options, const std::string& filepath) : _options(options), _filepath(filepath) {}
    virtual ~LoaderMDOS() = default;
    /// endregion </Constructors / destructors>

    /// region <Properties>
public:
    DiskImage* getImage()
    {
        return _diskImage;
    }
    void setImage(DiskImage* diskImage)
    {
        _diskImage = diskImage;
    }  // Does not take ownership

    /// Diagnostics collected by the last loadImage / writeImage call
    const std::vector<std::string>& lastWarnings() const
    {
        return _warnings;
    }
    /// endregion </Properties>

    /// region <Basic methods>
public:
    bool loadImage();
    bool writeImage();
    bool writeImage(const std::string& path);

    /// True when `data` (at least the boot sector) starts an MDOS raw image of `fileSize` bytes
    static bool detect(const uint8_t* data, size_t length, uint64_t fileSize);

    /// The boot sector's geometry when it carries the MDOS mark: cylinders (1..`maxCylinders`), sides (1, 2) and
    /// sectors a track (1..18)
    static bool bootGeometry(const uint8_t* boot, uint8_t& cylinders, uint8_t& sides, uint8_t& sectors,
                             uint8_t maxCylinders = MAX_CYLINDERS);

    /// The MFM track of `sectors` 512-byte sectors
    static DiskImage::TrackFormatSpec trackSpec(uint8_t sectors);

    /// True when every track carries the same sectors 1..N of 512 bytes
    static bool isRegularGeometry(DiskImage* diskImage, uint8_t* sectorsPerTrack = nullptr,
                                  std::string* reason = nullptr);

    DiskImage* parse(const uint8_t* data, size_t len, std::vector<std::string>& warnings);
    bool serialize(DiskImage* diskImage, std::vector<uint8_t>& out, std::vector<std::string>& warnings) const;
    /// endregion </Basic methods>
};
}  // namespace unrealdisk
