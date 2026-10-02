#pragma once

/// @file cdtypes.h
/// @brief Compact Disc addressing and track layout shared by the CD image
/// formats (ISO, CUE/BIN, CHD) and the ATAPI drive.
///
/// A disc is a run of 2352-byte frames ("sectors"), 75 per second. The
/// program area starts at LBA 0, which is MSF 00:02:00: the first two seconds
/// (150 frames) are the lead-in pregap of track 1. Every track has an
/// INDEX 01 (its start, the address the TOC lists) and may have an INDEX 00
/// before it (its pregap, silence between audio tracks); the TOC's lead-out
/// (track #AA) is the first LBA past the last track.
///
/// | Track mode | Frame contents (2352 bytes) | User data |
/// |---|---|---|
/// | AUDIO | 588 stereo samples, 16-bit little-endian, left first | - |
/// | MODE1 | sync (12), header (4), data (2048), EDC (4), zero (8), ECC (276) | 2048 at 16 |
/// | MODE2 (XA form 1) | sync (12), header (4), subheader (8), data (2048), EDC, ECC | 2048 at 24 |
///
/// Worked example: LBA 16 (the ISO 9660 volume descriptor) is MSF 00:02:16;
/// a track starting at MSF 03:12:40 starts at LBA (3 * 60 + 12) * 75 + 40 - 150 = 14290.

#include <cstdint>
#include <string>

namespace cd
{
    constexpr uint32_t kFrameBytes = 2352;       ///< one raw frame (sector)
    constexpr uint32_t kSubcodeBytes = 96;       ///< P-W subchannel of one frame
    constexpr uint32_t kUserBytes = 2048;        ///< mode 1 / mode 2 form 1 user data
    constexpr uint32_t kFramesPerSecond = 75;
    constexpr uint32_t kSamplesPerFrame = 588;   ///< 44100 / 75 stereo sample pairs
    constexpr uint32_t kSampleRate = 44100;
    constexpr uint32_t kLeadInFrames = 150;      ///< LBA 0 = MSF 00:02:00
    constexpr uint8_t kLeadOutTrack = 0xAA;

    /// The 12-byte sync pattern every data frame starts with
    constexpr uint8_t kSync[12] = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};

    struct Msf
    {
        uint8_t m = 0;
        uint8_t s = 0;
        uint8_t f = 0;
    };

    /// LBA -> absolute MSF (adds the 150-frame lead-in)
    constexpr Msf LbaToMsf(uint32_t lba)
    {
        const uint32_t frames = lba + kLeadInFrames;
        return Msf{static_cast<uint8_t>(frames / (60 * kFramesPerSecond)),
                   static_cast<uint8_t>((frames / kFramesPerSecond) % 60), static_cast<uint8_t>(frames % kFramesPerSecond)};
    }
    /// A frame count -> MSF (no lead-in: track-relative times and lengths)
    constexpr Msf FramesToMsf(uint32_t frames)
    {
        return Msf{static_cast<uint8_t>(frames / (60 * kFramesPerSecond)),
                   static_cast<uint8_t>((frames / kFramesPerSecond) % 60), static_cast<uint8_t>(frames % kFramesPerSecond)};
    }
    /// Absolute MSF -> LBA (may be negative inside the lead-in: MSF 00:00:00 is LBA -150)
    constexpr int32_t MsfToLba(uint8_t m, uint8_t s, uint8_t f)
    {
        return static_cast<int32_t>((m * 60u + s) * kFramesPerSecond + f) - static_cast<int32_t>(kLeadInFrames);
    }
    /// MSF -> a frame count (no lead-in: CUE sheet times)
    constexpr uint32_t MsfToFrames(uint8_t m, uint8_t s, uint8_t f)
    {
        return (m * 60u + s) * kFramesPerSecond + f;
    }

    inline uint8_t ToBcd(uint8_t value)
    {
        return static_cast<uint8_t>(((value / 10) << 4) | (value % 10));
    }

    enum class TrackMode : uint8_t
    {
        Audio = 0,
        Mode1 = 1,  ///< 2048 user bytes per frame
        Mode2 = 2,  ///< read as XA form 1: 2048 user bytes after the subheader
    };

    /// How a track's frames are stored in its source
    enum class StoredFormat : uint8_t
    {
        Raw2352,     ///< whole frames (audio, MODE1/2352, MODE2/2352)
        Cooked2048,  ///< user data only (MODE1/2048, ISO)
        Mode2_2336,  ///< everything after the header (MODE2/2336)
    };

    struct Track
    {
        uint8_t number = 1;
        TrackMode mode = TrackMode::Mode1;
        uint32_t pregapLba = 0;  ///< INDEX 00 (== startLba without a pregap)
        uint32_t startLba = 0;   ///< INDEX 01: the TOC address
        uint32_t endLba = 0;     ///< first LBA after the track (the next track's INDEX 00, or the lead-out)

        bool IsAudio() const { return mode == TrackMode::Audio; }
        /// Q-channel CONTROL nibble: 0 (2-channel audio, no pre-emphasis) or 4 (data)
        uint8_t Control() const { return IsAudio() ? 0x00 : 0x04; }
        /// INDEX 01 to the end: what a player shows as the track length
        uint32_t Frames() const { return endLba - startLba; }
    };

    const char* TrackModeName(TrackMode mode);
}  // namespace cd
