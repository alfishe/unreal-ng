#pragma once

/// @file diskfiles.h
/// @brief Files on the disks in the machine's drives for the assembler-source and symbol surfaces (unreal-asm tdd §7):
/// "disk:A/NAME.T" names the last live TR-DOS catalog entry NAME of type T on drive A. Reading and writing go through
/// unreal-disk's DiskFs on the drive's image, the code zxasm uses (unreal-asm tools/common/diskfiles.h): reading any
/// file system it reads (TR-DOS with the catalog hints and sector slack, +3DOS, iS-DOS, FAT), writing TR-DOS and
/// +3DOS; writing deletes an older live file of that name and type first, as TR-DOS does, is refused on a
/// write-protected disk, and leaves the image modified. Both run at a coherent moment of the emulator (paused, stopped or between frames).
/// Image files on the host (any format unreal-disk reads, and tapes) are read by ReadImageFiles.

#include <cstdint>
#include <string>
#include <vector>

#include "unrealasm/containers.h"

class EmulatorContext;

struct DiskFileRef
{
    uint8_t drive = 0;      ///< 0-3 for A-D
    std::string name;       ///< the TR-DOS name without trailing blanks, at most 8 characters
    char type = 0;          ///< the type letter; 0 = any (reading only)
};

/// "disk:A/NAME.T" (or "disk:A/NAME", any type) -> the reference; false when the text is no disk file reference
bool ParseDiskFileRef(const std::string& text, DiskFileRef& out);
bool IsDiskFileRef(const std::string& text);

/// Every live file of the disk in a drive (deleted entries left out), in catalog order
bool ReadDiskFiles(EmulatorContext* context, uint8_t drive, std::vector<unrealasm::containers::TrdosFile>& out, std::string& error);
/// The last live entry the reference names
bool ReadDiskFile(EmulatorContext* context, const DiskFileRef& ref, unrealasm::containers::TrdosFile& out, std::string& error);
/// Writes `bytes` as NAME.T with the start field `start`; an older live NAME.T is deleted first
bool WriteDiskFile(EmulatorContext* context, const DiskFileRef& ref, uint16_t start, const std::vector<uint8_t>& bytes, std::string& error);

/// `file` (its name, type and start; length 0: the length field left 0, as XAS saves) onto the disk in `drive`, an
/// older live NAME.T deleted first; for a caller already at a coherent moment of the emulator
bool WriteDiskFileNow(EmulatorContext* context, uint8_t drive, const unrealasm::containers::TrdosFile& file, std::string& error);

/// `extension` (lower case, no dot) is a disk image unreal-disk reads (TRD, SCL, FDI, UDI, TD0, DSK ...) or a tape (TAP, TZX)
bool IsImageExtension(const std::string& extension);
/// The live files of an image file's bytes (`name` gives its extension): a disk image's TR-DOS files through unreal-disk,
/// a tape's through unreal-asm; `context` gives the drivers the machine's settings (may be null)
bool ReadImageFiles(EmulatorContext* context, const std::string& name, const std::vector<uint8_t>& bytes,
                    std::vector<unrealasm::containers::TrdosFile>& out, std::string& error);
