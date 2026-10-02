#include "stdafx.h"

#include "chdwriter.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <unordered_map>

#include "common/filehelper.h"
#include "emulator/io/storage/chd/chdhuffman.h"

namespace chd
{
    namespace
    {
        constexpr uint32_t kHeaderBytes = 124;

        enum : uint8_t
        {
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

        bool Fail(std::string* error, const std::string& reason)
        {
            if (error)
                *error = reason;
            return false;
        }

        /// The output file: positioned writes, errors remembered
        class Output
        {
        public:
            explicit Output(const std::string& path) : _path(path)
            {
                _file.open(FileHelper::ToFsPath(path), std::ios::binary | std::ios::out | std::ios::trunc);
            }
            bool IsOpen() const { return _file.is_open(); }
            void WriteAt(uint64_t offset, const uint8_t* data, size_t length)
            {
                _file.seekp(static_cast<std::streamoff>(offset));
                _file.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(length));
            }
            bool Close()
            {
                _file.flush();
                const bool ok = static_cast<bool>(_file);
                _file.close();
                return ok && !_file.fail();
            }
            bool Good() const { return static_cast<bool>(_file); }
            void Discard()
            {
                _file.close();
                std::error_code ec;
                std::filesystem::remove(FileHelper::ToFsPath(_path), ec);
            }

        private:
            std::string _path;
            std::ofstream _file;
        };

        /// Metadata entries chained from `start`: the bytes and the first entry's offset
        std::vector<uint8_t> SerializeMetadata(const std::vector<MetadataEntry>& metadata, uint64_t start)
        {
            std::vector<uint8_t> bytes;
            uint64_t offset = start;
            for (size_t i = 0; i < metadata.size(); i++)
            {
                const MetadataEntry& entry = metadata[i];
                const uint64_t next = i + 1 < metadata.size() ? offset + 16 + entry.data.size() : 0;
                uint8_t header[16];
                PutBe32(header, entry.tag);
                header[4] = entry.flags;
                PutBe24(header + 5, static_cast<uint32_t>(entry.data.size()));
                PutBe64(header + 8, next);
                bytes.insert(bytes.end(), header, header + 16);
                bytes.insert(bytes.end(), entry.data.begin(), entry.data.end());
                offset += 16 + entry.data.size();
            }
            return bytes;
        }

        void PutHeader(uint8_t* h, const WriteOptions& options, uint64_t logicalBytes, uint64_t mapOffset, uint64_t metaOffset,
                       const Sha1& raw, const Sha1& overall, const Sha1& parent)
        {
            std::memset(h, 0, kHeaderBytes);
            std::memcpy(h, "MComprHD", 8);
            PutBe32(h + 8, kHeaderBytes);
            PutBe32(h + 12, 5);
            for (int i = 0; i < 4; i++)
                PutBe32(h + 16 + 4 * i, options.codecs[i]);
            PutBe64(h + 32, logicalBytes);
            PutBe64(h + 40, mapOffset);
            PutBe64(h + 48, metaOffset);
            PutBe32(h + 56, options.hunkBytes);
            PutBe32(h + 60, options.unitBytes);
            std::memcpy(h + 64, raw.data(), 20);
            std::memcpy(h + 84, overall.data(), 20);
            std::memcpy(h + 104, parent.data(), 20);
        }

        /// The v5 compressed map (MAME chd.cpp compress_v5_map): the entry types,
        /// run-length and Huffman coded, then per hunk its length and CRC, or the
        /// hunk / unit it copies, with runs of consecutive copies folded into
        /// pseudo-types
        bool CompressMap(const std::vector<uint8_t>& raw, uint32_t hunkCount, uint32_t hunkBytes, uint32_t unitBytes,
                         std::vector<uint8_t>& out, std::string* error)
        {
            const uint16_t mapCrc = Crc16(raw.data(), raw.size());
            std::vector<uint8_t> rle;
            rle.reserve(hunkCount + 4);
            HuffmanCoder encoder(16, 8);

            uint64_t maxSelf = 0;
            uint64_t lastSelf = 0;
            uint64_t maxParent = 0;
            uint64_t lastParent = 0;
            uint32_t maxLength = 0;
            uint8_t lastType = 0;
            int count = 0;
            auto emit = [&](uint8_t symbol) {
                rle.push_back(symbol);
                encoder.HistoOne(symbol);
            };
            for (uint32_t hunk = 0; hunk < hunkCount; hunk++)
            {
                const uint8_t* e = raw.data() + static_cast<size_t>(hunk) * 12;
                uint8_t type = e[0];
                if (type == kTypeSelf)
                {
                    const uint64_t ref = Be48(e + 4);
                    if (ref == lastSelf)
                        type = kSelf0;
                    else if (ref == lastSelf + 1)
                        type = kSelf1;
                    else
                        maxSelf = std::max(maxSelf, ref);
                    lastSelf = ref;
                }
                else if (type == kTypeParent)
                {
                    const uint64_t ref = Be48(e + 4);
                    if (ref == static_cast<uint64_t>(hunk) * hunkBytes / unitBytes)
                        type = kParentSelf;
                    else if (ref == lastParent)
                        type = kParent0;
                    else if (ref == lastParent + hunkBytes / unitBytes)
                        type = kParent1;
                    else
                        maxParent = std::max(maxParent, ref);
                    lastParent = ref;
                }
                else
                {
                    maxLength = std::max(maxLength, Be24(e + 1));
                }

                if (type == lastType)
                    count++;
                if (type != lastType || hunk == hunkCount - 1)
                {
                    while (count != 0)
                    {
                        if (count < 3)
                        {
                            emit(lastType);
                            count--;
                        }
                        else if (count <= 3 + 15)
                        {
                            emit(kRleSmall);
                            emit(static_cast<uint8_t>(count - 3));
                            count = 0;
                        }
                        else
                        {
                            const int run = std::min(count, 3 + 16 + 255);
                            emit(kRleLarge);
                            emit(static_cast<uint8_t>((run - 3 - 16) >> 4));
                            emit(static_cast<uint8_t>((run - 3 - 16) & 15));
                            count -= run;
                        }
                    }
                    if (type != lastType)
                    {
                        emit(type);
                        lastType = type;
                    }
                }
            }

            const int lengthBits = BitWidth(maxLength);
            const int selfBits = BitWidth(maxSelf);
            const int parentBits = BitWidth(maxParent);
            if (selfBits > 32 || parentBits > 32)
                return Fail(error, "the disk is too large for a CHD map");

            const size_t capacity = 64 + (static_cast<size_t>(hunkCount) * (12 + static_cast<size_t>(std::max({lengthBits + 16, selfBits, parentBits}))) + 7) / 8 * 2;
            out.assign(16 + capacity, 0);
            BitWriter bits(out.data() + 16, capacity);
            if (!encoder.ComputeTreeFromHisto() || !encoder.ExportTreeRle(bits))
                return Fail(error, "cannot code the CHD map");
            for (uint8_t symbol : rle)
                encoder.EncodeOne(bits, symbol);

            lastType = 0;
            count = 0;
            size_t src = 0;
            uint64_t firstOffset = 0;
            for (uint32_t hunk = 0; hunk < hunkCount; hunk++)
            {
                const uint8_t* e = raw.data() + static_cast<size_t>(hunk) * 12;
                const uint32_t length = Be24(e + 1);
                const uint64_t offset = Be48(e + 4);
                const uint16_t crc = Be16(e + 10);
                if (count == 0)
                {
                    const uint8_t value = rle[src++];
                    if (value == kRleSmall)
                    {
                        count = 2 + rle[src++];
                    }
                    else if (value == kRleLarge)
                    {
                        count = 2 + 16 + (rle[src++] << 4);
                        count += rle[src++];
                    }
                    else
                    {
                        lastType = value;
                    }
                }
                else
                {
                    count--;
                }

                switch (lastType)
                {
                    case 0:
                    case 1:
                    case 2:
                    case 3:
                        bits.Write(length, lengthBits);
                        bits.Write(crc, 16);
                        if (firstOffset == 0)
                            firstOffset = offset;
                        break;
                    case kTypeNone:
                        bits.Write(crc, 16);
                        if (firstOffset == 0)
                            firstOffset = offset;
                        break;
                    case kTypeSelf: bits.Write(static_cast<uint32_t>(offset), selfBits); break;
                    case kTypeParent: bits.Write(static_cast<uint32_t>(offset), parentBits); break;
                    default: break;  // the pseudo-types carry nothing
                }
            }
            const size_t length = bits.Flush();
            if (bits.Overflow())
                return Fail(error, "the CHD map overflowed its buffer");
            out.resize(16 + length);
            PutBe32(out.data(), static_cast<uint32_t>(length));
            PutBe48(out.data() + 4, firstOffset);
            PutBe16(out.data() + 10, mapCrc);
            out[12] = static_cast<uint8_t>(lengthBits);
            out[13] = static_cast<uint8_t>(selfBits);
            out[14] = static_cast<uint8_t>(parentBits);
            out[15] = 0;
            return true;
        }

        bool AllZero(const uint8_t* data, size_t length)
        {
            for (size_t i = 0; i < length; i++)
            {
                if (data[i])
                    return false;
            }
            return true;
        }

        struct Sha1Hash
        {
            size_t operator()(const Sha1& s) const
            {
                size_t h = 0;
                std::memcpy(&h, s.data(), sizeof(h));
                return h;
            }
        };
    }  // namespace

    bool WriteChd(const std::string& path, uint64_t logicalBytes, const HunkReader& read, const WriteOptions& options, std::string* error)
    {
        const uint32_t hunkBytes = options.hunkBytes;
        if (!hunkBytes || !options.unitBytes || hunkBytes % options.unitBytes != 0)
            return Fail(error, "the hunk size must be a whole number of units");
        if (logicalBytes == 0)
            return Fail(error, "an empty disk cannot be a CHD");
        const uint64_t hunks64 = (logicalBytes + hunkBytes - 1) / hunkBytes;
        if (hunks64 > 0xFFFFFFFFULL)
            return Fail(error, "the disk has too many hunks for a CHD");
        const uint32_t hunkCount = static_cast<uint32_t>(hunks64);
        bool compressed = options.codecs[0] != kCodecNone;
        bool seenNone = false;
        for (uint32_t tag : options.codecs)
        {
            if (tag == kCodecNone)
                seenNone = true;
            else if (seenNone)
                return Fail(error, "codecs must fill the first slots");
            else if (!IsSupportedCodec(tag))
                return Fail(error, "cannot write codec " + CodecName(tag));
        }
        ChdFile* parent = options.parent;
        if (parent && parent->UnitBytes() != options.unitBytes)
            return Fail(error, "the parent has " + std::to_string(parent->UnitBytes()) + "-byte units, the child " +
                                   std::to_string(options.unitBytes));

        Output out(path);
        if (!out.IsOpen())
            return Fail(error, "cannot create " + path);
        auto abort = [&](const std::string& reason) {
            out.Discard();
            return Fail(error, reason);
        };

        std::vector<uint8_t> hunk(hunkBytes);
        std::vector<uint8_t> parentHunk(parent ? hunkBytes : 0);
        auto sameAsParent = [&](uint32_t index, bool& same) {
            same = false;
            if (!parent)
                return true;
            std::string parentError;
            if (!parent->ReadBytes(static_cast<uint64_t>(index) * hunkBytes, parentHunk.data(), hunkBytes, &parentError))
                return Fail(error, "the parent: " + parentError);
            same = std::memcmp(parentHunk.data(), hunk.data(), hunkBytes) == 0;
            return true;
        };
        auto readHunk = [&](uint32_t index) {
            std::string readError;
            if (!read(index, hunk.data(), &readError))
                return Fail(error, readError.empty() ? "cannot read hunk " + std::to_string(index) : readError);
            // Past the logical end the hunk is zero, whatever the source had there
            const uint64_t start = static_cast<uint64_t>(index) * hunkBytes;
            if (start + hunkBytes > logicalBytes)
                std::memset(hunk.data() + (logicalBytes - start), 0, static_cast<size_t>(start + hunkBytes - logicalBytes));
            return true;
        };
        const Sha1 parentSha1 = parent ? parent->OverallSha1() : Sha1{};
        uint8_t header[kHeaderBytes];

        if (!compressed)
        {
            // Header, map, metadata, then whole hunks on hunk boundaries
            const uint64_t mapOffset = kHeaderBytes;
            const uint64_t metaOffset = mapOffset + static_cast<uint64_t>(hunkCount) * 4;
            const std::vector<uint8_t> meta = SerializeMetadata(options.metadata, metaOffset);
            uint64_t position = metaOffset + meta.size();
            position = (position + hunkBytes - 1) / hunkBytes * hunkBytes;
            std::vector<uint8_t> map(static_cast<size_t>(hunkCount) * 4, 0);
            out.WriteAt(metaOffset, meta.data(), meta.size());
            for (uint32_t h = 0; h < hunkCount; h++)
            {
                if (!readHunk(h))
                    return abort(error ? *error : std::string());
                bool same = false;
                if (!sameAsParent(h, same))
                    return abort(error ? *error : std::string());
                // Map entry 0: the parent's hunk, or zero without a parent
                if ((parent && same) || (!parent && AllZero(hunk.data(), hunkBytes)))
                    continue;
                if (position / hunkBytes > 0xFFFFFFFFULL)
                    return abort("the CHD file grew past 2^32 hunks");
                PutBe32(map.data() + static_cast<size_t>(h) * 4, static_cast<uint32_t>(position / hunkBytes));
                out.WriteAt(position, hunk.data(), hunkBytes);
                position += hunkBytes;
                if (!out.Good())
                    return abort("write error on " + path);
            }
            out.WriteAt(mapOffset, map.data(), map.size());
            PutHeader(header, options, logicalBytes, mapOffset, options.metadata.empty() ? 0 : metaOffset, Sha1{}, Sha1{}, parentSha1);
            out.WriteAt(0, header, kHeaderBytes);
            if (!out.Close())
                return abort("write error on " + path);
            return true;
        }

        // Compressed: header, metadata, hunks back to back, then the map
        const uint64_t metaOffset = kHeaderBytes;
        const std::vector<uint8_t> meta = SerializeMetadata(options.metadata, metaOffset);
        out.WriteAt(metaOffset, meta.data(), meta.size());
        uint64_t position = metaOffset + meta.size();

        std::array<std::unique_ptr<Codec>, 4> codecs;
        for (int i = 0; i < 4; i++)
            codecs[i] = options.codecs[i] ? CreateCodec(options.codecs[i], hunkBytes) : nullptr;
        ChdFile* reuse = (options.reuse && options.unchanged && options.reuse->Version() == 5 &&
                          options.reuse->HunkBytes() == hunkBytes && options.reuse->Codecs() == options.codecs)
                             ? options.reuse
                             : nullptr;

        // Every hunk of the parent by content (MAME walks the parent the same
        // way): a child hunk equal to any of them is a parent reference
        std::unordered_map<Sha1, uint64_t, Sha1Hash> parentHunks;
        if (parent)
        {
            const uint32_t parentHunkBytes = parent->HunkBytes();
            std::vector<uint8_t> data(parentHunkBytes);
            for (uint32_t h = 0; h < parent->HunkCount() && parentHunkBytes == hunkBytes; h++)
            {
                std::string parentError;
                if (!parent->ReadHunk(h, data.data(), &parentError))
                    return abort("the parent: " + parentError);
                parentHunks.emplace(Sha1Of(data.data(), hunkBytes), static_cast<uint64_t>(h) * hunkBytes / options.unitBytes);
            }
        }

        std::vector<uint8_t> raw(static_cast<size_t>(hunkCount) * 12, 0);
        std::unordered_map<Sha1, uint32_t, Sha1Hash> seen;
        std::vector<uint8_t> trial(hunkBytes);
        std::vector<uint8_t> best(hunkBytes);
        Sha1Builder rawSha1;
        for (uint32_t h = 0; h < hunkCount; h++)
        {
            if (!readHunk(h))
                return abort(error ? *error : std::string());
            const uint64_t start = static_cast<uint64_t>(h) * hunkBytes;
            rawSha1.Append(hunk.data(), static_cast<size_t>(std::min<uint64_t>(hunkBytes, logicalBytes - start)));
            uint8_t* e = raw.data() + static_cast<size_t>(h) * 12;
            const uint16_t crc = Crc16(hunk.data(), hunkBytes);

            bool same = false;
            if (!sameAsParent(h, same))
                return abort(error ? *error : std::string());
            if (same)
            {
                e[0] = kTypeParent;
                PutBe48(e + 4, start / options.unitBytes);
                continue;
            }
            const Sha1 sha1 = Sha1Of(hunk.data(), hunkBytes);
            if (auto it = parentHunks.find(sha1); it != parentHunks.end())
            {
                e[0] = kTypeParent;
                PutBe48(e + 4, it->second);
                continue;
            }
            if (auto it = seen.find(sha1); it != seen.end())
            {
                e[0] = kTypeSelf;
                PutBe48(e + 4, it->second);
                continue;
            }

            int type = -1;  // stored as is
            uint32_t length = hunkBytes;
            const uint8_t* payload = hunk.data();
            if (reuse && options.unchanged(h) && reuse->Entry(h).type == HunkEntry::Type::Codec)
            {
                const HunkEntry& entry = reuse->Entry(h);
                if (entry.length < hunkBytes && reuse->ReadFileBytes(entry.offset, best.data(), entry.length))
                {
                    type = entry.codec;
                    length = entry.length;
                    payload = best.data();
                }
            }
            else if (!(reuse && options.unchanged(h) && reuse->Entry(h).type == HunkEntry::Type::Uncompressed))
            {
                for (int i = 0; i < 4; i++)
                {
                    uint32_t written = 0;
                    if (codecs[i] && codecs[i]->Compress(hunk.data(), hunkBytes, trial.data(), written) && written < length)
                    {
                        type = i;
                        length = written;
                        std::swap(trial, best);
                        payload = best.data();
                    }
                }
            }
            out.WriteAt(position, payload, length);
            if (!out.Good())
                return abort("write error on " + path);
            e[0] = type < 0 ? static_cast<uint8_t>(kTypeNone) : static_cast<uint8_t>(type);
            PutBe24(e + 1, length);
            PutBe48(e + 4, position);
            PutBe16(e + 10, crc);
            position += length;
            seen.emplace(sha1, h);
        }

        std::vector<uint8_t> map;
        if (!CompressMap(raw, hunkCount, hunkBytes, options.unitBytes, map, error))
            return abort(error ? *error : std::string());
        const uint64_t mapOffset = position;
        out.WriteAt(mapOffset, map.data(), map.size());

        const Sha1 rawHash = rawSha1.Finish();
        const Sha1 overall = ChdFile::OverallSha1(rawHash, options.metadata);
        PutHeader(header, options, logicalBytes, mapOffset, options.metadata.empty() ? 0 : metaOffset, rawHash, overall, parentSha1);
        out.WriteAt(0, header, kHeaderBytes);
        if (!out.Close())
            return abort("write error on " + path);
        return true;
    }

    bool WriteChd(const std::string& path, IBlockDevice& device, const WriteOptions& options, std::string* error)
    {
        const uint64_t sectors = device.SectorCount();
        const uint64_t logical = sectors * IBlockDevice::kSectorSize;
        const uint32_t perHunk = options.hunkBytes / IBlockDevice::kSectorSize;
        if (options.hunkBytes % IBlockDevice::kSectorSize != 0)
            return Fail(error, "the hunk size must be a whole number of sectors");
        auto read = [&](uint32_t hunk, uint8_t* dst, std::string* readError) {
            const uint64_t first = static_cast<uint64_t>(hunk) * perHunk;
            for (uint32_t i = 0; i < perHunk; i++)
            {
                uint8_t* sector = dst + static_cast<size_t>(i) * IBlockDevice::kSectorSize;
                if (first + i >= sectors)
                {
                    std::memset(sector, 0, IBlockDevice::kSectorSize);
                    continue;
                }
                if (!device.ReadSector(first + i, sector))
                {
                    if (readError)
                        *readError = "cannot read sector " + std::to_string(first + i) + " of " + device.Describe();
                    return false;
                }
            }
            return true;
        };
        return WriteChd(path, logical, read, options, error);
    }

    /// region <Geometry>

    MetadataEntry HardDiskMetadata(const BlockGeometry& g, uint32_t sectorBytes)
    {
        char text[96];
        std::snprintf(text, sizeof(text), "CYLS:%u,HEADS:%u,SECS:%u,BPS:%u", g.cylinders, g.heads, g.sectors, sectorBytes);
        MetadataEntry entry;
        entry.tag = kTagHardDisk;
        entry.flags = kMetadataChecksum;
        const size_t length = std::strlen(text);
        entry.data.assign(text, text + length + 1);  // the NUL is part of the entry, as chdman writes it
        return entry;
    }

    bool ParseHardDiskMetadata(const std::string& text, BlockGeometry& geometry, uint32_t& sectorBytes)
    {
        unsigned c = 0;
        unsigned h = 0;
        unsigned s = 0;
        unsigned bps = 0;
        if (std::sscanf(text.c_str(), "CYLS:%u,HEADS:%u,SECS:%u,BPS:%u", &c, &h, &s, &bps) != 4)
            return false;
        geometry.cylinders = c;
        geometry.heads = h;
        geometry.sectors = s;
        sectorBytes = bps;
        return true;
    }

    std::optional<BlockGeometry> GuessGeometry(uint64_t sectors)
    {
        for (uint32_t perTrack = 63; perTrack > 1; perTrack--)
        {
            if (sectors % perTrack != 0)
                continue;
            const uint64_t tracks = sectors / perTrack;
            for (uint32_t heads = 16; heads > 1; heads--)
            {
                if (tracks % heads == 0 && tracks / heads <= 0xFFFFFFFFULL)
                    return BlockGeometry{static_cast<uint32_t>(tracks / heads), heads, perTrack};
            }
        }
        return std::nullopt;
    }

    /// endregion </Geometry>
}  // namespace chd
