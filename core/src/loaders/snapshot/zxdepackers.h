#pragma once

#include <cstddef>
#include <cstdint>

/// @file zxdepackers.h
/// @brief Depackers of two ZX Spectrum LZ formats, as used by SPG snapshots
/// (TS-Conf SDK) and many other ZX files.
///
/// Ported from lvd's mhmt (`mhmt-depack-megalz.c`, `mhmt-depack-hrust.c`,
/// GPL v3, [mhmt](https://github.com/lvd2/mhmt)) with bounds checks on both
/// streams: a corrupt stream fails instead of reading or writing past a buffer.
///
/// - **MegaLZ** (fyrex^mhm): the first byte is a literal, then an 8-bit bit
///   stream (MSB first, a new byte fetched when a bit is needed) interleaved
///   with data bytes; window 4352 bytes.
/// - **Hrust 1** (Dmitry Pyankov), raw stream without the "HR" header: a
///   16-bit little-endian bit stream (MSB of the second byte first, the next
///   word fetched as soon as the last bit is used), then the first literal;
///   window 64 KB, an expandable long displacement.
namespace ZxDepack
{
    /// @param out    destination, `capacity` bytes
    /// @param length bytes written
    /// @return false on a malformed or truncated stream or when the output does not fit
    bool MegaLz(const uint8_t* in, size_t inSize, uint8_t* out, size_t capacity, size_t& length);
    bool Hrust(const uint8_t* in, size_t inSize, uint8_t* out, size_t capacity, size_t& length);
}
