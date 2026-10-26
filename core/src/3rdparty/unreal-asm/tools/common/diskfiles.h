#pragma once

// The files of disk images for the unreal-asm tools (zxasm, symconv) and the emulator's assembler surfaces, through
// unreal-disk's DiskFs: every floppy format it reads (TRD, SCL, FDI, UDI, TD0, DSK, MGT, ...) with any file system it
// reads (TR-DOS, +3DOS, iS-DOS, FAT; writing TR-DOS and +3DOS), and tapes (TAP, TZX) through unreal-asm's tape reader.
// The library itself stays free of both; only the tools link unreal-disk.

#include <string>
#include <vector>

#include "unrealasm/containers.h"

namespace unrealdisk
{
class DiskImage;
}

namespace unrealasm::tools
{
/// `extension` (lower case, no dot) is a disk image or a tape the tools take as a set of files
bool IsImage(const std::string& extension);

/// The live files of a disk's first file system: TR-DOS files with each file's data and the rest of its last sector;
/// +3DOS, iS-DOS and FAT files (paths from the root) with their data, the extension as `extension`
bool ReadDiskFiles(unrealdisk::DiskImage& disk, std::vector<containers::TrdosFile>& out, std::string& error);
/// `file` onto the disk's first file system, an older file of that name deleted first: TR-DOS NAME.T, +3DOS NAME.TYP
/// with a +3DOS header (CODE at the file's start); the disk is changed in place
bool PutDiskFile(unrealdisk::DiskImage& disk, const containers::TrdosFile& file, std::string& error);

/// The live files of the image `name` (its bytes in `bytes`): a disk image's TR-DOS catalog with each file's data and
/// the rest of its last sector, a +3DOS disk's files (the data after the header, the type as `extension`), or a tape's
/// files; false with the reason
bool ReadImageFiles(const std::string& name, const std::vector<uint8_t>& bytes, std::vector<containers::TrdosFile>& out, std::string& error);

/// `file` written onto the disk image at `path` (an older NAME.T deleted first, as TR-DOS does; on a +3DOS disk NAME.TYP
/// with a +3DOS header) and the image saved to `output` in the format its extension names; a missing `path` starts from
/// a blank 80-track double-sided TR-DOS disk
bool PutImageFile(const std::string& path, const std::string& output, const containers::TrdosFile& file, std::string& error);
}  // namespace unrealasm::tools
