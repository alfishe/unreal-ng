#pragma once

/// @file cdecc.h
/// @brief Building whole 2352-byte data frames: sync, header, EDC (CRC-32
/// with the polynomial #D8018001) and the P / Q Reed-Solomon parity of
/// ECMA-130 Annex A. Needed where an image stores less than the whole frame
/// (MODE1/2048 and ISO tracks, CHD frames whose ECC was stripped) and the
/// drive is asked for the raw frame (READ CD with the sync / header / EDC bits).
///
/// Layout of a mode 1 frame: 0..11 sync, 12..14 address (BCD MSF), 15 mode,
/// 16..2063 user data, 2064..2067 EDC over bytes 0..2063 (little-endian),
/// 2068..2075 zero, 2076..2247 P parity (86 x 2), 2248..2351 Q parity (52 x 2).
/// Mode 2 form 1 puts an 8-byte subheader at 16, the data at 24, the EDC over
/// 16..2071 at 2072, and computes the parity as if the address bytes were zero.

#include <cstddef>
#include <cstdint>

#include "emulator/io/storage/cd/cdtypes.h"

namespace cd
{
    /// CRC-32 (EDC) of `length` bytes
    uint32_t ComputeEdc(const uint8_t* data, size_t length);

    /// Fill the P and Q parity of a mode 1 / mode 2 form 1 frame (sync and header already in place)
    void GenerateEcc(uint8_t* frame);
    /// The frame's P and Q parity is what GenerateEcc would write
    bool VerifyEcc(const uint8_t* frame);

    /// Sync and header (BCD MSF of `lba`, mode byte)
    void WriteHeader(uint8_t* frame, uint32_t lba, uint8_t mode);

    /// A whole mode 1 frame from 2048 user bytes
    void BuildMode1Frame(uint8_t* frame, uint32_t lba, const uint8_t* user);
    /// A whole mode 2 form 1 frame from 2048 user bytes (subheader zero)
    void BuildMode2Form1Frame(uint8_t* frame, uint32_t lba, const uint8_t* user);
}  // namespace cd
