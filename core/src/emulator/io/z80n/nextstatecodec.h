#pragma once

/// @file nextstatecodec.h
/// @brief The ZX Next's machine state as one byte image, for the time-travel debugger (design-ttd.md section 3, blob NextState):
/// the board (NextREG file, latches, reset bits), the memory map (MMU slots, paging bits, the DivMMC / Multiface / Layer 2 views), the
/// video registers (clip windows, palettes), sprites (attributes, patterns), the copper, the DMA, the CTC, the interrupt controller, the
/// I2C bus with its RTC, the Multiface, the DivMMC, the UARTs (registers, FIFOs, shifters; not their peers) and the esxDOS mode (open
/// handles, working directory). NOT in the image yet:
/// the SD cards' protocol state, the DivMMC / config-bank RAM of the system area. A restore re-derives the slot table from the image.
///
/// The image has a fixed length for the life of a machine (every array is whole), as the TTD serializer contract needs.

#include <cstddef>
#include <cstdint>
#include <vector>

class PortDecoder_Next;

struct NextStateCodec
{
    /// The machine's state as bytes (version byte first)
    static std::vector<uint8_t> Save(const PortDecoder_Next& decoder);
    /// Put a saved image back; false when it does not fit (wrong version or length), the machine untouched then
    static bool Load(PortDecoder_Next& decoder, const uint8_t* data, size_t size);

    struct Impl;  ///< the per-class field lists (nextstatecodec.cpp); a nested class of the befriended struct sees the private fields
};
