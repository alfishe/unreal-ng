#pragma once

/// @file graftvolume.h
/// @brief A composite medium built on a FAT disk image as its base ("graft"):
/// the image as it is, with the upper layers' files placed in its free
/// clusters and its directories and FAT patched. Every sector outside the
/// patches and the grafted clusters reads straight from the base, so the MBR,
/// loaders in reserved sectors, boot code and system files at fixed places
/// keep working. The base is only read; guest writes go to the composite's
/// change layer as for every composite.
/// Design: docs/inprogress/2026-10-05-media-multisource/phases/c4-graft.md.

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "common/unicodehelper.h"
#include "emulator/io/storage/compose/extentreader.h"
#include "emulator/io/storage/compose/filetree.h"
#include "emulator/io/storage/fat/fatvolumereader.h"
#include "emulator/io/storage/iblockdevice.h"

class SourcePool;

struct GraftOptions
{
    CodePage codePage = CodePage::Cp866;   ///< the base's short names (new names are made in it)
    std::optional<uint32_t> partition;      ///< the base layer's explicit MBR partition (1-4)
    std::optional<int64_t> fixedTimeUtc;    ///< tests: every new entry's time
};

/// Why a graft was not built: DT-4 falls back to a rebuild on these when the
/// descriptor says `build: auto`
/// Where a sector of a graft volume comes from (tests; provenance in phase C6)
enum class GraftSectorOrigin : uint8_t
{
    Base,    ///< the base image, unchanged
    Patch,   ///< a patched FAT, directory or FSInfo sector
    Graft,   ///< a cluster holding an upper layer's file
};

enum class GraftFailure : uint8_t
{
    None,
    BaseNotFat,   ///< the base has no FAT volume (or not the partition asked for)
    DoesNotFit,   ///< not enough free clusters, or a full FAT12 / FAT16 root
    Broken,       ///< the base's directories or FAT cannot be read
};

class GraftVolume : public IBlockDevice
{
public:
    /// Graft `tree` (the union, the base being its layer 0) onto the image
    /// `baseDevice` of `pool`. Nullptr with `error` and `failure` when it cannot
    /// be built; `report` lists names that could not be stored
    static std::unique_ptr<GraftVolume> Build(std::shared_ptr<const FileTree> tree, std::shared_ptr<SourcePool> pool,
                                              uint16_t baseDevice, const GraftOptions& options, uint64_t sourceIdentity,
                                              std::string description, std::string* error, std::vector<std::string>* report,
                                              GraftFailure* failure = nullptr);

    uint64_t SectorCount() const override;
    bool ReadSector(uint64_t lba, uint8_t* dst) override;
    bool WriteSector(uint64_t, const uint8_t*) override { return false; }
    bool IsWritable() const override { return false; }
    std::optional<BlockGeometry> NativeGeometry() const override;
    std::string Describe() const override;
    uint64_t ContentId() const override { return _contentId; }

    FatReaderType Type() const { return _type; }
    /// Directories re-encoded (touched or new): tests of the build cost
    uint32_t DirectoriesEncoded() const { return _directoriesEncoded; }
    uint32_t FilesGrafted() const { return _filesGrafted; }
    size_t PatchedSectors() const { return _patchLba.size(); }
    size_t GraftRuns() const { return _runs.size(); }
    uint32_t FreeClusters() const { return _freeClusters; }
    GraftSectorOrigin SectorOrigin(uint64_t lba) const;

private:
    GraftVolume(std::shared_ptr<const FileTree> tree, std::shared_ptr<SourcePool> pool, uint16_t baseDevice);

    struct Run
    {
        uint32_t firstCluster;
        uint32_t clusters;
        uint32_t fileClusterStart;  ///< the file's cluster index at firstCluster
        uint32_t node;              ///< the union node whose data the run holds
    };

    friend class GraftBuilder;

    /// The grafted run holding `lba`, or nullptr (updates the last-hit index)
    const Run* FindRun(uint64_t lba, uint64_t& cluster) const;

    std::shared_ptr<const FileTree> _tree;
    std::shared_ptr<SourcePool> _pool;
    IBlockDevice* _base = nullptr;
    ExtentReader _reader;

    FatReaderType _type = FatReaderType::Fat16;
    uint64_t _dataStart = 0;            ///< absolute LBA of cluster 2
    uint32_t _sectorsPerCluster = 1;
    uint32_t _clusterCount = 0;

    std::vector<uint64_t> _patchLba;    ///< sorted
    std::vector<uint8_t> _patchData;    ///< 512 bytes per patched sector, in _patchLba order
    std::vector<Run> _runs;             ///< sorted by firstCluster
    mutable size_t _lastRun = 0;

    uint64_t _contentId = 0;
    std::string _description;
    uint32_t _directoriesEncoded = 0;
    uint32_t _filesGrafted = 0;
    uint32_t _freeClusters = 0;
};
