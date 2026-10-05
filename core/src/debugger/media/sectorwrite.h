#pragma once

// A debugger's disk sector write for every automation surface (WebAPI PUT /disk/{drive}/sector/{cyl}/{side}/{sec},
// CLI disk write, Lua / Python disk_write_sector, the Qt debugger's Disk sector dialog; MCP through invoke_api):
// one implementation (docs/inprogress/2026-10-04-debugger-additions/tdd.md §3).
//
// The sector is found by its ID (the R byte of its address mark, 1-based on TR-DOS) on the track of the cylinder and
// side, whatever the interleave. The bytes go into its data field at `offset` the way the WD1793 WRITE SECTOR puts
// them there (Track::writeSectorData): the data CRC is recalculated and the image is marked modified. Refused: an
// empty drive, a write-protected disk, a missing track / sector, an ID-only sector, bytes past the data field. It
// runs at a coherent moment (paused, stopped, between two frames) and TTD keeps it as a tool edit.
//
// Example: drive A, cylinder 0, side 0, sector 9, offset 245, "MYDISK" renames a TR-DOS disk.

#include <cstdint>
#include <string>
#include <vector>

class Emulator;

namespace SectorWrite
{
struct Result
{
    bool ok = false;
    bool busy = false;          // no coherent moment within the timeout
    std::string moment;         // "paused", "stopped" or "frame" when ok
    uint16_t sectorSize = 0;    // the sector's data field size when it was found
    std::string error;          // non-empty when not ok
};

/// Write `bytes` into the data field of sector `sector` (its ID) on `drive` (0-3 = A-D) at `offset`
Result Write(Emulator* emulator, uint8_t drive, int cylinder, int side, int sector, uint32_t offset,
             const std::vector<uint8_t>& bytes, const char* source);

/// "A".."D" or "0".."3" (any case); false with the reason
bool ParseDrive(const std::string& text, uint8_t& drive, std::string& error);

/// Hex bytes, spaces / commas allowed ("4D 59", "4d59"); false with the reason
bool ParseHex(const std::string& text, std::vector<uint8_t>& bytes, std::string& error);
}  // namespace SectorWrite
