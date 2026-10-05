#pragma once

/// @file tsconfmemoryregions.h
/// @brief TS-Conf CRAM and SFILE as device memory regions (debugger additions tdd §2): the palette and the sprite
/// table live inside the chip, so every automation interface reaches them through DeviceMemory by name.
///
/// Layout (the FM window's, #000-#1FF and #200-#3FF): word n at offset 2n, low byte first. CRAM word n is color n
/// (bits 14-10 R, 9-5 G, 4-0 B, bit 15 the VDAC flag); SFILE words 3d..3d+2 are sprite d. Example: writing #1F,#00
/// at cram offset 2 makes color 1 pure blue.
///
/// A write changes one byte of the word and goes through PortDecoder_TSConf::CommitTableWord (the screen catches
/// up first, CRAM rebuilds the palette); it leaves the FM window's even-byte latch alone.

#include "emulator/memory/devicememory.h"
#include "emulator/ports/models/portdecoder_tsconf.h"

class TsConfTableRegion final : public IDeviceMemoryRegion
{
public:
    TsConfTableRegion(PortDecoder_TSConf& decoder, bool cram) : _decoder(decoder), _cram(cram) {}

    const char* Name() const override { return _cram ? "cram" : "sfile"; }
    const char* Description() const override
    {
        return _cram ? "TS-Conf palette: 256 colors, word n at offset 2n, low byte first (bits 14-10 R, 9-5 G, 4-0 B, "
                       "bit 15 VDAC); the FM window's #000-#1FF"
                     : "TS-Conf sprite table: 256 words, word n at offset 2n, low byte first; sprite d = words 3d, "
                       "3d+1, 3d+2; the FM window's #200-#3FF";
    }
    uint32_t Size() const override { return 512; }
    uint32_t PageSize() const override { return 512; }
    const char* WritePath() const override
    {
        return "PortDecoder_TSConf::CommitTableWord, as an FM-window write: the screen catches up first, CRAM rebuilds "
               "the palette";
    }
    uint8_t Read(uint32_t offset) const override
    {
        const uint16_t word = Table()[(offset >> 1) & 0xFF];
        return static_cast<uint8_t>((offset & 1) ? word >> 8 : word);
    }
    void Write(uint32_t offset, uint8_t value) override
    {
        const uint8_t index = static_cast<uint8_t>(offset >> 1);
        const uint16_t word = Table()[index];
        const uint16_t changed = (offset & 1) ? static_cast<uint16_t>((word & 0x00FF) | (value << 8))
                                              : static_cast<uint16_t>((word & 0xFF00) | value);
        _decoder.CommitTableWord(_cram, index, changed);
    }

private:
    const uint16_t* Table() const { return _cram ? _decoder.GetState().cram : _decoder.GetState().sfile; }

    PortDecoder_TSConf& _decoder;
    bool _cram;
};
