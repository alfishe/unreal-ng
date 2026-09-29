#pragma once

/// @file neogssound.h
/// @brief NeoGS sound block: eight sample latches, eight volumes and the mixer
/// (neogs-tdd.md §3.6; FPGA sound/sound_main.v, sound_mulacc.v, ports.v:549-570).
///
/// Capture: any memory read in #6000-#7FFF (opcode fetches included) copies
/// the byte into a channel latch - A9:A8 in 4-channel mode, A10:A8 with 8CHANS.
/// Volumes #06-#09 / #16-#19 are always stored (bits 5:0).
///
/// Mixer: sample x volume, the sample signed with its top bit flipped (sign =
/// NOT(d7 XOR INV7B): unsigned with #80 = silence normally, two's complement
/// with INV7B), 16-bit sums. The FPGA computes one side every 320 crystal
/// clocks, alternately; the mode bits are applied at mix time.
///
///   4 channels : L = 2 (c1 v1 + c2 v2)          R = 2 (c3 v3 + c4 v4)
///   8CHANS     : L = c1v1 + c2v2 + c5v5 + c6v6  R = c3v3 + c4v4 + c7v7 + c8v8
///   PAN4CH     : L = c1v1 + c2v2 + c3v5 + c4v6  R = c1v3 + c2v4 + c3v7 + c4v8
/// (PAN4CH only acts when 8CHANS is 0.)

#include <cstdint>

class NeoGSSound
{
public:
    static constexpr int CHANNELS = 8;

    // GSCFG0 mode bits
    static constexpr uint8_t CFG_8CHANS = 0x04;
    static constexpr uint8_t CFG_PAN4CH = 0x40;
    static constexpr uint8_t CFG_INV7B = 0x80;

    void powerOn()
    {
        for (int i = 0; i < CHANNELS; i++)
        {
            _sample[i] = 0x80;
            _volume[i] = 0;
        }
    }

    /// Memory read at `addr` returned `value`; `gscfg0` selects the decode
    /// (the caller has checked the #6000-#7FFF window)
    int capture(uint16_t addr, uint8_t value, uint8_t gscfg0)
    {
        const int channel = (gscfg0 & CFG_8CHANS) ? ((addr >> 8) & 7) : ((addr >> 8) & 3);
        _sample[channel] = value;
        return channel;
    }

    void setVolume(int channel, uint8_t value) { _volume[channel & 7] = static_cast<uint8_t>(value & 0x3F); }
    uint8_t volume(int channel) const { return _volume[channel & 7]; }
    uint8_t sample(int channel) const { return _sample[channel & 7]; }
    void setSampleRaw(int channel, uint8_t value) { _sample[channel & 7] = value; }

    /// One side of the mix as the FPGA computes it (16-bit two's complement)
    int32_t mix(bool right, uint8_t gscfg0) const
    {
        const int side = right ? 2 : 0;
        if (gscfg0 & CFG_8CHANS)
            return product(side, side, gscfg0) + product(side + 1, side + 1, gscfg0) +
                   product(side + 4, side + 4, gscfg0) + product(side + 5, side + 5, gscfg0);
        if (gscfg0 & CFG_PAN4CH)
            return product(0, side, gscfg0) + product(1, side + 1, gscfg0) + product(2, side + 4, gscfg0) +
                   product(3, side + 5, gscfg0);
        return 2 * (product(side, side, gscfg0) + product(side + 1, side + 1, gscfg0));
    }

    /// Sample value the multiplier sees (-128..127)
    static int32_t signedSample(uint8_t value, uint8_t gscfg0)
    {
        const uint8_t flip = (gscfg0 & CFG_INV7B) ? 0x00 : 0x80;
        return static_cast<int8_t>(static_cast<uint8_t>(value ^ flip));
    }

private:
    int32_t product(int sampleChannel, int volumeChannel, uint8_t gscfg0) const
    {
        return signedSample(_sample[sampleChannel], gscfg0) * static_cast<int32_t>(_volume[volumeChannel]);
    }

    uint8_t _sample[CHANNELS] = {0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80};
    uint8_t _volume[CHANNELS] = {};
};
