#pragma once

/// @file blockadvisory.h
/// @brief A non-blocking sanity check for a block medium (SD card, IDE hard
/// disk) right after it is opened: does sector 0 look like the layout this
/// slot's boot path actually reads? Mounting is never refused over this -
/// only advisory text, into the medium's Report() (media-drop-targets design,
/// docs/inprogress/2026-09-29-media-drop-targets/design.md "Boot compatibility
/// advisory"). Two concrete layouts this repo has hit confusion between:
/// - an IDE hard-disk unit (`tags` has "ide" and "hdd") boots from an MBR
///   partition table with FAT partitions (built by e.g. the NedoOS `hddfdisk`
///   tool, or xBIOS/ERS's own partitioner)
/// - a Z-Controller / NeoGS SD slot (`tags` has "sd") reads a FAT/FAT32
///   filesystem directly from sector 0, no MBR
/// Confirmed both ways empirically 2026-09-30: a raw FAT32 TS-Conf SD image
/// mounted on ATM3's `ide0.master` boots partway then parks in DI+HALT
/// (testdata/machines/tsconf/wildcommander/README.md); an MBR+FAT NedoOS HDD
/// image on the same slot drives real, varied `READ SECTORS` traffic
/// (testdata/machines/baseconf/hdd-images/README.md).

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "emulator/media/mediatypes.h"
#include "emulator/io/storage/iblockdevice.h"


/// What sector 0 of a block image holds, by the same rules as the advisory:
/// a FAT boot sector (the SD card layout) or an MBR partition table (the
/// IDE hard-disk layout). Used before a medium is open (MediaTargets::Classify)
enum class SectorZeroLayout : uint8_t
{
    Unknown,
    FatVolume,
    PartitionTable,
};
SectorZeroLayout ClassifySectorZero(const uint8_t* sector, size_t size);

/// Empty when sector 0 is unreadable, ambiguous, or matches what the tags
/// expect. Never throws, never fails the insert - `block`'s read position is
/// unaffected on return (this only reads sector 0)
std::string DescribeBlockLayoutMismatch(IBlockDevice& block, const std::vector<std::string>& tags);

/// The FAT flavour of a block medium: the BPB of sector 0 when it is a FAT
/// volume, or of the first FAT partition when sector 0 holds an MBR. nullopt
/// when this is no FAT volume this build knows (or unreadable) - deciding
/// what to do with the answer is the caller's business (BUGS.md #1: a slot
/// whose controller reads FAT32 only refuses a FAT16 image)
std::optional<FatType> ProbeFatType(IBlockDevice& block);

/// One FAT volume of a block medium (ProbeFatVolumes)
struct FatVolumeProbe
{
    uint32_t partition = 0;  ///< 0: the volume starts at sector 0; 1-4 primary, 5+ logical (as `partition: N`)
    FatType fs = FatType::Fat16;
};

/// Every FAT volume of a block medium: the one at sector 0, or each FAT partition of its MBR, primary and logical
/// (the extended partition's chain). Unreadable and non-FAT entries are left out
std::vector<FatVolumeProbe> ProbeFatVolumes(IBlockDevice& block);
