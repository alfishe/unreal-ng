#include "stdafx.h"

#include "cdaudioplayer.h"

#include <algorithm>
#include <cstdlib>

#include "emulator/io/storage/cd/cdimage.h"

namespace
{
    constexpr uint8_t kEndsInError = 0x01;  ///< reserved[0]: the play stops at a data track (status 14h)
    constexpr int64_t kSamplesPerFrame = cd::kSamplesPerFrame;
}  // namespace

void CdAudioPlayer::SetDisc(CdImage* disc)
{
    if (disc != _disc)
    {
        _cachedLba[0] = _cachedLba[1] = -1;
        _renderValid = false;
    }
    _disc = disc;
}

/// region <Machine side>

int64_t CdAudioPlayer::RawHead() const
{
    if (static_cast<CdAudioStatus>(_s.status) == CdAudioStatus::Playing)
        return _s.head + static_cast<int64_t>(Elapsed()) * kSampleRate;
    return _s.head;
}

void CdAudioPlayer::Settle()
{
    if (static_cast<CdAudioStatus>(_s.status) != CdAudioStatus::Playing)
        return;
    const int64_t end = static_cast<int64_t>(_s.endLba) * kSamplesPerFrame * kUnitsPerSample;
    if (RawHead() >= end)
    {
        // The head rests on the play's last frame (READ SUB-CHANNEL shows it)
        _s.head = end > 0 ? end - 1 : 0;
        _s.status = static_cast<uint8_t>((_s.reserved[0] & kEndsInError) ? CdAudioStatus::Error : CdAudioStatus::Completed);
    }
}

void CdAudioPlayer::Play(uint32_t startLba, uint32_t endLba)
{
    // A play crossing into a data track stops there with an error (14h);
    // SOTC stops at the end of the starting track (13h)
    uint8_t flags = 0;
    if (_disc)
    {
        const int first = _disc->TrackIndexAt(startLba);
        if (first >= 0 && _s.sotc)
            endLba = std::min(endLba, _disc->TrackAt(static_cast<size_t>(first)).endLba);
        for (size_t i = first < 0 ? _disc->TrackCount() : static_cast<size_t>(first); i < _disc->TrackCount(); i++)
        {
            const cd::Track& track = _disc->TrackAt(i);
            if (track.pregapLba >= endLba)
                break;
            if (!track.IsAudio())
            {
                endLba = std::max(startLba, track.pregapLba);
                flags |= kEndsInError;
                break;
            }
        }
        endLba = std::min(endLba, _disc->LeadOutLba());
    }
    _s.playStartLba = startLba;
    _s.endLba = endLba;
    _s.reserved[0] = flags;
    _s.head = static_cast<int64_t>(startLba) * kSamplesPerFrame * kUnitsPerSample - static_cast<int64_t>(Elapsed()) * kSampleRate;
    _s.status = static_cast<uint8_t>(CdAudioStatus::Playing);
    Settle();  // an empty range (it began at a data track) ends at once
}

bool CdAudioPlayer::Pause()
{
    Settle();
    switch (static_cast<CdAudioStatus>(_s.status))
    {
        case CdAudioStatus::Playing:
            _s.head = RawHead();
            _s.status = static_cast<uint8_t>(CdAudioStatus::Paused);
            return true;
        case CdAudioStatus::Paused: return true;
        default: return false;
    }
}

bool CdAudioPlayer::Resume()
{
    Settle();
    switch (static_cast<CdAudioStatus>(_s.status))
    {
        case CdAudioStatus::Paused:
            _s.head -= static_cast<int64_t>(Elapsed()) * kSampleRate;
            _s.status = static_cast<uint8_t>(CdAudioStatus::Playing);
            return true;
        case CdAudioStatus::Playing: return true;
        default: return false;
    }
}

void CdAudioPlayer::Stop()
{
    Settle();
    _s.head = RawHead();
    _s.status = static_cast<uint8_t>(CdAudioStatus::Idle);
}

void CdAudioPlayer::SeekTo(uint32_t lba)
{
    Stop();
    _s.head = static_cast<int64_t>(lba) * kSamplesPerFrame * kUnitsPerSample;
}

CdAudioStatus CdAudioPlayer::Status()
{
    Settle();
    return static_cast<CdAudioStatus>(_s.status);
}

uint8_t CdAudioPlayer::PeekStatusCode()
{
    switch (Status())
    {
        case CdAudioStatus::Playing: return 0x11;
        case CdAudioStatus::Paused: return 0x12;
        case CdAudioStatus::Completed: return 0x13;
        case CdAudioStatus::Error: return 0x14;
        case CdAudioStatus::Idle: break;
    }
    return 0x15;
}

uint8_t CdAudioPlayer::TakeStatusCode()
{
    const uint8_t code = PeekStatusCode();
    if (code == 0x13 || code == 0x14)
        _s.status = static_cast<uint8_t>(CdAudioStatus::Idle);  // reported once
    return code;
}

uint64_t CdAudioPlayer::HeadSample()
{
    Settle();
    const int64_t head = RawHead();
    return head <= 0 ? 0 : static_cast<uint64_t>(head / kUnitsPerSample);
}

uint32_t CdAudioPlayer::HeadLba()
{
    return static_cast<uint32_t>(HeadSample() / kSamplesPerFrame);
}

CdAudioStatus CdAudioPlayer::PeekStatus() const
{
    const CdAudioStatus status = static_cast<CdAudioStatus>(_s.status);
    if (status == CdAudioStatus::Playing && RawHead() >= static_cast<int64_t>(_s.endLba) * kSamplesPerFrame * kUnitsPerSample)
        return (_s.reserved[0] & kEndsInError) ? CdAudioStatus::Error : CdAudioStatus::Completed;
    return status;
}

uint64_t CdAudioPlayer::PeekHeadSample() const
{
    int64_t head = RawHead();
    const int64_t end = static_cast<int64_t>(_s.endLba) * kSamplesPerFrame * kUnitsPerSample;
    if (static_cast<CdAudioStatus>(_s.status) == CdAudioStatus::Playing && head >= end)
        head = end > 0 ? end - 1 : 0;
    return head <= 0 ? 0 : static_cast<uint64_t>(head / kUnitsPerSample);
}

void CdAudioPlayer::FrameEnd(uint32_t frameBaseT)
{
    // The CPU's frame counter was rebased before this runs: the new frame's
    // elapsed time counts from here, so the head moves the whole frame now
    if (static_cast<CdAudioStatus>(_s.status) == CdAudioStatus::Playing)
        _s.head += static_cast<int64_t>(frameBaseT) * kSampleRate;
    Settle();
}

void CdAudioPlayer::Reset()
{
    _s = CdAudioState{};
    _renderValid = false;
    _wasPlaying = false;
}

/// endregion </Machine side>

/// region <Renderer>

bool CdAudioPlayer::SampleAt(int64_t index, int32_t& left, int32_t& right)
{
    left = right = 0;
    if (!_disc || index < static_cast<int64_t>(_s.playStartLba) * kSamplesPerFrame ||
        index >= static_cast<int64_t>(_s.endLba) * kSamplesPerFrame)
        return false;
    const int64_t lba = index / kSamplesPerFrame;
    int slot = _cachedLba[0] == lba ? 0 : _cachedLba[1] == lba ? 1 : -1;
    if (slot < 0)
    {
        // Two frames cached: the one being played and the next (interpolation crosses frames)
        slot = _cachedLba[0] < _cachedLba[1] ? 0 : 1;
        if (_disc->ReadAudio(static_cast<uint32_t>(lba), _cache[slot].data()) != CdImage::ReadResult::Ok)
            _cache[slot].fill(0);
        _cachedLba[slot] = lba;
    }
    const size_t at = static_cast<size_t>(index % kSamplesPerFrame) * 2;
    left = _cache[slot][at];
    right = _cache[slot][at + 1];
    return true;
}

void CdAudioPlayer::Render(size_t samples, size_t rate)
{
    const CdAudioStatus status = Status();
    const bool playing = status == CdAudioStatus::Playing;
    const int64_t endSample = static_cast<int64_t>(_s.endLba) * kSamplesPerFrame;
    // A play that reached its end inside this frame still owes the frame its last samples
    const bool ended = status == CdAudioStatus::Completed || status == CdAudioStatus::Error;
    const bool tail = ended && _wasPlaying && _renderValid && static_cast<int64_t>(_cursor >> 32) < endSample;
    if (!playing && !tail)
    {
        _hasOutput = false;
        _hadSound = false;
        _wasPlaying = false;
        if (status != CdAudioStatus::Paused)
            _renderValid = false;  // a pause keeps the cursor: resuming continues without a jump
        return;
    }

    samples = std::min(samples, kMaxOutput);
    const uint64_t step = rate ? (static_cast<uint64_t>(kSampleRate) << 32) / rate : (1ull << 32);
    const int64_t target = static_cast<int64_t>(HeadSample());
    const int64_t expected = static_cast<int64_t>((_cursor + samples * step) >> 32);
    // The cursor follows the head; it jumps when they drift apart by more than two CD frames
    // (play start, seek, TTD restore). At a 44100 Hz mixer it never drifts
    if (!_renderValid || std::llabs(expected - target) > 2 * kSamplesPerFrame)
    {
        const int64_t start = target - static_cast<int64_t>((samples * step) >> 32);
        _cursor = start > 0 ? static_cast<uint64_t>(start) << 32 : 0;
    }

    const uint8_t select0 = _s.portSelect[0] & 3;
    const uint8_t select1 = _s.portSelect[1] & 3;
    const int32_t volume0 = _s.portVolume[0];
    const int32_t volume1 = _s.portVolume[1];
    auto route = [](uint8_t select, int32_t volume, int32_t left, int32_t right) -> int16_t {
        int32_t value = 0;
        switch (select)
        {
            case 1: value = left; break;
            case 2: value = right; break;
            case 3: value = (left + right) / 2; break;
            default: return 0;
        }
        if (volume != 0xFF)
            value = value * volume / 255;
        return static_cast<int16_t>(std::clamp(value, -32768, 32767));
    };

    bool sound = false;
    for (size_t i = 0; i < samples; i++)
    {
        const int64_t index = static_cast<int64_t>(_cursor >> 32);
        const uint32_t frac = static_cast<uint32_t>(_cursor);
        int32_t left = 0;
        int32_t right = 0;
        SampleAt(index, left, right);
        if (frac)
        {
            int32_t nextLeft = 0;
            int32_t nextRight = 0;
            SampleAt(index + 1, nextLeft, nextRight);
            left += static_cast<int32_t>((static_cast<int64_t>(nextLeft - left) * frac) >> 32);
            right += static_cast<int32_t>((static_cast<int64_t>(nextRight - right) * frac) >> 32);
        }
        const int16_t outLeft = route(select0, volume0, left, right);
        const int16_t outRight = route(select1, volume1, left, right);
        _out[2 * i] = outLeft;
        _out[2 * i + 1] = outRight;
        sound = sound || outLeft != 0 || outRight != 0;
        _cursor += step;
    }
    _hasOutput = true;
    _hadSound = sound;
    _renderValid = true;
    _wasPlaying = playing;
}

/// endregion </Renderer>
