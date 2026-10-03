#include "stdafx.h"

#include "chdcodec.h"

#include <algorithm>
#include <cctype>
#include <cstring>

#include <zstd.h>

#include "3rdparty/miniz/miniz.h"
#include "Alloc.h"
#include "LzmaDec.h"
#include "LzmaEnc.h"
#include "emulator/io/storage/chd/chdflac.h"
#include "emulator/io/storage/chd/chdhuffman.h"
#include "emulator/io/storage/cd/cdecc.h"

namespace chd
{
    namespace
    {
        /// region <zlib: raw deflate>

        class ZlibCodec : public Codec
        {
        public:
            bool Compress(const uint8_t* src, uint32_t length, uint8_t* dst, uint32_t& written) override
            {
                const mz_uint flags = tdefl_create_comp_flags_from_zip_params(MZ_BEST_COMPRESSION, -MZ_DEFAULT_WINDOW_BITS, MZ_DEFAULT_STRATEGY);
                const size_t size = tdefl_compress_mem_to_mem(dst, length, src, length, static_cast<int>(flags));
                if (size == 0 || size >= length)
                    return false;
                written = static_cast<uint32_t>(size);
                return true;
            }
            bool Decompress(const uint8_t* src, uint32_t length, uint8_t* dst, uint32_t dstLength) override
            {
                const size_t size = tinfl_decompress_mem_to_mem(dst, dstLength, src, length, 0);
                return size == dstLength;
            }
        };

        /// endregion </zlib>

        /// region <zstd>

        class ZstdCodec : public Codec
        {
        public:
            ~ZstdCodec() override
            {
                ZSTD_freeCCtx(_compress);
                ZSTD_freeDCtx(_decompress);
            }
            bool Compress(const uint8_t* src, uint32_t length, uint8_t* dst, uint32_t& written) override
            {
                if (!_compress)
                    _compress = ZSTD_createCCtx();
                if (!_compress)
                    return false;
                // MAME uses the top level; 19 is within a few bytes of it on a hunk and far quicker
                const size_t size = ZSTD_compressCCtx(_compress, dst, length, src, length, 19);
                if (ZSTD_isError(size) || size >= length)
                    return false;
                written = static_cast<uint32_t>(size);
                return true;
            }
            bool Decompress(const uint8_t* src, uint32_t length, uint8_t* dst, uint32_t dstLength) override
            {
                if (!_decompress)
                    _decompress = ZSTD_createDCtx();
                if (!_decompress)
                    return false;
                const size_t size = ZSTD_decompressDCtx(_decompress, dst, dstLength, src, length);
                return !ZSTD_isError(size) && size == dstLength;
            }

        private:
            ZSTD_CCtx* _compress = nullptr;
            ZSTD_DCtx* _decompress = nullptr;
        };

        /// endregion </zstd>

        /// region <lzma>

        /// The encoder properties MAME uses (chdcodec.cpp): level 6, the dictionary
        /// cut down to the hunk. The decoder derives the same properties: the
        /// stream itself carries none
        CLzmaEncProps LzmaProperties(uint32_t hunkBytes)
        {
            CLzmaEncProps props;
            LzmaEncProps_Init(&props);
            props.level = 6;
            props.reduceSize = hunkBytes;
            LzmaEncProps_Normalize(&props);
            return props;
        }

        class LzmaCodec : public Codec
        {
        public:
            explicit LzmaCodec(uint32_t hunkBytes) : _props(LzmaProperties(hunkBytes))
            {
                LzmaDec_Construct(&_decoder);
                CLzmaEncHandle encoder = LzmaEnc_Create(&g_Alloc);
                if (!encoder)
                    return;
                SizeT size = LZMA_PROPS_SIZE;
                _ready = LzmaEnc_SetProps(encoder, &_props) == SZ_OK && LzmaEnc_WriteProperties(encoder, _decoderProps, &size) == SZ_OK;
                LzmaEnc_Destroy(encoder, &g_Alloc, &g_BigAlloc);
                _ready = _ready && LzmaDec_Allocate(&_decoder, _decoderProps, LZMA_PROPS_SIZE, &g_Alloc) == SZ_OK;
            }
            ~LzmaCodec() override { LzmaDec_Free(&_decoder, &g_Alloc); }

            bool Compress(const uint8_t* src, uint32_t length, uint8_t* dst, uint32_t& written) override
            {
                CLzmaEncHandle encoder = LzmaEnc_Create(&g_Alloc);
                if (!encoder)
                    return false;
                SizeT size = length;
                SRes result = LzmaEnc_SetProps(encoder, &_props);
                if (result == SZ_OK)
                    result = LzmaEnc_MemEncode(encoder, dst, &size, src, length, 0, nullptr, &g_Alloc, &g_BigAlloc);
                LzmaEnc_Destroy(encoder, &g_Alloc, &g_BigAlloc);
                if (result != SZ_OK || size >= length)
                    return false;
                written = static_cast<uint32_t>(size);
                return true;
            }
            bool Decompress(const uint8_t* src, uint32_t length, uint8_t* dst, uint32_t dstLength) override
            {
                if (!_ready)
                    return false;
                LzmaDec_Init(&_decoder);
                SizeT consumed = length;
                SizeT decoded = dstLength;
                ELzmaStatus status;
                const SRes result = LzmaDec_DecodeToBuf(&_decoder, dst, &decoded, src, &consumed, LZMA_FINISH_END, &status);
                return result == SZ_OK && consumed == length && decoded == dstLength;
            }

        private:
            CLzmaEncProps _props;
            CLzmaDec _decoder;
            Byte _decoderProps[LZMA_PROPS_SIZE] = {};
            bool _ready = false;
        };

        /// endregion </lzma>

        /// region <huff>

        class HuffmanCodec : public Codec
        {
        public:
            bool Compress(const uint8_t* src, uint32_t length, uint8_t* dst, uint32_t& written) override
            {
                return Huffman8Encode(src, length, dst, length, written) && written < length;
            }
            bool Decompress(const uint8_t* src, uint32_t length, uint8_t* dst, uint32_t dstLength) override
            {
                return Huffman8Decode(src, length, dst, dstLength);
            }
        };

        /// endregion </huff>

        /// region <flac>

        /// The hunk as 16-bit stereo samples in FLAC frames of up to 2048 samples,
        /// the byte order ('L' / 'B') that compresses better first
        class FlacCodec : public Codec
        {
        public:
            explicit FlacCodec(uint32_t hunkBytes) : _blockSize(BlockSize(hunkBytes)) {}

            static uint32_t BlockSize(uint32_t hunkBytes)
            {
                uint32_t size = hunkBytes / 4;
                while (size > 2048)
                    size /= 2;
                return size;
            }

            bool Compress(const uint8_t* src, uint32_t length, uint8_t* dst, uint32_t& written) override
            {
                if (length % 4 != 0 || length < 64)
                    return false;
                const uint32_t frames = length / 4;
                std::vector<uint8_t> little;
                std::vector<uint8_t> big;
                Samples(src, length, false);
                flac::Encode(_samples.data(), frames, 2, _blockSize, little);
                Samples(src, length, true);
                flac::Encode(_samples.data(), frames, 2, _blockSize, big);
                const bool useBig = big.size() < little.size();
                const std::vector<uint8_t>& best = useBig ? big : little;
                if (best.size() + 1 >= length)
                    return false;
                dst[0] = useBig ? 'B' : 'L';
                std::memcpy(dst + 1, best.data(), best.size());
                written = static_cast<uint32_t>(best.size() + 1);
                return true;
            }
            bool Decompress(const uint8_t* src, uint32_t length, uint8_t* dst, uint32_t dstLength) override
            {
                if (length < 1 || dstLength % 4 != 0 || (src[0] != 'L' && src[0] != 'B'))
                    return false;
                const bool big = src[0] == 'B';
                _samples.resize(dstLength / 2);
                if (!flac::Decode(src + 1, length - 1, _samples.data(), dstLength / 4, 2))
                    return false;
                for (uint32_t i = 0; i < dstLength / 2; i++)
                {
                    const uint16_t v = static_cast<uint16_t>(_samples[i]);
                    dst[2 * i + (big ? 0 : 1)] = static_cast<uint8_t>(v >> 8);
                    dst[2 * i + (big ? 1 : 0)] = static_cast<uint8_t>(v);
                }
                return true;
            }

        private:
            void Samples(const uint8_t* src, uint32_t length, bool big)
            {
                _samples.resize(length / 2);
                for (uint32_t i = 0; i < length / 2; i++)
                {
                    const uint8_t a = src[2 * i];
                    const uint8_t b = src[2 * i + 1];
                    _samples[i] = static_cast<int16_t>(big ? ((a << 8) | b) : ((b << 8) | a));
                }
            }

            uint32_t _blockSize;
            std::vector<int16_t> _samples;
        };

        /// endregion </flac>

        /// region <CD-ROM codecs>

        /// Frames back together: the sector data of every frame, then the subcode
        /// of every frame -> 2448-byte frames
        void Interleave(const uint8_t* data, const uint8_t* subcode, uint32_t frames, uint8_t* dst)
        {
            for (uint32_t frame = 0; frame < frames; frame++)
            {
                std::memcpy(dst + frame * kCdFrameBytes, data + frame * kCdSectorBytes, kCdSectorBytes);
                std::memcpy(dst + frame * kCdFrameBytes + kCdSectorBytes, subcode + frame * kCdSubcodeBytes, kCdSubcodeBytes);
            }
        }

        /// `cdlz`, `cdzl`, `cdzs` (MAME chd_cd_decompressor): a bitmap of the frames
        /// whose sync and ECC were stripped, the base stream's length (2 or 3
        /// bytes), the sector data in the base codec, the subcode in the second
        class CdCodec : public Codec
        {
        public:
            CdCodec(uint32_t baseTag, uint32_t subcodeTag, uint32_t hunkBytes)
                : _frames(hunkBytes / kCdFrameBytes),
                  _base(CreateCodec(baseTag, _frames * kCdSectorBytes)),
                  _subcode(CreateCodec(subcodeTag, _frames * kCdSubcodeBytes)),
                  _buffer(static_cast<size_t>(_frames) * (kCdSectorBytes + kCdSubcodeBytes))
            {
            }
            bool Compress(const uint8_t*, uint32_t, uint8_t*, uint32_t&) override { return false; }
            bool Decompress(const uint8_t* src, uint32_t length, uint8_t* dst, uint32_t dstLength) override
            {
                const uint32_t frames = dstLength / kCdFrameBytes;
                if (frames != _frames || !_base || !_subcode)
                    return false;
                const uint32_t lengthBytes = dstLength < 65536 ? 2 : 3;
                const uint32_t eccBytes = (frames + 7) / 8;
                const uint32_t header = eccBytes + lengthBytes;
                if (length < header)
                    return false;
                uint32_t baseLength = (static_cast<uint32_t>(src[eccBytes]) << 8) | src[eccBytes + 1];
                if (lengthBytes > 2)
                    baseLength = (baseLength << 8) | src[eccBytes + 2];
                if (header + baseLength > length)
                    return false;
                uint8_t* data = _buffer.data();
                uint8_t* subcode = data + static_cast<size_t>(frames) * kCdSectorBytes;
                if (!_base->Decompress(src + header, baseLength, data, frames * kCdSectorBytes) ||
                    !_subcode->Decompress(src + header + baseLength, length - header - baseLength, subcode, frames * kCdSubcodeBytes))
                    return false;
                Interleave(data, subcode, frames, dst);
                for (uint32_t frame = 0; frame < frames; frame++)
                {
                    if (src[frame / 8] & (1u << (frame % 8)))
                    {
                        uint8_t* sector = dst + frame * kCdFrameBytes;
                        std::memcpy(sector, cd::kSync, sizeof(cd::kSync));
                        cd::GenerateEcc(sector);
                    }
                }
                return true;
            }

        private:
            uint32_t _frames;
            std::unique_ptr<Codec> _base;
            std::unique_ptr<Codec> _subcode;
            std::vector<uint8_t> _buffer;
        };

        /// `cdfl` (MAME chd_cd_flac_decompressor): the sector data as 16-bit stereo
        /// FLAC frames, stored back big-endian, then the subcode as raw deflate
        class CdFlacCodec : public Codec
        {
        public:
            explicit CdFlacCodec(uint32_t hunkBytes)
                : _frames(hunkBytes / kCdFrameBytes),
                  _subcode(CreateCodec(kCodecZlib, _frames * kCdSubcodeBytes)),
                  _samples(static_cast<size_t>(_frames) * kCdSectorBytes / 2),
                  _buffer(static_cast<size_t>(_frames) * (kCdSectorBytes + kCdSubcodeBytes))
            {
            }
            bool Compress(const uint8_t*, uint32_t, uint8_t*, uint32_t&) override { return false; }
            bool Decompress(const uint8_t* src, uint32_t length, uint8_t* dst, uint32_t dstLength) override
            {
                const uint32_t frames = dstLength / kCdFrameBytes;
                if (frames != _frames)
                    return false;
                size_t used = 0;
                if (!flac::Decode(src, length, _samples.data(), frames * kCdSectorBytes / 4, 2, &used) || used > length)
                    return false;
                uint8_t* data = _buffer.data();
                uint8_t* subcode = data + static_cast<size_t>(frames) * kCdSectorBytes;
                for (size_t i = 0; i < _samples.size(); i++)
                {
                    const uint16_t v = static_cast<uint16_t>(_samples[i]);
                    data[2 * i] = static_cast<uint8_t>(v >> 8);
                    data[2 * i + 1] = static_cast<uint8_t>(v);
                }
                if (!_subcode->Decompress(src + used, static_cast<uint32_t>(length - used), subcode, frames * kCdSubcodeBytes))
                    return false;
                Interleave(data, subcode, frames, dst);
                return true;
            }

        private:
            uint32_t _frames;
            std::unique_ptr<Codec> _subcode;
            std::vector<int16_t> _samples;
            std::vector<uint8_t> _buffer;
        };

        /// endregion </CD-ROM codecs>
    }  // namespace

    bool IsCdCodec(uint32_t tag)
    {
        return tag == kCodecCdLzma || tag == kCodecCdZlib || tag == kCodecCdZstd || tag == kCodecCdFlac;
    }

    std::unique_ptr<Codec> CreateCdCodec(uint32_t tag, uint32_t hunkBytes)
    {
        if (hunkBytes == 0 || hunkBytes % kCdFrameBytes != 0)
            return nullptr;
        switch (tag)
        {
            case kCodecCdLzma: return std::make_unique<CdCodec>(kCodecLzma, kCodecZlib, hunkBytes);
            case kCodecCdZlib: return std::make_unique<CdCodec>(kCodecZlib, kCodecZlib, hunkBytes);
            case kCodecCdZstd: return std::make_unique<CdCodec>(kCodecZstd, kCodecZstd, hunkBytes);
            case kCodecCdFlac: return std::make_unique<CdFlacCodec>(hunkBytes);
            default: return nullptr;
        }
    }

    std::unique_ptr<Codec> CreateCodec(uint32_t tag, uint32_t hunkBytes)
    {
        switch (tag)
        {
            case kCodecZlib: return std::make_unique<ZlibCodec>();
            case kCodecZstd: return std::make_unique<ZstdCodec>();
            case kCodecLzma: return std::make_unique<LzmaCodec>(hunkBytes);
            case kCodecHuffman: return std::make_unique<HuffmanCodec>();
            case kCodecFlac: return std::make_unique<FlacCodec>(hunkBytes);
            default: return nullptr;
        }
    }

    bool IsSupportedCodec(uint32_t tag)
    {
        return tag == kCodecZlib || tag == kCodecZstd || tag == kCodecLzma || tag == kCodecHuffman || tag == kCodecFlac;
    }

    std::string CodecName(uint32_t tag)
    {
        if (tag == kCodecNone)
            return "none";
        std::string name;
        for (int shift = 24; shift >= 0; shift -= 8)
        {
            const char c = static_cast<char>((tag >> shift) & 0xFF);
            if (!std::isprint(static_cast<unsigned char>(c)))
            {
                char hex[16];
                std::snprintf(hex, sizeof(hex), "#%08X", tag);
                return hex;
            }
            name.push_back(c);
        }
        return name;
    }

    bool ParseCodecList(const std::string& text, CodecList& codecs, std::string* error)
    {
        auto fail = [error](const std::string& reason) {
            if (error)
                *error = reason;
            return false;
        };

        std::string lower;
        for (char c : text)
        {
            if (!std::isspace(static_cast<unsigned char>(c)))
                lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
        codecs = {};
        if (lower == "none" || lower == "uncompressed")
            return true;
        if (lower.empty() || lower == "default")
        {
            codecs = kDefaultHardDiskCodecs;
            return true;
        }

        size_t count = 0;
        size_t start = 0;
        while (start <= lower.size())
        {
            size_t end = lower.find_first_of(",+", start);
            if (end == std::string::npos)
                end = lower.size();
            const std::string name = lower.substr(start, end - start);
            uint32_t tag = 0;
            if (name == "zlib" || name == "deflate")
                tag = kCodecZlib;
            else if (name == "lzma")
                tag = kCodecLzma;
            else if (name == "huff" || name == "huffman")
                tag = kCodecHuffman;
            else if (name == "flac")
                tag = kCodecFlac;
            else if (name == "zstd")
                tag = kCodecZstd;
            else
                return fail("compression '" + name + "': expected none, default, or up to four of zlib, lzma, huff, flac, zstd");
            if (std::find(codecs.begin(), codecs.begin() + static_cast<std::ptrdiff_t>(count), tag) != codecs.begin() + static_cast<std::ptrdiff_t>(count))
                return fail("compression '" + text + "' names " + name + " twice");
            if (count == codecs.size())
                return fail("compression '" + text + "': a CHD takes at most four codecs");
            codecs[count++] = tag;
            start = end + 1;
        }
        return true;
    }

    std::string FormatCodecList(const CodecList& codecs)
    {
        std::string text;
        for (uint32_t tag : codecs)
        {
            if (tag == kCodecNone)
                break;
            text += (text.empty() ? "" : ",") + CodecName(tag);
        }
        return text.empty() ? "none" : text;
    }
}  // namespace chd
