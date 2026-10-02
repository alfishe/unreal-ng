#pragma once

/// @file chdfile.h
/// @brief Reading a CHD file (MAME's "Compressed Hunks of Data"): header v3, v4
/// and v5, the hunk map (v5 compressed or not), metadata, and parent CHDs.
///
/// A CHD stores a disk as hunks (4 KB for chdman's hard disks). Each hunk is
/// compressed with one of up to four codecs, stored as is, a copy of an
/// earlier hunk (self), a copy of the parent CHD's data (parent), or - in an
/// uncompressed v5 file - absent: zero, or the parent's. Every v5 hunk read
/// is checked against its CRC-16 (compressed files), v3 / v4 against CRC-32.
///
/// A child names its parent by the parent's SHA-1. Open finds it among the
/// `.chd` files in the child's folder (MAME takes it from the romset), unless
/// the caller passes one.
///
/// Format: docs/file-formats/disk-images/chd.md; design:
/// docs/inprogress/2026-10-02-media-chd/design.md.

#include <array>
#include <cstdint>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "emulator/io/storage/chd/chdcodec.h"
#include "emulator/io/storage/chd/chdutil.h"

namespace chd
{
    constexpr uint32_t kTagHardDisk = MakeTag('G', 'D', 'D', 'D');   ///< "CYLS:%d,HEADS:%d,SECS:%d,BPS:%d"
    constexpr uint32_t kTagIdentify = MakeTag('I', 'D', 'N', 'T');   ///< the ATA IDENTIFY block
    constexpr uint8_t kMetadataChecksum = 0x01;                     ///< the entry counts in the overall SHA-1

    struct MetadataEntry
    {
        uint32_t tag = 0;
        uint8_t flags = 0;
        std::vector<uint8_t> data;
    };

    /// Where a hunk's data is (the v5 map, expanded)
    struct HunkEntry
    {
        enum class Type : uint8_t
        {
            Codec,         ///< compressed with codecs[codec]
            Uncompressed,  ///< stored as is
            Self,          ///< same as hunk `offset`
            Parent,        ///< the parent's data from unit `offset` (v5) / hunk `offset` (v3 / v4)
            Zero,          ///< uncompressed v5 without a parent: never written, all zero
            Mini,          ///< v3 / v4: the 8 bytes of `offset` repeated
        };
        Type type = Type::Zero;
        uint8_t codec = 0;
        uint32_t length = 0;  ///< bytes in the file
        uint64_t offset = 0;
        uint32_t crc = 0;     ///< CRC-16 (v5) / CRC-32 (v3 / v4) of the data
        bool hasCrc = false;
    };

    class ChdFile
    {
    public:
        /// Find a parent by its SHA-1; nullptr when there is none
        using ParentFinder = std::function<std::unique_ptr<ChdFile>(const Sha1& sha1, const std::string& childPath, std::string* error)>;

        /// Open `path`. A child's parent comes from `finder`, by default from the
        /// child's folder. Fails (nullptr, reason in `error`) on anything not a
        /// readable CHD, an unsupported codec, a broken map or a missing parent
        static std::unique_ptr<ChdFile> Open(const std::string& path, std::string* error = nullptr, ParentFinder finder = {});
        /// Open a CD-ROM CHD (CHT2 / CHTR track metadata, 2448-byte frames, the
        /// CD codecs). A hard-disk CHD is refused, as Open refuses a CD
        static std::unique_ptr<ChdFile> OpenCd(const std::string& path, std::string* error = nullptr);

        /// The default parent search: every *.chd in the child's folder
        static std::unique_ptr<ChdFile> FindParentNextTo(const Sha1& sha1, const std::string& childPath, std::string* error);

        /// The header of a CHD file without its map: version and the overall
        /// SHA-1 (parent search, probes). False when the file is no CHD
        static bool PeekHeader(const std::string& path, uint32_t& version, Sha1& sha1);

        ~ChdFile();
        ChdFile(const ChdFile&) = delete;
        ChdFile& operator=(const ChdFile&) = delete;

        const std::string& Path() const { return _path; }
        uint32_t Version() const { return _version; }
        uint64_t LogicalBytes() const { return _logicalBytes; }
        uint32_t HunkBytes() const { return _hunkBytes; }
        uint32_t UnitBytes() const { return _unitBytes; }
        uint32_t HunkCount() const { return _hunkCount; }
        const CodecList& Codecs() const { return _codecs; }
        bool Compressed() const { return _codecs[0] != kCodecNone; }
        uint64_t FileBytes() const { return _fileBytes; }

        const Sha1& RawSha1() const { return _rawSha1; }
        const Sha1& OverallSha1() const { return _sha1; }
        const Sha1& ParentSha1() const { return _parentSha1; }
        ChdFile* Parent() const { return _parent.get(); }

        const std::vector<MetadataEntry>& Metadata() const { return _metadata; }
        /// The first entry with `tag` as text (a trailing NUL dropped)
        std::optional<std::string> MetadataText(uint32_t tag) const;

        /// The map entry of a hunk
        const HunkEntry& Entry(uint32_t hunk) const { return _map[hunk]; }

        /// Read one hunk (HunkBytes() bytes). False with a reason on an I/O
        /// error, corrupt data or a CRC mismatch
        bool ReadHunk(uint32_t hunk, uint8_t* dst, std::string* error = nullptr);
        /// Read logical bytes (any range inside LogicalBytes(); hunks decoded through a one-hunk cache)
        bool ReadBytes(uint64_t offset, uint8_t* dst, uint32_t length, std::string* error = nullptr);
        /// Raw file bytes (a hunk's stored payload, for a writer that copies it)
        bool ReadFileBytes(uint64_t offset, uint8_t* dst, uint32_t length);

        /// A CD-ROM CHD (opened with OpenCd)
        bool IsCd() const { return _cdRom; }

        /// Recompute the SHA-1 of the data and compare it with the header (a
        /// compressed CHD; an uncompressed one has no checksum: true). Also the
        /// overall SHA-1 over data and metadata (v4, v5)
        bool Verify(std::string* error = nullptr);

        /// The overall SHA-1 for a raw-data SHA-1: SHA-1 of that hash and the
        /// sorted (tag, SHA-1) pairs of every checksummed metadata entry
        static Sha1 OverallSha1(const Sha1& raw, const std::vector<MetadataEntry>& metadata);

    private:
        ChdFile() = default;
        static std::unique_ptr<ChdFile> OpenAs(const std::string& path, std::string* error, ParentFinder finder, bool cdRom);
        static std::unique_ptr<ChdFile> FindParent(const Sha1& sha1, const std::string& childPath, std::string* error, bool cdRom);
        bool ParseHeader(const uint8_t* header, std::string* error);
        bool ReadMap(std::string* error);
        bool ReadCompressedMap(std::string* error);
        bool ReadMetadata(std::string* error);
        uint32_t GuessUnitBytes() const;
        bool Fail(std::string* error, const std::string& reason) const;

        std::string _path;
        std::ifstream _file;
        uint64_t _fileBytes = 0;
        uint32_t _version = 0;
        uint64_t _logicalBytes = 0;
        uint64_t _mapOffset = 0;
        uint64_t _metaOffset = 0;
        uint32_t _hunkBytes = 0;
        uint32_t _unitBytes = 0;
        uint32_t _hunkCount = 0;
        CodecList _codecs{};
        Sha1 _rawSha1{};
        Sha1 _sha1{};
        Sha1 _parentSha1{};
        bool _hasParent = false;
        std::vector<HunkEntry> _map;
        std::vector<MetadataEntry> _metadata;
        std::array<std::unique_ptr<Codec>, 4> _decoders;
        std::unique_ptr<ChdFile> _parent;
        std::vector<uint8_t> _compressed;  ///< scratch for one stored hunk
        std::vector<uint8_t> _cache;       ///< ReadBytes' last hunk
        int64_t _cacheHunk = -1;
        bool _cdRom = false;               ///< opened as a CD-ROM CHD
        int _selfDepth = 0;                ///< guards a self reference loop in a broken file
    };
}  // namespace chd
