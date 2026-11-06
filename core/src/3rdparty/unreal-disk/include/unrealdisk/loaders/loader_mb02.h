#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "unrealdisk/detail/std.h"
#include "unrealdisk/diskimage.h"
#include "unrealdisk/options.h"

namespace unrealdisk
{

/// MB-02 raw image loader / writer (.mb2, RealSpectrum .mbd): the 1024-byte sectors of an MB-02 disk in logical
/// order, no header: LSN = 2 * SPT * track + SPT * side + (sector - 1), sector ids 1..SPT.
///
/// Recognized by its boot sector (LSN 0), whatever the extension: JR (#18) at #00, the MB-02 mark #02 at #03, 2
/// surfaces at #08, 1 sector a cluster at #0A, #00 at #20 and #25, and the geometry it gives (tracks at #04, sectors
/// a track at #06) matching the file size. Typical: HD 80 or 82 tracks x 2 x 11 sectors (1 802 240 / 1 847 296
/// bytes), DD 80 x 2 x 5 (819 200 bytes, the size of an MGT image: the boot sector tells them apart).
///
/// Every track is an MFM track of 1024-byte sectors 1..SPT: 12 500 bytes (500 kbit/s) past 5 sectors, else 6 250.
/// Saving writes the sectors back while the layout is still regular (the same SPT x 1024 on every track).
/// Layout: docs/emulator/design/disk-file-systems.md (MB-02).
class LoaderMB02
{
    /// region <Constants>
public:
    static constexpr const size_t SECTOR_SIZE = 1024;
    static constexpr const uint8_t SIDES = 2;
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
    LoaderMB02(const LoaderOptions& options, const std::string& filepath) : _options(options), _filepath(filepath) {}
    virtual ~LoaderMB02() = default;
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

    /// True when `data` (at least the boot sector) starts an MB-02 raw image of `fileSize` bytes
    static bool detect(const uint8_t* data, size_t length, uint64_t fileSize);

    /// The boot sector's geometry when it is an MB-02 one: tracks (1..86) and sectors a track (1..22)
    static bool bootGeometry(const uint8_t* boot, uint8_t& tracks, uint8_t& sectors);

    /// The MFM track of `sectors` 1024-byte sectors
    static DiskImage::TrackFormatSpec trackSpec(uint8_t sectors);

    /// True when both sides of every cylinder carry the same sectors 1..N of 1024 bytes
    static bool isRegularGeometry(DiskImage* diskImage, uint8_t* sectorsPerTrack = nullptr,
                                  std::string* reason = nullptr);

    DiskImage* parse(const uint8_t* data, size_t len, std::vector<std::string>& warnings);
    bool serialize(DiskImage* diskImage, std::vector<uint8_t>& out, std::vector<std::string>& warnings) const;
    /// endregion </Basic methods>
};
}  // namespace unrealdisk
