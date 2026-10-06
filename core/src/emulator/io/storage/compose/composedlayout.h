#pragma once

/// @file composedlayout.h
/// @brief Provenance of a composite volume's sectors: what each LBA holds
/// (partition table, volume header, FAT, a directory, a file's data, free
/// space) and, for directories and files, the union tree node it belongs to
/// and that node's layer. Answered from the run tables the read path already
/// has: nothing is stored per sector.
/// Design: docs/inprogress/2026-10-05-media-multisource/phases/c6-provenance-flatten.md §3.

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>

#include "emulator/io/storage/changeview.h"
#include "emulator/io/storage/compose/filetree.h"

class SourcePool;

enum class SectorRole : uint8_t
{
    PartitionTable,  ///< LBA 0 of a volume with an MBR
    BootArea,        ///< between the MBR and the partition (a loader there)
    VolumeHeader,    ///< the boot sector and the other reserved sectors
    Fat,             ///< any FAT copy
    Directory,       ///< a directory's entries (the FAT12 / FAT16 root region too)
    FileData,        ///< a file's bytes
    Free,            ///< a cluster no file or directory uses
};

struct SectorOwner
{
    SectorRole role = SectorRole::Free;
    /// Directory / FileData: the union tree node (FileTree::kNone when the volume
    /// does not know it)
    uint32_t node = FileTree::kNone;
    uint16_t layer = 0;    ///< the node's layer
    /// Directory: the directory's own first cluster; FileData: its parent
    /// directory's. 0 is the root on every FAT type
    uint32_t dirCluster = 0;
    uint64_t offset = 0;   ///< Directory / FileData: the byte offset of the sector in it
    bool patched = false;  ///< graft: a sector re-encoded over the base (directory, FAT, FSInfo, boot)
    /// C4b: a directory or file of a graft's base that the build never read into the tree (an untouched
    /// directory): no node, but `layer` (0), `dirCluster` and `offset` hold
    bool unlisted = false;

    bool HasNode() const { return node != FileTree::kNone; }
    /// The layer and directory are known (a tree node, or an unlisted base entry)
    bool Known() const { return HasNode() || unlisted; }
};

class IComposedLayout
{
public:
    virtual ~IComposedLayout() = default;

    /// What the sector at `lba` holds as the volume was built (guest writes aside)
    virtual SectorOwner OwnerOf(uint64_t lba) const = 0;
    /// The union tree the volume was built from (T0)
    virtual const FileTree& Tree() const = 0;
    /// The sources the tree's data names (host files as scanned: S4 conflict checks)
    virtual const SourcePool* Pool() const { return nullptr; }
};

/// A layout seen from a window that starts `offset` sectors into it (a partition cut out of a graft over an image
/// partition)
class OffsetLayout : public IComposedLayout
{
public:
    OffsetLayout(const IComposedLayout* inner, uint64_t offset) : _inner(inner), _offset(offset) {}
    SectorOwner OwnerOf(uint64_t lba) const override { return _inner->OwnerOf(lba + _offset); }
    const FileTree& Tree() const override { return _inner->Tree(); }
    const SourcePool* Pool() const override { return _inner->Pool(); }

private:
    const IComposedLayout* _inner;
    uint64_t _offset;
};

/// The owners of a change layer's sectors, in LBA order
inline void ForEachChangedOwner(const IComposedLayout& layout,
                                const IChangeView& changes,
                                const std::function<void(uint64_t, const SectorOwner&)>& visit)
{
    for (std::optional<uint64_t> lba = changes.NextChanged(0); lba; lba = changes.NextChanged(*lba + 1))
        visit(*lba, layout.OwnerOf(*lba));
}
