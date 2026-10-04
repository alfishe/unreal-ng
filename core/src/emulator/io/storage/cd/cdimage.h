#pragma once

/// @file cdimage.h
/// @brief A Compact Disc as the CD drive sees it: tracks (audio and data),
/// whole 2352-byte frames, 2048-byte user blocks of the data tracks, and the
/// same disc as 512-byte sectors (IBlockDevice) for everything that only reads
/// data. One class for every format: ISO, CUE/BIN (with WAVE files), MAME CHD.
/// The formats only describe where each track's frames are stored.
///
/// | Read | Audio track | Data track |
/// |---|---|---|
/// | ReadFrame (2352) | the samples, little-endian, as the drive outputs them | the whole frame; sync, header, EDC and ECC built when the image stores less |
/// | ReadUser (2048) | `AudioTrack` (the drive answers ILLEGAL MODE FOR THIS TRACK) | the user data |
/// | ReadSector (512) | false | a quarter of the user block |
///
/// Frames between a track's stored data (a PREGAP / POSTGAP the image does
/// not store, the gap before INDEX 01 of a disc that skipped it) read as
/// silence / zero data.
///
/// Worked example (a mixed-mode disc: data track + two audio tracks with a
/// 2-second pregap stored in the BIN, `INDEX 00 05:00:00`, `INDEX 01 05:02:00`):
/// track 2 has pregapLba 22350, startLba 22500; READ TOC lists LBA 22500;
/// PLAY AUDIO from 22350 plays the pregap's silence first.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "emulator/media/mediareadjournal.h"
#include "emulator/io/storage/cd/cdtypes.h"
#include "emulator/io/storage/iblockdevice.h"

namespace cd
{
    /// Bytes of a track's storage: a file, a WAVE file's data chunk, a CHD
    class IFrameSource
    {
    public:
        virtual ~IFrameSource() = default;
        /// Read `length` bytes at `offset`; past the end reads zeros. False on an I/O error
        virtual bool Read(uint64_t offset, uint8_t* dst, uint32_t length) = 0;
        virtual std::string Describe() const = 0;
    };

    /// A track and where its frames are
    struct StoredTrack
    {
        Track track;
        int source = -1;                          ///< index into the image's sources; -1: nothing stored
        StoredFormat format = StoredFormat::Raw2352;
        uint64_t offset = 0;                      ///< byte offset of the frame at storedFirstLba
        uint32_t stride = kFrameBytes;            ///< bytes from one stored frame to the next
        uint32_t storedFirstLba = 0;              ///< frames [storedFirstLba, storedEndLba) are stored
        uint32_t storedEndLba = 0;
        bool audioBigEndian = false;              ///< audio samples stored big-endian (CHD, CUE MOTOROLA)
    };
}  // namespace cd

class CdImage : public IBlockDevice
{
public:
    enum class ReadResult : uint8_t
    {
        Ok,
        AudioTrack,  ///< user data asked of an audio frame
        OutOfRange,  ///< past the lead-out
        IoError
    };

    /// `tracks` in track order, LBAs ascending, no overlap, the first starting at 0.
    /// `format`: "iso", "cue", "chd"
    CdImage(std::vector<std::unique_ptr<cd::IFrameSource>> sources, std::vector<cd::StoredTrack> tracks, std::string format,
            std::string description, uint64_t contentId);
    ~CdImage() override;

    /// region <Disc>
    const std::string& Format() const { return _format; }
    size_t TrackCount() const { return _tracks.size(); }
    const cd::Track& TrackAt(size_t index) const { return _tracks[index].track; }
    const cd::StoredTrack& StoredAt(size_t index) const { return _tracks[index]; }
    uint8_t FirstTrackNumber() const { return _tracks.empty() ? 1 : _tracks.front().track.number; }
    uint8_t LastTrackNumber() const { return _tracks.empty() ? 1 : _tracks.back().track.number; }
    /// The first LBA after the last track (the TOC's lead-out: the last session's)
    uint32_t LeadOutLba() const { return _tracks.empty() ? 0 : _tracks.back().track.endLba; }
    /// The track an LBA belongs to (its pregap included); -1 past the lead-out and in the
    /// lead-out / lead-in between two sessions (nothing there is readable)
    int TrackIndexAt(uint32_t lba) const;

    /// Sessions (1 for every single-session disc): tracks carry ascending session numbers
    uint8_t SessionCount() const { return _tracks.empty() ? 1 : _tracks.back().track.session; }
    /// The first / last track index of a session; -1 when there is none
    int FirstTrackIndexOfSession(uint8_t session) const;
    int LastTrackIndexOfSession(uint8_t session) const;
    /// A session's lead-out: the first LBA after its last track
    uint32_t SessionLeadOutLba(uint8_t session) const;
    /// The index number at an LBA: 0 in a pregap, 1 after INDEX 01
    uint8_t IndexAt(uint32_t lba) const;
    /// -1 when there is no track with that number
    int TrackIndexForNumber(uint8_t number) const;
    bool HasAudio() const;
    /// endregion </Disc>

    /// region <Reading>
    /// The whole frame at `lba` (2352 bytes). A drive's read: time travel
    /// records it (or, replaying, hands back the recorded frame)
    ReadResult ReadFrame(uint32_t lba, uint8_t* frame);
    /// 2048 user bytes of a data frame. A drive's read, recorded as ReadFrame
    ReadResult ReadUser(uint32_t lba, uint8_t* user);
    /// The frame's 16-bit stereo samples (588 left / right pairs, host order) of an audio frame; silence otherwise.
    /// Not recorded: the samples are the drive's output to the host, not machine state
    ReadResult ReadAudio(uint32_t lba, int16_t* samples);

    /// Time travel's media read journal for the drive's data reads (the media
    /// manager binds it with the slot, as MediaReadTap); null = none
    void BindReadJournal(IMediaReadJournal* const* journal, std::string slot)
    {
        _journal = journal;
        _journalSlot = std::move(slot);
    }
    /// endregion </Reading>

    /// region <IBlockDevice: the data blocks as 512-byte sectors>
    uint64_t SectorCount() const override { return static_cast<uint64_t>(LeadOutLba()) * 4; }
    bool ReadSector(uint64_t lba, uint8_t* dst) override;
    bool WriteSector(uint64_t, const uint8_t*) override { return false; }
    bool IsWritable() const override { return false; }
    std::string Describe() const override { return _description; }
    uint64_t ContentId() const override { return _contentId; }
    /// endregion </IBlockDevice>

    /// A name per track (an audio CD built from a folder: the file each track came from); "" without one
    const std::string& TrackTitle(size_t index) const;
    void SetTrackTitles(std::vector<std::string> titles) { _titles = std::move(titles); }

    /// One line per track for people: "1 mode1 0-1234, 2 audio 1235-..." (with "session 2:"
    /// before the first track of every later session of a multisession disc)
    std::string DescribeTracks() const;

private:
    /// The reads themselves, unrecorded (ReadAudio, ReadSector: the block stack's tap records those)
    ReadResult ReadFrameStored(uint32_t lba, uint8_t* frame);
    ReadResult ReadUserStored(uint32_t lba, uint8_t* user);
    /// Recording / playback around a drive's read of @p size bytes
    template <typename Read>
    ReadResult Journaled(uint32_t lba, uint8_t* dst, uint32_t size, Read&& read);

    IMediaReadJournal* const* _journal = nullptr;
    std::string _journalSlot;

    bool ReadStored(const cd::StoredTrack& stored, uint32_t lba, uint8_t* dst, uint32_t offsetInFrame, uint32_t length);

    std::vector<std::unique_ptr<cd::IFrameSource>> _sources;
    std::vector<cd::StoredTrack> _tracks;
    std::string _format;
    std::string _description;
    std::vector<std::string> _titles;
    uint64_t _contentId = 0;
    mutable int _lastTrack = 0;
    // ReadSector reads a block as four sectors: the block it decoded last
    int64_t _cachedBlock = -1;
    uint8_t _cache[cd::kUserBytes] = {};
};
