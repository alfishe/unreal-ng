#pragma once
#include <algorithm>
#include <string>
#include <vector>

#include "common/modulelogger.h"
#include "common/sound/audiofilehelper.h"
#include "common/sound/filters/filter_interpolate.h"
#include "common/sound/filters/audio_character_chain.h"
#include "emulator/sound/audio.h"
#include "common/sound/filters/resampler_drc.h"
#include "emulator/sound/beeper.h"
#include "emulator/sound/covox.h"
#include "emulator/sound/chips/soundchip_ay8910.h"
#include "emulator/sound/chips/soundchip_turbosound.h"
#include "stdafx.h"

class EmulatorContext;

/// Audio source types for device/channel selection (shared with recording)
enum class AudioSourceType
{
    MasterMix,
    Beeper,
    AY1_All,
    AY2_All,
    AY3_All,
    COVOX,
    GeneralSound,
    Moonsound,
    AY1_ChannelA, AY1_ChannelB, AY1_ChannelC,
    AY2_ChannelA, AY2_ChannelB, AY2_ChannelC,
    AY3_ChannelA, AY3_ChannelB, AY3_ChannelC,
    Custom
};

/// Per-device descriptor for the registry-driven mixer
struct AudioDeviceInfo
{
    AudioSourceType type;
    std::string     name;

    // Monitor state (runtime, per emulator instance)
    bool  mute   = false;
    bool  solo   = false;
    float volume = 1.0f;

    // Read-only status for UI (updated each frame)
    float peak           = 0.0f;
    bool  activeRecently = false;
};

class SoundManager
{
    /// region <Fields>
protected:
    EmulatorContext* _context;
    ModuleLogger* _logger;

    volatile bool _mute = false;  // MUST initialize - sound unmuted by default
    bool _soundEnabled = true;

    // The core audio rate all chip DSP is designed for (multirate plan phase
    // 6). Resolved ONCE at construction from [SOUND] CoreRate (auto = device
    // native rate when already published, else 44100); every filter, chain
    // and chip designs itself for this value. Changing it requires a sound
    // stack rebuild - no hot switch.
    size_t _coreRate = CORE_SAMPLING_RATE;

    size_t resolveCoreRate() const;

    // Live core-rate change request (device reroute with CoreRate=auto).
    // Written from any thread via requestCoreRate(); APPLIED only at the next
    // frame boundary on the emulation thread (handleFrameStart), which owns
    // all DSP state. 0 = no change pending. Deferred while recording.
    std::atomic<uint32_t> _pendingCoreRate{0};
    bool _pendingRateLoggedWhileRecording = false;

    void applyCoreRate(size_t rate);

    // Process-wide device native rate, published by the frontend right after
    // audio device init - BEFORE any emulator exists. CoreRate=auto consults
    // it when the per-emulator pAudioDeviceSampleRate cell is still unset
    // (emulators are constructed before the frontend binds/publishes to
    // them, so the per-context cell alone resolves auto to 44100 always).
    static std::atomic<uint32_t> _defaultDeviceSampleRate;

    AudioFrameDescriptor _beeperAudioDescriptor;                                   // Audio descriptor for the beeper
    int16_t* const _beeperBuffer = (int16_t*)_beeperAudioDescriptor.memoryBuffer;  // Shortcut to it's sample buffer

    AudioFrameDescriptor _outAudioDescriptor;                                // Audio descriptor for mixer output
    int16_t* const _outBuffer = (int16_t*)_outAudioDescriptor.memoryBuffer;  // Shortcut to it's sample buffer

    // Supported sound chips
    Beeper* _beeper = nullptr;
    SoundChip_TurboSound* _turboSound = nullptr;
    Covox* _covox = nullptr;
    // SoundChip_TurboSoundFM;
    // SoundChip_MoonSound;
    // SoundChip_SAA1099;
    // SoundChip_GeneralSound;

    // Audio character chains (punch enhancement + room simulation)
    // Separate chains per AY chip to preserve independent DSP state
    AudioCharacterChain _ayChain0;     // For AY chip 0 (TurboSound first chip)
    AudioCharacterChain _ayChain1;     // For AY chip 1 (TurboSound second chip)
    AudioCharacterChain _beeperChain;  // For beeper (digidrums, PWM synths)

    // DRC resampler stage between the mixed CORE_RATE stream and the device
    // callback (audio-sync design, Fix 2). Unity bypass by default. The
    // recording tap sits UPSTREAM and never sees resampled audio.
    // Device buffer sized for ratio up to ~1.1x (48k device / 44.1k core
    // plus max trim) over the largest frame.
    static constexpr size_t DEVICE_BUFFER_FRAMES = MAX_SAMPLES_PER_FRAME + MAX_SAMPLES_PER_FRAME / 4;
    ResamplerDRC _drcResampler;
    int16_t _deviceBuffer[DEVICE_BUFFER_FRAMES * AUDIO_CHANNELS] = {};

    // DRC PI controller state (audio-sync design 5.1). Process variable: ring
    // occupancy in ms (EMA-filtered); output: resample-ratio trim in +-0.5%.
    // Sampled once per frame in the emulation thread. Disengaged (unity
    // bypass, integrator reset) when no occupancy cell is registered, in
    // turbo mode, or with sound disabled.
public:
    // Ring occupancy setpoint = the audio presentation delay (occupancy IS
    // the A/V offset: video presents within ~1 frame, audio is delayed by
    // exactly the ring content plus the device HW buffer). LATENCY BUDGET:
    // target + HW buffer (~11 ms) must stay under the ~45 ms lip-sync
    // perception threshold for audio-late. 40 ms = ~7 device callback
    // periods of underrun margin (5.8 ms each) - regression-guarded by
    // SoundAdaptivity.AVLatencyBudget.
    static constexpr double DRC_TARGET_MS = 40.0;

    // Emergency-refill trigger (MainLoop): produce frames back-to-back when
    // ring occupancy collapses below this. MUST sit well below the occupancy
    // sawtooth trough (target - 1 frame ~= 20 ms on Pentagon): production is
    // bursty, so instantaneous occupancy legitimately dips that far every
    // frame cycle. A threshold above the trough makes the "emergency" path
    // fire routinely, injecting extra frames and spiking occupancy ~+20 ms -
    // the DRC then fights the refill forever. (This exact regression shipped
    // when the target moved 70 -> 40 ms with the old 2048-frame (~46 ms)
    // threshold left in place.) Guarded by SoundAdaptivity.AVLatencyBudget.
    static constexpr double EMERGENCY_REFILL_MS = 15.0;

    // Hard-resync trigger (frontend device callback): occupancy beyond this
    // is unrecoverable by the DRC's +-0.5% trim in reasonable time (draining
    // 500 ms excess would take minutes) - the consumer discards down to
    // DRC_TARGET_MS in one step and tracking restarts from there. Reached
    // only through abnormal events (device re-init windows, long stalls).
    static constexpr double HARD_RESYNC_MS = 160.0;  // 4x target

    /// Restart the DRC controller state (EMA seed + integrator): called on
    /// device re-establishment so tracking resumes from the fresh occupancy
    /// instead of stale pre-reroute state
    void resetDrcController()
    {
        _drcOccFiltered = -1.0;
        _drcErrIntegral = 0.0;
    }

protected:
    static constexpr double DRC_MAX_TRIM = 0.005;
    static constexpr double DRC_KP = 0.08;
    static constexpr double DRC_KI = 0.0008;
    static constexpr double DRC_EMA_ALPHA = 0.05;
    double _drcOccFiltered = -1.0;  // <0 = uninitialized (seeded on first sample)
    double _drcErrIntegral = 0.0;

    void updateDrcControl();

    // Exact per-frame sample count accumulator (audio-sync design, Fix 1).
    // Units: T-states x sampling rate, carried modulo CPU_CLOCK_RATE so the
    // fractional sample per frame is never lost. Reset in reset() only -
    // NOT at frame or speed-multiplier boundaries.
    uint64_t _sampleAccumulator = 0;

    // Last frequency multiplier applied to the synths (turbo switches).
    // 0 forces a re-apply on the first frame after reset()
    uint8_t _lastFrequencyMultiplier = 0;
    uint64_t _accumulatorClampCount = 0;  // Diagnostics: overflow-guard activations
    uint64_t _blipMismatchCount = 0;      // Diagnostics: blip vs accumulator divergence

    // Device registry (replaces hardwired master volumes)
    std::vector<AudioDeviceInfo> _devices;

    // Legacy master volume fields kept for backward compat (delegate to registry)
    double _ayVolume = 1.0;
    double _beeperVolume = 1.0;

    // Save to Wave file
    TinyWav _tinyWav;

    // Feature cache flags (updated by FeatureManager::onFeatureChanged)
    bool _feature_sound_enabled = true;
    bool _feature_soundhq_enabled = true;

    /// endregion </Fields>

    /// region <Constructors / Destructors>
public:
    SoundManager() = delete;                     // Disable default constructor
    SoundManager(const SoundManager&) = delete;  // Disable copy constructor
    SoundManager(EmulatorContext* context);
    virtual ~SoundManager();

    /// endregion </Constructors / Destructors>

    /// region <Methods>

public:
    void reset();
    void mute();
    void unmute();

    const AudioFrameDescriptor& getAudioBufferDescriptor();
    Beeper& getBeeper();

    // TurboSound/AY chip access for debugging
    bool hasTurboSound() const
    {
        return _turboSound != nullptr;
    }
    SoundChip_TurboSound* getTurboSound() const
    {
        return _turboSound;
    }
    SoundChip_AY8910* getAYChip(int index) const;
    int getAYChipCount() const;
    bool isMuted() const
    {
        return _mute;
    }

    // Covox access
    bool hasCovox() const { return _covox != nullptr; }
    Covox* getCovox() const { return _covox; }

    /// Compatibility shim for tape audio. Routes amplitude into the beeper's
    /// blip_buf at the given T-state position. New code should use
    /// Beeper::handlePortOut() or Beeper::handleTapeAudio() directly.
    void updateDAC(uint32_t frameTState, int16_t left, int16_t right);

    // Audio character chains (punch + room simulation)
    // Returns chain for chip 0 (settings shared between both chips)
    AudioCharacterChain& getAYChain() { return _ayChain0; }
    AudioCharacterChain& getBeeperChain() { return _beeperChain; }

    // Apply AY chain settings to both chips
    void syncAYChainSettings();

    // Device registry API
    const std::vector<AudioDeviceInfo>& devices() const { return _devices; }
    AudioDeviceInfo* device(AudioSourceType type);
    const AudioDeviceInfo* device(AudioSourceType type) const;
    const int16_t* deviceBuffer(AudioSourceType type) const;
    void setDeviceMute(AudioSourceType type, bool mute);
    void setDeviceSolo(AudioSourceType type, bool solo);
    void setDeviceVolume(AudioSourceType type, float volume);

    // Legacy master volume controls (delegate to registry entries)
    void setAYVolume(double volume);
    void setBeeperVolume(double volume);
    double getAYVolume() const { return _ayVolume; }
    double getBeeperVolume() const { return _beeperVolume; }

    // Legacy accessor for compatibility
    AudioCharacterChain& getCharacterChain() { return _ayChain0; }

    // Feature cache update (called by FeatureManager::onFeatureChanged)
    void UpdateFeatureCache();

    /// The resolved core audio rate (Hz) - recording and analysis consumers
    /// must read this instead of assuming 44100
    size_t getCoreRate() const { return _coreRate; }

    /// Request a live core-rate change (thread-safe; applied at the next
    /// frame boundary on the emulation thread). Every rate-dependent DSP
    /// stage re-derives: beeper/covox blip resamplers, AY sample PLL and
    /// decimation FIRs, character chains, the exact sample accumulator, and
    /// the recording rate. No-op for unsupported rates or when equal to the
    /// current core rate; deferred while a recording is in progress.
    void requestCoreRate(uint32_t rate);

    /// Publish the audio device's native rate for CoreRate=auto resolution.
    /// Call right after device init (and re-init on reroute), before creating
    /// emulators. Process-wide: the playback device is shared by all
    /// emulator instances.
    static void PublishDefaultDeviceSampleRate(uint32_t rate)
    {
        _defaultDeviceSampleRate.store(rate, std::memory_order_release);
    }

    // DRC telemetry (tests / diagnostics)
    double getDrcRatio() const { return _drcResampler.getRatio(); }
    double getDrcFilteredOccupancyMs() const { return _drcOccFiltered; }
    /// endregion </Methods>

    /// region <Emulation events>
public:
    void handleFrameStart();
    void handleStep();
    void handleFrameEnd();
    /// endregion </Emulation events>

    /// region <Wave file export>
public:
    bool openWaveFile(std::string& path);
    void closeWaveFile();

    void writeToWaveFile(uint8_t* buffer, size_t len);

    /// endregion </Wave file export>

    /// region <Port interconnection>

public:
    bool attachToPorts();
    bool detachFromPorts();

    /// endregion </Port interconnection>
};
