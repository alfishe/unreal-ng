#pragma once

/// @file isosynthvolume.h
/// @brief An ISO 9660 volume synthesized from a FileTree (the union of a
/// composite's layers): the optical target of composite media. Volume
/// descriptors, path tables and directory records (an ISO tree with level 1
/// or 2 names and, by default, a Joliet tree with long names) are generated at
/// build time; file data is never copied: each file is one extent shared by
/// both trees, read through ExtentReader from where it lives (a host file, a
/// FAT or ISO image). The volume is a cd::IFrameSource; MakeDisc puts it under a
/// CdImage as one Mode 1 track, which the ATAPI drive reads like a pressed CD.
/// Design: docs/inprogress/2026-10-05-media-multisource/phases/c5-iso.md §3.

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "emulator/io/storage/cd/cdimage.h"
#include "emulator/io/storage/compose/extentreader.h"
#include "emulator/io/storage/compose/filetree.h"

class SourcePool;

struct IsoTargetOptions
{
    int level = 1;                         ///< 1: 8.3 names; 2: up to 31 characters
    bool joliet = true;                    ///< add a Joliet tree (long names)
    bool relaxDepth = false;               ///< allow more than 8 directory levels
    std::string volumeId = "UNREAL_NG";    ///< d-characters, up to 32
    std::optional<int64_t> fixedTimeUtc;   ///< tests / reproducible builds: every date this value
};

class IsoSynthVolume : public cd::IFrameSource
{
public:
    static constexpr uint32_t kBlock = 2048;
    /// The largest section of a multi-extent file (4 GiB less one block)
    static constexpr uint64_t kMaxSection = 0xFFFFF800ull;

    /// Nullptr with `error` when the tree does not fit ISO 9660 (depth, size);
    /// `report` lists renamed entries
    static std::unique_ptr<IsoSynthVolume> Build(std::shared_ptr<const FileTree> tree, std::shared_ptr<SourcePool> pool,
                                                 const IsoTargetOptions& options, std::string* error,
                                                 std::vector<std::string>* report);

    /// The volume as a CD: one session, one Mode 1 track over it
    static std::unique_ptr<CdImage> MakeDisc(std::unique_ptr<IsoSynthVolume> volume, std::string description, uint64_t contentId);

    bool Read(uint64_t offset, uint8_t* dst, uint32_t length) override;
    std::string Describe() const override { return "ISO 9660 volume"; }

    uint32_t Blocks() const { return _blocks; }
    uint32_t MetadataBlocks() const { return static_cast<uint32_t>(_metadata.size() / kBlock); }

private:
    IsoSynthVolume(std::shared_ptr<const FileTree> tree, std::shared_ptr<SourcePool> pool);

    struct Run
    {
        uint32_t firstBlock;
        uint32_t blocks;
        uint64_t fileBlockStart;  ///< the file's block index at firstBlock
        uint32_t node;
    };

    friend class IsoLayout;

    bool ReadBlock(uint32_t block, uint8_t* dst);

    std::shared_ptr<const FileTree> _tree;
    std::shared_ptr<SourcePool> _pool;
    ExtentReader _reader;
    std::vector<uint8_t> _metadata;  ///< blocks [0, metadata end): descriptors, path tables, directories
    std::vector<Run> _runs;          ///< sorted by firstBlock
    size_t _lastRun = 0;
    uint32_t _blocks = 0;
};
