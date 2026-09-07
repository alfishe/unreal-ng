#pragma once

#include "stdafx.h"

#include <cstdint>
#include <string>
#include <vector>

#include "emulator/io/fdc/diskimage.h"

class EmulatorContext;

/// MGT / IMG loader / writer (DISCiPLE, +D, SAM Coupe raw sector dumps).
///
/// Raw dump without any low-level information: 80 cylinders x 2 sides x 10 sectors x 512 bytes = 819 200 bytes
/// (409 600 bytes for 40-cylinder dumps). Sectors are numbered 1..10, size code N = 2. The two extensions differ only
/// in the order of the tracks inside the file:
///   .mgt  cylinder-major, sides interleaved:  c0/s0, c0/s1, c1/s0, c1/s1, ...
///   .img  side-major:                         c0/s0, c1/s0, ... c79/s0, c0/s1, ... c79/s1
/// The order is taken from the file extension; setTrackOrder() overrides it for callers without one.
///
/// Every track is formatted with TrackFormatSpec::plusD() (10 x 512, gap3 = 0x18) and the sector data is copied in.
/// Saving is strict: the image must carry exactly sectors 1..10 with 512-byte data fields on every track, otherwise
/// the save is refused with a warning (use UDI to save such an image).
///
/// See docs/inprogress/2026-09-02-universal-track-model/loader-mgt.md
class LoaderMGT
{
    /// region <Types>
public:
    enum class TrackOrder : uint8_t
    {
        Auto = 0,           // Decide by extension (.img => SideMajor, anything else => CylinderMajor)
        CylinderMajor = 1,  // .mgt
        SideMajor = 2       // .img
    };
    /// endregion </Types>

    /// region <Constants>
public:
    static constexpr const size_t SECTOR_SIZE = 512;
    static constexpr const size_t SECTORS_PER_TRACK = 10;
    static constexpr const size_t SIDES = 2;
    static constexpr const size_t TRACK_SIZE = SECTOR_SIZE * SECTORS_PER_TRACK;         // 5120
    static constexpr const size_t IMAGE_SIZE_80 = TRACK_SIZE * SIDES * 80;              // 819 200
    static constexpr const size_t IMAGE_SIZE_40 = TRACK_SIZE * SIDES * 40;              // 409 600
    /// endregion </Constants>

    /// region <Fields>
protected:
    EmulatorContext* _context = nullptr;
    std::string _filepath;

    DiskImage* _diskImage = nullptr;
    std::vector<std::string> _warnings;

    TrackOrder _trackOrder = TrackOrder::Auto;
    /// endregion </Fields>

    /// region <Constructors / destructors>
public:
    LoaderMGT(EmulatorContext* context, const std::string& filepath) : _context(context), _filepath(filepath) {}
    virtual ~LoaderMGT() = default;
    /// endregion </Constructors / destructors>

    /// region <Properties>
public:
    DiskImage* getImage() { return _diskImage; }
    void setImage(DiskImage* diskImage) { _diskImage = diskImage; }  // Does not take ownership

    /// Diagnostics collected by the last loadImage / writeImage call
    const std::vector<std::string>& lastWarnings() const { return _warnings; }

    /// Override the track order derived from the file extension (TrackOrder::Auto restores extension detection)
    void setTrackOrder(TrackOrder order) { _trackOrder = order; }
    TrackOrder getTrackOrder() const { return _trackOrder; }
    /// endregion </Properties>

    /// region <Basic methods>
public:
    /// Load the file into a new DiskImage (ownership passes to the caller via getImage())
    bool loadImage();

    /// Save the current image to the loader's path
    bool writeImage();

    /// Save the current image to the given path (Save As). The extension of `path` decides the track order
    /// unless setTrackOrder() was used.
    bool writeImage(const std::string& path);

    /// True when the size is a 40- or 80-cylinder dump and the extension is .mgt or .img (case-insensitive,
    /// with or without the leading dot)
    static bool detect(size_t fileSize, const std::string& extension);

    /// Number of cylinders for a dump of the given size (80, 40) or 0 when the size is not an MGT dump
    static uint8_t cylindersForSize(size_t fileSize);

    /// Track order implied by a file extension / path (.img => SideMajor, everything else => CylinderMajor)
    static TrackOrder orderForExtension(const std::string& extensionOrPath);

    /// True when every track carries exactly sectors 1..10 with 512-byte data fields
    /// @param reason Filled with a human readable explanation when the image does not qualify
    static bool isPlusDGeometry(DiskImage* diskImage, std::string* reason = nullptr);

    /// Parse an in-memory dump into a new DiskImage. Used by loadImage() and by tests.
    /// @param order Track order of the buffer (Auto is treated as CylinderMajor)
    /// @return New DiskImage or nullptr
    DiskImage* parse(const uint8_t* data, size_t len, TrackOrder order, std::vector<std::string>& warnings);

    /// Serialise the image to an in-memory dump in the given order. Strict: refuses non-+D geometry.
    bool serialize(DiskImage* diskImage, TrackOrder order, std::vector<uint8_t>& out, std::vector<std::string>& warnings) const;
    /// endregion </Basic methods>

    /// region <Helper methods>
protected:
    /// Track order to use for the given path: the override when set, otherwise by extension
    TrackOrder effectiveOrder(const std::string& path) const;

    /// Byte offset of track (cylinder, side) inside a dump with the given order
    static size_t trackOffset(uint8_t cylinders, uint8_t cylinder, uint8_t side, TrackOrder order);
    /// endregion </Helper methods>
};

//
// Code Under Test (CUT) wrapper to allow access to protected and private properties and methods for unit testing
//
#ifdef _CODE_UNDER_TEST

class LoaderMGTCUT : public LoaderMGT
{
public:
    LoaderMGTCUT(EmulatorContext* context, const std::string& filepath) : LoaderMGT(context, filepath) {}

public:
    using LoaderMGT::_diskImage;
    using LoaderMGT::_trackOrder;
    using LoaderMGT::effectiveOrder;
    using LoaderMGT::trackOffset;
};
#endif  // _CODE_UNDER_TEST
