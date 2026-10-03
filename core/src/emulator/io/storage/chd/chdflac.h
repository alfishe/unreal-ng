#pragma once

/// @file chdflac.h
/// @brief FLAC audio frames for the CHD `flac` codec: a hunk is read as 16-bit
/// stereo samples and stored as bare FLAC frames (no "fLaC" marker, no
/// STREAMINFO: the reader knows the block size from the hunk size).
///
/// - **Decoder**: the whole frame syntax of RFC 9639 that a 16-bit stream can
///   carry: every block size and channel assignment (independent, left/side,
///   side/right, mid/side), CONSTANT, VERBATIM, FIXED (orders 0-4) and LPC
///   (orders 1-32) subframes, wasted bits, Rice and Rice2 partitions with
///   escapes, and both frame CRCs. It reads what libFLAC writes for MAME.
/// - **Encoder**: CONSTANT, VERBATIM and FIXED subframes with the best stereo
///   decorrelation and Rice partition order per frame. No LPC: the frames are
///   valid FLAC, only less compact than libFLAC's; CHD keeps whichever codec
///   gives the smallest hunk anyway.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace chd::flac
{
    /// Encode `frames` sample frames of `channels` interleaved 16-bit samples
    /// in FLAC frames of `blockSize` samples. Appends to `out`
    void Encode(const int16_t* samples, uint32_t frames, int channels, uint32_t blockSize, std::vector<uint8_t>& out);

    /// Decode FLAC frames into exactly `frames` sample frames of `channels`
    /// interleaved 16-bit samples. False on a syntax or CRC error, or when the
    /// data ends early. `consumed` (optional): the bytes the frames took (the
    /// CD codec `cdfl` stores the subcode right after them)
    bool Decode(const uint8_t* src, size_t length, int16_t* samples, uint32_t frames, int channels, size_t* consumed = nullptr);
}  // namespace chd::flac
