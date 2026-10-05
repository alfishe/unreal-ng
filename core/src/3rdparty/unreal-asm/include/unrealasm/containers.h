#pragma once

// File containers of the ZX world the codecs' sources come in: hobeta files (a TR-DOS file with a 17-byte header,
// "NAME.$A") and TR-DOS disk images (.trd). Reading for every container, writing for hobeta. The emulator has its
// own media manager; these are for the library's tools, tests and standalone users.

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "unrealasm/codec.h"

namespace unrealasm::containers
{
/// One TR-DOS file: catalog fields and its bytes
struct TrdosFile
{
    std::string name;           ///< 8 characters as stored (trailing blanks kept)
    char type = 0;              ///< type letter
    uint16_t start = 0;
    uint16_t length = 0;
    uint8_t sectors = 0;
    std::vector<uint8_t> data;  ///< `length` bytes (the catalog's length field)
    std::vector<uint8_t> tail;  ///< the bytes after `length` up to the end of the last sector (kept for exact copies)

    /// Catalog hints for codec detection
    CatalogHints Hints() const;
    /// The name without trailing blanks
    std::string TrimmedName() const;
};

/// A hobeta file: name(8) type(1) start(2) length(2) zero(1) sectors(1) checksum(2), then sectors x 256 bytes
bool ReadHobeta(std::span<const uint8_t> bytes, TrdosFile& out, std::string& error);
/// The checksum hobeta stores: sum over the first 15 header bytes of (byte * 257 + index), 16 bits
uint16_t HobetaChecksum(std::span<const uint8_t> header15);
/// A hobeta file of `file` (data + tail padded to whole sectors)
std::vector<uint8_t> WriteHobeta(const TrdosFile& file);

/// The files of a TR-DOS image (deleted entries skipped); false with the reason when it is not one
bool ReadTrd(std::span<const uint8_t> image, std::vector<TrdosFile>& out, std::string& error);
}  // namespace unrealasm::containers
