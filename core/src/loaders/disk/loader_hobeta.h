#pragma once

#include "stdafx.h"

#include <cstdint>
#include <string>
#include <vector>

#include "emulator/io/fdc/diskimage.h"
#include "emulator/io/fdc/trdos.h"
#include "loaders/disk/loader_scl.h"   // TRDOSDirectoryEntryBase / TRDOSDirectoryEntry

class EmulatorContext;

/// Hobeta ($-file) loader / exporter: a single TR-DOS file with a 17-byte header.
///
/// Not a disk image. "Loading" means injecting the file into a TR-DOS disk, exactly what LoaderSCL does for every
/// SCL entry: loadImage() creates a blank DS80 disk and adds the file, injectInto() adds it to an existing TR-DOS
/// image at the first free track/sector recorded in the volume sector (track 0, sector 9).
///
/// Header (little-endian):
///   0x00  8   file name (TR-DOS, space padded)
///   0x08  1   type / extension character (B, C, D, #, ...)
///   0x09  2   start (BASIC: autostart line; CODE: load address)
///   0x0B  2   length in bytes
///   0x0D  1   unused (first byte of a 16-bit sector count in some tools) - preserved verbatim
///   0x0E  1   length in sectors
///   0x0F  2   checksum = sum(i = 0..14) header[i] * 257 + i   (16-bit)
///   0x11  ..  data, sectors x 256 bytes
///
/// See docs/inprogress/2026-09-02-universal-track-model/loader-hobeta.md
class LoaderHobeta
{
    /// region <Types>
public:
    /// Decoded 17-byte header
    struct Header
    {
        char name[8] = { ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' };
        uint8_t type = 0;
        uint16_t start = 0;
        uint16_t length = 0;
        uint8_t reserved = 0;       // Byte 0x0D, preserved verbatim
        uint8_t sectors = 0;
        uint16_t checksum = 0;      // As stored in the file

        std::string nameString() const { return std::string(name, 8); }
    };

    /// Catalog entry together with its slot number in the TR-DOS catalog
    struct CatalogEntry
    {
        uint8_t index = 0;          // 0-based catalog slot (entry i lives in sector i / 16, offset (i % 16) * 16)
        TRDOSDirectoryEntry entry;

        std::string nameString() const { return std::string(entry.Name, 8); }
    };
    /// endregion </Types>

    /// region <Constants>
public:
    static constexpr const size_t HEADER_SIZE = 17;
    static constexpr const size_t CHECKSUM_OFFSET = 15;
    static constexpr const size_t NAME_SIZE = 8;
    /// endregion </Constants>

    /// region <Fields>
protected:
    EmulatorContext* _context = nullptr;
    std::string _filepath;

    DiskImage* _diskImage = nullptr;
    std::vector<std::string> _warnings;

    Header _header;                     // Header of the last successfully parsed $-file
    std::vector<uint8_t> _fileData;     // Body of the last successfully parsed $-file (sectors x 256)
    bool _fileLoaded = false;

    int _exportIndex = -1;              // Catalog slot selected for export, -1 = select by name
    std::string _exportName;            // Name selected for export (space padded to 8), empty = first file
    /// endregion </Fields>

    /// region <Constructors / destructors>
public:
    LoaderHobeta(EmulatorContext* context, const std::string& filepath) : _context(context), _filepath(filepath) {}
    virtual ~LoaderHobeta() = default;
    /// endregion </Constructors / destructors>

    /// region <Properties>
public:
    DiskImage* getImage() { return _diskImage; }
    void setImage(DiskImage* diskImage) { _diskImage = diskImage; }  // Does not take ownership

    /// Diagnostics collected by the last loadImage / injectInto / writeImage call
    const std::vector<std::string>& lastWarnings() const { return _warnings; }

    /// Header of the last successfully parsed $-file (valid after loadFile / loadImage / injectInto)
    const Header& getHeader() const { return _header; }
    const std::vector<uint8_t>& getFileData() const { return _fileData; }
    bool isFileLoaded() const { return _fileLoaded; }

    /// Select the catalog slot to export with writeImage() (takes precedence over setExportName)
    void setExportEntry(int index) { _exportIndex = index; }
    /// Select the file to export by TR-DOS name (up to 8 characters, padded with spaces); "" = first file
    void setExportName(const std::string& name);
    /// endregion </Properties>

    /// region <Basic methods>
public:
    /// Read and validate the $-file at the loader's path (header + body kept in the loader)
    bool loadFile();

    /// Create a blank DS80 TR-DOS image and inject the $-file into it
    /// (ownership of the image passes to the caller via getImage())
    bool loadImage();

    /// Inject the $-file into an existing TR-DOS image. The image is left untouched (dirty flags unchanged)
    /// when it is not a TR-DOS disk on the touched tracks, the catalog is full or the free space is insufficient.
    /// @param placed Receives the resulting catalog entry (with StartTrack / StartSector) on success
    bool injectInto(DiskImage* diskImage, TRDOSDirectoryEntry* placed = nullptr);

    /// Export the selected catalog entry of the current image to the loader's path
    bool writeImage();

    /// Export the selected catalog entry of the current image to the given path
    bool writeImage(const std::string& path);

    /// True when the buffer starts with a Hobeta header whose checksum matches and whose sector count
    /// matches the buffer size (17 + sectors * 256)
    static bool detect(const uint8_t* data, size_t len);

    /// Checksum over the first 15 header bytes
    static uint16_t computeChecksum(const uint8_t* header);

    /// Decode and validate a header. Reports why it was rejected.
    static bool parseHeader(const uint8_t* data, size_t len, Header& header, std::vector<std::string>& warnings);

    /// Encode a header (checksum computed)
    static void serializeHeader(const Header& header, uint8_t* out);

    /// Non-deleted catalog entries of a TR-DOS image (empty when track 0 is not a TR-DOS track)
    static std::vector<CatalogEntry> listFiles(DiskImage* diskImage);

    /// Shared "add a file to a TR-DOS disk" step (same algorithm as LoaderSCL::addFile, with validation first)
    static bool addFile(DiskImage* diskImage, const TRDOSDirectoryEntryBase& descriptor, const uint8_t* fileData,
                        std::vector<std::string>& warnings, TRDOSDirectoryEntry* placed = nullptr);

    /// Serialise one catalog entry as a $-file (header + sector chain)
    /// @param reserved Value for header byte 0x0D
    static bool exportFile(DiskImage* diskImage, const TRDOSDirectoryEntry& entry, uint8_t reserved,
                           std::vector<uint8_t>& out, std::vector<std::string>& warnings);
    /// endregion </Basic methods>

    /// region <Helper methods>
protected:
    /// Resolve the export selection against the current image
    bool selectExportEntry(CatalogEntry& selected);

    /// Track 0 sectors 1..9 present as 256-byte sectors with data, TR-DOS signature in the volume sector
    static bool hasTrdosSystemTrack(DiskImage* diskImage, std::string* reason);

    /// Sector for a TR-DOS locator (track * 16 + sector), nullptr when outside the geometry / not 256 bytes
    static DiskImage::Sector* sectorForLocator(DiskImage* diskImage, uint16_t locator);
    /// endregion </Helper methods>
};

//
// Code Under Test (CUT) wrapper to allow access to protected and private properties and methods for unit testing
//
#ifdef _CODE_UNDER_TEST

class LoaderHobetaCUT : public LoaderHobeta
{
public:
    LoaderHobetaCUT(EmulatorContext* context, const std::string& filepath) : LoaderHobeta(context, filepath) {}

public:
    using LoaderHobeta::_diskImage;
    using LoaderHobeta::_header;
    using LoaderHobeta::_fileData;
    using LoaderHobeta::_exportIndex;
    using LoaderHobeta::_exportName;
    using LoaderHobeta::hasTrdosSystemTrack;
    using LoaderHobeta::sectorForLocator;
};
#endif  // _CODE_UNDER_TEST
