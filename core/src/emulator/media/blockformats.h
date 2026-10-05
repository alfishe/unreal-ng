#pragma once

/// @file blockformats.h
/// @brief Writing block media (SD cards, hard disks) to image files, for
/// `export` and `save`. The target's extension picks the format:
///
/// | Target | Written as |
/// |---|---|
/// | `.chd` | a CHD v5: compressed with `compression` (default: the source CHD's codecs, else chdman's lzma, zlib, huff, flac), or uncompressed (`none`); a child of `parent` when one is named |
/// | anything else | a raw image: sector n at byte n × 512 |
///
/// A save writes the medium back into its own file and empties the change
/// layer: sector by sector for the raw family (raw, HDF, HDI, fixed VHD), the
/// whole file again for a CHD (unchanged hunks keep their stored bytes; a
/// child stays a child of the same parent). Every whole-file write goes to a
/// temporary file renamed over the target, so a failed write never leaves a
/// half-written image. MediaManager calls this; slots never see a format.

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>
#include <string>

#include "emulator/media/mediatypes.h"

class ChdImage;
class IBlockDevice;
class Medium;

struct BlockWriteOptions
{
    /// CHD codecs: "none", "default", or up to four of zlib, lzma, huff, flac,
    /// zstd ("lzma,zlib"). Empty: the source CHD's, else chdman's default
    std::string compression;
    /// CHD: write a child of this CHD file (only hunks that differ are stored)
    std::string parent;
    /// S1 compact: write a re-synthesized FAT volume (every file contiguous) instead of the layout as it is
    bool compact = false;
    std::optional<FatType> fs;             ///< compact: the target's FAT type (default: the volume's)
    std::optional<uint64_t> size;          ///< compact: total bytes (default: the medium's, or the content's)
};

class BlockFormats
{
public:
    /// The merged FAT volume on `device`, re-synthesized (S1 compact): the volume read through
    /// FatImageSource becomes the single layer of a new FatSynthVolume. Its label, MBR and boot
    /// structures are carried. `device` must outlive `volume`
    static MediaResult Compact(IBlockDevice& device, const BlockWriteOptions& options, std::unique_ptr<IBlockDevice>& volume);

    /// The format a target path is written in: "chd" for `.chd`, "vhd" for `.vhd`, else "raw"
    static std::string WriterFor(const std::string& path);

    /// Write `device` as the guest sees it to `path` (export). `unchanged(h)`
    /// tells a CHD writer that hunk h is the source CHD's hunk h as stored
    static MediaResult Write(IBlockDevice& device, const std::string& path, const BlockWriteOptions& options,
                             const std::function<bool(uint64_t firstSector, uint64_t count)>& unchanged = {});

    /// Save a block medium into its own image file (`target` empty) or into
    /// `target`, which it then stands for. The change layer is empty afterwards.
    /// The emulator must not be running
    static MediaResult Save(Medium& medium, const std::string& target, const BlockWriteOptions& options, std::string& savedPath);

    /// The CHD at the bottom of a block stack (under the change layer or a
    /// read-only guard), nullptr when the medium is no CHD
    static ChdImage* FindChd(IBlockDevice* device);
};
