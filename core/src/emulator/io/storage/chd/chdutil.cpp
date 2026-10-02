#include "stdafx.h"

#include "chdutil.h"

#include "3rdparty/digestpp/algorithm/sha1.hpp"
#include "3rdparty/miniz/miniz.h"

namespace chd
{
    namespace
    {
        struct Crc16Table
        {
            uint16_t entries[256];
            Crc16Table()
            {
                for (uint32_t i = 0; i < 256; i++)
                {
                    uint32_t crc = i << 8;
                    for (int bit = 0; bit < 8; bit++)
                        crc = (crc & 0x8000) ? ((crc << 1) ^ 0x1021) : (crc << 1);
                    entries[i] = static_cast<uint16_t>(crc);
                }
            }
        };
    }  // namespace

    uint16_t Crc16(const uint8_t* data, size_t length)
    {
        static const Crc16Table table;
        uint16_t crc = 0xFFFF;
        for (size_t i = 0; i < length; i++)
            crc = static_cast<uint16_t>((crc << 8) ^ table.entries[((crc >> 8) ^ data[i]) & 0xFF]);
        return crc;
    }

    uint32_t Crc32(const uint8_t* data, size_t length)
    {
        return static_cast<uint32_t>(mz_crc32(MZ_CRC32_INIT, data, length));
    }

    struct Sha1Builder::State
    {
        digestpp::sha1 hasher;
    };

    Sha1Builder::Sha1Builder() : _state(new State) {}

    Sha1Builder::~Sha1Builder()
    {
        delete _state;
    }

    void Sha1Builder::Append(const uint8_t* data, size_t length)
    {
        _state->hasher.absorb(data, length);
    }

    Sha1 Sha1Builder::Finish()
    {
        Sha1 out{};
        _state->hasher.digest(out.data(), out.size());
        return out;
    }

    Sha1 Sha1Of(const uint8_t* data, size_t length)
    {
        Sha1Builder builder;
        builder.Append(data, length);
        return builder.Finish();
    }

    std::string Sha1Hex(const Sha1& sha1)
    {
        static const char* hex = "0123456789abcdef";
        std::string text;
        for (uint8_t b : sha1)
        {
            text.push_back(hex[b >> 4]);
            text.push_back(hex[b & 15]);
        }
        return text;
    }
}  // namespace chd
