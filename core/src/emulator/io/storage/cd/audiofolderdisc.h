#pragma once

/// @file audiofolderdisc.h
/// @brief A host folder of MP3 / FLAC / WAV files as a Red Book audio CD (CD-DA): what a
/// CD burner makes of a playlist. The media manager mounts it in any CD slot
/// (`format: audio-cd`, or a folder with audio files in it); the drive sees a pressed
/// audio disc: one session, audio tracks only, read-only.
///
/// | Rule | Value |
/// |---|---|
/// | Files | the folder's own regular files (no subfolders) named *.mp3, *.flac, *.wav / *.wave, in natural order; hidden files (".name") and other files are ignored (counted in the report) |
/// | Natural order | names compared ignoring ASCII case, a run of digits as one number ("2 b" < "10 a"), ties by the bytes of the name |
/// | Track | one per file, decoded to 44100 Hz 16-bit stereo (AudioFileDecoder), padded with silence to whole frames and to the Red Book minimum of 4 s (300 frames) |
/// | Pregap | track 1: the disc's 2-second lead-in pregap (LBA -150..-1, not addressable); every later track: a 2-second INDEX 00 pregap of silence, as a disc burned track-at-once has (cdrecord's default) |
/// | Tracks | at most 99 (Red Book) |
/// | Capacity | 80 minutes: the lead-out starts at 80:00:00 at the latest (360 000 frames from the start of the program area's pregap, LBA 359 850): an 80-minute CD-R, the disc every burner writes today. The Red Book's nominal 74 minutes (333 000 frames) would refuse albums a burner accepts |
/// | Selection | the files in order while they fit: the first file that does not fit (or the 100th) ends the disc, the rest are not taken. No packing: a later shorter file is not tried |
/// | Errors | a file that does not decode is skipped with the reason and the next one is tried |
///
/// Decoding happens at mount, all of it (naive v1): the disc's PCM is held in memory
/// (10.1 MiB per minute, about 808 MiB for a full 80-minute disc). Nothing reads the
/// folder afterwards: changing it does not change the mounted disc.
///
/// Identity (ContentId, the media manager's and TTD's): FNV-1a over each taken file's
/// name and bytes and the track layout. The same folder content (wherever it is) builds the
/// same disc, sample for sample, and has the same identity.
///
/// Worked example: a folder with "1 intro.mp3" (0:30), "2 song.flac" (3:00), "10 outro.wav"
/// (0:02) and cover.jpg: track 1 = intro at LBA 0, track 2 = song at LBA 2400 (pregap from
/// 2250), track 3 = outro padded to 4 s at LBA 16050; cover.jpg ignored.

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "emulator/io/storage/cd/cdimage.h"

namespace AudioFolderDisc
{
    /// 80 minutes from the program area's start (track 1's pregap included)
    constexpr uint32_t kCapacityFrames = 360000;
    constexpr uint32_t kMinTrackFrames = 300;  ///< 4 seconds
    constexpr uint8_t kMaxTracks = 99;

    struct Options
    {
        uint32_t capacityFrames = kCapacityFrames;
        std::function<bool()> cancelRequested;
        /// After every file: files looked at, bytes read (the media panel's progress and stall watchdog)
        std::function<void(uint64_t files, uint64_t bytes)> onProgress;
    };

    struct FileEntry
    {
        std::string name;
        std::string kind;       ///< "mp3", "flac", "wav"
        enum class Status : uint8_t { Taken, Skipped, NotTaken } status = Status::NotTaken;
        std::string reason;     ///< why it was skipped / not taken
        uint8_t track = 0;      ///< taken: its track number
        uint32_t frames = 0;    ///< taken: the track's frames (after padding)
        uint64_t samples = 0;   ///< taken: decoded 44.1 kHz sample pairs (before padding)
    };

    struct Result
    {
        std::vector<FileEntry> files;  ///< every audio file, in disc order
        uint32_t ignored = 0;          ///< other entries of the folder (not audio, hidden, subfolders)
        uint32_t leadOutLba = 0;
        double seconds = 0;            ///< the build's wall time
        /// One line per fact for people (the insert's report)
        std::vector<std::string> Lines() const;
    };

    /// Natural order of two names: < 0, 0, > 0
    int NaturalCompare(const std::string& a, const std::string& b);

    /// The folder holds at least one MP3 / FLAC / WAV file of its own
    bool HasAudioFiles(const std::string& folder);

    /// Build the disc. nullptr with `error` when nothing could be taken, the folder cannot be read
    /// or the build was cancelled
    std::unique_ptr<CdImage> Build(const std::string& folder, Result& result, std::string* error = nullptr,
                                   const Options& options = Options{});
}  // namespace AudioFolderDisc
