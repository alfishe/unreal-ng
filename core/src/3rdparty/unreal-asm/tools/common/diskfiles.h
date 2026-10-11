#pragma once

// The files of disk images for the unreal-asm tools (zxasm, symconv), through unreal-disk: every floppy format it reads
// (TRD, SCL, FDI, UDI, TD0, DSK, MGT, ...) with the TR-DOS file system, and tapes (TAP, TZX) through unreal-asm's tape
// reader. The library itself stays free of both; only the tools link unreal-disk.

#include <string>
#include <vector>

#include "unrealasm/containers.h"

namespace unrealasm::tools
{
/// `extension` (lower case, no dot) is a disk image or a tape the tools take as a set of files
bool IsImage(const std::string& extension);

/// The live files of the image `name` (its bytes in `bytes`): a disk image's TR-DOS catalog with each file's data and
/// the rest of its last sector, or a tape's files; false with the reason
bool ReadImageFiles(const std::string& name, const std::vector<uint8_t>& bytes, std::vector<containers::TrdosFile>& out, std::string& error);

/// `file` written onto the disk image at `path` (an older NAME.T deleted first, as TR-DOS does) and the image saved to
/// `output` in the format its extension names; a missing `path` starts from a blank 80-track double-sided TR-DOS disk
bool PutImageFile(const std::string& path, const std::string& output, const containers::TrdosFile& file, std::string& error);
}  // namespace unrealasm::tools
