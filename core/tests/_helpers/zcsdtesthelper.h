#pragma once

/// @file zcsdtesthelper.h
/// @brief The SD protocol spoken through a Z-Controller data port, the way
/// guest code does it: commands, card init, one-block writes. Shared by the
/// ZX-Evo decoder tests and the EvoSdCard TTD serializer tests.

#include <cstdint>
#include <memory>

#include "emulator/io/storage/memorydisk.h"
#include "emulator/ports/models/portdecoder_atm3.h"

namespace zcsdtest
{
    /// Send one SD command through a port pair and return R1 (IN twice per
    /// byte: the first clocks it in, the second returns it)
    inline uint8_t SdCommand(PortDecoder_ATM3* decoder, uint16_t dataPort, uint8_t index, uint32_t arg, uint8_t crc = 0xFF)
    {
        decoder->DecodePortOut(dataPort, static_cast<uint8_t>(0x40 | index), 0);
        for (int shift = 24; shift >= 0; shift -= 8)
            decoder->DecodePortOut(dataPort, static_cast<uint8_t>(arg >> shift), 0);
        decoder->DecodePortOut(dataPort, crc, 0);
        decoder->DecodePortIn(dataPort, 0);  // the CRC exchange's byte (NCR)
        for (int i = 0; i < 16; i++)
        {
            const uint8_t r = decoder->DecodePortIn(dataPort, 0);
            if (r != 0xFF)
                return r;
        }
        return 0xFF;
    }

    /// CMD0, then ACMD41 with HCS until the card leaves idle
    inline bool SdInit(PortDecoder_ATM3* decoder, uint16_t dataPort)
    {
        if (SdCommand(decoder, dataPort, 0, 0, 0x95) != 0x01)
            return false;
        for (int tries = 0; tries < 20; tries++)
        {
            SdCommand(decoder, dataPort, 55, 0);
            if (SdCommand(decoder, dataPort, 41, 0x40000000) == 0x00)
                return true;
        }
        return false;
    }

    /// Write one block through #57 (CMD24, data token, 512 bytes, CRC, data
    /// response, busy); returns the data response
    inline uint8_t SdWriteBlock(PortDecoder_ATM3* decoder, uint32_t address, uint8_t fill)
    {
        if (SdCommand(decoder, 0x0057, 24, address) != 0x00)
            return 0xFF;
        decoder->DecodePortOut(0x0057, 0xFF, 0);
        decoder->DecodePortOut(0x0057, 0xFE, 0);
        for (int i = 0; i < 512; i++)
            decoder->DecodePortOut(0x0057, fill, 0);
        decoder->DecodePortOut(0x0057, 0xFF, 0);
        decoder->DecodePortOut(0x0057, 0xFF, 0);
        uint8_t response = 0xFF;
        for (int i = 0; i < 16 && response == 0xFF; i++)
            response = decoder->DecodePortIn(0x0057, 0);
        for (int i = 0; i < 200 && decoder->DecodePortIn(0x0057, 0) != 0xFF; i++)
        {
        }
        return static_cast<uint8_t>(response & 0x1F);
    }

    /// A disk whose every byte is known: sector s, offset o holds s + (s * 512 + o) % 7
    inline std::unique_ptr<MemoryDisk> PatternDisk(uint64_t sectors)
    {
        auto disk = std::make_unique<MemoryDisk>(sectors);
        for (uint64_t i = 0; i < sectors * 512; i++)
            disk->Data()[i] = static_cast<uint8_t>(i / 512 + i % 7);
        return disk;
    }
}  // namespace zcsdtest
