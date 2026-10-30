#pragma once

#include "unrealdisk/detail/std.h"

#include <cstdint>
#include <string>
#include <vector>

#include "unrealdisk/diskimage.h"
#include "unrealdisk/options.h"

namespace unrealdisk
{

/// Profi CP/M floppy loader / writer (.pro, as ZXMAK2, Unreal Speccy and SteinBlume keep it): the sector dump of a
/// Profi MicroDOS disk without any low-level information.
///
/// 80 cylinders x 2 sides x 5 sectors x 1024 bytes (819 200 bytes; 81-83 cylinders taken too), sector IDs 1..5,
/// N = 3, the head byte the side. Cylinder 0 side 0 (FORMAT.COM's system track) has IDs 1, 2, 3, 4, 9: the BIOS
/// boots from R = 9 (BIOS 2.0 at #0861: `LD (IX+3),9`), and the floppy driver reads the fifth sector of logical track
/// 0 as R = 9 (HDD22.DRV at #094D: `CP 5 : ... : LD (IX+3),9` while the track is 0). Not an interleave: the physical
/// order varies (1 9 4 3 2, 9 4 3 2 1), the ID set does not. The tracks follow each other cylinder-major with the
/// sides interleaved: c0/h0, c0/h1, c1/h0, ... - the order of the CP/M logical tracks (DOSBIO.DRV: SPT 40, one side
/// a logical track). Recognized by its extension and size only (no signature).
///
/// Every track is a double-density MFM track of 6 250 bytes (250 kbit/s). Saving writes the sectors back while the
/// layout is still regular (5 sectors of 1024 bytes, 1..5 on every track, 9 standing for 5 on the system track);
/// otherwise the save is refused with the reason, and the image can be exported as UDI.
///
/// Format and references: docs/file-formats/disk-images/pro.md
class LoaderPRO
{
    /// region <Constants>
public:
    static constexpr const size_t SECTOR_SIZE = 1024;
    static constexpr const uint8_t SECTORS = 5;
    static constexpr const uint8_t SIDES = 2;
    static constexpr const uint8_t MIN_CYLINDERS = 80;
    static constexpr const uint8_t MAX_CYLINDERS = 83;
    static constexpr const size_t TRACK_SIZE = SECTOR_SIZE * SECTORS;  // 5 120
    static constexpr const uint8_t GAP3 = 54;
    static constexpr const uint8_t SYSTEM_SECTOR = 9;  // The fifth sector's ID on cylinder 0 side 0
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
    LoaderPRO(const LoaderOptions& options, const std::string& filepath) : _options(options), _filepath(filepath) {}
    virtual ~LoaderPRO() = default;
    /// endregion </Constructors / destructors>

    /// region <Properties>
public:
    DiskImage* getImage() { return _diskImage; }
    void setImage(DiskImage* diskImage) { _diskImage = diskImage; }  // Does not take ownership

    /// Diagnostics collected by the last loadImage / writeImage call
    const std::vector<std::string>& lastWarnings() const { return _warnings; }
    /// endregion </Properties>

    /// region <Basic methods>
public:
    /// Load the file into a new DiskImage (ownership passes to the caller via getImage())
    bool loadImage();

    /// Save the current image to the loader's path
    bool writeImage();

    /// Save the current image to the given path (Save As)
    bool writeImage(const std::string& path);

    /// The cylinders of a dump of this size (80..83), 0 when the size is no .pro image
    static uint8_t cylindersForSize(size_t fileSize);

    /// The Profi MicroDOS track layout: 5 sectors 1..5 of 1024 bytes on a 6 250-byte MFM track; the system track
    /// (cylinder 0 side 0) numbers them 1, 2, 3, 4, 9
    static DiskImage::TrackFormatSpec trackSpec(bool systemTrack = false);

    /// Logical sector 1..5 of a track: the sector of that ID, on the system track 9 for a missing 5
    static DiskImage::Sector* findSector(DiskImage::Track* track, uint8_t cylinder, uint8_t side, uint8_t number);

    /// True when the image has 80..83 cylinders on 2 sides and every track the 5 sectors 1..5 of 1024 bytes (9 for 5
    /// on the system track)
    /// @param reason Filled with a human readable explanation when the image does not qualify (may be nullptr)
    static bool isRegularGeometry(DiskImage* diskImage, std::string* reason = nullptr);

    /// Parse an in-memory dump into a new DiskImage. Used by loadImage() and by tests.
    /// @return New DiskImage or nullptr
    DiskImage* parse(const uint8_t* data, size_t len, std::vector<std::string>& warnings);

    /// Serialize the image to an in-memory dump. Strict: refuses an irregular layout.
    bool serialize(DiskImage* diskImage, std::vector<uint8_t>& out, std::vector<std::string>& warnings) const;
    /// endregion </Basic methods>
};
}  // namespace unrealdisk
