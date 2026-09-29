#pragma once

/// @file gsaudioout.h
/// @brief Stereo output stage shared by the General Sound slot cards
/// (neogs-tdd.md §5.4): a sample-and-hold level per side, turned into band-
/// limited steps by a blip_buf pair and read out once per frame into the
/// card's interleaved int16 buffer.
///
/// The card calls set() whenever its mixed output may have changed, with the
/// position inside the current frame in the blip clock domain. Only real
/// changes add steps.

#include <cstddef>
#include <cstdint>

struct blip_t;

class GSAudioOut
{
public:
    GSAudioOut(double clockHz, size_t sampleRate);
    ~GSAudioOut();

    GSAudioOut(const GSAudioOut&) = delete;
    GSAudioOut& operator=(const GSAudioOut&) = delete;

    /// Blip input clock (card units that positions are given in) and output rate
    void setRates(double clockHz, size_t sampleRate);
    double clockHz() const { return _clockHz; }
    size_t sampleRate() const { return _sampleRate; }

    /// Drop pending steps (rate change, TTD restore, synthesis resumed)
    void clear();

    /// Power-on: levels back to 0 and pending steps dropped
    void reset();

    /// New output levels at `position` (clamped to [0, frameLength-1]).
    /// With `synthesize` false (turbo, or before the first frame base) the
    /// levels are tracked without adding steps. Returns true when a step was
    /// added (the frame had audible activity).
    bool set(int64_t position, int64_t frameLength, int32_t left, int32_t right, bool synthesize);

    /// Adopt levels without a step (TTD restore: the seek position already
    /// produced its own audio)
    void setLevels(int32_t left, int32_t right)
    {
        _lastL = left;
        _lastR = right;
    }
    int32_t lastLeft() const { return _lastL; }
    int32_t lastRight() const { return _lastR; }

    /// Close the frame (`frameLength` blip clocks) and read `samples` stereo
    /// samples into `buffer` (interleaved L/R); samples blip cannot supply are
    /// zero-filled. Returns the number of samples written.
    int endFrame(int64_t frameLength, int samples, int16_t* buffer);

private:
    blip_t* _blipL = nullptr;
    blip_t* _blipR = nullptr;
    double _clockHz;
    size_t _sampleRate;
    int32_t _lastL = 0;
    int32_t _lastR = 0;
};
