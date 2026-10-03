#include "stdafx.h"

#include "cdecc.h"

#include <array>
#include <cstring>

namespace cd
{
    namespace
    {
        /// GF(2^8) tables (primitive polynomial x^8 + x^4 + x^3 + x^2 + 1) and the EDC table
        struct Tables
        {
            std::array<uint8_t, 256> forward{};   ///< multiply by alpha
            std::array<uint8_t, 256> backward{};  ///< divide (alpha + 1)
            std::array<uint32_t, 256> edc{};

            Tables()
            {
                for (uint32_t i = 0; i < 256; i++)
                {
                    const uint32_t j = (i << 1) ^ ((i & 0x80) ? 0x11D : 0);
                    forward[i] = static_cast<uint8_t>(j);
                    backward[i ^ j] = static_cast<uint8_t>(i);
                    uint32_t crc = i;
                    for (int bit = 0; bit < 8; bit++)
                        crc = (crc >> 1) ^ ((crc & 1) ? 0xD8018001u : 0);
                    edc[i] = crc;
                }
            }
        };

        const Tables& T()
        {
            static const Tables tables;
            return tables;
        }

        /// One parity block over the 2340 bytes after the sync (ECMA-130 Annex A):
        /// P: 86 columns of 24 bytes; Q: 52 diagonals of 43 bytes
        void ComputeBlock(const uint8_t* src, uint32_t majorCount, uint32_t minorCount, uint32_t majorMult, uint32_t minorInc,
                          uint8_t* dest)
        {
            const Tables& t = T();
            const uint32_t size = majorCount * minorCount;
            for (uint32_t major = 0; major < majorCount; major++)
            {
                uint32_t index = (major >> 1) * majorMult + (major & 1);
                uint8_t a = 0;
                uint8_t b = 0;
                for (uint32_t minor = 0; minor < minorCount; minor++)
                {
                    const uint8_t value = src[index];
                    index += minorInc;
                    if (index >= size)
                        index -= size;
                    a ^= value;
                    b ^= value;
                    a = t.forward[a];
                }
                a = t.backward[t.forward[a] ^ b];
                dest[major] = a;
                dest[major + majorCount] = static_cast<uint8_t>(a ^ b);
            }
        }

        constexpr size_t kPOffset = 0x81C;
        constexpr size_t kQOffset = 0x8C8;

        /// P and Q of `frame` into `p` (172 bytes) and `q` (104 bytes)
        void Parity(const uint8_t* frame, uint8_t* p, uint8_t* q)
        {
            // Mode 2: the address bytes count as zero (they change when the sector is copied)
            uint8_t work[kFrameBytes];
            std::memcpy(work, frame, kFrameBytes);
            if (frame[15] == 2)
                std::memset(work + 12, 0, 4);
            // Q covers the P parity: compute P into the work frame first
            ComputeBlock(work + 12, 86, 24, 2, 86, work + kPOffset);
            ComputeBlock(work + 12, 52, 43, 86, 88, work + kQOffset);
            std::memcpy(p, work + kPOffset, 172);
            std::memcpy(q, work + kQOffset, 104);
        }
    }  // namespace

    uint32_t ComputeEdc(const uint8_t* data, size_t length)
    {
        const Tables& t = T();
        uint32_t edc = 0;
        for (size_t i = 0; i < length; i++)
            edc = (edc >> 8) ^ t.edc[(edc ^ data[i]) & 0xFF];
        return edc;
    }

    void GenerateEcc(uint8_t* frame)
    {
        Parity(frame, frame + kPOffset, frame + kQOffset);
    }

    bool VerifyEcc(const uint8_t* frame)
    {
        uint8_t p[172];
        uint8_t q[104];
        Parity(frame, p, q);
        return std::memcmp(p, frame + kPOffset, sizeof(p)) == 0 && std::memcmp(q, frame + kQOffset, sizeof(q)) == 0;
    }

    void WriteHeader(uint8_t* frame, uint32_t lba, uint8_t mode)
    {
        std::memcpy(frame, kSync, sizeof(kSync));
        const Msf msf = LbaToMsf(lba);
        frame[12] = ToBcd(msf.m);
        frame[13] = ToBcd(msf.s);
        frame[14] = ToBcd(msf.f);
        frame[15] = mode;
    }

    namespace
    {
        void PutLe32(uint8_t* out, uint32_t value)
        {
            out[0] = static_cast<uint8_t>(value);
            out[1] = static_cast<uint8_t>(value >> 8);
            out[2] = static_cast<uint8_t>(value >> 16);
            out[3] = static_cast<uint8_t>(value >> 24);
        }
    }  // namespace

    void BuildMode1Frame(uint8_t* frame, uint32_t lba, const uint8_t* user)
    {
        WriteHeader(frame, lba, 1);
        std::memcpy(frame + 16, user, kUserBytes);
        PutLe32(frame + 2064, ComputeEdc(frame, 2064));
        std::memset(frame + 2068, 0, 8);
        GenerateEcc(frame);
    }

    void BuildMode2Form1Frame(uint8_t* frame, uint32_t lba, const uint8_t* user)
    {
        WriteHeader(frame, lba, 2);
        std::memset(frame + 16, 0, 8);
        std::memcpy(frame + 24, user, kUserBytes);
        PutLe32(frame + 2072, ComputeEdc(frame + 16, 2056));
        GenerateEcc(frame);
    }
}  // namespace cd
