#pragma once

/// @file commitjournal.h
/// @brief The undo journal of an S3 commit (a composite's sectors written into
/// its graft base image): the old content of every sector about to be written
/// goes to `<image>.ujournal` and is synced before the image is touched, and
/// the journal is removed once the image is written and synced. A journal met
/// when the image is opened again means the commit was interrupted: a complete
/// journal puts the old sectors back; an unfinished one means the image was
/// not touched yet.
///
/// File: "UNGJRNL1", u64 image sector count, u64 entry count; per entry u64
/// lba and the 512 old bytes; "UNGJEND!" and the FNV-1a hash of the entries.
/// Design: docs/inprogress/2026-10-05-media-multisource/phases/c8-commit-writeback.md §2 (DT-14).

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

class IBlockDevice;

class CommitJournal
{
public:
    enum class Recovery : uint8_t
    {
        None,        ///< no journal
        RolledBack,  ///< an interrupted commit: the old sectors are back
        Dropped,     ///< an unfinished journal: the image had not been touched
        Damaged,     ///< a finished journal that does not check out: kept as *.ujournal.bad, the image as it is
    };

    static std::filesystem::path PathFor(const std::filesystem::path& image);

    /// The old content of `lbas` of `image` (opened on `device`) into the journal, synced
    static bool Write(const std::filesystem::path& image, IBlockDevice& device, const std::vector<uint64_t>& lbas, std::string* error);

    /// The journal is no longer needed (the commit is written and synced)
    static void Remove(const std::filesystem::path& image);

    /// Before `image` is opened: undo an interrupted commit. `detail` is a report line when not None
    static Recovery Recover(const std::filesystem::path& image, std::string* detail);

    /// Push a file's written data to the disk (fsync / _commit)
    static bool Sync(const std::filesystem::path& file);
};
