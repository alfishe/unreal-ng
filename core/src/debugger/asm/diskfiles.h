#pragma once

/// @file diskfiles.h
/// @brief Files on the disks in the machine's drives for the assembler-source and symbol surfaces (unreal-asm tdd §7):
/// "disk:A/NAME.T" names the last live TR-DOS catalog entry NAME of type T on drive A. Reading goes through the
/// library's TR-DOS reader (the same catalog hints and sector slack as zxasm); writing adds a catalog entry like TR-DOS
/// does, deleting an older live file of that name and type first, refused on a write-protected disk, and leaves the
/// image modified. Both run at a coherent moment of the emulator (paused, stopped or between frames).

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
