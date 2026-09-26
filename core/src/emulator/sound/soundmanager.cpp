#include "soundmanager.h"
#include "debugger/ttd/timetravelmanager.h"

#include <cmath>

#include "base/featuremanager.h"
#include "common/dumphelper.h"
#include "common/sound/audiohelper.h"
#include "common/sound/audioutils.h"
#include "common/stringhelper.h"
#include "debugger/analyzers/analyzermanager.h"  // Analyzer audio tap (MCP automation)
#include "debugger/debugmanager.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/chips/soundchip_turbosoundfm.h"
#include "stdafx.h"

#ifdef UNREALNG_HAVE_OPL4
#include "emulator/sound/chips/soundchip_moonsound.h"
#endif

/// region <Constructors / Destructors>

std::atomic<uint32_t> SoundManager::_defaultDeviceSampleRate{0};

/// The core audio rate as ONE pure function of three inputs with a fixed
/// priority (multirate plan phase 6, refined): runtime pin > attached
/// device > [SOUND] CoreRate > 44100. The ini value can therefore never
/// lock a UI client that has a device attached (a stale CoreRate=44100
/// next to a 48 kHz DAC resolves to 48 kHz) - it only decides the rate
/// while NO device is known, which is exactly the headless case
/// (recordings and analyzers at a chosen rate). The device rate comes from
/// the per-emulator cell if the frontend already bound this emulator,
/// otherwise from the process-wide default published at audio device init
/// (the usual case: emulators are constructed BEFORE the frontend binds
/// audio to them).
size_t SoundManager::targetCoreRate() const
{
    if (const uint32_t pin = _coreRatePin.load(std::memory_order_acquire))
        return pin;  // explicit runtime intent beats everything

    uint32_t devRate = _context->pAudioDeviceSampleRate.load(std::memory_order_relaxed);
    if (devRate == 0)
        devRate = _defaultDeviceSampleRate.load(std::memory_order_acquire);
    if (IsSupportedCoreRate(devRate))
        return devRate;  // a connected device always outranks the ini

    if (IsSupportedCoreRate(_context->config.sound.coreRate))
        return _context->config.sound.coreRate;  // headless: rate from the ini

    return CORE_SAMPLING_RATE;
}

SoundManager::SoundManager(EmulatorContext* context)
{
    _context = context;
    _logger = context->pModuleLogger;

    _coreRate = targetCoreRate();
    if (_coreRate != CORE_SAMPLING_RATE)
    {
        LOGINFO("SoundManager: core audio rate %zu Hz", _coreRate);
    }

    _beeper = new Beeper(_context, CPU_CLOCK_RATE, _coreRate, _beeperBuffer);

    // TurboSound slot device (TSFM design §3.2): the config kind decides what
    // occupies the slot. SoundManager holds it through ITurboSoundDevice from
    // here on - everything below (registry, chains, TTD registration) is
    // device-agnostic. TurboSound = None leaves the slot empty: no AY/TSFM
    // device exists, its ports stay undecoded and every path below skips it.
    switch (_context->config.sound.turboSoundKind)
    {
        case TurboSoundKind::None:
            LOGINFO("SoundManager: TurboSound slot empty (no AY / TSFM fitted)");
            break;
        case TurboSoundKind::FM:
            // TSFM (2 x YM2203): the chip core advances in T-states; its
            // output stage is silent until P6, but the core, ports and
            // status reads are live from here on.
            _turboSound = new SoundChip_TurboSoundFM(_context);
            LOGINFO("SoundManager: TurboSound slot = TSFM (TurboSound FM, 2 x YM2203)");
            break;
        default:
            _turboSound = new SoundChip_TurboSound(_context);
            break;
    }
    if (_turboSound)
    {
        _turboSound->setDecimatorQuality(_context->config.sound.decimatorHighFidelity
                                             ? FilterDecimator::Quality::HighFidelity
                                             : FilterDecimator::Quality::Reference);
        _turboSound->setCoreRate(_coreRate);
    }

    // Build the device registry based on what this machine has
    // Beeper is always present
    _devices.push_back({AudioSourceType::Beeper, "Beeper", false, false, 1.0f, 0.0f, false});
    // AY 1 whenever the slot is occupied (single AY or first chip of TurboSound)
    if (_turboSound)
    {
        _devices.push_back({AudioSourceType::AY1_All, "AY 1", false, false, 1.0f, 0.0f, false});
    }
    // AY 2 only if TurboSound (second chip)
    if (_turboSound && _turboSound->getChipCount() > 1)
    {
        _devices.push_back({AudioSourceType::AY2_All, "AY 2", false, false, 1.0f, 0.0f, false});
    }
    // FM-only entries when the slot device has FM channels (TSFM, §7.2)
    if (_turboSound && _turboSound->hasFm())
    {
        _devices.push_back({AudioSourceType::FM1, "FM 1", false, false, 1.0f, 0.0f, false});
        _devices.push_back({AudioSourceType::FM2, "FM 2", false, false, 1.0f, 0.0f, false});
    }

    // Covox / SoundDrive when either config flag is set. The same 4-channel
    // DAC class serves both: SD=1 wires the full SoundDrive quad (#F1/#F3/
    // #F9/#FB), CovoxFB=1 alone wires only the mono Covox port #FB
    // (see attachToPorts)
    if (_context->config.sound.covoxFB || _context->config.sound.sd)
    {
        _covox = new Covox(_context, _coreRate);
        _devices.push_back({AudioSourceType::COVOX, "COVOX", false, false, 1.0f, 0.0f, false});
    }

    // General Sound card ([SOUND] GSType, GS design §5.1): personality slot
    // behind the GeneralSoundCard interface (design:
    // docs/inprogress/2026-09-19-general-sound). Z80 = LLE coprocessor
    // + 4xDAC; LW = in-tree lightweight mod player (no coprocessor); NGS
    // (NeoGS, neogs-tdd.md) is a P2 placeholder that parses but creates no
    // device yet. RAM comes from [SOUND] GSRamSize (default 128 KB stock):
    // [NGS] RamSize is a NeoGS-only key and must not leak here - its shipped
    // 2048 KB default clamps to 512 KB, which quadruples the firmware POST so
    // fastdisk-booted trainers probe the card mid-POST and read 0xFF instead
    // of the 0x7E idle signature they check for (scorpion-family boots lost
    // that race and fell back to no-GS sound, verification BUG-6). The
    // firmware ROM is optional - the chip warns and runs zeroed when
    // missing, so a config error never blocks the machine.
    if (_context->config.sound.gsTypeKind == GSTypeKind::Z80 || _context->config.sound.gsTypeKind == GSTypeKind::LW)
    {
        GSTypeKind gsKind = _context->config.sound.gsTypeKind;
        if (gsKind == GSTypeKind::Z80 && _context->pFeatureManager
            && _context->pFeatureManager->isEnabled(Features::kGSLightweight))
        {
            // gs_lightweight feature override: fit the lightweight card
            // without touching [SOUND] GSType (runtime switching design) -
            // the configured personality returns when the feature clears
            gsKind = GSTypeKind::LW;
        }
        _gs = createGeneralSoundCard(gsKind);
        _devices.push_back({AudioSourceType::GeneralSound, "GS", false, false, 1.0f, 0.0f, false});
    }
    // MoonSound (ZXM-MoonSound / YMF278B / OPL4) if the legacy key is set
    // (D1/D2). Two registry sources (D5), legacy volume scale.
#ifdef UNREALNG_HAVE_OPL4
    if (_context->config.sound.moonsound)
    {
        _moonsound = new SoundChip_Moonsound(_context, _coreRate);
        const float moonsoundVolume = std::clamp(_context->config.sound.moonsound_vol, 0, 8192) / 8192.0f;
        _devices.push_back({AudioSourceType::Moonsound_FM, "MoonSound FM (OPL3)", false, false, moonsoundVolume, 0.0f, false});
        _devices.push_back({AudioSourceType::Moonsound_PCM, "MoonSound PCM (wave)", false, false, moonsoundVolume, 0.0f, false});

        // A full-scale 16-bit source is attached: the wide float bus and the
        // master limiter own the master mix from now on (5.2/D7). The legacy
        // integer path stays dormant while the device exists (R6 covers the
        // no-device configuration only).
        enableWideMix(true);
    }
#endif

    // Initialize AY character chains (one per TurboSound chip for independent DSP state)
    // - ChipType::AY uses shorter delay and no LP (preserves square wave harmonics)
    // - Punch: AY preset (gentler - square waves already have rich harmonics)
    _ayChain0.setup(_coreRate);
    _ayChain0.setChipType(AudioCharacterChain::ChipType::AY);
    _ayChain0.setPunchPreset(AudioCharacterChain::PunchPreset::AY);
    _ayChain0.setPunchEnabled(true);
    _ayChain0.setRoomMode(AudioCharacterChain::RoomMode::Off);

    _ayChain1.setup(_coreRate);
    _ayChain1.setChipType(AudioCharacterChain::ChipType::AY);
    _ayChain1.setPunchPreset(AudioCharacterChain::PunchPreset::AY);
    _ayChain1.setPunchEnabled(true);
    _ayChain1.setRoomMode(AudioCharacterChain::RoomMode::Off);

    // FM-only chains (TSFM, §7.2): both stages off - the hardware-derived
    // gain staging (§7.1) must reach the mix untouched
    _fmChain0.setup(_coreRate);
    _fmChain0.setChipType(AudioCharacterChain::ChipType::AY);
    _fmChain0.setPunchEnabled(false);
    _fmChain0.setRoomMode(AudioCharacterChain::RoomMode::Off);

    _fmChain1.setup(_coreRate);
    _fmChain1.setChipType(AudioCharacterChain::ChipType::AY);
    _fmChain1.setPunchEnabled(false);
    _fmChain1.setRoomMode(AudioCharacterChain::RoomMode::Off);

    // Initialize beeper character chain
    // - ChipType::AY (no LP) - beeper is also square waves, LP kills brightness
    // - Punch: Beeper preset (stronger - 1-bit audio needs attack definition)
    _beeperChain.setup(_coreRate);
    _beeperChain.setChipType(AudioCharacterChain::ChipType::AY);
    _beeperChain.setPunchPreset(AudioCharacterChain::PunchPreset::Beeper);
    _beeperChain.setPunchEnabled(false);
    _beeperChain.setRoomMode(AudioCharacterChain::RoomMode::Off);

    // Master limiter designs itself for the resolved core rate (5.2)
    _limiter.Configure(static_cast<double>(_coreRate));
}

SoundManager::~SoundManager()
{
    if (_covox)
    {
        delete _covox;
    }

    if (_gs)
    {
        delete _gs;
    }
#ifdef UNREALNG_HAVE_OPL4
    if (_moonsound)
    {
        delete _moonsound;
    }
#endif

    if (_turboSound)
    {
        delete _turboSound;
    }

    if (_beeper)
    {
        delete _beeper;
    }
}

/// endregion </Constructors / Destructors>

/// region <Methods>

void SoundManager::reset()
{
    // Reset all chips state
    if (_turboSound)
        _turboSound->reset();
    _activityIndicators.reset();
    _beeper->reset();
    if (_covox)
        _covox->reset();
    // GS reset follows the GSReset rule (design §5.4): the card is a
    // separate subsystem and survives a ZX reset unless coupled
    if (_gs)
        _gs->hostReset();
#ifdef UNREALNG_HAVE_OPL4
    if (_moonsound)
        _moonsound->reset();
#endif

    std::fill(_beeperBuffer, _beeperBuffer + AUDIO_BUFFER_SAMPLES_PER_FRAME, 0);
    std::fill(_outBuffer, _outBuffer + AUDIO_BUFFER_SAMPLES_PER_FRAME, 0);
    std::fill(_mixBus, _mixBus + AUDIO_BUFFER_SAMPLES_PER_FRAME, 0.0f);
    _limiter.Reset();

    // Restart the exact sample accumulator (machine change / hard reset /
    // snapshot load all route through reset())
    _sampleAccumulator = 0;

    // New wave file
    // closeWaveFile();
    // std::string filePath = "unreal.wav";
    // openWaveFile(filePath);
}

void SoundManager::enableWideMix(bool enable)
{
    if (_wideMix == enable)
        return;
    _wideMix = enable;
    _limiter.Reset();
    std::fill(_mixBus, _mixBus + AUDIO_BUFFER_SAMPLES_PER_FRAME, 0.0f);
}

void SoundManager::mute()
{
    _mute = true;
}

void SoundManager::unmute()
{
    _mute = false;
}

void SoundManager::onEmulatorPaused()
{
    if (_gs)
        _gs->onEmulatorPaused();

    // Nothing plays while paused: every LED and HUD nudge goes dark now
    for (AudioDeviceInfo& d : _devices)
    {
        d.peak = 0.0f;
        d.activeRecently = false;
    }
    _activityIndicators.stop(_context->emulatorId);
}

const AudioFrameDescriptor& SoundManager::getAudioBufferDescriptor()
{
    return _beeperAudioDescriptor;
}

Beeper& SoundManager::getBeeper()
{
    return *_beeper;
}

/// Compatibility shim for tape audio.
/// Routes the amplitude into the beeper's blip_buf at the given T-state position.
/// This preserves backward compatibility with Tape::handlePortOut() which
/// calls updateDAC() with pre-filtered samples.
void SoundManager::updateDAC(uint32_t frameTState, int16_t left, [[maybe_unused]] int16_t right)
{
    // Feed the averaged mono amplitude into the beeper's blip_buf.
    // Tape output is mono (left == right), so we use left as the amplitude.
    _beeper->handleTapeAudio(static_cast<int32_t>(left), _context->emulatorState.AudioTstate(frameTState));
}

// TurboSound/AY chip access for debugging
SoundChip_AY8910* SoundManager::getAYChip(int index) const
{
    if (!_turboSound)
        return nullptr;

    return _turboSound->getChip(index);
}

int SoundManager::getAYChipCount() const
{
    if (!_turboSound)
        return 0;

    return _turboSound->getChipCount();
}

// Device registry API
AudioDeviceInfo* SoundManager::device(AudioSourceType type)
{
    for (auto& d : _devices)
        if (d.type == type)
            return &d;
    return nullptr;
}

const AudioDeviceInfo* SoundManager::device(AudioSourceType type) const
{
    for (auto& d : _devices)
        if (d.type == type)
            return &d;
    return nullptr;
}

const int16_t* SoundManager::deviceBuffer(AudioSourceType type) const
{
    switch (type)
    {
        case AudioSourceType::MasterMix:
            return _outBuffer;
        case AudioSourceType::Beeper:
            return _beeperBuffer;
        case AudioSourceType::AY1_All:
            return _turboSound ? _turboSound->getChipBuffer(0) : nullptr;
        case AudioSourceType::AY2_All:
            return _turboSound ? _turboSound->getChipBuffer(1) : nullptr;
        case AudioSourceType::FM1:
            return _turboSound ? _turboSound->getFmBuffer(0) : nullptr;
        case AudioSourceType::FM2:
            return _turboSound ? _turboSound->getFmBuffer(1) : nullptr;
        case AudioSourceType::COVOX:
            return _covox ? _covox->getBuffer() : nullptr;
        case AudioSourceType::GeneralSound:
            return _gs ? _gs->getBuffer() : nullptr;
#ifdef UNREALNG_HAVE_OPL4
        case AudioSourceType::Moonsound_FM:
            return _moonsound ? _moonsound->getFmBuffer() : nullptr;
        case AudioSourceType::Moonsound_PCM:
            return _moonsound ? _moonsound->getPcmBuffer() : nullptr;
#endif
        default:
            return nullptr;
    }
}

void SoundManager::setDeviceMute(AudioSourceType type, bool mute)
{
    if (auto* d = device(type))
        d->mute = mute;
}

void SoundManager::setDeviceSolo(AudioSourceType type, bool solo)
{
    if (auto* d = device(type))
        d->solo = solo;
}

void SoundManager::setDeviceVolume(AudioSourceType type, float volume)
{
    if (auto* d = device(type))
        d->volume = std::clamp(volume, 0.0f, 1.0f);
}

void SoundManager::syncAYChainSettings()
{
    // Copy settings from chain 0 to chain 1 (UI edits chain 0, both should match)
    _ayChain1.setChipType(_ayChain0.getChipType());
    _ayChain1.setPunchPreset(_ayChain0.getPunchPreset());
    _ayChain1.setPunchEnabled(_ayChain0.isPunchEnabled());
    _ayChain1.setRoomMode(_ayChain0.getRoomMode());
}

// Legacy volume API delegates to registry
void SoundManager::setAYVolume(double volume)
{
    _ayVolume = std::clamp(volume, 0.0, 1.0);
    setDeviceVolume(AudioSourceType::AY1_All, static_cast<float>(_ayVolume));
    setDeviceVolume(AudioSourceType::AY2_All, static_cast<float>(_ayVolume));
}

void SoundManager::setBeeperVolume(double volume)
{
    _beeperVolume = std::clamp(volume, 0.0, 1.0);
    setDeviceVolume(AudioSourceType::Beeper, static_cast<float>(_beeperVolume));
}

/// endregion </Methods>

/// region <Emulation events>
void SoundManager::requestCoreRate(uint32_t rate)
{
    if (!IsSupportedCoreRate(rate))
    {
        LOGWARNING("SoundManager::requestCoreRate: unsupported rate %u ignored", rate);
        return;
    }

    if (rate == _coreRate)
    {
        _pendingCoreRate.store(0, std::memory_order_release);  // cancel a superseded request
        return;
    }

    _pendingCoreRate.store(rate, std::memory_order_release);
}

void SoundManager::setCoreRatePin(uint32_t rate)
{
    if (rate != 0 && !IsSupportedCoreRate(rate))
    {
        LOGWARNING("SoundManager::setCoreRatePin: unsupported rate %u ignored", rate);
        return;
    }

    _coreRatePin.store(rate, std::memory_order_release);
    reevaluateCoreRate();
}

void SoundManager::reevaluateCoreRate()
{
    requestCoreRate(static_cast<uint32_t>(targetCoreRate()));
}

/// Re-derive the whole audio pipeline for a new core rate. Emulation thread
/// only (frame boundary): no consumer touches DSP state here - the device
/// callback only reads the ring buffer downstream of the DRC resampler.
void SoundManager::applyCoreRate(size_t rate)
{
    const size_t oldRate = _coreRate;
    _coreRate = rate;

    // Band-limited synthesis resamplers (T-state -> core rate)
    _beeper->setSampleRate(rate);
    if (_covox)
        _covox->setSampleRate(rate);
    if (_gs)
        _gs->setSampleRate(rate);
#ifdef UNREALNG_HAVE_OPL4
    if (_moonsound)
        _moonsound->setCoreRate(rate);
#endif

    // AY: sample PLL increment, decimation ratios, anti-alias FIR redesign
    if (_turboSound)
        _turboSound->setCoreRate(rate);

    // Character chains: re-derive envelope/room/punch coefficients for the
    // new rate (setup preserves chip type and presets; resets DSP state)
    _ayChain0.setup(rate);
    _ayChain1.setup(rate);
    _beeperChain.setup(rate);
    _fmChain0.setup(rate);
    _fmChain1.setup(rate);
    _limiter.Configure(static_cast<double>(rate));

    // Restart the exact sample accumulator - its residue is in old-rate units
    _sampleAccumulator = 0;

    // Recording must stamp future captures with the new rate (applyCoreRate
    // is never reached while a recording is active - see handleFrameStart)
#ifdef ENABLE_RECORDING
    if (_context->pRecordingManager)
    {
        _context->pRecordingManager->SetAudioSampleRate(static_cast<uint32_t>(rate));
    }
#endif  // ENABLE_RECORDING

    LOGINFO("SoundManager: core audio rate re-established %zu -> %zu Hz (all filters re-derived)",
            oldRate, rate);
}

void SoundManager::handleFrameStart()
{
    // Apply a pending live core-rate change at the frame boundary (the
    // emulation thread owns all DSP state here). Deferred while a recording
    // is in progress - a recording must keep one rate end to end.
    const uint32_t pending = _pendingCoreRate.load(std::memory_order_acquire);
    if (pending != 0)
    {
#ifdef ENABLE_RECORDING
        const bool recording = _context->pRecordingManager && _context->pRecordingManager->IsRecording();
#else
        const bool recording = false;
#endif  // ENABLE_RECORDING
        if (recording)
        {
            if (!_pendingRateLoggedWhileRecording)
            {
                LOGINFO("SoundManager: core-rate change to %u Hz deferred until recording stops", pending);
                _pendingRateLoggedWhileRecording = true;
            }
        }
        else
        {
            _pendingCoreRate.store(0, std::memory_order_release);
            _pendingRateLoggedWhileRecording = false;
            if (pending != _coreRate)
                applyCoreRate(pending);
        }
    }

    // Apply a pending GS personality switch (gs_lightweight feature toggle,
    // WebAPI action=switch_personality) at the frame boundary: the switch
    // deletes and recreates the card, and this thread owns the card between
    // frames (the new card receives setSynthesisSuppressed/handleFrameStart
    // right below, so audio flows within the same frame)
    const uint8_t pendingGS = _pendingGSSwitch.exchange(0xFF, std::memory_order_acq_rel);
    if (pendingGS != 0xFF)
        switchGeneralSoundCard(static_cast<GSTypeKind>(pendingGS));

    // Turbo mode without audio: no synthesis at all this frame. Decided once here so
    // the per-step and per-edge paths only test a cached bool. Recording keeps the
    // full path so captured audio stays intact.
    {
        const CONFIG& config = _context->config;
        bool suppressed = config.turbo_mode && !config.turbo_mode_audio;
#ifdef ENABLE_RECORDING
        if (suppressed && _context->pRecordingManager && _context->pRecordingManager->IsRecording())
            suppressed = false;
#endif
        _synthesisSuppressed = suppressed;

        // Sound feature off = GENERATION off for every generator, exactly like turbo without audio:
        // the port / register / level state keeps being tracked (what the program can read back and
        // what the first edge after un-muting depends on), only synthesis and filtering stop
        const bool generationOff = suppressed || !_feature_sound_enabled;
        _beeper->setSynthesisSuppressed(generationOff);
        if (_covox)
            _covox->setSynthesisSuppressed(generationOff);
#ifdef UNREALNG_HAVE_OPL4
        if (_moonsound)
        {
            // Sound feature off is generation off for MoonSound too: output dropped and buffers zeroed,
            // chip state untouched (SoundChip_Moonsound::handleFrameEnd)
            _moonsound->setSynthesisSuppressed(generationOff);
            // D3: the synthesis core runs every frame - including suppressed
            // (turbo / sound-off) frames, where this is the only hook that fires: BUSY/LD
            // and register state stay guest-correct.
            _moonsound->handleFrameStart();
        }
#endif

        // §6.1: the TurboSound-slot device is always reached now, in every
        // mode - a device with an emulated core (TSFM) advances it even when
        // its output stage is off. Sound feature off counts as suppressed
        // for the device's rendering; its frame buffer clears still run
        // (the sound-off output path relies on zeroed buffers).
        if (_turboSound)
        {
            const bool turboSoundSuppressed = generationOff;
            _turboSound->setSynthesisSuppressed(turboSoundSuppressed);

            // With the output stage off, the FM core's internal synthesis state feeds nothing the CPU can
            // see - except through the TTD core hash, so TTD recording / replay keeps the full core running
            const ttd::TimeTravelManager* ttd = _context->pTimeTravelManager;
            const bool ttdActive = ttd != nullptr && (ttd->IsRecording() || ttd->IsReplayActive());
            _turboSound->setCoreSynthesisSkipped(turboSoundSuppressed && !ttdActive);

            _turboSound->handleFrameStart();
        }

        // GS: same always-advanced rule as the TSFM slot device - the
        // coprocessor keeps running even when its output stage is suppressed
        // (program-visible state must not depend on audio settings)
        if (_gs)
        {
            _gs->setSynthesisSuppressed(suppressed || !_feature_sound_enabled);
            _gs->handleFrameStart();
        }

        // Covox frame start runs in every mode: its idle-channel decay writes
        // the DAC latch (TTD state), so it must not depend on turbo; the
        // decay's audio step is gated on the covox's own suppressed flag
        if (_covox)
            _covox->handleFrameStart();

        if (suppressed)
            return;  // Skip beeper frame setup and buffer clears (never consumed in turbo)
    }

    // Beeper starts its frame (blip_buf ready to receive deltas)
    _beeper->handleFrameStart();

    // Clear the beeper output buffer (will be filled by handleFrameEnd)
    memset(_beeperBuffer, 0x00, _beeperAudioDescriptor.memoryBufferSizeInBytes);
}

void SoundManager::handleStep()
{
    // §6.1: the TurboSound-slot device is always reached - even when sound
    // generation is disabled or synthesis is suppressed (turbo without
    // audio, turbo tape design r4) - because a device with an emulated core
    // (TSFM) advances that core here. The device gates its own rendering on
    // its suppressed flag; for the legacy device the whole cost in those
    // modes is a single early return inside. Skipping the analog generator
    // work is unobservable to the program: the AY register file is written
    // by PortDecoder on OUT. Recording keeps the full path so DSD
    // native-rate capture and recorded audio stay intact.
    if (_turboSound)
        _turboSound->handleStep();
}

void SoundManager::handleFrameEnd()
{
    // §6.1 frame-end drain: the device's output stage runs here (empty for
    // the legacy device; TSFM drains its word queues). Axis trap: z80->t
    // has already been rebased by AdjustFrameCounters when this runs, so a
    // device must drain to its own end-of-frame position, never to
    // AudioTstate(z80->t). Always called - the device handles suppression
    // internally and posts HUD notifications regardless.
    if (_turboSound)
        _turboSound->handleFrameEnd();

    // Turbo without audio: the frame still ENDS for every device with an
    // emulated core - its program-visible state (GS coprocessor catch-up,
    // MoonSound core + time axis) must not depend on the host audio mode,
    // or a turbo stretch would change the machine's trajectory and its TTD
    // checkpoints. Only the host-audio work below (sample accounting,
    // character chains, mixing, delivery) is skipped - the same condition
    // MainLoop used to gate this whole call on
    const CONFIG& frameConfig = _context->config;
    if (frameConfig.turbo_mode && !frameConfig.turbo_mode_audio)
    {
        if (_gs)
            _gs->handleFrameEnd(0);
#ifdef UNREALNG_HAVE_OPL4
        if (_moonsound)
            _moonsound->handleFrameEnd(0);
#endif
        return;
    }

    /// region <Determine actual samples for this frame>
    // Per-frame sample count derives from the machine's frame length, NOT the
    // 50 Hz SAMPLES_PER_FRAME constant: Pentagon (71680 t-states, 48.83 fps)
    // produces 903.168 samples/frame, ZX48 880.5888, ZX128 (70908) 893.4408.
    //
    // Exact integer accumulator (audio-sync design, Fix 1): the fractional
    // part is CARRIED, not rounded away. Rounding emitted a systematic rate
    // bias (-0.019% Pentagon / +0.047% ZX48) - the dominant source of both
    // realtime ring drift and audio-behind-video drift in recordings. With
    // the carry, the sequence is exactly periodic (903,903,...,904 with
    // period 125 on Pentagon@44.1k) and drift-free by construction.
    //
    // Units: T-states x rate, modulo CPU_CLOCK_RATE - the same accumulator
    // every device (TurboSound, TSFM) renders its buffers with, so both count
    // the same samples in every frame. config.frame_duration_us is the pacing
    // clock, rounded UP to whole microseconds; counting in it disagreed with
    // the devices on every other frame wherever the frame is not a whole
    // number of microseconds (70908 T = 20259.43 us on 128K/+3, 99880 T on
    // ATM): the mixer read a never-rendered zero sample or dropped one.
    // Recordings stamp video with the same exact frame/CPU_CLOCK_RATE
    // duration; the realtime pacing difference (<30 ppm) is absorbed by DRC.
    size_t samplesThisFrame = SAMPLES_PER_FRAME;
    uint32_t frameDuration = 0;     // T-states (for beeper)
    {
        CONFIG& config = _context->config;
        // Host multiplier only: the Scorpion hardware turbo doubles CPU
        // T-states inside an unchanged 20 ms frame, so it must NOT double the
        // samples of that frame (it overfilled the ring 2x - hard resyncs)
        uint8_t speedMultiplier = _context->emulatorState.HostSpeedMultiplier();
        frameDuration = config.frame * speedMultiplier;

        // A video frame takes the same real time at any CPU clock, so the
        // sample count is multiplier-invariant: the base frame length
        if (config.frame > 0)
        {
            _sampleAccumulator += static_cast<uint64_t>(config.frame) * _coreRate;
            samplesThisFrame = static_cast<size_t>(_sampleAccumulator / CPU_CLOCK_RATE);
            _sampleAccumulator %= CPU_CLOCK_RATE;

            // Overflow guard: buffers are sized MAX_SAMPLES_PER_FRAME. Drop
            // the excess KNOWINGLY; a silent overrun would be worse.
            if (samplesThisFrame > MAX_SAMPLES_PER_FRAME)
            {
                if ((_accumulatorClampCount++ % 256) == 0)
                {
                    LOGWARNING("SoundManager: samplesThisFrame %zu clamped to %d (speed multiplier %u)",
                                samplesThisFrame, MAX_SAMPLES_PER_FRAME, speedMultiplier);
                }
                samplesThisFrame = MAX_SAMPLES_PER_FRAME;
                _sampleAccumulator = 0;
            }
        }
    }
    /// endregion </Determine actual samples for this frame>

    /// region <Process AY through its character chain>
    // The character chains (punch / room) are HQ-only post-processing: with
    // `soundhq` off (or the turbo override on) they are skipped entirely -
    // no float round trip, no per-sample DSP - and the raw chip / beeper
    // buffers go straight to the mixer, the same as the LQ boxcar path
    // inside the devices. On the first HQ frame after a bypass the chains'
    // delay lines and envelopes are cleared so they do not replay audio
    // from before the switch.
    // Sound feature off: no generator runs, no character chain runs, nothing is mixed. The frame
    // buffers are zeroed instead, so the output path (and the per-device meters) see silence
    const bool soundOff = !_feature_sound_enabled;

    const bool chainsActive = isHQActive() && !soundOff;
    if (chainsActive && _chainsBypassed)
    {
        _ayChain0.reset();
        _ayChain1.reset();
        _fmChain0.reset();
        _fmChain1.reset();
        _beeperChain.reset();
    }
    _chainsBypassed = !chainsActive;

    // AY chain: gentler punch (square waves already have harmonics)
    // Room uses no LP to preserve brightness
    // Process per-chip buffers with separate chain instances to preserve DSP state
    if (_turboSound && chainsActive)
    {
        int16_t* chip0Buf = _turboSound->getChipBuffer(0);
        int16_t* chip1Buf = _turboSound->getChipBuffer(1);
        if (chip0Buf)
            _ayChain0.processInt16(chip0Buf, samplesThisFrame);
        if (chip1Buf)
            _ayChain1.processInt16(chip1Buf, samplesThisFrame);

        // FM-only buffers through their (bypass by default) chains (§7.2)
        if (_turboSound->hasFm())
        {
            int16_t* fm0Buf = _turboSound->getFmBuffer(0);
            int16_t* fm1Buf = _turboSound->getFmBuffer(1);
            if (fm0Buf)
                _fmChain0.processInt16(fm0Buf, samplesThisFrame);
            if (fm1Buf)
                _fmChain1.processInt16(fm1Buf, samplesThisFrame);
        }
    }
    /// endregion </Process AY>

    /// region <Process beeper>
    if (soundOff)
    {
        // The beeper only tracked its level this frame (no deltas were generated)
        memset(_beeperBuffer, 0x00, _beeperAudioDescriptor.memoryBufferSizeInBytes);
    }
    else
    {
        // Finalize the beeper's blip_buf frame — produces band-limited output
        _beeper->handleFrameEnd(frameDuration);

        // Cross-check blip's internal fractional accumulator against ours. Both
        // are driven by the same clock ratio and stay in lockstep; >1 sample
        // divergence indicates an accumulator reset bug (logged, not asserted -
        // snapshot load / reset may legitimately differ for 1 frame)
        {
            int blipRead = _beeper->getLastSamplesRead();
            int diff = blipRead - static_cast<int>(samplesThisFrame);
            if (diff > 1 || diff < -1)
            {
                if ((_blipMismatchCount++ % 256) == 0)
                {
                    LOGWARNING("SoundManager: blip delivered %d samples, accumulator expects %zu", blipRead,
                                samplesThisFrame);
                }
            }

            // Pad shortfall with the last delivered value so the mixer never
            // consumes a stale tail (blip can be 1 short right after a reset)
            if (blipRead >= 1 && static_cast<size_t>(blipRead) < samplesThisFrame)
            {
                for (size_t i = blipRead; i < samplesThisFrame; i++)
                {
                    _beeperBuffer[i * 2] = _beeperBuffer[(blipRead - 1) * 2];
                    _beeperBuffer[i * 2 + 1] = _beeperBuffer[(blipRead - 1) * 2 + 1];
                }
            }
        }

        // Beeper chain: operates on alias-free blip_buf output (HQ only, see above)
        if (chainsActive)
            _beeperChain.processInt16(_beeperBuffer, samplesThisFrame);
    }
    /// endregion </Process beeper>

    /// region <Registry-driven mixing with mute/solo/volume + peak calculation>
    // Finalize Covox frame (DC removal etc.) before mixing
    if (_covox)
    {
        if (soundOff)
            memset(_covox->getBuffer(), 0x00, AudioFrameDescriptor::memoryBufferSizeInBytes);
        else
            _covox->handleFrameEnd(samplesThisFrame);
    }

#ifdef UNREALNG_HAVE_OPL4
    // Finalize MoonSound frame (advance the core to the frame end; render)
    if (_moonsound)
        _moonsound->handleFrameEnd(samplesThisFrame);
#endif

    // Finalize GS frame: coprocessor catch-up to the frame end + blip drain
    // into the registry buffer
    if (_gs)
        _gs->handleFrameEnd(samplesThisFrame);

    // NOTE: _turboSound->handleFrameEnd() is NOT called again here. It
    // already ran once at the top of this function (word-queue drain, §6.1)
    // and is "always called" - once.

    // Determine if any device has solo active
    bool soloActive = false;
    for (const auto& d : _devices)
    {
        if (d.solo)
        {
            soloActive = true;
            break;
        }
    }

    // Clear output buffer before mixing
    memset(_outBuffer, 0, samplesThisFrame * AUDIO_CHANNELS * sizeof(int16_t));
    if (_wideMix)
    {
        // Wide path (5.2): the float bus is zeroed here; _outBuffer is fully
        // rewritten by the limiter quantisation after the mix.
        memset(_mixBus, 0, samplesThisFrame * AUDIO_CHANNELS * sizeof(float));
    }

    // Mix each device according to audibility rules and compute peaks
    for (auto& d : _devices)
    {
        if (soundOff)
        {
            // All sources are silent; the output buffer is already cleared
            d.peak = 0.0f;
            d.activeRecently = false;
            continue;
        }

        // Audibility: if any solo is active, only soloed devices are audible;
        // otherwise, non-muted devices are audible
        bool audible = soloActive ? d.solo : !d.mute;

        // Get the device's buffer
        const int16_t* srcBuffer = nullptr;
        switch (d.type)
        {
            case AudioSourceType::Beeper:
                srcBuffer = _beeperBuffer;
                break;
            case AudioSourceType::AY1_All:
                srcBuffer = _turboSound ? _turboSound->getChipBuffer(0) : nullptr;
                break;
            case AudioSourceType::AY2_All:
                srcBuffer = _turboSound ? _turboSound->getChipBuffer(1) : nullptr;
                break;
            case AudioSourceType::FM1:
                srcBuffer = _turboSound ? _turboSound->getFmBuffer(0) : nullptr;
                break;
            case AudioSourceType::FM2:
                srcBuffer = _turboSound ? _turboSound->getFmBuffer(1) : nullptr;
                break;
            case AudioSourceType::COVOX:
                srcBuffer = _covox ? _covox->getBuffer() : nullptr;
                break;
            case AudioSourceType::GeneralSound:
                srcBuffer = _gs ? _gs->getBuffer() : nullptr;
                break;
#ifdef UNREALNG_HAVE_OPL4
            case AudioSourceType::Moonsound_FM:
                srcBuffer = _moonsound ? _moonsound->getFmBuffer() : nullptr;
                break;
            case AudioSourceType::Moonsound_PCM:
                srcBuffer = _moonsound ? _moonsound->getPcmBuffer() : nullptr;
                break;
#endif
            default:
                break;
        }

        if (!srcBuffer)
            continue;

        // Compute peak and activity (always, even if muted — for UI meters)
        float peak = 0.0f;
        for (size_t i = 0; i < samplesThisFrame * AUDIO_CHANNELS; i++)
        {
            float absVal = std::abs(static_cast<float>(srcBuffer[i])) / 32768.0f;
            if (absVal > peak)
                peak = absVal;
        }
        d.peak = peak;
        if (d.type == AudioSourceType::GeneralSound)
        {
            // Peak amplitude alone is the wrong signal for GS: a DAC channel
            // latched away from centre by a command (a one-shot digi sample's
            // last byte, a firmware self-test tone) and then left alone
            // renders as a constant-but-non-zero PCM level forever after, so
            // a naive peak > threshold check reads as permanently "active"
            // once anything has ever touched a channel - not just while the
            // card is actually playing. hadAudioActivityLastFrame() is the
            // signal the HUD nudge is held from (AudioActivityIndicators).
            d.activeRecently = _gs && _gs->hadAudioActivityLastFrame();
        }
        else if (d.type == AudioSourceType::Beeper)
        {
            // Same reason as GS: a beeper (or tape level) left high renders
            // as a constant non-zero output - silence, not activity
            d.activeRecently = _beeper->hadSoundLastFrame();
        }
        else if (d.type == AudioSourceType::COVOX)
        {
            // Same again for a DAC latched away from 0x80 (DC removal is off
            // by default)
            d.activeRecently = _covox && _covox->hadSoundLastFrame();
        }
        else
        {
            d.activeRecently = (peak > AUDIO_ACTIVITY_PEAK);
        }

        // Mix into output if audible
        if (audible && d.volume > 0.0f)
        {
            float vol = d.volume;
            if (_wideMix)
            {
                // Wide path (5.2): unclipped float accumulation - clipping
                // is the master limiter's job alone.
                for (size_t i = 0; i < samplesThisFrame * AUDIO_CHANNELS; i++)
                    _mixBus[i] += static_cast<float>(srcBuffer[i]) * vol;
            }
            else
            {
                for (size_t i = 0; i < samplesThisFrame * AUDIO_CHANNELS; i++)
                {
                    int32_t mixed = _outBuffer[i] + static_cast<int32_t>(srcBuffer[i] * vol);
                    // Saturating add
                    _outBuffer[i] = static_cast<int16_t>(std::clamp(mixed, -32768, 32767));
                }
            }
        }
    }
    /// endregion </Registry-driven mixing>

    // HUD audio nudges: the LEDs just computed, held for a second - one
    // measurement, so every nudge agrees with its LED
    _activityIndicators.endFrame(_context->emulatorId, _devices);

    if (_wideMix)
    {
        // Master DC blocker + soft limiter (5.2), then a single int16
        // quantisation (round-to-nearest). The wide path may differ from the
        // legacy path by +-1 LSB by design; R6 guarantees byte-identity only
        // for the legacy branch, taken when no MoonSound device is attached.
        _limiter.Process(_mixBus, samplesThisFrame);
        for (size_t i = 0; i < samplesThisFrame * AUDIO_CHANNELS; i++)
        {
            const long rounded = std::lrintf(_mixBus[i]);
            _outBuffer[i] = static_cast<int16_t>(std::clamp(rounded, -32768L, 32767L));
        }
    }

#ifdef ENABLE_RECORDING
    // Capture audio for recording BEFORE muting
    // This ensures recordings get the actual audio, not silence
    if (_context->pRecordingManager && _context->pRecordingManager->IsRecording())
    {
        _context->pRecordingManager->CaptureAudio(_outBuffer, samplesThisFrame * AUDIO_CHANNELS);
    }
#endif

    /// region <Analyzer audio tap (MCP automation)>
    // Post-mix, pre-mute/DRC vantage — identical to the recording tap above.
    // One dispatch per stereo frame; guarded so the common case (no analyzer
    // subscribed) costs a single branch.
    if (_context->pDebugManager)
    {
        AnalyzerManager* analyzerManager = _context->pDebugManager->GetAnalyzerManager();
        if (analyzerManager && analyzerManager->hasAudioSampleSubscribers())
        {
            for (size_t i = 0; i < samplesThisFrame; i++)
            {
                analyzerManager->dispatchAudioSample(_outBuffer[i * AUDIO_CHANNELS],
                                                      _outBuffer[i * AUDIO_CHANNELS + 1]);
            }
        }
    }
    /// endregion </Analyzer audio tap>

    // Enqueue generated sound data via previously registered application callback
    // Note: Audio callbacks are cleared when emulator loses audio device access to prevent
    // multiple emulators from using the same audio device simultaneously
    // Use memory_order_acquire to ensure we see the latest values written by the UI thread
    AudioCallback callback = _context->pAudioCallback.load(std::memory_order_acquire);
    void* obj = _context->pAudioManagerObj.load(std::memory_order_acquire);

    if (callback && obj)
    {
        // If muted, send silence instead of actual audio.
        // No need to send silence if sound generation is disabled -
        // buffer was already zeroed out in SoundManager::handleFrameStart() method
        if (_feature_sound_enabled && _mute)
        {
            // Zero out the buffer (silence)
            memset(_outBuffer, 0, samplesThisFrame * AUDIO_CHANNELS * sizeof(int16_t));
        }

        // DRC rate control (audio-sync design, Fix 2): trim the resample
        // ratio from ring occupancy, once per frame
        updateDrcControl();

        // DRC resampler stage. Sits AFTER the recording tap above - recording
        // always receives the pure CORE_RATE stream - and BEFORE the device
        // callback. At unity ratio (controller disengaged) this is a
        // bit-exact memcpy bypass.
        size_t deviceFrames =
            _drcResampler.process(_outBuffer, samplesThisFrame, _deviceBuffer, DEVICE_BUFFER_FRAMES);

        try
        {
            callback(obj, _deviceBuffer, deviceFrames * AUDIO_CHANNELS);
        }
        catch (const std::exception& e)
        {
            // Log error but don't crash - audio callback failure shouldn't stop emulation
            LOGERROR("SoundManager::handleFrameEnd - Audio callback failed: %s\n", e.what());
        }
        catch (...)
        {
            // Log error but don't crash - audio callback failure shouldn't stop emulation
            LOGERROR("SoundManager::handleFrameEnd - Audio callback failed with unknown exception\n");
        }
    }
}

/// DRC PI controller (audio-sync design 5.1): holds ring occupancy at
/// DRC_TARGET_MS by trimming the resample ratio within +-0.5%. Ring
/// occupancy IS the A/V offset, so this defines and stabilizes lip-sync.
/// Sign: ring too full => producing faster than the DAC consumes => emit
/// fewer output samples per input sample => negative trim.
void SoundManager::updateDrcControl()
{
    const std::atomic<uint32_t>* occCell = _context->pAudioRingOccupancy.load(std::memory_order_acquire);
    const bool engaged = occCell != nullptr && !_context->config.turbo_mode && _feature_sound_enabled;

    if (!engaged)
    {
        _drcResampler.setRatio(1.0);
        _drcErrIntegral = 0.0;
        _drcOccFiltered = -1.0;
        return;
    }

    // Device native rate (audio-sync Fix 3): base resample ratio dev/core;
    // ring occupancy is measured in DEVICE-rate frames
    const uint32_t devRateRaw = _context->pAudioDeviceSampleRate.load(std::memory_order_relaxed);
    const double devRate = (devRateRaw == 0) ? static_cast<double>(_coreRate)
                                             : static_cast<double>(devRateRaw);
    const double baseRatio = devRate / static_cast<double>(_coreRate);

    const double occMs = occCell->load(std::memory_order_relaxed) * 1000.0 / devRate;

    if (_drcOccFiltered < 0.0)
        _drcOccFiltered = occMs;  // Seed the EMA on first engagement
    else
        _drcOccFiltered += DRC_EMA_ALPHA * (occMs - _drcOccFiltered);

    const double err = (_drcOccFiltered - DRC_TARGET_MS) / DRC_TARGET_MS;

    // Soft deadband: inside the band the error is noise, above it the band
    // width is subtracted (unit slope, continuous at the edge - no chatter).
    const double effErr = (err > DRC_ERR_DEADBAND)    ? err - DRC_ERR_DEADBAND
                          : (err < -DRC_ERR_DEADBAND) ? err + DRC_ERR_DEADBAND
                                                      : 0.0;

    // Anti-windup, two rules (GS pitch-drift investigation, 2026-09-20):
    // 1. The integral may only hold what the actuator can use:
    //    KI * I <= DRC_MAX_TRIM. The former +-50 clamp let KI*I reach 8x the
    //    +-0.5% output rail, so any sustained disturbance pinned I at the
    //    clamp - seconds of railed, wrong-signed trim afterwards, audible as
    //    the whole mix gliding +-8.6 cents (GS modules expose it loudest).
    // 2. While the output is railed, back-calculate I from the saturated
    //    output instead of accumulating: I stays consistent with what is
    //    actually being produced, so the output leaves the rail the first
    //    frame the error allows instead of unwinding for ~100 frames.
    const double integralLimit = DRC_MAX_TRIM / DRC_KI;
    double trim = -(DRC_KP * effErr + DRC_KI * _drcErrIntegral);
    if (trim > DRC_MAX_TRIM || trim < -DRC_MAX_TRIM)
    {
        trim = std::clamp(trim, -DRC_MAX_TRIM, DRC_MAX_TRIM);
        _drcErrIntegral = std::clamp(-(trim + DRC_KP * effErr) / DRC_KI, -integralLimit, integralLimit);
    }
    else
    {
        _drcErrIntegral = std::clamp(_drcErrIntegral + effErr, -integralLimit, integralLimit);
    }

    _drcResampler.setRatio(baseRatio * (1.0 + trim));
}

/// @brief Update feature cache flags from FeatureManager.
///
/// This method is automatically called by FeatureManager::onFeatureChanged() whenever
/// sound-related feature states change. It updates cached boolean flags to avoid
/// repeated hash map lookups in hot paths (handleStep is called ~70,000 times/frame).
///
/// @note Do NOT call directly - use FeatureManager API to change states.
///
/// **Triggered by (CLI):**
/// ```bash
/// feature sound off       # Disables sound generation (~18% CPU savings)
/// feature sound on        # Re-enables sound generation
/// feature soundhq off     # Switches to low-quality DSP (~15% CPU savings)
/// feature soundhq on      # Switches to high-quality DSP (FIR + oversampling)
/// ```
///
/// **Triggered by (API):**
/// ```cpp
/// context->pFeatureManager->setFeature("sound", false);
/// context->pFeatureManager->setFeature("soundhq", true);
/// ```
///
/// **Propagation Flow:**
/// ```
/// User CLI/API → FeatureManager::setFeature()
///     ↓
/// FeatureManager::onFeatureChanged()
///     ↓
/// SoundManager::UpdateFeatureCache()  ← YOU ARE HERE
///     ↓
/// _feature_sound_enabled, _feature_soundhq_enabled updated
///     ↓
/// Hot paths (handleStep) use cached flags
/// ```
void SoundManager::UpdateFeatureCache()
{
    if (_context && _context->pFeatureManager)
    {
        bool newSoundEnabled = _context->pFeatureManager->isEnabled(Features::kSoundGeneration);
        _feature_soundhq_enabled = _context->pFeatureManager->isEnabled(Features::kSoundHQ);

        _feature_sound_enabled = newSoundEnabled;

        // Propagate the effective HQ flag (feature state minus the turbo override) to TurboSound
        if (_turboSound)
        {
            _turboSound->setHQEnabled(isHQActive());
        }

        // GS personality follows the gs_lightweight feature (runtime
        // switching design): ON fits the lightweight card, OFF returns to
        // the configured [SOUND] GSType personality - the config file is
        // never touched. Requested here, applied at the next frame
        // boundary (the switch recreates the card). Driven by an actual
        // feature TRANSITION only: this cache refresh runs on every
        // FeatureManager notification (any feature), and re-requesting the
        // configured personality each time would silently revert a runtime
        // WebAPI switch_personality override at the next unrelated feature
        // event (observed live: LW -> LLE switch reverted within 38 ms by a
        // ring-error feature refresh while GSType=LW was configured)
        const bool gsLightweightOn = _context->pFeatureManager->isEnabled(Features::kGSLightweight);
        if (gsLightweightOn != _gsLightweightFeatureWasOn)
        {
            _gsLightweightFeatureWasOn = gsLightweightOn;
            if (gsLightweightOn)
            {
                requestGeneralSoundCardSwitch(GSTypeKind::LW);
            }
            else if (_context->config.sound.gsTypeKind == GSTypeKind::Z80
                     || _context->config.sound.gsTypeKind == GSTypeKind::LW)
            {
                requestGeneralSoundCardSwitch(_context->config.sound.gsTypeKind);
            }
        }
    }
    else
    {
        // Fallback: if FeatureManager unavailable, ensure sound is ON by default
        LOGWARNING("SoundManager::UpdateFeatureCache - FeatureManager unavailable, defaulting sound ON");
        _feature_sound_enabled = true;
        _feature_soundhq_enabled = true;
    }
}

void SoundManager::setTurboLowQualityOverride(bool enabled)
{
    if (_turboLowQualityOverride == enabled)
        return;

    _turboLowQualityOverride = enabled;
    if (_turboSound)
    {
        _turboSound->setHQEnabled(isHQActive());
    }
}

/// endregion </Emulation events>

/// region <Wave file export>
bool SoundManager::openWaveFile(std::string& path)
{
    bool result = false;

    int res =
        tinywav_open_write(&_tinyWav, AUDIO_CHANNELS, (int32_t)_coreRate, TW_INT16, TW_INTERLEAVED, path.c_str());

    if (res == 0 && _tinyWav.file)
    {
        result = true;
    }

    return result;
}

void SoundManager::closeWaveFile()
{
    if (_tinyWav.file)
    {
        tinywav_close_write(&_tinyWav);
    }
}

void SoundManager::SoundManager::writeToWaveFile(uint8_t* buffer, size_t len)
{
    // Convert length from bytes to samples (stereo sample still counts as single)
    size_t lengthInSamples = len / AUDIO_CHANNELS / sizeof(uint16_t);

    // Save using method with Int16 samples input
    tinywav_write_i(&_tinyWav, buffer, lengthInSamples);
}

/// endregion </Wave file export>

/// region <General Sound personality switching>

GeneralSoundCard* SoundManager::createGeneralSoundCard(GSTypeKind kind) const
{
    switch (kind)
    {
        case GSTypeKind::Z80:
        {
            // LLE: second z80ex coprocessor + gs105a firmware. The ROM is
            // optional - loadROM warns and runs zeroed when missing, so a
            // config error never blocks the machine
            auto* card = new SoundChip_GeneralSound(_context, _context->config.sound.gsRamKB, _coreRate);
            card->loadROM(_context->config.gs_rom_path);
            return card;
        }
        case GSTypeKind::LW:
            // Lightweight personality: same mailbox contract, in-tree player,
            // no ROM (loadROM would only warn) - the virtual RAM geometry
            // still comes from [SOUND] GSRamSize for the 20/21/23 queries
            return new SoundChip_GSLightweight(_context, _context->config.sound.gsRamKB, _coreRate);
        default:
            // BASS folded into LW at config parse; NONE/NGS parse but map to
            // no device (neogs-tdd.md P2 placeholder)
            LOGWARNING("SoundManager: GSType %u maps to no creatable personality - no General Sound card",
                       static_cast<unsigned>(kind));
            return nullptr;
    }
}

bool SoundManager::switchGeneralSoundCard(GSTypeKind target)
{
    if (target != GSTypeKind::Z80 && target != GSTypeKind::LW)
    {
        LOGWARNING("SoundManager: personality switch target GSType %u is not switchable (Z80 | LW)",
                   static_cast<unsigned>(target));
        return false;
    }

    if (!_gs)
    {
        LOGWARNING("SoundManager: personality switch requested but no General Sound card is fitted ([SOUND] GSType)");
        return false;
    }

    const GSCardImplementation targetImplementation =
        target == GSTypeKind::Z80 ? GSCardImplementation::LLE : GSCardImplementation::LW;
    if (_gs->implementation() == targetImplementation)
        return true; // already the requested personality

    const char* from = _gs->implementation() == GSCardImplementation::LLE ? "LLE (Z80)" : "lightweight";
    const char* to = targetImplementation == GSCardImplementation::LLE ? "LLE (Z80)" : "lightweight";

    // 1. Snapshot the outgoing card: host-visible mailbox, activity counters
    //    and the v1 module handoff payload (only the lightweight card
    //    captures one - its upload store survives the parse)
    const GSForwardMailbox mailbox = _gs->snapshotMailbox();
    const GSActivityCounters counters = _gs->getActivityCounters();
    std::vector<uint8_t> moduleBytes;
    bool wasPlaying = false;
    const bool hadModule = _gs->captureModuleUpload(moduleBytes, wasPlaying);
    // The outgoing card's TTD slot - captured before delete below, since
    // it's read through the about-to-be-freed pointer (see 3b.)
    const ttd::PeripheralId outgoingTtdId = _gs->TTDPeripheralId();

    // 2. Unregister the host ports first - the decoder holds the outgoing
    //    card's raw pointer and must never dispatch into a deleted object
    auto* decoder = _context->pPortDecoder;
    if (decoder)
    {
        decoder->UnregisterPortHandler(GeneralSoundCard::PORT_DATA);
        decoder->UnregisterPortHandler(GeneralSoundCard::PORT_COMMAND);
        decoder->UnregisterPortHandler(GeneralSoundCard::PORT_CONTROL);
    }

    // 3. Construct the target (the LLE loads its firmware ROM inside the
    //    factory); on failure roll the ports back onto the surviving card
    GeneralSoundCard* card = createGeneralSoundCard(target);
    if (!card)
    {
        if (decoder)
        {
            decoder->RegisterPortHandler(GeneralSoundCard::PORT_DATA, _gs, static_cast<PortTagSet>(PortTag::SoundGs));
            decoder->RegisterPortHandler(GeneralSoundCard::PORT_COMMAND, _gs, static_cast<PortTagSet>(PortTag::SoundGs));
            decoder->RegisterPortHandler(GeneralSoundCard::PORT_CONTROL, _gs, static_cast<PortTagSet>(PortTag::SoundGs));
        }
        LOGERROR("SoundManager: General Sound personality switch to %s failed at construction - keeping %s", to, from);
        return false;
    }

    delete _gs;
    _gs = card;

    // 3b. Re-point the TTD peripheral registry at the new card. TTD registers
    //     the GS slot by raw pointer only at StartRecording/session load
    //     (RegisterModelPeripherals) - without this, a switch during an
    //     active recording leaves the registry holding a pointer to the card
    //     just deleted above, and the next checkpoint's TTDSaveState call is
    //     a use-after-free. LLE and LW register under different peripheral
    //     ids (GeneralSound vs GeneralSoundLightweight, same split as the
    //     TurboSound/TSFM slots) so a checkpoint recorded on one personality
    //     cannot silently restore into the other - UpdatePeripheral moves the
    //     registration from the outgoing card's slot to the new card's own
    //     TTDPeripheralId(), which differs across a personality switch by
    //     construction. Safe to call unconditionally: a null
    //     TimeTravelManager (TTD unavailable) no-ops.
    if (_context && _context->pTimeTravelManager)
        _context->pTimeTravelManager->UpdatePeripheral(outgoingTtdId, _gs->TTDPeripheralId(), _gs);

    // 4. Re-register the host ports for the new card (#B3/#BB/#33, GS design §6)
    bool portsRegistered = true;
    if (decoder)
    {
        portsRegistered &= decoder->RegisterPortHandler(GeneralSoundCard::PORT_DATA, _gs, static_cast<PortTagSet>(PortTag::SoundGs));
        portsRegistered &= decoder->RegisterPortHandler(GeneralSoundCard::PORT_COMMAND, _gs, static_cast<PortTagSet>(PortTag::SoundGs));
        portsRegistered &= decoder->RegisterPortHandler(GeneralSoundCard::PORT_CONTROL, _gs, static_cast<PortTagSet>(PortTag::SoundGs));
    }
    if (!portsRegistered)
        LOGWARNING("SoundManager: GS host port re-registration failed after the personality switch");

    // 5. Module handoff on virgin queues (running BEFORE the mailbox restore
    //    keeps the param/command/stream/D2 ordering pristine), then restore
    //    the host-visible mailbox and fold the counters - triage totals
    //    survive the handoff
    if (hadModule)
        _gs->replayModuleUpload(moduleBytes, wasPlaying);
    _gs->restoreMailbox(mailbox);
    _gs->accumulateActivityCounters(counters);

    LOGINFO("SoundManager: General Sound personality switched %s -> %s%s", from, to,
            hadModule ? " (module upload replayed)" : "");
    return portsRegistered;
}

bool SoundManager::requestGeneralSoundCardSwitch(GSTypeKind target)
{
    if (target != GSTypeKind::Z80 && target != GSTypeKind::LW)
        return false;

    _pendingGSSwitch.store(static_cast<uint8_t>(target), std::memory_order_release);
    return true;
}

/// endregion </General Sound personality switching>

/// region <Port interconnection>

bool SoundManager::attachToPorts()
{
    // An empty TurboSound slot claims no ports: #FFFD / #BFFD read the floating bus
    bool result = _turboSound ? _turboSound->attachToPorts(_context->pPortDecoder) : true;

    // SoundDrive/Covox is a self-decoding device (Covox::tryClaimOut/In):
    // its Fitment (Mono #FB only vs Quad mode-1+mode-2) is baked in at
    // construction from config.sound.sd/covoxFB, so registration is just
    // "plug the card in" - no per-port wiring, and no exact-address
    // dispatch-map slot to collide with WD1793 or anything else.
    if (_covox && _context->pPortDecoder)
    {
        result &= _context->pPortDecoder->RegisterSelfDecodingDevice(_covox);
    }

    // Attach the General Sound card to its host ports #B3/#BB/#33 (GS design
    // §6). Registered only when the card exists; the decode rows themselves
    // are static per machine model (Pentagon family table, Scorpion chain).
    if (_gs && _context->pPortDecoder)
    {
        result &= _context->pPortDecoder->RegisterPortHandler(GeneralSoundCard::PORT_DATA, _gs,
                                                             static_cast<PortTagSet>(PortTag::SoundGs));
        result &= _context->pPortDecoder->RegisterPortHandler(GeneralSoundCard::PORT_COMMAND, _gs,
                                                             static_cast<PortTagSet>(PortTag::SoundGs));
        result &= _context->pPortDecoder->RegisterPortHandler(GeneralSoundCard::PORT_CONTROL, _gs,
                                                             static_cast<PortTagSet>(PortTag::SoundGs));
    }

#ifdef UNREALNG_HAVE_OPL4
    // The card owns its bus decode (low-byte full-decode observer, D4) -
    // SoundManager only routes the lifecycle call
    if (_moonsound && _context->pPortDecoder)
    {
        result &= _moonsound->attachToPorts(_context->pPortDecoder);
    }
#endif

    return result;
}

bool SoundManager::detachFromPorts()
{
    bool result = true;

    if (_turboSound)
        _turboSound->detachFromPorts();

    if (_covox && _context->pPortDecoder)
    {
        _context->pPortDecoder->UnregisterSelfDecodingDevice(_covox);
    }

    // Detach the General Sound card from #B3/#BB/#33
    if (_gs && _context->pPortDecoder)
    {
        _context->pPortDecoder->UnregisterPortHandler(GeneralSoundCard::PORT_DATA);
        _context->pPortDecoder->UnregisterPortHandler(GeneralSoundCard::PORT_COMMAND);
        _context->pPortDecoder->UnregisterPortHandler(GeneralSoundCard::PORT_CONTROL);
    }

#ifdef UNREALNG_HAVE_OPL4
    // Detach MoonSound's low-byte full-decode observer registrations
    if (_moonsound)
    {
        _moonsound->detachFromPorts();
    }
#endif

    return result;
}

/// endregion </Port interconnection>
