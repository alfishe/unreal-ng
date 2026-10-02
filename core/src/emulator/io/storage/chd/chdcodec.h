#pragma once

/// @file chdcodec.h
/// @brief The hunk codecs of CHD files, by their four-character tags.
///
/// | Tag | Codec | Library |
/// |---|---|---|
/// | `zlib` | raw deflate, best compression | miniz (vendored) |
/// | `lzma` | LZMA, level 6, dictionary sized to the hunk, no end marker | LZMA SDK 19.00 (vendored) |
/// | `huff` | one 8-bit Huffman table per hunk | own (chdhuffman) |
/// | `flac` | the hunk as 16-bit stereo, `L` / `B` byte order prefix | own (chdflac) |
/// | `zstd` | a Zstandard frame | zstd 1.5.7 (vendored) |
///
/// The CD codecs (`cdlz`, `cdzl`, `cdzs`, `cdfl`) read CD-ROM CHDs (hunks of
/// 2448-byte frames: 2352 bytes of sector data + 96 of subcode; CreateCdCodec).
/// They decompress only: the emulator never writes a CD CHD. A/V Huffman
/// (`avhu`) is known by name only: a CHD that uses it is refused.

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace chd
{
    constexpr uint32_t MakeTag(char a, char b, char c, char d)
    {
        return (static_cast<uint32_t>(static_cast<uint8_t>(a)) << 24) | (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 16) |
               (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 8) | static_cast<uint32_t>(static_cast<uint8_t>(d));
    }

    constexpr uint32_t kCodecNone = 0;
    constexpr uint32_t kCodecZlib = MakeTag('z', 'l', 'i', 'b');
    constexpr uint32_t kCodecZstd = MakeTag('z', 's', 't', 'd');
    constexpr uint32_t kCodecLzma = MakeTag('l', 'z', 'm', 'a');
    constexpr uint32_t kCodecHuffman = MakeTag('h', 'u', 'f', 'f');
    constexpr uint32_t kCodecFlac = MakeTag('f', 'l', 'a', 'c');
    constexpr uint32_t kCodecCdZlib = MakeTag('c', 'd', 'z', 'l');
    constexpr uint32_t kCodecCdZstd = MakeTag('c', 'd', 'z', 's');
    constexpr uint32_t kCodecCdLzma = MakeTag('c', 'd', 'l', 'z');
    constexpr uint32_t kCodecCdFlac = MakeTag('c', 'd', 'f', 'l');
    constexpr uint32_t kCodecAvHuff = MakeTag('a', 'v', 'h', 'u');

    using CodecList = std::array<uint32_t, 4>;

    /// chdman's default for hard disks: lzma, zlib, huff, flac
    constexpr CodecList kDefaultHardDiskCodecs = {kCodecLzma, kCodecZlib, kCodecHuffman, kCodecFlac};

    class Codec
    {
    public:
        virtual ~Codec() = default;
        /// Compress a whole hunk. False when the result is not smaller than the
        /// hunk (or the codec fails): the hunk is then stored as is
        virtual bool Compress(const uint8_t* src, uint32_t length, uint8_t* dst, uint32_t& written) = 0;
        /// Decompress into exactly `dstLength` bytes. False on corrupt data
        virtual bool Decompress(const uint8_t* src, uint32_t length, uint8_t* dst, uint32_t dstLength) = 0;
    };

    /// A codec for hunks of `hunkBytes`, or nullptr for an unsupported tag
    std::unique_ptr<Codec> CreateCodec(uint32_t tag, uint32_t hunkBytes);
    bool IsSupportedCodec(uint32_t tag);

    /// region <CD-ROM codecs>
    constexpr uint32_t kCdFrameBytes = 2448;    ///< one frame in a CD CHD hunk
    constexpr uint32_t kCdSectorBytes = 2352;   ///< its sector data
    constexpr uint32_t kCdSubcodeBytes = 96;    ///< its subcode
    bool IsCdCodec(uint32_t tag);
    /// A decompressor for CD hunks of `hunkBytes` (a multiple of 2448), nullptr
    /// for another tag. The hunk's frames are put back together as MAME stores
    /// them: the sector data (audio big-endian), then the subcode; a frame whose
    /// sync and ECC were stripped gets them rebuilt (ECMA-130)
    std::unique_ptr<Codec> CreateCdCodec(uint32_t tag, uint32_t hunkBytes);
    /// endregion </CD-ROM codecs>

    /// "zlib", "lzma", ... ("none" for 0, the hex value for an unknown tag)
    std::string CodecName(uint32_t tag);
    /// The codecs a user names: "none" (uncompressed), "default" (chdman's hard
    /// disk set), or up to four of zlib / lzma / huff / flac / zstd separated by
    /// commas or "+". False with a reason for anything else
    bool ParseCodecList(const std::string& text, CodecList& codecs, std::string* error = nullptr);
    std::string FormatCodecList(const CodecList& codecs);
}  // namespace chd
