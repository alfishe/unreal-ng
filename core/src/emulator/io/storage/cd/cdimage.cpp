#include "stdafx.h"

#include "cdimage.h"

#include <cstring>
#include <sstream>

#include "emulator/io/storage/cd/cdecc.h"

using namespace cd;

const char* cd::TrackModeName(TrackMode mode)
{
    switch (mode)
    {
        case TrackMode::Audio: return "audio";
        case TrackMode::Mode1: return "mode1";
        case TrackMode::Mode2: return "mode2";
    }
    return "?";
}

CdImage::CdImage(std::vector<std::unique_ptr<IFrameSource>> sources, std::vector<StoredTrack> tracks, std::string format,
                 std::string description, uint64_t contentId)
    : _sources(std::move(sources)),
      _tracks(std::move(tracks)),
      _format(std::move(format)),
      _description(std::move(description)),
      _contentId(contentId)
{
}

CdImage::~CdImage() = default;

/// region <Disc>

int CdImage::TrackIndexAt(uint32_t lba) const
{
    if (_tracks.empty() || lba >= LeadOutLba())
        return -1;
    // Playback walks forward: the track of the previous call is the likely answer
    const int count = static_cast<int>(_tracks.size());
    if (_lastTrack < count && lba >= _tracks[_lastTrack].track.pregapLba && lba < _tracks[_lastTrack].track.endLba)
        return _lastTrack;
    for (int i = 0; i < count; i++)
    {
        if (lba < _tracks[i].track.endLba)
        {
            _lastTrack = i;
            return i;
        }
    }
    return -1;
}

uint8_t CdImage::IndexAt(uint32_t lba) const
{
    const int index = TrackIndexAt(lba);
    if (index < 0)
        return 1;
    return lba < _tracks[index].track.startLba ? 0 : 1;
}

int CdImage::TrackIndexForNumber(uint8_t number) const
{
    for (size_t i = 0; i < _tracks.size(); i++)
    {
        if (_tracks[i].track.number == number)
            return static_cast<int>(i);
    }
    return -1;
}

bool CdImage::HasAudio() const
{
    for (const StoredTrack& t : _tracks)
    {
        if (t.track.IsAudio())
            return true;
    }
    return false;
}

std::string CdImage::DescribeTracks() const
{
    std::ostringstream out;
    for (size_t i = 0; i < _tracks.size(); i++)
    {
        const Track& t = _tracks[i].track;
        if (i)
            out << ", ";
        out << int(t.number) << " " << TrackModeName(t.mode) << " " << t.startLba << "-" << (t.endLba ? t.endLba - 1 : 0);
    }
    return out.str();
}

/// endregion </Disc>

/// region <Reading>

bool CdImage::ReadStored(const StoredTrack& stored, uint32_t lba, uint8_t* dst, uint32_t offsetInFrame, uint32_t length)
{
    if (stored.source < 0 || lba < stored.storedFirstLba || lba >= stored.storedEndLba)
    {
        std::memset(dst, 0, length);
        return true;
    }
    const uint64_t at = stored.offset + static_cast<uint64_t>(lba - stored.storedFirstLba) * stored.stride + offsetInFrame;
    return _sources[stored.source]->Read(at, dst, length);
}

CdImage::ReadResult CdImage::ReadFrame(uint32_t lba, uint8_t* frame)
{
    const int index = TrackIndexAt(lba);
    if (index < 0)
        return ReadResult::OutOfRange;
    const StoredTrack& stored = _tracks[index];
    const bool present = stored.source >= 0 && lba >= stored.storedFirstLba && lba < stored.storedEndLba;

    if (stored.track.IsAudio())
    {
        if (!ReadStored(stored, lba, frame, 0, kFrameBytes))
            return ReadResult::IoError;
        if (present && stored.audioBigEndian)
        {
            for (uint32_t i = 0; i < kFrameBytes; i += 2)
                std::swap(frame[i], frame[i + 1]);
        }
        return ReadResult::Ok;
    }

    switch (stored.format)
    {
        case StoredFormat::Raw2352:
            if (!present)
                break;  // a gap of a data track: built below from zero data
            return ReadStored(stored, lba, frame, 0, kFrameBytes) ? ReadResult::Ok : ReadResult::IoError;
        case StoredFormat::Mode2_2336:
            if (!present)
                break;
            WriteHeader(frame, lba, 2);
            return ReadStored(stored, lba, frame + 16, 0, kFrameBytes - 16) ? ReadResult::Ok : ReadResult::IoError;
        case StoredFormat::Cooked2048:
            break;
    }

    uint8_t user[kUserBytes];
    if (!ReadStored(stored, lba, user, 0, kUserBytes))
        return ReadResult::IoError;
    if (stored.track.mode == TrackMode::Mode2)
        BuildMode2Form1Frame(frame, lba, user);
    else
        BuildMode1Frame(frame, lba, user);
    return ReadResult::Ok;
}

CdImage::ReadResult CdImage::ReadUser(uint32_t lba, uint8_t* user)
{
    const int index = TrackIndexAt(lba);
    if (index < 0)
        return ReadResult::OutOfRange;
    const StoredTrack& stored = _tracks[index];
    if (stored.track.IsAudio())
        return ReadResult::AudioTrack;

    uint32_t offset = 0;
    switch (stored.format)
    {
        case StoredFormat::Cooked2048: offset = 0; break;
        case StoredFormat::Mode2_2336: offset = 8; break;  // after the subheader
        case StoredFormat::Raw2352: offset = stored.track.mode == TrackMode::Mode2 ? 24 : 16; break;
    }
    return ReadStored(stored, lba, user, offset, kUserBytes) ? ReadResult::Ok : ReadResult::IoError;
}

CdImage::ReadResult CdImage::ReadAudio(uint32_t lba, int16_t* samples)
{
    const int index = TrackIndexAt(lba);
    if (index < 0)
        return ReadResult::OutOfRange;
    if (!_tracks[index].track.IsAudio())
    {
        std::memset(samples, 0, kFrameBytes);
        return ReadResult::Ok;  // a data frame plays as silence (real drives mute it)
    }
    uint8_t frame[kFrameBytes];
    const ReadResult result = ReadFrame(lba, frame);
    if (result != ReadResult::Ok)
        return result;
    for (uint32_t i = 0; i < kFrameBytes / 2; i++)
        samples[i] = static_cast<int16_t>(static_cast<uint16_t>(frame[2 * i] | (frame[2 * i + 1] << 8)));
    return ReadResult::Ok;
}

bool CdImage::ReadSector(uint64_t sector, uint8_t* dst)
{
    const uint64_t block = sector / 4;
    if (block >= LeadOutLba())
        return false;
    if (static_cast<int64_t>(block) != _cachedBlock)
    {
        if (ReadUser(static_cast<uint32_t>(block), _cache) != ReadResult::Ok)
        {
            _cachedBlock = -1;
            return false;
        }
        _cachedBlock = static_cast<int64_t>(block);
    }
    std::memcpy(dst, _cache + (sector % 4) * kSectorSize, kSectorSize);
    return true;
}

/// endregion </Reading>
