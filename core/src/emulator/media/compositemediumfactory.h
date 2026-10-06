#pragma once

/// @file compositemediumfactory.h
/// @brief A composite medium from its descriptor: layers resolved (folder
/// scans with their manifests and filters), merged into one tree, checked
/// against the target, and laid out as a FAT volume whose file data stays in
/// the sources. The result is an ordinary block medium (session / read-only
/// access layer, TTD tap) that every slot takes like any other.
///
/// Phase C2 builds folder layers into a rebuilt FAT16 / FAT32 volume. FAT image
/// layers (C3), graft (C4), ISO (C5) and partitions (C7) fail with
/// NotSupported naming the phase.
/// Design: docs/inprogress/2026-10-05-media-multisource/architecture.md §4,
/// tdd.md §11; target file system: fs-compatibility.md §6 (DT-6).

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "emulator/media/composedescriptor.h"
#include "emulator/media/mediatypes.h"
#include "emulator/media/sessiondelta.h"

class IBlockDevice;
class Medium;
struct OpenRequest;

/// What one layer contributed (the `layers` verb, the build report)
struct CompositeLayerInfo
{
    std::string name;
    std::string kind;        ///< folder | image | iso
    std::string path;        ///< the source, UTF-8
    std::string mount;
    std::string from;
    uint64_t files = 0;      ///< files the layer provided before the merge
    uint64_t bytes = 0;
    uint64_t identity = 0;   ///< the source's identity (folder snapshot)
};

/// One partition of a partitioned composite (phase C7)
struct CompositePartitionInfo
{
    std::string name;
    std::string kind;          ///< image (passthrough) | compose
    std::string fs;            ///< fat12 | fat16 | fat32 | "" (a passthrough of another kind)
    std::string build;         ///< compose: rebuild | graft
    uint8_t type = 0;
    uint64_t start = 0;
    uint64_t sectors = 0;
    size_t firstLayer = 0;     ///< its layers in CompositeInfo::layers
    size_t layerCount = 0;
};

/// The facts of a built composite, kept by the medium
struct CompositeInfo
{
    std::string descriptor;  ///< the file, or "(inline)"
    std::string normalized;  ///< ComposeDescriptor::Normalized()
    FatType fs = FatType::Fat16;   ///< the family: a FAT12 graft base counts as FAT16
    std::string fsName = "fat16";  ///< "fat12", "fat16" or "fat32": the volume's own type
    std::string build = "rebuild"; ///< "rebuild" (FatSynthVolume) or "graft" (GraftVolume onto the bottom image)
    uint64_t sectors = 0;
    uint32_t clusterCount = 0;
    uint32_t sectorsPerCluster = 0;
    uint64_t contentId = 0;
    uint64_t files = 0;      ///< files in the merged tree
    uint64_t bytes = 0;
    uint32_t sourceDevices = 0;  ///< images opened for the layers (one per image and partition, shared)
    std::vector<CompositeLayerInfo> layers;
    std::vector<CompositePartitionInfo> partitions;  ///< a partitioned composite (build "partitions")
    std::filesystem::path delta;   ///< S2: the session delta file (writes.delta, or <descriptor>.delta); empty: none
    std::string writesSave = "delta";  ///< writes.save: the strategy a save runs (DT-9)
};

/// The slot's side of the build (from OpenRequest / InsertOptions)
struct CompositeBuildOptions
{
    std::optional<MediaKind> slotKind;       ///< the slot's kind (block / optical); none: the descriptor decides
    std::vector<FatType> allowedFs;          ///< the slot's fsCompatibility; empty: both
    std::optional<FatType> fs;               ///< an explicit request (`--fs`); else the descriptor, else auto
    FatType defaultFs = FatType::Fat16;      ///< the slot's defaultFs
    bool mbr = true;                         ///< the slot's folderMbr, unless the descriptor says
    std::optional<CodePage> codePage;
    std::optional<uint64_t> freeBytes;
    std::function<bool()> cancelRequested;
    std::function<void(uint64_t, uint64_t)> onProgress;
};

class CompositeMediumFactory
{
public:
    /// Build the volume a descriptor describes. On success `volume` and `info`
    /// are set; `result.report` lists everything left out, shadowed or chosen
    static MediaResult Build(const ComposeDescriptor& descriptor, const CompositeBuildOptions& options,
                             std::unique_ptr<IBlockDevice>& volume, CompositeInfo& info);

    /// The partitions variant (phase C7): a PartitionedDisk of passthrough and composed partitions
    static MediaResult BuildPartitioned(const ComposeDescriptor& descriptor, const CompositeBuildOptions& options,
                                        std::unique_ptr<IBlockDevice>& volume, CompositeInfo& info, MediaResult result);

    /// What a session delta of this composite is written over (S2, DT-13)
    static DeltaIdentity DeltaIdentityOf(const CompositeInfo& info);

    /// The registry's entry: a descriptor file (or inline body) into a medium
    static MediaResult Open(const OpenRequest& request, std::unique_ptr<Medium>& medium);

    /// The target file system for `want` under the slot's rules (DT-6):
    /// candidates in the order they are tried. Empty with `error` when the
    /// explicit choice is one the slot cannot read
    static std::vector<FatType> FsCandidates(std::optional<FatType> want, const std::vector<FatType>& allowed,
                                             FatType defaultFs, std::string* error);
};
