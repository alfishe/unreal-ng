#pragma once

/// @file filetree.h
/// @brief A tree of files whose bytes live elsewhere: the common shape of a
/// source (a host folder, later a FAT or ISO image) and of the union of several
/// sources that a composite medium is built from.
///
/// Nodes sit in one flat vector and refer to each other by index; a node's
/// data says where its bytes are (a host file in the SourcePool, extents of a
/// source device, or nothing). Nothing here reads file contents.
/// Design: docs/inprogress/2026-10-05-media-multisource/tdd.md §2, §3.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

/// A run of whole sectors of a source device holding consecutive bytes of a file
struct Extent
{
    uint64_t sourceLba = 0;        ///< 512-byte sector on the source device
    uint32_t sectors = 0;
    uint32_t fileSectorStart = 0;  ///< the file's sector index at the start of this extent
};
static_assert(sizeof(Extent) == 16, "an extent is 16 bytes (memory budget, tdd.md §12)");

/// Where a file's bytes are
struct FileData
{
    enum class Storage : uint8_t
    {
        Zero,           ///< no stored bytes: reads as zeros (empty or sparse files)
        HostFile,       ///< a host file of the SourcePool, from offset 0
        DeviceExtents,  ///< extents of a SourcePool device
    };
    Storage storage = Storage::Zero;
    uint16_t source = 0;       ///< DeviceExtents: the pool's device index
    uint32_t hostFile = 0;     ///< HostFile: the pool's host-file index
    uint32_t firstExtent = 0;  ///< DeviceExtents: into the tree's extent table
    uint32_t extentCount = 0;
    uint64_t bytes = 0;
};

struct TreeNode
{
    std::string name;                 ///< UTF-8, no path
    uint32_t parent = 0;
    std::vector<uint32_t> children;   ///< directories only, in medium order
    FileData data;                    ///< files only
    int64_t mtimeUtc = 0;
    uint16_t layer = 0;               ///< the layer that provided the node (the first for a merged directory)
    uint8_t attributes = 0;           ///< FAT attribute bits from the source (hidden, read-only, system)
    bool isDirectory = false;
    /// C4b: a directory of a lazily read FAT image whose entries are not in the tree (FatImageExpander reads them);
    /// `baseCluster` is its first cluster in the image
    bool unexpanded = false;
    uint32_t baseCluster = 0;
};

class FileTree
{
public:
    static constexpr uint32_t kRoot = 0;

    FileTree();

    /// Append a node under `parent`; returns its index
    uint32_t Add(uint32_t parent, TreeNode node);

    TreeNode& Node(uint32_t index) { return _nodes[index]; }
    const TreeNode& Node(uint32_t index) const { return _nodes[index]; }
    size_t NodeCount() const { return _nodes.size(); }

    std::vector<Extent>& Extents() { return _extents; }
    const std::vector<Extent>& Extents() const { return _extents; }

    /// The child of `dir` named exactly `name`, or kNone
    uint32_t Child(uint32_t dir, std::string_view name) const;
    /// "/", "/GAMES/sub": the node at a '/'-separated path, or kNone
    uint32_t Find(std::string_view path) const;
    /// "/GAMES/sub" for a node (the root is "/")
    std::string PathOf(uint32_t index) const;

    /// Detach `index` from its parent (the node stays in the vector, unreachable)
    void Detach(uint32_t index);

    /// Copy the subtree of `source` at `from` under `parent`; nodes get `layer`.
    /// Returns the copy's index
    uint32_t CopySubtree(const FileTree& source, uint32_t from, uint32_t parent, uint16_t layer);

    static constexpr uint32_t kNone = 0xFFFFFFFFu;

private:
    std::vector<TreeNode> _nodes;
    std::vector<Extent> _extents;
};
