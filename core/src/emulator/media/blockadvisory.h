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

#include <string>
#include <vector>

class IBlockDevice;

/// Empty when sector 0 is unreadable, ambiguous, or matches what the tags
/// expect. Never throws, never fails the insert - `block`'s read position is
/// unaffected on return (this only reads sector 0)
std::string DescribeBlockLayoutMismatch(IBlockDevice& block, const std::vector<std::string>& tags);
