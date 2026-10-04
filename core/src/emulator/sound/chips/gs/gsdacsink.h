#pragma once

/// @file gsdacsink.h
/// @brief Receiver of the General Sound DAC register writes for a board whose
/// DACs are shared with other sources (GSProfile::dacSink, gsprofile.h). No
/// emulator dependencies: the receiver (MultiSoundDacs) stays self-contained.

#include <cstdint>

/// Receiver of the GS DAC register writes when a board shares the DACs with
/// other sources (the MultiSound's MultiSoundDacs). Replaces the card's own
/// stereo mix: with a sink set, the card adds no steps to its own buffer.
///
/// `time` is the host time (the AudioTstate domain, the same axis the card's
/// frame bases use) of the instruction that made the access. The strobe ends
/// inside that instruction; the card CPU reports bus accesses per instruction,
/// so the start is the finest position it has (the classic card places its own
/// steps there too).
class IGSDacSink
{
public:
    virtual ~IGSDacSink() = default;

    /// GS memory read at #6000-#7FFF: `value` is the byte read (the raw bus byte), channel = A9-A8
    virtual void GsSample(uint64_t time, int channel, uint8_t value) = 0;
    /// GS port 6-9 write: the channel's 6-bit volume
    virtual void GsVolume(uint64_t time, int channel, uint8_t volume) = 0;
};
