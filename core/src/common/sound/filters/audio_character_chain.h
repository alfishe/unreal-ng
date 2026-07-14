#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <algorithm>

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
/// - 3ms delay (early reflection timing, not ITD)
/// - 10kHz lowpass (air absorption, preserves HF clarity)
/// - Levels from -15dB to -9dB
///
/// On transient-heavy music, use -14dB or -15dB. Higher levels cause audible
/// comb filtering (notches at 167Hz, 500Hz, 833Hz...) on fast attacks.
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
        Room_12dB,      // Moderate, may color transients
        Room_9dB,       // Strong, for ambient/pad-heavy music
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

    /// Reset all filter states (call on song change, etc.)
    void reset();
    /// endregion </Setup>

    /// region <Punch Configuration>
public:
    void setPunchEnabled(bool enabled) { _punchEnabled = enabled; }
    bool isPunchEnabled() const { return _punchEnabled; }

    /// Set punch parameters using a preset tuned for specific chip type
    void setPunchPreset(PunchPreset preset);

    /// Set custom punch parameters
    /// @param edgeBlend Constant +6dB/oct tilt amount (0.03-0.15 typical)
    /// @param transBoost Envelope-gated transient boost (0.1-0.3 typical)
    /// @param attack Envelope attack speed (0.1-0.5)
    /// @param release Envelope release coefficient (0.99-0.9995)
    void setPunchParams(float edgeBlend, float transBoost, float attack, float release);
    /// endregion </Punch Configuration>

    /// region <Room Configuration>
public:
    void setRoomMode(RoomMode mode);
    RoomMode getRoomMode() const { return _roomMode; }
    static const char* roomModeName(RoomMode mode);
    /// endregion </Room Configuration>

    /// region <Processing>
public:
    /// Process stereo float samples in-place
    /// @param left Left channel buffer (modified in-place)
    /// @param right Right channel buffer (modified in-place)
    /// @param numSamples Number of samples to process
    void process(float* left, float* right, int32_t numSamples);

    /// Process stereo int16 samples in-place
    /// Converts to float internally, processes, converts back
    /// @param buffer Interleaved L-R-L-R stereo buffer
    /// @param numSamples Number of stereo sample pairs
    void processInt16(int16_t* buffer, int32_t numSamples);
    /// endregion </Processing>

private:
    double _sampleRate = 44100.0;

    /// region <Punch state>
    // Punch enhancement: transient designer + exciter
    // - edgeBlend: constant +6dB/oct tilt (first difference blend)
    // - transBoost: envelope-gated attack boost
    // - attack/release: envelope follower time constants
    bool _punchEnabled = false;
    float _edgeBlend = 0.08f;   // Paula default; use 0.03 for AY
    float _transBoost = 0.2f;   // Paula default; use 0.1 for AY
    float _attack = 0.3f;       // Envelope attack speed
    float _release = 0.998f;    // ~11ms @ 44.1kHz; use 0.9995 (~45ms) for AY
    float _prevOutL = 0, _prevOutR = 0;  // Previous samples for diff calculation
    float _envL = 0, _envR = 0;          // Envelope follower state
    /// endregion </Punch state>

    /// region <Room simulation state>
    // Room simulation: delayed opposite-channel bleed with gentle LP
    // - roomDelay: 3ms (early reflection, not ITD)
    // - roomLpCoef: ~10kHz LP (air absorption, not head shadow)
    // - roomLevel: -15dB to -9dB crossfeed level
    RoomMode _roomMode = RoomMode::Off;
    bool _roomEnabled = false;
    float _roomLevel = 0.0f;    // Linear level (0.178 = -15dB, 0.35 = -9dB)
    int _roomDelay = 0;         // Delay in samples (3ms * sampleRate)
    float _roomLpCoef = 0.0f;   // One-pole LP coefficient (~0.7 for 10kHz)

    static constexpr int MAX_DELAY = 512;  // ~11ms @ 44.1kHz
    std::array<float, MAX_DELAY> _delayL{};  // Left channel delay line
    std::array<float, MAX_DELAY> _delayR{};  // Right channel delay line
    int _delayIdx = 0;                       // Current position in delay line
    float _roomLpL = 0, _roomLpR = 0;        // LP filter state for crossfeed
    /// endregion </Room simulation state>
};
