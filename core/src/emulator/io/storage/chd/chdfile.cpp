#include "stdafx.h"

#include "chdfile.h"

#include <algorithm>
#include <filesystem>
#include <system_error>

#include "common/filehelper.h"
#include "common/stringhelper.h"
#include "emulator/io/storage/chd/chdhuffman.h"

namespace chd
{
    namespace
    {
        constexpr uint32_t kV3HeaderBytes = 120;
        constexpr uint32_t kV4HeaderBytes = 108;
        constexpr uint32_t kV5HeaderBytes = 124;
        constexpr uint32_t kMetadataHeaderBytes = 16;
        constexpr int kMaxParentDepth = 8;

        /// v5 map entry types; 7.. only appear inside the compressed map
        enum : uint8_t
        {
            kType0 = 0,
            kType3 = 3,
            kTypeNone = 4,
            kTypeSelf = 5,
            kTypeParent = 6,
            kRleSmall = 7,
            kRleLarge = 8,
            kSelf0 = 9,
            kSelf1 = 10,
            kParentSelf = 11,
            kParent0 = 12,
            kParent1 = 13,
        };

        /// v3 / v4 map entry types (low nibble of the flags byte)
        enum : uint8_t
        {
            kV34Compressed = 1,
            kV34Uncompressed = 2,
            kV34Mini = 3,
            kV34Self = 4,
            kV34Parent = 5,
            kV34SecondCompressed = 6,
        };
        constexpr uint8_t kV34NoCrc = 0x10;

        std::string Utf8(const std::filesystem::path& path)
        {
            const auto text = path.u8string();
            return std::string(text.begin(), text.end());
        }

        bool IsCdTag(uint32_t tag)
        {
            return tag == MakeTag('C', 'H', 'C', 'D') || tag == MakeTag('C', 'H', 'T', 'R') || tag == MakeTag('C', 'H', 'T', '2') ||
                   tag == MakeTag('C', 'H', 'G', 'T') || tag == MakeTag('C', 'H', 'G', 'D') || tag == MakeTag('D', 'V', 'D', ' ');
        }

        thread_local int t_parentDepth = 0;
    }  // namespace

    bool ChdFile::Fail(std::string* error, const std::string& reason) const
    {
        if (error)
            *error = _path + ": " + reason;
        return false;
    }

    /// region <Opening>

    bool ChdFile::PeekHeader(const std::string& path, uint32_t& version, Sha1& sha1)
    {
        std::ifstream in(FileHelper::ToFsPath(path), std::ios::binary);
        uint8_t header[kV5HeaderBytes] = {};
        if (!in)
            return false;
        in.read(reinterpret_cast<char*>(header), sizeof(header));
        if (in.gcount() < 16 || std::memcmp(header, "MComprHD", 8) != 0)
            return false;
        version = Be32(header + 12);
        const size_t at = version == 3 ? 80 : version == 4 ? 48 : version == 5 ? 84 : 0;
        if (!at || static_cast<size_t>(in.gcount()) < at + 20)
            return false;
        std::memcpy(sha1.data(), header + at, 20);
        return true;
    }

    std::unique_ptr<ChdFile> ChdFile::FindParentNextTo(const Sha1& sha1, const std::string& childPath, std::string* error)
    {
        const std::filesystem::path child = FileHelper::ToFsPath(childPath);
        std::error_code ec;
        const std::filesystem::path folder = child.has_parent_path() ? child.parent_path() : std::filesystem::path(".");
        for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec))
        {
            std::error_code fileError;
            if (!it->is_regular_file(fileError))
                continue;
            const std::string name = Utf8(it->path());
            if (StringHelper::ToLower(FileHelper::GetFileExtension(name)) != "chd" ||
                std::filesystem::equivalent(it->path(), child, fileError))
                continue;
            uint32_t version = 0;
            Sha1 candidate{};
            if (PeekHeader(name, version, candidate) && candidate == sha1)
                return Open(name, error);
        }
        if (error)
            *error = childPath + ": needs its parent CHD (SHA-1 " + Sha1Hex(sha1) + "): put the parent file in the same folder";
        return nullptr;
    }

    std::unique_ptr<ChdFile> ChdFile::Open(const std::string& path, std::string* error, ParentFinder finder)
    {
        std::unique_ptr<ChdFile> chd(new ChdFile());
        chd->_path = path;

        const std::filesystem::path fsPath = FileHelper::ToFsPath(path);
        std::error_code ec;
        chd->_fileBytes = std::filesystem::file_size(fsPath, ec);
        if (ec)
        {
            chd->Fail(error, "cannot read the file size");
            return nullptr;
        }
        chd->_file.open(fsPath, std::ios::binary);
        if (!chd->_file)
        {
            chd->Fail(error, "cannot open");
            return nullptr;
        }

        uint8_t header[kV5HeaderBytes] = {};
        chd->_file.read(reinterpret_cast<char*>(header), sizeof(header));
        const size_t got = static_cast<size_t>(chd->_file.gcount());
        chd->_file.clear();
        if (got < 16 || std::memcmp(header, "MComprHD", 8) != 0)
        {
            chd->Fail(error, "no CHD signature (MComprHD)");
            return nullptr;
        }
        if (got < Be32(header + 8))
        {
            chd->Fail(error, "the header is cut short");
            return nullptr;
        }
        if (!chd->ParseHeader(header, error) || !chd->ReadMetadata(error) || !chd->ReadMap(error))
            return nullptr;

        for (const MetadataEntry& entry : chd->_metadata)
        {
            if (IsCdTag(entry.tag))
            {
                chd->Fail(error, "a CD-ROM / GD-ROM / DVD CHD: only hard-disk CHDs are supported (extract it with chdman extractcd)");
                return nullptr;
            }
        }

        if (chd->_hasParent)
        {
            if (t_parentDepth >= kMaxParentDepth)
            {
                chd->Fail(error, "the parent chain is deeper than " + std::to_string(kMaxParentDepth));
                return nullptr;
            }
            t_parentDepth++;
            std::string parentError;
            chd->_parent = finder ? finder(chd->_parentSha1, path, &parentError) : FindParentNextTo(chd->_parentSha1, path, &parentError);
            t_parentDepth--;
            if (!chd->_parent)
            {
                if (error)
                    *error = parentError.empty() ? path + ": its parent CHD was not found" : parentError;
                return nullptr;
            }
            if (chd->_parent->OverallSha1() != chd->_parentSha1)
            {
                chd->Fail(error, "the parent " + chd->_parent->Path() + " is not the one this CHD was made from (SHA-1 mismatch)");
                return nullptr;
            }
        }

        chd->_compressed.resize(chd->_hunkBytes);
        chd->_cache.resize(chd->_hunkBytes);
        return chd;
    }

    ChdFile::~ChdFile() = default;

    bool ChdFile::ParseHeader(const uint8_t* h, std::string* error)
    {
        _version = Be32(h + 12);
        const uint32_t length = Be32(h + 8);
        switch (_version)
        {
            case 3:
            case 4:
            {
                if (length != (_version == 3 ? kV3HeaderBytes : kV4HeaderBytes))
                    return Fail(error, "bad v" + std::to_string(_version) + " header length");
                const uint32_t flags = Be32(h + 16);
                switch (Be32(h + 20))
                {
                    case 0: _codecs[0] = kCodecNone; break;
                    case 1:
                    case 2: _codecs[0] = kCodecZlib; break;
                    case 3: return Fail(error, "a LaserDisc (A/V) CHD: only hard-disk CHDs are supported");
                    default: return Fail(error, "unknown v" + std::to_string(_version) + " compression " + std::to_string(Be32(h + 20)));
                }
                _hunkCount = Be32(h + 24);
                _logicalBytes = Be64(h + 28);
                _metaOffset = Be64(h + 36);
                if (_version == 3)
                {
                    _hunkBytes = Be32(h + 76);
                    std::memcpy(_sha1.data(), h + 80, 20);
                    _rawSha1 = _sha1;
                    std::memcpy(_parentSha1.data(), h + 100, 20);
                    _mapOffset = kV3HeaderBytes;
                }
                else
                {
                    _hunkBytes = Be32(h + 44);
                    std::memcpy(_sha1.data(), h + 48, 20);
                    std::memcpy(_parentSha1.data(), h + 68, 20);
                    std::memcpy(_rawSha1.data(), h + 88, 20);
                    _mapOffset = kV4HeaderBytes;
                }
                _hasParent = (flags & 1) != 0;
                break;
            }
            case 5:
            {
                if (length != kV5HeaderBytes)
                    return Fail(error, "bad v5 header length");
                for (int i = 0; i < 4; i++)
                    _codecs[i] = Be32(h + 16 + 4 * i);
                _logicalBytes = Be64(h + 32);
                _mapOffset = Be64(h + 40);
                _metaOffset = Be64(h + 48);
                _hunkBytes = Be32(h + 56);
                _unitBytes = Be32(h + 60);
                std::memcpy(_rawSha1.data(), h + 64, 20);
                std::memcpy(_sha1.data(), h + 84, 20);
                std::memcpy(_parentSha1.data(), h + 104, 20);
                _hasParent = !IsNull(_parentSha1);
                if (!_hunkBytes || !_unitBytes)
                    return Fail(error, "zero hunk or unit size");
                _hunkCount = static_cast<uint32_t>((_logicalBytes + _hunkBytes - 1) / _hunkBytes);
                break;
            }
            default:
                return Fail(error, "CHD version " + std::to_string(_version) + " is not supported (versions 3, 4 and 5 are, as in MAME)");
        }

        if (!_hunkBytes || _hunkBytes > (1u << 24))
            return Fail(error, "hunk size " + std::to_string(_hunkBytes) + " is out of range");
        if (_logicalBytes > static_cast<uint64_t>(_hunkCount) * _hunkBytes)
            return Fail(error, "the hunk count does not cover the logical size");

        for (size_t i = 0; i < _codecs.size(); i++)
        {
            const uint32_t tag = _codecs[i];
            if (tag == kCodecNone)
                continue;
            if (tag == kCodecCdZlib || tag == kCodecCdZstd || tag == kCodecCdLzma || tag == kCodecCdFlac)
                return Fail(error, "a CD-ROM CHD (codec " + CodecName(tag) + "): only hard-disk CHDs are supported (extract it with chdman extractcd)");
            if (tag == kCodecAvHuff)
                return Fail(error, "a LaserDisc (A/V) CHD: only hard-disk CHDs are supported");
            _decoders[i] = CreateCodec(tag, _hunkBytes);
            if (!_decoders[i])
                return Fail(error, "unknown codec " + CodecName(tag));
        }
        return true;
    }

    bool ChdFile::ReadMetadata(std::string* error)
    {
        uint64_t offset = _metaOffset;
        while (offset != 0)
        {
            if (_metadata.size() >= 4096)
                return Fail(error, "the metadata chain does not end");
            uint8_t header[kMetadataHeaderBytes];
            if (offset + kMetadataHeaderBytes > _fileBytes || !ReadFileBytes(offset, header, kMetadataHeaderBytes))
                return Fail(error, "metadata beyond the end of the file");
            MetadataEntry entry;
            entry.tag = Be32(header);
            entry.flags = header[4];
            const uint32_t length = Be24(header + 5);
            const uint64_t next = Be64(header + 8);
            if (offset + kMetadataHeaderBytes + length > _fileBytes)
                return Fail(error, "metadata beyond the end of the file");
            entry.data.resize(length);
            if (length && !ReadFileBytes(offset + kMetadataHeaderBytes, entry.data.data(), length))
                return Fail(error, "cannot read the metadata");
            _metadata.push_back(std::move(entry));
            offset = next;
        }
        if (_version < 5)
            _unitBytes = GuessUnitBytes();
        return true;
    }

    uint32_t ChdFile::GuessUnitBytes() const
    {
        // v3 / v4 have no unit size: a hard disk's sector size, else the hunk
        if (auto text = MetadataText(kTagHardDisk))
        {
            const size_t at = text->find("BPS:");
            if (at != std::string::npos)
            {
                const uint32_t bps = static_cast<uint32_t>(std::strtoul(text->c_str() + at + 4, nullptr, 10));
                if (bps)
                    return bps;
            }
        }
        return _hunkBytes;
    }

    std::optional<std::string> ChdFile::MetadataText(uint32_t tag) const
    {
        for (const MetadataEntry& entry : _metadata)
        {
            if (entry.tag != tag)
                continue;
            std::string text(entry.data.begin(), entry.data.end());
            while (!text.empty() && text.back() == '\0')
                text.pop_back();
            return text;
        }
        return std::nullopt;
    }

    bool ChdFile::ReadMap(std::string* error)
    {
        _map.assign(_hunkCount, HunkEntry{});
        if (_version < 5)
        {
            const uint64_t bytes = static_cast<uint64_t>(_hunkCount) * 16;
            if (_mapOffset + bytes > _fileBytes)
                return Fail(error, "the map is cut short");
            std::vector<uint8_t> raw(static_cast<size_t>(bytes));
            if (bytes && !ReadFileBytes(_mapOffset, raw.data(), static_cast<uint32_t>(bytes)))
                return Fail(error, "cannot read the map");
            for (uint32_t hunk = 0; hunk < _hunkCount; hunk++)
            {
                const uint8_t* e = raw.data() + static_cast<size_t>(hunk) * 16;
                HunkEntry& entry = _map[hunk];
                entry.offset = Be64(e);
                entry.crc = Be32(e + 8);
                entry.length = Be16(e + 12) | (static_cast<uint32_t>(e[14]) << 16);
                entry.hasCrc = (e[15] & kV34NoCrc) == 0;
                switch (e[15] & 0x0F)
                {
                    case kV34Compressed:
                        if (_codecs[0] == kCodecNone)
                            return Fail(error, "a compressed hunk in an uncompressed CHD");
                        entry.type = HunkEntry::Type::Codec;
                        break;
                    case kV34Uncompressed:
                        entry.type = HunkEntry::Type::Uncompressed;
                        entry.length = _hunkBytes;
                        break;
                    case kV34Mini: entry.type = HunkEntry::Type::Mini; break;
                    case kV34Self: entry.type = HunkEntry::Type::Self; break;
                    case kV34Parent: entry.type = HunkEntry::Type::Parent; break;
                    case kV34SecondCompressed:
                        return Fail(error, "hunk " + std::to_string(hunk) + " uses the v3 / v4 secondary (CD audio) codec");
                    default: return Fail(error, "hunk " + std::to_string(hunk) + " has an invalid map entry");
                }
                if (entry.type == HunkEntry::Type::Self && entry.offset >= _hunkCount)
                    return Fail(error, "hunk " + std::to_string(hunk) + " copies a hunk that does not exist");
            }
            return true;
        }

        if (Compressed())
            return ReadCompressedMap(error);

        // v5 uncompressed: one 32-bit hunk number per hunk (0: absent)
        const uint64_t bytes = static_cast<uint64_t>(_hunkCount) * 4;
        if (_mapOffset + bytes > _fileBytes)
            return Fail(error, "the map is cut short");
        std::vector<uint8_t> raw(static_cast<size_t>(bytes));
        if (bytes && !ReadFileBytes(_mapOffset, raw.data(), static_cast<uint32_t>(bytes)))
            return Fail(error, "cannot read the map");
        for (uint32_t hunk = 0; hunk < _hunkCount; hunk++)
        {
            const uint32_t value = Be32(raw.data() + static_cast<size_t>(hunk) * 4);
            HunkEntry& entry = _map[hunk];
            if (value == 0)
            {
                entry.type = _hasParent ? HunkEntry::Type::Parent : HunkEntry::Type::Zero;
                entry.offset = hunk;  // the parent's hunk of the same number
            }
            else
            {
                entry.type = HunkEntry::Type::Uncompressed;
                entry.offset = static_cast<uint64_t>(value) * _hunkBytes;
                entry.length = _hunkBytes;
            }
        }
        return true;
    }

    bool ChdFile::ReadCompressedMap(std::string* error)
    {
        uint8_t header[16];
        if (_mapOffset == 0 || _mapOffset + 16 > _fileBytes || !ReadFileBytes(_mapOffset, header, 16))
            return Fail(error, "the compressed map is missing or cut short");
        const uint32_t mapBytes = Be32(header);
        const uint64_t firstOffset = Be48(header + 4);
        const uint16_t mapCrc = Be16(header + 10);
        const int lengthBits = header[12];
        const int selfBits = header[13];
        const int parentBits = header[14];
        if (_mapOffset + 16 + mapBytes > _fileBytes || lengthBits > 32 || selfBits > 32 || parentBits > 32)
            return Fail(error, "the compressed map is cut short");
        std::vector<uint8_t> compressed(mapBytes);
        if (mapBytes && !ReadFileBytes(_mapOffset + 16, compressed.data(), mapBytes))
            return Fail(error, "cannot read the map");

        BitReader bits(compressed.data(), compressed.size());
        HuffmanCoder decoder(16, 8);
        if (!decoder.ImportTreeRle(bits))
            return Fail(error, "the map's code table is broken");

        // The expanded map, 12 bytes per hunk as MAME holds it: its CRC-16 checks the decoding
        std::vector<uint8_t> raw(static_cast<size_t>(_hunkCount) * 12, 0);
        uint8_t last = 0;
        int repeat = 0;
        for (uint32_t hunk = 0; hunk < _hunkCount; hunk++)
        {
            uint8_t* e = raw.data() + static_cast<size_t>(hunk) * 12;
            if (repeat > 0)
            {
                e[0] = last;
                repeat--;
                continue;
            }
            const uint32_t value = decoder.DecodeOne(bits);
            if (value == kRleSmall)
            {
                e[0] = last;
                repeat = 2 + static_cast<int>(decoder.DecodeOne(bits));
            }
            else if (value == kRleLarge)
            {
                e[0] = last;
                repeat = 2 + 16 + (static_cast<int>(decoder.DecodeOne(bits)) << 4);
                repeat += static_cast<int>(decoder.DecodeOne(bits));
            }
            else
            {
                e[0] = last = static_cast<uint8_t>(value);
            }
        }

        uint64_t offset = firstOffset;
        uint64_t lastSelf = 0;
        uint64_t lastParent = 0;
        const uint64_t unitsPerHunk = _hunkBytes / _unitBytes;
        for (uint32_t hunk = 0; hunk < _hunkCount; hunk++)
        {
            uint8_t* e = raw.data() + static_cast<size_t>(hunk) * 12;
            uint64_t entryOffset = offset;
            uint32_t length = 0;
            uint16_t crc = 0;
            switch (e[0])
            {
                case 0:
                case 1:
                case 2:
                case 3:
                    length = bits.Read(lengthBits);
                    offset += length;
                    crc = static_cast<uint16_t>(bits.Read(16));
                    break;
                case kTypeNone:
                    length = _hunkBytes;
                    offset += length;
                    crc = static_cast<uint16_t>(bits.Read(16));
                    break;
                case kTypeSelf:
                    lastSelf = entryOffset = bits.Read(selfBits);
                    break;
                case kTypeParent:
                    lastParent = entryOffset = bits.Read(parentBits);
                    break;
                case kSelf1:
                    lastSelf++;
                    e[0] = kTypeSelf;
                    entryOffset = lastSelf;
                    break;
                case kSelf0:
                    e[0] = kTypeSelf;
                    entryOffset = lastSelf;
                    break;
                case kParentSelf:
                    e[0] = kTypeParent;
                    lastParent = entryOffset = static_cast<uint64_t>(hunk) * _hunkBytes / _unitBytes;
                    break;
                case kParent1:
                    lastParent += unitsPerHunk;
                    e[0] = kTypeParent;
                    entryOffset = lastParent;
                    break;
                case kParent0:
                    e[0] = kTypeParent;
                    entryOffset = lastParent;
                    break;
                default:
                    return Fail(error, "hunk " + std::to_string(hunk) + " has an invalid map type " + std::to_string(e[0]));
            }
            PutBe24(e + 1, length);
            PutBe48(e + 4, entryOffset);
            PutBe16(e + 10, crc);
        }
        if (bits.Overflow() || Crc16(raw.data(), raw.size()) != mapCrc)
            return Fail(error, "the map fails its CRC");

        for (uint32_t hunk = 0; hunk < _hunkCount; hunk++)
        {
            const uint8_t* e = raw.data() + static_cast<size_t>(hunk) * 12;
            HunkEntry& entry = _map[hunk];
            entry.length = Be24(e + 1);
            entry.offset = Be48(e + 4);
            entry.crc = Be16(e + 10);
            switch (e[0])
            {
                case 0:
                case 1:
                case 2:
                case 3:
                    if (_codecs[e[0]] == kCodecNone)
                        return Fail(error, "hunk " + std::to_string(hunk) + " names codec slot " + std::to_string(e[0]) + ", which is empty");
                    entry.type = HunkEntry::Type::Codec;
                    entry.codec = e[0];
                    entry.hasCrc = true;
                    break;
                case kTypeNone:
                    entry.type = HunkEntry::Type::Uncompressed;
                    entry.hasCrc = true;
                    break;
                case kTypeSelf:
                    if (entry.offset >= hunk)
                        return Fail(error, "hunk " + std::to_string(hunk) + " copies a later hunk");
                    entry.type = HunkEntry::Type::Self;
                    break;
                default:
                    if (!_hasParent)
                        return Fail(error, "hunk " + std::to_string(hunk) + " refers to a parent, but the CHD has none");
                    entry.type = HunkEntry::Type::Parent;
                    break;
            }
        }
        return true;
    }

    /// endregion </Opening>

    /// region <Reading>

    bool ChdFile::ReadFileBytes(uint64_t offset, uint8_t* dst, uint32_t length)
    {
        if (offset + length > _fileBytes)
            return false;
        _file.clear();
        _file.seekg(static_cast<std::streamoff>(offset));
        _file.read(reinterpret_cast<char*>(dst), static_cast<std::streamsize>(length));
        const bool ok = static_cast<uint32_t>(_file.gcount()) == length;
        _file.clear();
        return ok;
    }

    bool ChdFile::ReadHunk(uint32_t hunk, uint8_t* dst, std::string* error)
    {
        if (hunk >= _hunkCount)
            return Fail(error, "hunk " + std::to_string(hunk) + " is beyond the end");
        const HunkEntry& entry = _map[hunk];
        const std::string where = "hunk " + std::to_string(hunk);
        switch (entry.type)
        {
            case HunkEntry::Type::Codec:
            {
                if (entry.length > _compressed.size())
                    _compressed.resize(entry.length);
                if (!ReadFileBytes(entry.offset, _compressed.data(), entry.length))
                    return Fail(error, where + ": the file is cut short");
                if (!_decoders[entry.codec]->Decompress(_compressed.data(), entry.length, dst, _hunkBytes))
                    return Fail(error, where + ": " + CodecName(_codecs[entry.codec]) + " data is corrupt");
                break;
            }
            case HunkEntry::Type::Uncompressed:
                if (!ReadFileBytes(entry.offset, dst, _hunkBytes))
                    return Fail(error, where + ": the file is cut short");
                break;
            case HunkEntry::Type::Self:
            {
                if (_selfDepth > 64)
                    return Fail(error, where + ": a loop of self references");
                _selfDepth++;
                const bool ok = ReadHunk(static_cast<uint32_t>(entry.offset), dst, error);
                _selfDepth--;
                return ok;
            }
            case HunkEntry::Type::Parent:
            {
                if (!_parent)
                    return Fail(error, where + ": needs the parent CHD");
                uint64_t byteOffset = 0;
                if (_version < 5)
                    byteOffset = entry.offset * _parent->HunkBytes();
                else if (Compressed())
                    byteOffset = entry.offset * _parent->UnitBytes();
                else
                    byteOffset = static_cast<uint64_t>(hunk) * _hunkBytes;
                std::string parentError;
                if (!_parent->ReadBytes(byteOffset, dst, _hunkBytes, &parentError))
                    return Fail(error, where + ": the parent: " + parentError);
                return true;
            }
            case HunkEntry::Type::Zero:
                std::memset(dst, 0, _hunkBytes);
                return true;
            case HunkEntry::Type::Mini:
                PutBe64(dst, entry.offset);
                for (uint32_t i = 8; i < _hunkBytes; i++)
                    dst[i] = dst[i - 8];
                break;
        }

        if (entry.hasCrc)
        {
            const bool match = _version < 5 ? Crc32(dst, _hunkBytes) == entry.crc : Crc16(dst, _hunkBytes) == entry.crc;
            if (!match)
                return Fail(error, where + ": CRC mismatch (the file is damaged)");
        }
        return true;
    }

    bool ChdFile::ReadBytes(uint64_t offset, uint8_t* dst, uint32_t length, std::string* error)
    {
        while (length > 0)
        {
            const uint64_t hunk = offset / _hunkBytes;
            const uint32_t within = static_cast<uint32_t>(offset % _hunkBytes);
            const uint32_t take = std::min(length, _hunkBytes - within);
            if (hunk >= _hunkCount)
            {
                // A child larger than its parent: the parent has no data there
                std::memset(dst, 0, take);
            }
            else
            {
                if (static_cast<int64_t>(hunk) != _cacheHunk)
                {
                    _cacheHunk = -1;
                    if (!ReadHunk(static_cast<uint32_t>(hunk), _cache.data(), error))
                        return false;
                    _cacheHunk = static_cast<int64_t>(hunk);
                }
                std::memcpy(dst, _cache.data() + within, take);
            }
            dst += take;
            offset += take;
            length -= take;
        }
        return true;
    }

    /// endregion </Reading>

    /// region <Checksums>

    Sha1 ChdFile::OverallSha1(const Sha1& raw, const std::vector<MetadataEntry>& metadata)
    {
        std::vector<std::array<uint8_t, 24>> pairs;
        for (const MetadataEntry& entry : metadata)
        {
            if (!(entry.flags & kMetadataChecksum))
                continue;
            std::array<uint8_t, 24> pair{};
            PutBe32(pair.data(), entry.tag);
            const Sha1 sha1 = Sha1Of(entry.data.data(), entry.data.size());
            std::memcpy(pair.data() + 4, sha1.data(), 20);
            pairs.push_back(pair);
        }
        std::sort(pairs.begin(), pairs.end());
        Sha1Builder builder;
        builder.Append(raw.data(), raw.size());
        for (const auto& pair : pairs)
            builder.Append(pair.data(), pair.size());
        return builder.Finish();
    }

    bool ChdFile::Verify(std::string* error)
    {
        if (!Compressed() || IsNull(_rawSha1))
            return true;  // MAME keeps no checksum for an uncompressed CHD

        Sha1Builder builder;
        std::vector<uint8_t> hunk(_hunkBytes);
        for (uint32_t h = 0; h < _hunkCount; h++)
        {
            if (!ReadHunk(h, hunk.data(), error))
                return false;
            const uint64_t start = static_cast<uint64_t>(h) * _hunkBytes;
            builder.Append(hunk.data(), static_cast<size_t>(std::min<uint64_t>(_hunkBytes, _logicalBytes - start)));
        }
        const Sha1 raw = builder.Finish();
        if (raw != _rawSha1)
            return Fail(error, "the data SHA-1 is " + Sha1Hex(raw) + ", the header says " + Sha1Hex(_rawSha1));
        if (_version >= 4)
        {
            const Sha1 overall = OverallSha1(raw, _metadata);
            if (overall != _sha1)
                return Fail(error, "the overall SHA-1 is " + Sha1Hex(overall) + ", the header says " + Sha1Hex(_sha1));
        }
        return true;
    }

    /// endregion </Checksums>
}  // namespace chd
