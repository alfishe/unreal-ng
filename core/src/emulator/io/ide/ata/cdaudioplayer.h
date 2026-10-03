#pragma once

/// @file cdaudioplayer.h
/// @brief The audio side of the ATAPI CD drive (CD-DA, Red Book): where the
/// optical head plays, at 75 frames (44100 stereo samples) per second of
/// EMULATED time, and its line output for the mixer, through the volume and
/// routing of MODE SENSE / SELECT page 0Eh. AtapiCdrom owns one and runs the
/// audio commands (PLAY AUDIO, PAUSE / RESUME, STOP, READ SUB-CHANNEL) on it.
///
/// Time base: the machine's frames. A frame lasts `config.frame` base
/// T-states (3.5 MHz) whatever the host speed or a hardware turbo, so the
/// head moves 44100 / 3.5 MHz samples per base T-state, in exact integers:
/// the head is kept in units of 1 / 3,500,000 sample. Inside a frame the head
/// is "frame start + elapsed base T-states" (the clock the IDE board gives:
/// EmulatorState::AudioTstate, divided by the host multiplier), so a guest that
/// polls READ SUB-CHANNEL mid-frame sees the head move. FrameEnd adds the whole
/// frame. Nothing here depends on the host's audio rate, turbo or sound
/// settings: that is all in the renderer.
///
/// | State | Head | Audio status (READ SUB-CHANNEL) |
/// |---|---|---|
/// | Idle | where play stopped | 15h no current status |
/// | Playing | frame start + elapsed | 11h |
/// | Paused | frozen | 12h |
/// | Completed | the play's last frame | 13h, once; then Idle |
/// | Error | where it stopped (it ran into a data track) | 14h, once; then Idle |
///
/// Worked example: PLAY AUDIO MSF 00:02:00 - 00:04:00 at T 1000 of a
/// Pentagon frame (71680 T): the head is LBA 0 + 1000 T x 44100 / 3.5 MHz =
/// 12.6 samples. After 98 frames (2.007 s) it passes LBA 150, the status
/// turns to 13h and the next READ SUB-CHANNEL reports it, then 15h.
///
/// Renderer (host side, not machine state): every frame it reads the disc
/// at a cursor that follows the head at the mixer's rate (linear
/// interpolation; at a 44100 Hz mixer exactly the track's samples), applies
/// page 0Eh, and leaves a stereo buffer for the mixer, or none while nothing
/// plays (the mixer then skips the source: no cost).

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <type_traits>

class CdImage;

enum class CdAudioStatus : uint8_t
{
    Idle = 0,
    Playing = 1,
    Paused = 2,
    Completed = 3,
    Error = 4,
};

/// The drive's audio state as TTD keeps it (no padding: its bytes are its value)
struct CdAudioState
{
    int64_t head = 0;              ///< sample x kUnitsPerSample; while playing relative to the frame start
    uint32_t playStartLba = 0;     ///< where the current play began (the renderer plays nothing before it)
    uint32_t endLba = 0;           ///< the play stops before this frame
    uint8_t status = 0;            ///< CdAudioStatus
    uint8_t sotc = 0;              ///< page 0Eh: stop on track crossing
    uint8_t portSelect[4] = {1, 2, 0, 0};  ///< page 0Eh: channels routed to output port 0..3 (bit 0 left, bit 1 right)
    uint8_t portVolume[4] = {0xFF, 0xFF, 0, 0};
    uint8_t reserved[6] = {};
};
static_assert(std::has_unique_object_representations_v<CdAudioState>, "CdAudioState must have no padding: its bytes are the TTD blob");
static_assert(sizeof(CdAudioState) == 32, "CdAudioState layout changed: bump the AtaChannel TTD version");

class CdAudioPlayer
{
public:
    /// Head units per sample: one base T-state moves the head 44100 units
    static constexpr int64_t kUnitsPerSample = 3'500'000;

    /// Base T-states elapsed in the current frame
    using Clock = std::function<uint32_t()>;

    CdAudioPlayer() = default;

    void SetClock(Clock clock) { _clock = std::move(clock); }
    void SetDisc(CdImage* disc);
    CdImage* Disc() const { return _disc; }

    /// region <Machine side (emulation thread)>
    /// Start playing [startLba, endLba); false (nothing changes) when the range is empty
    void Play(uint32_t startLba, uint32_t endLba);
    /// PAUSE / RESUME: false when no play is in progress (or paused, for resume)
    bool Pause();
    bool Resume();
    /// STOP PLAY / SCAN, a seek, a read, a disc change: the head stays where it is
    void Stop();
    /// Move the head (SEEK): stops play
    void SeekTo(uint32_t lba);

    CdAudioStatus Status();
    bool IsPlaying() { return Status() == CdAudioStatus::Playing; }
    bool InProgress() { const CdAudioStatus s = Status(); return s == CdAudioStatus::Playing || s == CdAudioStatus::Paused; }
    /// The status byte READ SUB-CHANNEL reports (11h-15h); a completed or failed play is reported once
    uint8_t TakeStatusCode();
    /// Peek at the status code without consuming it (automation)
    uint8_t PeekStatusCode();
    /// The head's frame (LBA) and the sample inside it
    uint32_t HeadLba();
    uint64_t HeadSample();

    /// The same without settling anything (automation, any thread: reads only)
    CdAudioStatus PeekStatus() const;
    uint64_t PeekHeadSample() const;

    /// The frame ended: `frameBaseT` base T-states passed
    void FrameEnd(uint32_t frameBaseT);
    /// Power-on / the reset line (DEVICE RESET keeps playing on real drives; the hard reset stops)
    void Reset();

    const CdAudioState& State() const { return _s; }
    void SetState(const CdAudioState& state) { _s = state; _renderValid = false; }
    CdAudioState& MutableState() { return _s; }
    /// endregion </Machine side>

    /// region <Renderer (host side)>
    /// Render this frame's output: `samples` stereo pairs at `rate` Hz. Call after FrameEnd
    void Render(size_t samples, size_t rate);
    /// The rendered frame, or nullptr when this frame carried no CD audio
    const int16_t* Buffer() const { return _hasOutput ? _out.data() : nullptr; }
    int16_t* MutableBuffer() { return _hasOutput ? _out.data() : nullptr; }
    /// A non-zero sample left the drive in the last rendered frame
    bool HadSoundLastFrame() const { return _hadSound; }
    /// No output this frame (sound off, turbo without audio)
    void RenderNothing() { _hasOutput = false; _hadSound = false; _renderValid = false; }
    /// endregion </Renderer>

private:
    static constexpr int64_t kSampleRate = 44100;
    static constexpr size_t kMaxOutput = 8192;  ///< stereo pairs a frame can ask for (MAX_SAMPLES_PER_FRAME)

    uint32_t Elapsed() const { return _clock ? _clock() : 0; }
    /// Absolute head position in units, without settling
    int64_t RawHead() const;
    /// A play that reached its end (or a data track) ends here
    void Settle();
    /// The sample at `index` (disc samples from LBA 0) of channel 0 / 1
    bool SampleAt(int64_t index, int32_t& left, int32_t& right);

    CdAudioState _s;
    Clock _clock;
    CdImage* _disc = nullptr;

    // Renderer
    std::array<int16_t, kMaxOutput * 2> _out{};
    bool _hasOutput = false;
    bool _hadSound = false;
    bool _renderValid = false;
    bool _wasPlaying = false;
    uint64_t _cursor = 0;          ///< render position, 32.32 fixed-point samples
    int64_t _cachedLba[2] = {-1, -1};
    std::array<int16_t, 588 * 2> _cache[2]{};
};
