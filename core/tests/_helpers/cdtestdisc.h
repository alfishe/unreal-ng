#pragma once

/// @file cdtestdisc.h
/// @brief Synthetic CD images for the CD layer and CD audio tests: sawtooth
/// ramps whose every sample is known from its position, tones for listening
/// checks, data frames, WAVE files, and CUE sheets that tie them together.
///
/// The ramps are the ones tools/cd/make-test-fixtures.py writes into the CHD
/// fixtures (testdata/media/cd/), so a CHD and a CUE/BIN built here hold the
/// same samples:
///   ramp A: left = n * 64, right = n * -96            (n = sample index in the track, 16-bit wrap)
///   ramp B: left = 1000 + n * 32, right = n * 48

#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "emulator/io/storage/cd/cdecc.h"

namespace cdtest
{
    constexpr uint32_t kFrame = 2352;
    constexpr uint32_t kSamples = 588;

    struct Ramp
    {
        int32_t leftStep = 64;
        int32_t rightStep = -96;
        int32_t leftBase = 0;

        int16_t Left(uint64_t n) const { return static_cast<int16_t>(static_cast<uint16_t>(leftBase + static_cast<int64_t>(n) * leftStep)); }
        int16_t Right(uint64_t n) const { return static_cast<int16_t>(static_cast<uint16_t>(static_cast<int64_t>(n) * rightStep)); }
    };
    inline const Ramp kRampA{64, -96, 0};
    inline const Ramp kRampB{32, 48, 1000};

    /// `frames` frames of a ramp, little-endian (BINARY / WAVE) or big-endian (MOTOROLA)
    inline std::string RampPcm(const Ramp& ramp, uint32_t frames, bool bigEndian = false)
    {
        std::string out(static_cast<size_t>(frames) * kFrame, '\0');
        for (uint64_t n = 0; n < static_cast<uint64_t>(frames) * kSamples; n++)
        {
            const uint16_t left = static_cast<uint16_t>(ramp.Left(n));
            const uint16_t right = static_cast<uint16_t>(ramp.Right(n));
            char* at = out.data() + n * 4;
            if (bigEndian)
            {
                at[0] = static_cast<char>(left >> 8), at[1] = static_cast<char>(left);
                at[2] = static_cast<char>(right >> 8), at[3] = static_cast<char>(right);
            }
            else
            {
                at[0] = static_cast<char>(left), at[1] = static_cast<char>(left >> 8);
                at[2] = static_cast<char>(right), at[3] = static_cast<char>(right >> 8);
            }
        }
        return out;
    }

    /// A sine tone (left `hz`, right `hz` * 1.5), little-endian, amplitude `amplitude`
    inline std::string TonePcm(uint32_t frames, double hz, int amplitude = 12000)
    {
        std::string out(static_cast<size_t>(frames) * kFrame, '\0');
        for (uint64_t n = 0; n < static_cast<uint64_t>(frames) * kSamples; n++)
        {
            const double t = static_cast<double>(n) / 44100.0;
            const int16_t left = static_cast<int16_t>(amplitude * std::sin(2 * 3.14159265358979323846 * hz * t));
            const int16_t right = static_cast<int16_t>(amplitude * std::sin(2 * 3.14159265358979323846 * hz * 1.5 * t));
            char* at = out.data() + n * 4;
            at[0] = static_cast<char>(left), at[1] = static_cast<char>(static_cast<uint16_t>(left) >> 8);
            at[2] = static_cast<char>(right), at[3] = static_cast<char>(static_cast<uint16_t>(right) >> 8);
        }
        return out;
    }

    /// The user data of block `lba` on the test discs (as the fixture script writes it)
    inline std::vector<uint8_t> UserData(uint32_t lba)
    {
        char line[64];
        std::snprintf(line, sizeof(line), "UNREAL-NG CD TEST DISC, LBA %02u. ", lba);
        std::vector<uint8_t> out(cd::kUserBytes);
        const size_t length = std::strlen(line);
        for (size_t i = 0; i < out.size(); i++)
            out[i] = static_cast<uint8_t>(line[i % length]);
        return out;
    }

    /// `frames` MODE1 frames from LBA `first` (whole 2352-byte frames, or the 2048 user bytes)
    inline std::string DataFrames(uint32_t first, uint32_t frames, bool raw)
    {
        std::string out;
        for (uint32_t i = 0; i < frames; i++)
        {
            const std::vector<uint8_t> user = UserData(first + i);
            if (raw)
            {
                uint8_t frame[kFrame];
                cd::BuildMode1Frame(frame, first + i, user.data());
                out.append(reinterpret_cast<const char*>(frame), kFrame);
            }
            else
            {
                out.append(reinterpret_cast<const char*>(user.data()), user.size());
            }
        }
        return out;
    }

    inline std::string Wave(const std::string& pcm)
    {
        auto u32 = [](uint32_t v) { return std::string{static_cast<char>(v), static_cast<char>(v >> 8), static_cast<char>(v >> 16), static_cast<char>(v >> 24)}; };
        auto u16 = [](uint16_t v) { return std::string{static_cast<char>(v), static_cast<char>(v >> 8)}; };
        std::string out = "RIFF" + u32(static_cast<uint32_t>(36 + pcm.size())) + "WAVE";
        out += "fmt " + u32(16) + u16(1) + u16(2) + u32(44100) + u32(44100 * 4) + u16(4) + u16(16);
        out += "data" + u32(static_cast<uint32_t>(pcm.size())) + pcm;
        return out;
    }

    inline std::string Utf8(const std::filesystem::path& path)
    {
        const auto u8 = path.u8string();
        return std::string(u8.begin(), u8.end());
    }

    inline void WriteFile(const std::filesystem::path& path, const std::string& bytes)
    {
        std::ofstream(path, std::ios::binary | std::ios::trunc).write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }

    /// A mixed-mode disc as one BIN per track (the CHD fixtures' layout):
    ///   track 1 MODE1/2352 LBA 0-3; track 2 AUDIO pregap (stored) LBA 4-7, INDEX 01 at 8, ramp A to 15;
    ///   track 3 AUDIO PREGAP 2 (not stored) LBA 16-17, INDEX 01 at 18, ramp B to 21; lead-out 22.
    /// Returns the CUE sheet's path
    inline std::string WriteFixtureDisc(const std::filesystem::path& folder)
    {
        WriteFile(folder / "track1.bin", DataFrames(0, 4, true));
        WriteFile(folder / "track2.bin", std::string(4 * kFrame, '\0') + RampPcm(kRampA, 8));
        WriteFile(folder / "track3.bin", RampPcm(kRampB, 4));
        WriteFile(folder / "disc.cue",
                  "FILE \"track1.bin\" BINARY\r\n  TRACK 01 MODE1/2352\r\n    INDEX 01 00:00:00\r\n"
                  "FILE \"track2.bin\" BINARY\r\n  TRACK 02 AUDIO\r\n    INDEX 00 00:00:00\r\n    INDEX 01 00:00:04\r\n"
                  "FILE \"track3.bin\" BINARY\r\n  TRACK 03 AUDIO\r\n    PREGAP 00:00:02\r\n    INDEX 01 00:00:00\r\n");
        return Utf8(folder / "disc.cue");
    }

    /// A data track and `tracks` audio tracks of `seconds` each (tones of rising pitch,
    /// a 2-second pregap stored before every audio track), one BIN. For the playback
    /// and real-software tests. Returns the CUE sheet's path
    inline std::string WriteMusicDisc(const std::filesystem::path& folder, int tracks, uint32_t seconds, uint32_t dataFrames = 300)
    {
        std::string bin = DataFrames(0, dataFrames, true);
        std::string cue = "FILE \"music.bin\" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n";
        uint32_t frame = dataFrames;
        for (int t = 0; t < tracks; t++)
        {
            char line[128];
            const auto msf = [](uint32_t f) {
                char text[16];
                std::snprintf(text, sizeof(text), "%02u:%02u:%02u", f / 4500, (f / 75) % 60, f % 75);
                return std::string(text);
            };
            std::snprintf(line, sizeof(line), "  TRACK %02d AUDIO\n    INDEX 00 %s\n    INDEX 01 %s\n", t + 2, msf(frame).c_str(),
                          msf(frame + 150).c_str());
            cue += line;
            bin += std::string(150 * kFrame, '\0');
            bin += TonePcm(seconds * 75, 330.0 * (t + 1));
            frame += 150 + seconds * 75;
        }
        WriteFile(folder / "music.bin", bin);
        WriteFile(folder / "music.cue", cue);
        return Utf8(folder / "music.cue");
    }
}  // namespace cdtest
