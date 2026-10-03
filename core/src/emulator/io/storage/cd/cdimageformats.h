#pragma once

/// @file cdimageformats.h
/// @brief The CD image formats the CD drive takes, each turned into a CdImage
/// (tracks + where their frames are).
///
/// | Format | Files | Tracks |
/// |---|---|---|
/// | `iso` | an ISO 9660 image (2048-byte blocks) | one MODE1 data track |
/// | `bin` | a lone raw image of 2352-byte frames with sync patterns | one MODE1 / MODE2 track (from the first header) |
/// | `cue` | a CUE sheet and its BINARY / MOTOROLA / WAVE files | every TRACK: AUDIO, MODE1/2048, MODE1/2352, MODE2/2336, MODE2/2352, CDG; INDEX 00 / 01, PREGAP, POSTGAP; several FILEs; sessions: REM SESSION nn, REM LEAD-OUT / LEAD-IN / PREGAP |
/// | `chd` | a MAME CD-ROM CHD (cdlz / cdzl / cdzs / cdfl, or uncompressed) | from the CHT2 / CHTR metadata (sessions: the CHSE entries); audio is stored big-endian |
///
/// Multisession: between two sessions lie the earlier one's lead-out and the next
/// one's lead-in, which no image stores (except ImgBurn's filler, marked by REM
/// LEAD-OUT in a one-file sheet): the reader puts back REM LEAD-OUT / LEAD-IN, else
/// the standard 6750 (2250 after a later session) + 4500 frames. Example
/// (tools/cd/make-audio-disc.py): `REM SESSION 01`, audio tracks 1-3 to LBA 4800 in
/// the BIN, `REM SESSION 02`, track 4 MODE2/2352 with its pregap in the BIN: track 4's
/// pregap is LBA 4800 + 11250 = 16050, its INDEX 01 LBA 16200.
///
/// Worked example (a CUE sheet with a data track and an audio track in two files):
/// @code
///   FILE "game.bin" BINARY
///     TRACK 01 MODE1/2352
///       INDEX 01 00:00:00
///   FILE "music.wav" WAVE
///     TRACK 02 AUDIO
///       PREGAP 00:02:00
///       INDEX 01 00:00:00
/// @endcode
/// game.bin holds 1000 frames: track 1 is LBA 0-999; track 2's two-second
/// pregap (not stored) is LBA 1000-1149 and its INDEX 01 is LBA 1150.

#include <memory>
#include <string>

#include "emulator/io/storage/cd/cdimage.h"

namespace CdImageFormats
{
    /// "iso", "bin", "cue", "chd" by content (and the extension for a CUE
    /// sheet), "" with a reason for anything that is no CD image
    std::string Probe(const std::string& path, std::string* error = nullptr);

    /// Open by Probe's verdict
    std::unique_ptr<CdImage> Open(const std::string& path, std::string* error = nullptr);

    std::unique_ptr<CdImage> OpenIso(const std::string& path, std::string* error = nullptr);
    std::unique_ptr<CdImage> OpenRawBin(const std::string& path, std::string* error = nullptr);
    std::unique_ptr<CdImage> OpenCue(const std::string& path, std::string* error = nullptr);
    std::unique_ptr<CdImage> OpenChd(const std::string& path, std::string* error = nullptr);

    /// A CUE sheet's text; FILE names resolve against `folder`. `sheetPath`
    /// names the image in descriptions
    std::unique_ptr<CdImage> ParseCue(const std::string& text, const std::string& folder, const std::string& sheetPath,
                                      std::string* error = nullptr);

    /// A MAME CD-ROM CHD (CHT2 / CHTR metadata) or not
    bool IsCdChd(const std::string& path);
}  // namespace CdImageFormats
