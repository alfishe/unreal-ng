#pragma once

/// @file iso9660reader.h
/// @brief Reads the ISO 9660 file system of a CD data track through any
/// IBlockDevice whose 512-byte sectors are the track's user data (a CdImage, an
/// ISO image): the volume descriptors, the Joliet tree when there is one,
/// directory records (several per block, never across a block), multi-extent
/// files and recording dates. Written from ECMA-119 and the Joliet
/// specification alone, independently of IsoSynthVolume (the writer), so each
/// tests the other. Design: docs/inprogress/2026-10-05-media-multisource/phases/c5-iso.md.

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "emulator/io/storage/iblockdevice.h"

struct IsoDirEntry
{
    std::string name;       ///< the Joliet name when the volume has Joliet, else the ISO name without ";1" (UTF-8)
    std::string isoName;    ///< the ISO 9660 name as recorded ("README.TXT;1")
    bool isDirectory = false;
    bool hidden = false;    ///< the "existence" flag
    uint64_t size = 0;      ///< all sections together
    int64_t mtimeUtc = 0;   ///< the recording date, UTC
    /// Where the bytes are: (block, bytes) per section, in order (several for a multi-extent file)
    std::vector<std::pair<uint32_t, uint32_t>> sections;
    uint32_t Block() const { return sections.empty() ? 0 : sections.front().first; }
};

/// One boot entry of an El Torito catalog
struct IsoBootEntry
{
    uint8_t platform = 0;       ///< 0 x86, 1 PowerPC, 2 Mac, 0xEF EFI
    bool bootable = true;       ///< indicator #88 (else #00)
    uint8_t emulation = 0;      ///< 0 none, 1 1.2M, 2 1.44M, 3 2.88M floppy, 4 hard disk
    uint16_t loadSegment = 0;
    uint8_t systemType = 0;
    uint16_t sectorCount = 0;   ///< 512-byte sectors loaded (no emulation)
    uint32_t loadBlock = 0;     ///< the image's first block
    /// The image's size: the floppy for floppy emulation, the disk its MBR
    /// describes for hard-disk emulation, sectorCount x 512 without emulation
    uint64_t imageBytes = 0;
};

class Iso9660Reader
{
public:
    static constexpr uint32_t kBlock = 2048;

    /// Read the volume descriptors from block 16 on. `useJoliet`: prefer the Joliet tree when present
    bool Open(IBlockDevice& device, std::string* error = nullptr, bool useJoliet = true);

    bool HasJoliet() const { return _joliet; }
    bool UsingJoliet() const { return _useJoliet; }
    const std::string& VolumeId() const { return _volumeId; }
    uint32_t VolumeBlocks() const { return _volumeBlocks; }
    /// The block of the Boot Record's catalog, 0 without El Torito
    uint32_t BootCatalogBlock() const { return _bootCatalog; }
    /// The El Torito catalog's entries (default first, then the sections'), in order.
    /// False with `error` when there is no catalog or its validation entry fails
    bool ReadBootCatalog(std::vector<IsoBootEntry>& entries, std::string* error = nullptr);
    /// The root directory as an entry (its block and size)
    const IsoDirEntry& Root() const { return _root; }

    /// The entries of a directory, "." and ".." left out, in recorded order
    bool List(const IsoDirEntry& directory, std::vector<IsoDirEntry>& entries, std::string* error = nullptr);
    /// "/", "/DIR/SUB": the entry at a path; parts match names ASCII case-insensitively
    bool Stat(const std::string& path, IsoDirEntry& entry, std::string* error = nullptr);
    bool ReadFile(const std::string& path, std::vector<uint8_t>& data, std::string* error = nullptr);
    /// A whole block
    bool ReadBlock(uint32_t block, uint8_t* dst);

    /// The UTC second count of a 7-byte directory record date
    static int64_t RecordDate(const uint8_t* date);

private:
    IBlockDevice* _device = nullptr;
    bool _joliet = false;
    bool _useJoliet = false;
    std::string _volumeId;
    uint32_t _volumeBlocks = 0;
    uint32_t _bootCatalog = 0;
    IsoDirEntry _root;
};
