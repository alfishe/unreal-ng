#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <string_view>

/// @brief Chip-agnostic audio character chain for post-processing
///
/// Provides punch enhancement and room simulation for headphone listening.
/// Designed to be inserted after chip rendering, before final output.
///
/// Signal chain: Input → Punch → Room Simulation → Output
///
/// ## Punch Enhancement
///
/// Hybrid transient designer + exciter:
/// - Edge component: constant +6dB/oct tilt (first difference * edgeBlend)
/// - Transient component: envelope-gated boost on attacks (diff * env * transBoost)
///
/// For AY, use gentler settings (edgeBlend=0.03, transBoost=0.1) because
/// square waves already have rich harmonics. Paula/beeper benefit from
/// stronger settings (0.08, 0.2) since sampled material has sparse attacks.
///
/// ## Room Simulation
///
/// Reduces headphone fatigue from hard L-R panning (AY ABC/ACB, Amiga LRRL).
/// Unlike traditional crossfeed (bs2b) which uses 600Hz lowpass (head shadow),
/// this uses:
/// - an early-reflection delay (not ITD): 2ms for ChipType::AY (every chain
///   in unreal-ng), 3ms for ChipType::Paula
/// - Paula: ~10kHz lowpass (air absorption); AY: no lowpass (the square
///   wave's harmonics are kept)
/// - Levels from -15dB to -1dB
///
/// On transient-heavy music, use -14dB or -15dB. Higher levels cause audible
/// comb filtering (2ms: notches at 250Hz, 750Hz, 1250Hz...) on fast attacks.
///
/// ## Bypass and live switching
///
/// With every effect off (or the chain inactive: Sound HQ off) processInt16()
/// returns at once: the buffer is not touched - no float round trip, no
/// per-sample work. The decision is taken per call (one frame) from a few
/// flags, nothing is allocated.
///
/// Switching an effect on or off (or changing the room level) while audio
/// plays is click-free: the next call (the frame boundary) ramps linearly
/// across that frame from the old to the new setting - the same one-frame
/// linear crossfade VoicingStage uses for profile changes. When the whole
/// chain turns on or off, the frame crossfades between the untouched input
/// and the processed signal, so both ends match their steady paths exactly.
///
/// An effect never replays old audio: when it turns on it starts from the
/// current input (punch: previous sample = the frame's first sample, envelope
/// 0; room: the delay line and its lowpass hold the frame's first sample), and
/// when it has ramped out its state is cleared. reset() (a gap: sound off,
/// turbo without audio, TTD restore, machine reset) and setup() (a rate
/// change) clear everything; the first call afterwards takes the current
/// settings at once, without a ramp.
///
/// ## Usage
///
///   AudioCharacterChain chain;
///   chain.setup(44100);
///   chain.setPunchPreset(AudioCharacterChain::PunchPreset::AY);
///   chain.setPunchEnabled(false);  // AY doesn't need punch usually
///   chain.setRoomMode(AudioCharacterChain::RoomMode::Room_14dB);
///   // In audio callback:
///   chain.processInt16(interleavedBuffer, numSamples);
///
/// @note Ported from amiga-paula project (PWM renderer post-processing).
class AudioCharacterChain
{
public:
    /// Room simulation modes for headphone listening
    /// Reduces stereo fatigue from hard L-R panning (AY ABC/ACB, Amiga LRRL)
    enum class RoomMode
    {
        Off = 0,        // No spatial processing
        Room_15dB,      // Subtle, safe for all material
        Room_14dB,      // Recommended for most music
        Room_13dB,      // Light, good for slower tracks
        Room_12dB,      // Moderate
        Room_9dB,       // Strong
        Room_6dB,       // Very strong
        Room_3dB,       // Extreme
        Room_2dB,       // Near mono
        Room_1dB,       // Almost mono
        COUNT
    };

    /// Punch preset for different chip characteristics
    enum class PunchPreset
    {
        Paula,          // Original Paula preset: edgeBlend=0.08, transBoost=0.2
        AY,             // Gentler for square waves: edgeBlend=0.03, transBoost=0.1
        Beeper,         // For 1-bit/digidrums: same as Paula
        Custom          // Use manual parameters
    };

    /// Chip type affects room simulation parameters
    enum class ChipType
    {
        Paula,          // 3ms delay, 10kHz LP (8-bit samples tolerate filtering)
        AY              // 2ms delay, no LP (preserve square wave harmonics)
    };

    /// region <Constructors / Destructors>
public:
    AudioCharacterChain();
    ~AudioCharacterChain() = default;
    /// endregion </Constructors / Destructors>

    /// region <Setup>
public:
    /// Initialize the chain for a given sample rate
    /// @param sampleRate Output sample rate (e.g. 44100, 48000)
    void setup(double sampleRate);

    /// Reset all filter states (a gap in the stream: song change, sound off,
    /// TTD restore...). The next processInt16() call takes the settings at
    /// once (no ramp): the stream is discontinuous there anyway
    void reset();

    /// Host gate (Sound HQ): an inactive chain ramps every effect out over one
    /// frame and then passes audio untouched; becoming active ramps them back
    /// in from a clean state. Applied at the next processInt16() call
    void setActive(bool active) { _active = active; }
    bool isActive() const { return _active; }

    /// True when the next processInt16() call leaves the buffer untouched
    /// (no effect on, none ramping out)
    bool isBypassed() const
    {
        return !_engaged && !(_active && (_punchEnabled || _roomEnabled));
    }
    /// endregion </Setup>

    /// region <Punch Configuration>
public:
    void setPunchEnabled(bool enabled) { _punchEnabled = enabled; }
    bool isPunchEnabled() const { return _punchEnabled; }

    /// Set punch parameters using a preset tuned for specific chip type
    void setPunchPreset(PunchPreset preset);
    PunchPreset getPunchPreset() const { return _punchPreset; }

    /// Set custom punch parameters
    /// @param edgeBlend Constant +6dB/oct tilt amount (0.03-0.15 typical)
    /// @param transBoost Envelope-gated transient boost (0.1-0.3 typical)
    /// @param attack Envelope attack speed (0.1-0.5)
    /// @param release Envelope release coefficient (0.99-0.9995)
    void setPunchParams(float edgeBlend, float transBoost, float attack, float release);
    /// endregion </Punch Configuration>

    /// region <Room Configuration>
public:
    /// Set chip type - affects room simulation parameters
    /// Must be called before setRoomMode() to take effect
    void setChipType(ChipType type) { _chipType = type; }
    ChipType getChipType() const { return _chipType; }

    void setRoomMode(RoomMode mode);
    RoomMode getRoomMode() const { return _roomMode; }
    static const char* roomModeName(RoomMode mode);
    /// Stable machine-readable ID ("off", "15db" ... "1db") for settings,
    /// automation and persistence
    static const char* roomModeId(RoomMode mode);
    /// Parse a room ID: "off", "15db", "-15db", "15" or "-15" (case-insensitive)
    static bool parseRoomMode(std::string_view text, RoomMode& out);
    /// endregion </Room Configuration>

    /// region <Processing>
public:
    /// Process stereo float samples in-place
    /// @param left Left channel buffer (modified in-place)
    /// @param right Right channel buffer (modified in-place)
    /// @param numSamples Number of samples to process
    void process(float* left, float* right, int32_t numSamples);

    /// Process stereo int16 samples in-place (one frame per call)
    /// Bypassed (see isBypassed()): returns at once, the buffer is untouched.
    /// Otherwise converts to float internally, processes, converts back
    /// @param buffer Interleaved L-R-L-R stereo buffer
    /// @param numSamples Number of stereo sample pairs
    void processInt16(int16_t* buffer, int32_t numSamples)
    {
        if (isBypassed())
        {
            _snapPending = false;   // the settings in force now are the ones a later switch ramps from
            return;
        }
        processEngaged(buffer, numSamples);
    }
    /// endregion </Processing>

private:
    void processEngaged(int16_t* buffer, int32_t numSamples);
    template <bool Punch, bool Room>
    void processSteady(int16_t* buffer, int32_t numSamples);
    void processRamp(int16_t* buffer, int32_t numSamples, float punchFrom, float punchTo, float roomFrom,
                     float roomTo, float wetFrom, float wetTo);

    void clearPunchState();
    void clearRoomState();
    void primePunch(const int16_t* buffer);
    void primeRoom(const int16_t* buffer);

    // Applied state at the end of the last processed call (what the next
    // switch ramps from): punch weight 0 / 1, room level 0 .. _roomLevel
    bool _active = true;
    bool _engaged = false;          // _punchGain != 0 || _roomGain != 0
    bool _snapPending = true;       // after reset(): take the settings without a ramp
    float _punchGain = 0.0f;
    float _roomGain = 0.0f;
    bool _punchDirty = false;       // the punch followers hold audio
    bool _roomDirty = false;        // the delay line / lowpass hold audio

private:
    double _sampleRate = 44100.0;

    /// region <Punch state>
    // Punch enhancement: transient designer + exciter
    // - edgeBlend: constant +6dB/oct tilt (first difference blend)
    // - transBoost: envelope-gated attack boost
    // - attack/release: envelope follower time constants
    bool _punchEnabled = false;
    PunchPreset _punchPreset = PunchPreset::AY;
    float _edgeBlend = 0.08f;   // Paula default; use 0.03 for AY
    float _transBoost = 0.2f;   // Paula default; use 0.1 for AY
    float _attack = 0.3f;       // Envelope attack speed
    float _release = 0.998f;    // ~11ms @ 44.1kHz; use 0.9995 (~45ms) for AY
    // Rate-derived effective coefficients (audio-multirate plan C.1-C.4).
    // Presets/API store 44.1kHz-referenced values; deriveRateCoefficients()
    // maps them to the actual rate via coeff^(44100/fs) - the exact
    // time-constant transformation, bit-identical at 44.1kHz.
    float _attackEff = 0.3f;
    float _releaseEff = 0.998f;
    float _roomLpCoefEff = 0.7f;
    // First-difference normalization: diff gain scales with fs (2*sin(pi*f/fs));
    // without this the punch tilt shrinks linearly and the transient boost
    // QUADRATICALLY with rising sample rate (plan C.1)
    float _diffNorm = 1.0f;

    void deriveRateCoefficients();

    float _prevOutL = 0, _prevOutR = 0;  // Previous samples for diff calculation
    float _envL = 0, _envR = 0;          // Envelope follower state
    /// endregion </Punch state>

    /// region <Room simulation state>
    // Room simulation: delayed opposite-channel bleed with gentle LP
    // - roomDelay: 2ms AY / 3ms Paula (early reflection, not ITD)
    // - roomLpCoef: ~10kHz LP (air absorption, not head shadow)
    // - roomLevel: -15dB to -9dB crossfeed level
    RoomMode _roomMode = RoomMode::Off;
    bool _roomEnabled = false;
    float _roomLevel = 0.0f;    // Linear level (0.178 = -15dB, 0.35 = -9dB)
    int _roomDelay = 0;         // Delay in samples (2ms AY / 3ms Paula * sampleRate)
    float _roomLpCoef = 0.0f;   // One-pole LP coefficient (~0.7 for 10kHz)

    static constexpr int MAX_DELAY = 1024;  // 0.003s x 192kHz = 576; was 512 (overflowed at >=176.4kHz)
    static_assert((MAX_DELAY & (MAX_DELAY - 1)) == 0, "the delay index wraps with a mask");
    std::array<float, MAX_DELAY> _delayL{};  // Left channel delay line
    std::array<float, MAX_DELAY> _delayR{};  // Right channel delay line
    int _delayIdx = 0;                       // Current position in delay line
    float _roomLpL = 0, _roomLpR = 0;        // LP filter state for crossfeed

    ChipType _chipType = ChipType::AY;       // Default to AY for unreal-ng
    /// endregion </Room simulation state>
};
