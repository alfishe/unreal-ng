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
    std::vector<uint8_t> data;  ///< `length` bytes (the catalog's length field; fewer when it claims more than the sectors)
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

/// A +3DOS file (the Spectrum +3 and the Next's NextZXOS: a 128-byte header "PLUS3DOS" #1A, the issue and version, the
/// whole file's length (4, little endian), the BASIC type (0 program, 1 number array, 2 character array, 3 code), the
/// data length (2) and the BASIC parameters (3 x 2), blanks, and a checksum byte: the sum of the first 127). `type` and
/// `start` are the BASIC type and its first parameter; `data` follows the header up to the length it states (the rest, if
/// any, is `tail`)
struct Plus3dosFile
{
    uint8_t type = 3;
    uint16_t start = 0;
    uint16_t length = 0;          ///< the data length the header states
    std::vector<uint8_t> data;
    std::vector<uint8_t> tail;
};
bool ReadPlus3dos(std::span<const uint8_t> bytes, Plus3dosFile& out, std::string& error);
/// A +3DOS file of `file` (the header made from its fields)
std::vector<uint8_t> WritePlus3dos(const Plus3dosFile& file);

/// The files of a TR-DOS image (deleted entries skipped); false with the reason when it is not one
bool ReadTrd(std::span<const uint8_t> image, std::vector<TrdosFile>& out, std::string& error);

/// One data block of a tape image: the flag byte (0 header, #FF data) and the bytes between it and the checksum
struct TapeBlock
{
    uint8_t flag = 0;
    std::vector<uint8_t> data;
    bool checksumOk = true;
};

/// The data blocks of a TAP or TZX image (TZX: standard, turbo and pure data blocks; the other blocks are skipped);
/// false with the reason when it is neither
bool ReadTapeBlocks(std::span<const uint8_t> image, std::vector<TapeBlock>& out, std::string& error);

/// The files of a TAP or TZX image as TR-DOS-like entries: a header block names the data block after it (type 3
/// "Bytes" is `C` with its start, 0 "Program" `B`, the arrays `D`); a data block without a header becomes `C` named
/// BLOCKnn
bool ReadTape(std::span<const uint8_t> image, std::vector<TrdosFile>& out, std::string& error);
}  // namespace unrealasm::containers
