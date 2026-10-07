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
#include <unordered_map>
#include <utility>
#include <vector>

#include "common/unicodehelper.h"
#include "emulator/io/storage/compose/composedlayout.h"
#include "emulator/io/storage/compose/extentreader.h"
#include "emulator/io/storage/compose/filetree.h"
#include "emulator/io/storage/fat/fatsynthvolume.h"
#include "emulator/io/storage/fat/fatvolumereader.h"
#include "emulator/io/storage/iblockdevice.h"

class SourcePool;

struct GraftOptions
{
    CodePage codePage = CodePage::Cp866;   ///< the base's short names (new names are made in it)
    std::optional<uint32_t> partition;      ///< the base layer's explicit MBR partition (1-4)
    std::optional<int64_t> fixedTimeUtc;    ///< tests: every new entry's time
    /// The descriptor's boot section (D-6): patched over the base's own boot structures
    std::shared_ptr<const FatBootPlan> boot;
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

class GraftVolume : public IBlockDevice, public IComposedLayout
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
    /// The base's own runs, cut at the next patched sector or grafted run (C10)
    uint64_t ZeroRun(uint64_t lba) override;
    std::optional<BlockGeometry> NativeGeometry() const override;
    std::string Describe() const override;
    uint64_t ContentId() const override { return _contentId; }

    FatReaderType Type() const { return _type; }
    /// Where the volume starts on the base image, and its size by its BPB (a partition of an MBR image: C7 cuts it out)
    uint64_t VolumeStart() const { return _volumeStart; }
    uint64_t VolumeSectors() const { return _volumeSectors; }
    /// Directories re-encoded (touched or new): tests of the build cost
    uint32_t DirectoriesEncoded() const { return _directoriesEncoded; }
    uint32_t FilesGrafted() const { return _filesGrafted; }
    size_t PatchedSectors() const { return _patchLba.size(); }
    size_t GraftRuns() const { return _runs.size(); }
    uint32_t FreeClusters() const { return _freeClusters; }
    GraftSectorOrigin SectorOrigin(uint64_t lba) const;

    /// S3 commit plan: the re-encoded sectors (sorted), and the runs of sectors holding grafted files
    /// (first LBA, count), in the base image's coordinates
    const std::vector<uint64_t>& PatchLbas() const { return _patchLba; }
    std::vector<std::pair<uint64_t, uint64_t>> GraftedSectorRuns() const;
    uint16_t BaseDevice() const { return _baseDevice; }

    /// Provenance (C6b). A base file's data is found from the union tree's extents on the
    /// base image (an index built at the first call that needs it). C4b: the directories and files
    /// under base directories the build did not read are indexed from the image at the first call
    /// that needs them (SectorOwner::unlisted)
    SectorOwner OwnerOf(uint64_t lba) const override;
    const FileTree& Tree() const override { return *_tree; }
    const SourcePool* Pool() const override { return _pool.get(); }

private:
    GraftVolume(std::shared_ptr<const FileTree> tree, std::shared_ptr<SourcePool> pool, uint16_t baseDevice);

    struct Run
    {
        uint32_t firstCluster;
        uint32_t clusters;
        uint32_t fileClusterStart;  ///< the file's cluster index at firstCluster
        uint32_t node;              ///< the union node whose data the run holds
    };

    /// A cluster of a directory (base or new), in cluster order
    struct DirCluster
    {
        uint32_t cluster;
        uint32_t firstCluster;  ///< the directory's own first cluster (the root: 0)
        uint32_t node;          ///< the union directory node
    };
    /// A run of a base file's sectors on the volume's LBAs
    struct BaseRun
    {
        uint64_t lba;
        uint32_t sectors;
        uint32_t fileSectorStart;
        uint32_t node;
    };

    friend class GraftBuilder;

    void IndexBaseFiles() const;
    void IndexUnexpanded() const;

    /// C4b: a cluster of a directory under an untouched base directory
    struct LazyDir
    {
        uint32_t cluster;
        uint32_t firstCluster;  ///< the directory's own first cluster
        uint32_t index;         ///< the cluster's place in the directory's chain
    };
    /// C4b: a run of a file under an untouched base directory, on the volume's LBAs
    struct LazyRun
    {
        uint64_t lba;
        uint32_t sectors;
        uint32_t fileSectorStart;
        uint32_t dirCluster;    ///< its directory's first cluster
    };

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
    std::vector<DirCluster> _dirClusters;  ///< sorted by cluster
    std::unordered_map<uint32_t, uint32_t> _dirClusterOfNode;  ///< directory node -> first cluster (root: 0)
    mutable std::vector<BaseRun> _baseRuns;  ///< sorted by lba; built on demand
    mutable bool _baseIndexed = false;
    std::vector<uint32_t> _unexpanded;           ///< C4b: first clusters of the base directories kept unread
    mutable std::vector<LazyDir> _lazyDirs;      ///< sorted by cluster; built on demand
    mutable std::vector<LazyRun> _lazyRuns;      ///< sorted by lba; built on demand
    mutable bool _lazyIndexed = false;
    CodePage _codePage = CodePage::Cp866;
    uint16_t _baseDevice = 0;
    int _extentDevice = -1;             ///< the pool device the base files' extents name (the image, or its partition window)
    uint64_t _extentOffset = 0;         ///< the LBA of that device's sector 0 on the image
    uint64_t _volumeStart = 0;          ///< absolute LBA of the volume's boot sector
    uint64_t _fatStart = 0;             ///< absolute LBA of the first FAT
    uint64_t _rootStart = 0;            ///< absolute LBA after the FATs (the FAT12 / FAT16 root region)
    uint64_t _volumeSectors = 0;        ///< the volume's size by its BPB
    mutable size_t _lastRun = 0;

    uint64_t _contentId = 0;
    std::string _description;
    uint32_t _directoriesEncoded = 0;
    uint32_t _filesGrafted = 0;
    uint32_t _freeClusters = 0;
};
