#pragma once
#include <algorithm>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include "common/modulelogger.h"
#include "common/sound/audiofilehelper.h"
#include "common/sound/filters/filter_interpolate.h"
#include "common/sound/filters/audio_character_chain.h"
#include "common/sound/filters/voicingstage.h"
#include "emulator/sound/audio.h"
#include "common/sound/filters/masterlimiter.h"
#include "common/sound/filters/resampler_drc.h"
#include "emulator/sound/beeper.h"
#include "emulator/sound/covox.h"
#include "emulator/sound/modelaudiosource.h"
#include "emulator/sound/chips/soundchip_ay8910.h"
#include "emulator/sound/chips/soundchip_turbosound.h"
#include "emulator/sound/audioactivityindicators.h"
#include "emulator/sound/audiodeviceinfo.h"
#include "emulator/sound/chips/gs/soundchip_gs.h"
#include "emulator/sound/chips/gs/soundchip_gslw.h"
#include "emulator/sound/chips/neogs/soundchip_neogs.h"
#include "emulator/platform.h"  // GSTypeKind (personality switching)
#include "stdafx.h"

class EmulatorContext;
class SoundChip_Moonsound;

/// The GS slot as other threads see it (SoundManager::generalSoundSlot)
struct GeneralSoundSlot
{
    GSTypeKind kind = GSTypeKind::NONE;
    std::string sdCardImage; // NeoGS: the inserted SD image, empty = none
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
    // 6). Resolved at construction from the priority chain below; every
    // filter, chain and chip designs itself for this value. Changing it
    // requires a sound stack rebuild - applied only at frame boundaries via
    // requestCoreRate()/applyCoreRate().
    size_t _coreRate = CORE_SAMPLING_RATE;

    // Runtime core-rate pin (automation 'audio_rate': CLI setting, WebAPI
    // settings, Lua/Python set_audio_rate). Explicit intent for THIS run
    // only - never written to CONFIG/ini, so it cannot lock a UI client's
    // rate by accident and cannot go stale. 0 = no pin.
    std::atomic<uint32_t> _coreRatePin{0};

    // The core rate as ONE pure function of three inputs with a fixed
    // priority: runtime pin > attached device (per-context cell, else the
    // process-wide default) > [SOUND] CoreRate > 44100. The ini value can
    // therefore never pin a UI client that has a device attached; it only
    // decides the rate while no device is known (the headless case).
    // Table-tested in Multirate_Test.CoreRateResolution.
    size_t targetCoreRate() const;

    /// Personality factory shared by Init and switchGeneralSoundCard (GS
    /// design §5.1); nullptr for kinds that map to no device (NONE/NGS)
    GeneralSoundCard* createGeneralSoundCard(GSTypeKind kind) const;

    // Live core-rate change request (device reroute, pin set/release).
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
    ITurboSoundDevice* _turboSound = nullptr;  // TurboSound slot (legacy AY pair today, TSFM later - design §3.3)
    Covox* _covox = nullptr;
    // A machine's own DAC (the Sprinter's Covox / Covox-Blaster), owned by its port decoder and
    // mixed in the COVOX slot (attachModelAudioSource). Null for every other machine
    IModelAudioSource* _modelAudio = nullptr;
    GeneralSoundCard* _gs = nullptr;  // General Sound slot ([SOUND] GSType=Z80|LW - any personality, GS design §5.1)
#ifdef UNREALNG_HAVE_OPL4
    SoundChip_Moonsound* _moonsound = nullptr;
#endif
    // SoundChip_SAA1099;

    // Pending GS personality switch request (gs_lightweight feature,
    // WebAPI action=switch_personality): the switch deletes and recreates
    // the card, so like the core-rate change it is only APPLIED at the
    // frame boundary on the emulation thread. 0xFF = none pending (a
    // GSTypeKind value otherwise)
    std::atomic<uint8_t> _pendingGSSwitch{0xFF};

    // NeoGS stereo mode, set from any thread, applied at frame start
    std::atomic<uint8_t> _neoGSStereoMode{0};

    // What the GS slot holds now, for other threads (generalSoundSlot())
    mutable std::mutex _gsSlotMutex;
    GeneralSoundSlot _gsSlot;

    // Last gs_lightweight feature state seen by UpdateFeatureCache. The
    // feature drives a personality switch only on an actual TRANSITION
    // (on -> lightweight, off -> configured [SOUND] GSType): the cache
    // refresh itself fires on every FeatureManager notification (any
    // feature), and re-requesting the configured personality there would
    // silently clobber a runtime WebAPI switch_personality override
    bool _gsLightweightFeatureWasOn = false;

    // Audio character chains (punch enhancement + room simulation)
    // Separate chains per AY chip to preserve independent DSP state
    AudioCharacterChain _ayChain0;     // For AY chip 0 (TurboSound first chip)
    AudioCharacterChain _ayChain1;     // For AY chip 1 (TurboSound second chip)
    AudioCharacterChain _beeperChain;  // For beeper (digidrums, PWM synths)
    // FM-only chains (TSFM, design §7.2): punch Off, room Off - the §7.1
    // gain staging is hardware-derived and must reach the mix untouched
    AudioCharacterChain _fmChain0;
    AudioCharacterChain _fmChain1;

    // AY / SSG tone voicing (FilterVoicing), one stage per TurboSound chip
    // buffer. Runs BEFORE the character chains and, unlike them, in HQ and
    // LQ alike: it sets the tonal balance, so toggling HQ or leaving turbo
    // must not change the bass. Not reset when HQ returns (its state ran
    // through the LQ frames). FM buffers are never voiced.
    // Design: docs/inprogress/2026-09-25-ay-tone-voicing/ay-tone-voicing-tdd.md
    VoicingStage _ayVoicing0{MAX_SAMPLES_PER_FRAME};
    VoicingStage _ayVoicing1{MAX_SAMPLES_PER_FRAME};

    /// Built-in AY headphone crossfeed (ay_room) for a new sound stack: -9 dB,
    /// a clear reduction of the hard ABC / ACB panning on headphones. Sound HQ
    /// only; the FM and beeper chains keep room off
    static constexpr AudioCharacterChain::RoomMode DEFAULT_AY_ROOM = AudioCharacterChain::RoomMode::Room_9dB;

    // Character-chain settings requested from any thread (GUI, automation)
    // and the last request the emulation thread applied. A request is applied
    // at the next frame boundary and only when it CHANGED, so code that edits
    // a chain directly (getAYChain(), tests) is not overridden every frame
    std::atomic<bool> _requestedAYPunch{true};
    std::atomic<uint8_t> _requestedAYRoom{static_cast<uint8_t>(DEFAULT_AY_ROOM)};
    std::atomic<bool> _requestedBeeperPunch{false};
    bool _appliedAYPunch = true;
    uint8_t _appliedAYRoom = static_cast<uint8_t>(DEFAULT_AY_ROOM);
    bool _appliedBeeperPunch = false;

    /// Frame boundary (emulation thread): push changed punch / room requests into the chains
    void applyCharacterRequests();

    // DRC resampler stage between the mixed CORE_RATE stream and the device
    // callback (audio-sync design, Fix 2). Unity bypass by default. The
    // recording tap sits UPSTREAM and never sees resampled audio.
    // Device buffer sized for ratio up to ~1.1x (48k device / 44.1k core
    // plus max trim) over the largest frame.
    static constexpr size_t DEVICE_BUFFER_FRAMES = MAX_SAMPLES_PER_FRAME + MAX_SAMPLES_PER_FRAME / 4;
    ResamplerDRC _drcResampler;

    // Output delay line (whole frames): a temporal video effect (ZX DLSS) shows
    // each frame later than the A/V sync delay; the device stream waits as long.
    // Sits after the recording and analyzer taps (they stay in emulated time)
    // and before the DRC resampler. Emulation thread only.
    int _outputDelayFrames = 0;
    std::deque<std::vector<int16_t>> _outputDelayLine;
    std::vector<int16_t> _delayedOut;
    int16_t _deviceBuffer[DEVICE_BUFFER_FRAMES * AUDIO_CHANNELS] = {};

    // Wide mix bus (MoonSound integration design 5.2/D7). Enabled only while
    // a MoonSound device is attached: sources sum into a float bus without
    // per-add clipping, then a DC blocker + soft limiter run on the master and
    // the result quantises to int16 once. The legacy integer path is a
    // separate branch that stays byte-identical in every configuration
    // without the device (R6) - it is not modified by this feature.
    bool _wideMix = false;
    MasterLimiter _limiter;
    float _mixBus[MAX_SAMPLES_PER_FRAME * AUDIO_CHANNELS] = {};

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

    // Error deadband (fraction of the setpoint, +-0.8 ms at the 40 ms
    // target): occupancy ripple inside the band is measurement noise - the
    // production-burst/drain sawtooth and device-callback quantization -
    // not real drift. Feeding it to the controller only modulates playback
    // pitch (the trim IS cents) without moving the plant anywhere useful.
    static constexpr double DRC_ERR_DEADBAND = 0.02;
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
    uint64_t _accumulatorClampCount = 0;  // Diagnostics: overflow-guard activations
    uint64_t _blipMismatchCount = 0;      // Diagnostics: blip vs accumulator divergence

    // Device registry (replaces hardwired master volumes)
    std::vector<AudioDeviceInfo> _devices;
    AudioActivityIndicators _activityIndicators;  // HUD audio nudges: the LEDs above, held for a second

    // Legacy master volume fields kept for backward compat (delegate to registry)
    double _ayVolume = 1.0;
    double _beeperVolume = 1.0;

    // Save to Wave file
    TinyWav _tinyWav;

    // Feature cache flags (updated by FeatureManager::onFeatureChanged)
    bool _feature_sound_enabled = true;
    bool _feature_soundhq_enabled = true;

    /// Turbo mode forces the low-quality DSP path without touching the user's
    /// `soundhq` feature state; clearing the override restores the previous quality
    bool _turboLowQualityOverride = false;

    /// Per-frame cache: turbo mode with audio not requested and no recording in progress
    bool _synthesisSuppressed = false;

    /// True while the character chains (punch / room) were skipped on the last
    /// frame because HQ is off. When HQ comes back the chains' delay lines and
    /// envelopes are reset before their first use, so no stale audio replays.
    bool _chainsBypassed = false;

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
    /// Emulator paused: no more handleFrameEnd calls will arrive until
    /// resumed, so a device mid-playback at the moment of pause would
    /// otherwise keep reporting "active" (buffer non-silent, HUD nudge / UI
    /// LED lit) for as long as the pause lasts - nothing left to naturally
    /// clear it. Forwards to the GS card (see GeneralSoundCard::
    /// onEmulatorPaused), clears every registry row's LED so a UI that reads
    /// devices() directly (audiosettingswidget) sees it immediately, and
    /// ends every HUD nudge (AudioActivityIndicators::stop).
    void onEmulatorPaused();

    /// Force low-quality DSP while turbo mode is on (audio is muted anyway, and the
    /// HQ FIR / oversampling chain is pure CPU cost at 50x speed). The `soundhq`
    /// feature itself is left untouched, so leaving turbo re-enables the previous state.
    void setTurboLowQualityOverride(bool enabled);
    bool isTurboLowQualityOverride() const { return _turboLowQualityOverride; }

    /// Effective DSP quality: the user's `soundhq` feature unless turbo overrides it
    bool isHQActive() const { return _feature_soundhq_enabled && !_turboLowQualityOverride; }

    /// True while no audio is synthesised at all (turbo mode without audio request and
    /// not recording): AY / beeper / Covox / tape edge rendering is skipped, register and
    /// level state is still tracked so program-visible behaviour is unchanged.
    /// Evaluated once per frame in handleFrameStart.
    bool isSynthesisSuppressed() const { return _synthesisSuppressed; }

    /// TTD restore of a device that carries its own copy of the sample phase
    /// (same units as _sampleAccumulator - SoundChip_TurboSoundFM restores it
    /// for generator determinism): take the same position, so the frame's
    /// sample count and the device's rendered count stay equal. Left behind,
    /// the mixer kept its pre-seek phase and read one never-rendered (zero)
    /// sample every few frames for the rest of the session
    void adoptSamplePhase(uint64_t tstateRatePhase)
    {
        _sampleAccumulator = tstateRatePhase;
    }

    const AudioFrameDescriptor& getAudioBufferDescriptor();
    Beeper& getBeeper();

    // TurboSound/AY chip access for debugging
    bool hasTurboSound() const
    {
        return _turboSound != nullptr;
    }
    /// The TurboSound-slot device through its interface (design §3.3):
    /// legacy two-AY TurboSound today, SoundChip_TurboSoundFM once
    /// TurboSound = FM is configured. TTD keys on its TTDPeripheralId().
    ITurboSoundDevice* getTurboSound() const
    {
        return _turboSound;
    }
    /// HUD audio nudges: every source's LED held for a second
    const AudioActivityIndicators& getActivityIndicators() const
    {
        return _activityIndicators;
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

    /// A machine's own DAC device (IModelAudioSource): it takes the COVOX mixer slot (row, recording
    /// source "COVOX", HUD LED) under its own name; a generic Covox, if one is configured, is no longer
    /// mixed. The owner (the port decoder) detaches before it is destroyed. Emulation thread
    void attachModelAudioSource(IModelAudioSource* source);
    void detachModelAudioSource(IModelAudioSource* source);
    IModelAudioSource* getModelAudioSource() const { return _modelAudio; }

    // General Sound access (automation, TTD, tests - M8 pattern). The slot is
    // personality-agnostic: LLE (Z80+firmware) or LW (in-tree mod player) both
    // arrive as GeneralSoundCard (design: docs/inprogress/2026-09-19-general-sound)
    bool hasGeneralSound() const { return _gs != nullptr; }
    /// Mixer source name of the fitted GS-slot card ("GS" / "NeoGS")
    std::string generalSoundDeviceName() const;
    /// Add or remove the "NeoGS MP3" source to match the fitted card
    void syncGeneralSoundAuxDevice();
    GeneralSoundCard* getGeneralSound() const { return _gs; }

    /// Swap the General Sound card's personality at runtime (design:
    /// gs-card-interface.md §Runtime switching). The forward mailbox
    /// (queues, latches, pending flags) and the activity counters survive
    /// the handoff; a completed module upload captured by the lightweight
    /// card is replayed through a fresh LLE firmware (v1 limit: LLE -> LW
    /// stops playback, the module lives inside firmware RAM). The GS mixer
    /// slot and device registry entry are shared - no audio rerouting.
    /// Must run on the emulation thread (same ownership as the frame
    /// lifecycle); no-op (true) when the requested personality is already
    /// fitted. GSTypeKind::NONE/BASS select no card - rejected.
    bool switchGeneralSoundCard(GSTypeKind target);

    /// Thread-safe variant for cross-thread callers (WebAPI actions, the
    /// gs_lightweight feature toggle): queues the target and returns true;
    /// the switch itself runs at the next frame boundary on the emulation
    /// thread (handleFrameStart), the only point where the card may be
    /// deleted/recreated safely
    /// Refused (false, reason in `error`) while a TTD user recording runs.
    bool requestGeneralSoundCardSwitch(GSTypeKind target, std::string* error = nullptr);

    /// What the GS slot holds now, safe to read from any thread (the GUI,
    /// automation): the card - Z80 (classic), LW, NGS or NONE - and, on
    /// NeoGS, the SD card image (empty: no card). The card itself may be
    /// replaced or changed only on the emulation thread, so other threads
    /// read this copy instead of the card
    /// Any thread. The SD image comes from the media manager's slot `sd.ngs`
    /// when there is one (it changes at frame boundaries, not with the card)
    GeneralSoundSlot generalSoundSlot() const;
    GSTypeKind fittedGeneralSoundKind() const { return generalSoundSlot().kind; }
    /// Refresh generalSoundSlot() from the card: emulation thread, after the
    /// card or its media changed
    void publishGeneralSoundSlot();
    /// NeoGS stereo mode (separated / GS cross-feed / mono): any thread; the
    /// fitted NeoGS takes it at the next frame boundary, and a card fitted
    /// later starts with it. Initialised from [NGS] StereoMode
    void setNeoGSStereoMode(NeoGSConfig::StereoMode mode) { _neoGSStereoMode.store(static_cast<uint8_t>(mode), std::memory_order_relaxed); }
    NeoGSConfig::StereoMode neoGSStereoMode() const
    {
        return static_cast<NeoGSConfig::StereoMode>(_neoGSStereoMode.load(std::memory_order_relaxed));
    }
    /// A requestGeneralSoundCardSwitch() waits for the next frame boundary
    bool generalSoundSwitchPending() const { return _pendingGSSwitch.load(std::memory_order_acquire) != 0xFF; }
#ifdef UNREALNG_HAVE_OPL4
    // MoonSound access
    bool hasMoonSound() const { return _moonsound != nullptr; }
    SoundChip_Moonsound* getMoonSound() const { return _moonsound; }
#endif

    /// Wide mix bus + master limiter (MoonSound integration 5.2). Called with
    /// true when a MoonSound device is attached, false when it is not; the
    /// legacy integer path is used whenever the wide path is off (R6).
    void enableWideMix(bool enable);
    bool wideMixEnabled() const { return _wideMix; }

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

    /// region <Sound character settings (thread-safe, applied at the next frame boundary)>
    /// AY / SSG tone voicing profile (both chips). Any thread; the stream
    /// switches click-free at the next frame boundary (VoicingStage)
    void setAYVoicing(FilterVoicing::Preset preset);
    /// The REQUESTED profile - a read right after a write shows the new value
    FilterVoicing::Preset getAYVoicing() const
    {
        return _ayVoicing0.requested();
    }
    /// The profile chip 0 runs right now (tests, diagnostics)
    FilterVoicing::Preset getActiveAYVoicing() const
    {
        return _ayVoicing0.active();
    }
    const VoicingStage& getAYVoicingStage(int chip) const
    {
        return chip == 1 ? _ayVoicing1 : _ayVoicing0;
    }

    void setAYPunch(bool enabled);
    bool getAYPunch() const
    {
        return _requestedAYPunch.load(std::memory_order_acquire);
    }
    void setAYRoomMode(AudioCharacterChain::RoomMode mode);
    AudioCharacterChain::RoomMode getAYRoomMode() const
    {
        return static_cast<AudioCharacterChain::RoomMode>(_requestedAYRoom.load(std::memory_order_acquire));
    }
    void setBeeperPunch(bool enabled);
    bool getBeeperPunch() const
    {
        return _requestedBeeperPunch.load(std::memory_order_acquire);
    }
    /// endregion </Sound character settings>

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

    /// Pin the core audio rate for this run (one of the rates
    /// IsSupportedCoreRate accepts; 0 = release the pin and follow the
    /// device/config again). Runtime-only - never persisted to any ini.
    /// Thread-safe; applied at the next frame boundary like every re-rate,
    /// deferred while a recording is in progress. Unsupported rates are
    /// refused with a warning and leave the pin untouched.
    void setCoreRatePin(uint32_t rate);

    /// Current runtime pin (0 = none)
    uint32_t getCoreRatePin() const { return _coreRatePin.load(std::memory_order_acquire); }

    /// Recompute the core rate from the priority chain (pin > device >
    /// [SOUND] CoreRate) and request the change. No-op when the target
    /// equals the current rate. Call whenever any chain input changes.
    void reevaluateCoreRate();

    /// Where the rate would land right now (pin > device > config > 44100);
    /// getCoreRate() reaches this at the next applied frame boundary
    uint32_t getTargetCoreRate() const { return static_cast<uint32_t>(targetCoreRate()); }

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

    /// Delay the audio sent to the device by whole frames (0 = none), to follow a
    /// video effect that presents frames later (Screen temporal effects). Growing
    /// the delay inserts silence, shrinking it drops the oldest frames. Recording
    /// and analyzers are not delayed. Emulation thread.
    void setOutputDelayFrames(int frames) { _outputDelayFrames = frames < 0 ? 0 : frames; }
    int getOutputDelayFrames() const { return _outputDelayFrames; }
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
