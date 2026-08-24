#include "soundmanager.h"

#include "base/featuremanager.h"
#include "common/dumphelper.h"
#include "common/sound/audiohelper.h"
#include "common/sound/audioutils.h"
#include "common/stringhelper.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "stdafx.h"

/// region <Constructors / Destructors>

std::atomic<uint32_t> SoundManager::_defaultDeviceSampleRate{0};

/// Resolve the core audio rate from [SOUND] CoreRate (multirate plan phase 6).
/// Explicit supported rates pass through (validated at config load); auto (0)
/// matches the audio device's native rate when known and supported, else
/// falls back to 44100. The device rate comes from the per-emulator cell if
/// the frontend already bound this emulator, otherwise from the process-wide
/// default published at audio device init (the usual case: emulators are
/// constructed BEFORE the frontend binds audio to them).
size_t SoundManager::resolveCoreRate() const
{
    const unsigned configured = _context->config.sound.coreRate;
    if (configured != 0)
        return configured;

    uint32_t devRate = _context->pAudioDeviceSampleRate.load(std::memory_order_relaxed);
    if (devRate == 0)
        devRate = _defaultDeviceSampleRate.load(std::memory_order_acquire);

    switch (devRate)
    {
        case 44100:
        case 48000:
        case 88200:
        case 96000:
        case 176400:
        case 192000:
            return devRate;
        default:
            return CORE_SAMPLING_RATE;
    }
}

SoundManager::SoundManager(EmulatorContext* context)
{
    _context = context;
    _logger = context->pModuleLogger;

    _coreRate = resolveCoreRate();
    if (_coreRate != CORE_SAMPLING_RATE)
    {
        LOGINFO("SoundManager: core audio rate %zu Hz", _coreRate);
    }

    _beeper = new Beeper(_context, CPU_CLOCK_RATE, _coreRate, _beeperBuffer);
    _turboSound = new SoundChip_TurboSound(_context);
    _turboSound->setCoreRate(_coreRate);

    // Build the device registry based on what this machine has
    // Beeper is always present
    _devices.push_back({AudioSourceType::Beeper, "Beeper", false, false, 1.0f, 0.0f, false});
    // AY 1 is always present (single AY or first chip of TurboSound)
    _devices.push_back({AudioSourceType::AY1_All, "AY 1", false, false, 1.0f, 0.0f, false});
    // AY 2 only if TurboSound (second chip)
    if (_turboSound && _turboSound->getChipCount() > 1)
    {
        _devices.push_back({AudioSourceType::AY2_All, "AY 2", false, false, 1.0f, 0.0f, false});
    }

    // Covox if config flag is set (Pentagon/Scorpion style)
    if (_context->config.sound.covoxFB)
    {
        _covox = new Covox(_context, _coreRate);
        _devices.push_back({AudioSourceType::COVOX, "COVOX", false, false, 1.0f, 0.0f, false});
    }

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

    // Initialize beeper character chain
    // - ChipType::AY (no LP) - beeper is also square waves, LP kills brightness
    // - Punch: Beeper preset (stronger - 1-bit audio needs attack definition)
    _beeperChain.setup(_coreRate);
    _beeperChain.setChipType(AudioCharacterChain::ChipType::AY);
    _beeperChain.setPunchPreset(AudioCharacterChain::PunchPreset::Beeper);
    _beeperChain.setPunchEnabled(false);
    _beeperChain.setRoomMode(AudioCharacterChain::RoomMode::Off);
}

SoundManager::~SoundManager()
{
    if (_covox)
    {
        delete _covox;
    }

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
    _turboSound->reset();
    _beeper->reset();
    if (_covox)
        _covox->reset();

    std::fill(_beeperBuffer, _beeperBuffer + AUDIO_BUFFER_SAMPLES_PER_FRAME, 0);
    std::fill(_outBuffer, _outBuffer + AUDIO_BUFFER_SAMPLES_PER_FRAME, 0);

    // Restart the exact sample accumulator (machine change / hard reset /
    // snapshot load all route through reset())
    _sampleAccumulator = 0;
    _lastFrequencyMultiplier = 0;  // Force synth re-clock on the next frame

    // New wave file
    // closeWaveFile();
    // std::string filePath = "unreal.wav";
    // openWaveFile(filePath);
}

void SoundManager::mute()
{
    _mute = true;
}

void SoundManager::unmute()
{
    _mute = false;
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
    _beeper->handleTapeAudio(static_cast<int32_t>(left), frameTState);
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
        case AudioSourceType::COVOX:
            return _covox ? _covox->getBuffer() : nullptr;
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
    switch (rate)
    {
        case 44100:
        case 48000:
        case 88200:
        case 96000:
        case 176400:
        case 192000:
            break;
        default:
            LOGWARNING("SoundManager::requestCoreRate: unsupported rate %u ignored", rate);
            return;
    }

    if (rate == _coreRate)
        return;

    _pendingCoreRate.store(rate, std::memory_order_release);
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

    // AY: sample PLL increment, decimation ratios, anti-alias FIR redesign
    _turboSound->setCoreRate(rate);

    // Character chains: re-derive envelope/room/punch coefficients for the
    // new rate (setup preserves chip type and presets; resets DSP state)
    _ayChain0.setup(rate);
    _ayChain1.setup(rate);
    _beeperChain.setup(rate);

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

    // Turbo / speed-multiplier change: re-point the synths' T-state->sample
    // mapping at the new CPU clock. Z80::t counts multiplied cycles, so the
    // beeper/covox blip input clocks and the AY PLL increment must scale by
    // the same multiplier - otherwise every synth produces multiplier-times
    // realtime samples and the ring overfills (hard-resync drops). Applied
    // at the frame boundary only, matching Z80::Z80FrameCycle
    const uint8_t frequencyMultiplier = _context->emulatorState.current_z80_frequency_multiplier;
    if (frequencyMultiplier != _lastFrequencyMultiplier)
    {
        _lastFrequencyMultiplier = frequencyMultiplier;
        const size_t synthClock = CPU_CLOCK_RATE * frequencyMultiplier;
        _beeper->setClockRate(synthClock);
        if (_covox)
            _covox->setClockRate(synthClock);
        _turboSound->setFrequencyMultiplier(frequencyMultiplier);
    }

    _turboSound->handleFrameStart();
    if (_covox)
        _covox->handleFrameStart();

    // Beeper starts its frame (blip_buf ready to receive deltas)
    _beeper->handleFrameStart();

    // Clear the beeper output buffer (will be filled by handleFrameEnd)
    memset(_beeperBuffer, 0x00, _beeperAudioDescriptor.memoryBufferSizeInBytes);
}

void SoundManager::handleStep()
{
    // Fast exit if sound generation disabled
    if (!_feature_sound_enabled)
        return;

    _turboSound->handleStep();
}

void SoundManager::handleFrameEnd()
{
    /// region <Determine actual samples for this frame>
    // Per-frame sample count derives from the machine's frame length, NOT the
    // 50 Hz SAMPLES_PER_FRAME constant: Pentagon (71680 t-states, 48.83 fps)
    // produces 903.168 samples/frame, ZX48/128 produces 880.5888.
    //
    // Exact integer accumulator (audio-sync design, Fix 1): the fractional
    // part is CARRIED, not rounded away. Rounding emitted a systematic rate
    // bias (-0.019% Pentagon / +0.047% ZX48) - the dominant source of both
    // realtime ring drift and audio-behind-video drift in recordings. With
    // the carry, the sequence is exactly periodic (903,903,...,904 with
    // period 125 on Pentagon@44.1k) and drift-free by construction.
    size_t samplesThisFrame = SAMPLES_PER_FRAME;
    uint32_t frameDuration = 0;     // T-states (for beeper)
    uint32_t frameDurationUs = 0;   // microseconds (for sample calculation)
    {
        CONFIG& config = _context->config;
        uint8_t speedMultiplier = _context->emulatorState.current_z80_frequency_multiplier;
        // T-states executed this frame: at turbo the CPU runs multiplier-
        // times the base frame length (Z80::frameLimit scales), so the
        // beeper's blip frame is scaled - its input clock is re-clocked to
        // base x multiplier (handleFrameStart), keeping blip output realtime
        frameDuration = config.frame * speedMultiplier;
        // Wall-clock frame duration: a video frame takes the SAME real time
        // at any CPU clock, so the realtime sample count (and ring fill
        // rate) is multiplier-invariant
        frameDurationUs = config.frame_duration_us;

        if (frameDurationUs > 0)
        {
            // Accumulate: (frame_us * sample_rate), then divide by 1,000,000 for samples
            _sampleAccumulator += static_cast<uint64_t>(frameDurationUs) * _coreRate;
            samplesThisFrame = static_cast<size_t>(_sampleAccumulator / 1'000'000ULL);
            _sampleAccumulator %= 1'000'000ULL;

            // Overflow guard: buffers are sized MAX_SAMPLES_PER_FRAME (speed
            // multiplier >= 3 exceeds it). Drop the excess KNOWINGLY - turbo
            // has no realtime constraint; a silent overrun would be worse.
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
    // AY chain: gentler punch (square waves already have harmonics)
    // Room uses no LP to preserve brightness
    // Process per-chip buffers with separate chain instances to preserve DSP state
    if (_turboSound)
    {
        int16_t* chip0Buf = _turboSound->getChipBuffer(0);
        int16_t* chip1Buf = _turboSound->getChipBuffer(1);
        if (chip0Buf)
            _ayChain0.processInt16(chip0Buf, samplesThisFrame);
        if (chip1Buf)
            _ayChain1.processInt16(chip1Buf, samplesThisFrame);
    }
    /// endregion </Process AY>

    /// region <Process beeper>
    // Finalize the beeper's blip_buf frame — produces band-limited output
    _beeper->handleFrameEnd(frameDuration);

    // Cross-check blip's internal fractional accumulator against ours. Both
    // are driven by the same clock ratio and stay in lockstep; >1 sample
    // divergence indicates an accumulator reset bug (logged, not asserted -
    // snapshot load / multiplier changes may legitimately differ for 1 frame)
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

    // Beeper chain: operates on alias-free blip_buf output
    _beeperChain.processInt16(_beeperBuffer, samplesThisFrame);
    /// endregion </Process beeper>

    /// region <Registry-driven mixing with mute/solo/volume + peak calculation>
    // Finalize Covox frame (DC removal etc.) before mixing
    if (_covox)
        _covox->handleFrameEnd(samplesThisFrame);

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

    // Mix each device according to audibility rules and compute peaks
    for (auto& d : _devices)
    {
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
            case AudioSourceType::COVOX:
                srcBuffer = _covox ? _covox->getBuffer() : nullptr;
                break;
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
        d.activeRecently = (peak > 0.001f);

        // Mix into output if audible
        if (audible && d.volume > 0.0f)
        {
            float vol = d.volume;
            for (size_t i = 0; i < samplesThisFrame * AUDIO_CHANNELS; i++)
            {
                int32_t mixed = _outBuffer[i] + static_cast<int32_t>(srcBuffer[i] * vol);
                // Saturating add
                _outBuffer[i] = static_cast<int16_t>(std::clamp(mixed, -32768, 32767));
            }
        }
    }
    /// endregion </Registry-driven mixing>

#ifdef ENABLE_RECORDING
    // Capture audio for recording BEFORE muting
    // This ensures recordings get the actual audio, not silence
    if (_context->pRecordingManager && _context->pRecordingManager->IsRecording())
    {
        _context->pRecordingManager->CaptureAudio(_outBuffer, samplesThisFrame * AUDIO_CHANNELS);
    }
#endif

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
    _drcErrIntegral = std::clamp(_drcErrIntegral + err, -50.0, 50.0);  // Anti-windup

    const double trim = std::clamp(-(DRC_KP * err + DRC_KI * _drcErrIntegral), -DRC_MAX_TRIM, DRC_MAX_TRIM);

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

        // Propagate HQ flag to TurboSound
        if (_turboSound)
        {
            _turboSound->setHQEnabled(_feature_soundhq_enabled);
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

/// region <Port interconnection>

bool SoundManager::attachToPorts()
{
    bool result = false;

    // result = _ay8910->attachToPorts(_context->pPortDecoder);
    result = _turboSound->attachToPorts(_context->pPortDecoder);

    // Attach SOUNDRIVE/Covox to port #FB (all 4 ports decode to same handler)
    if (_covox && _context->pPortDecoder)
    {
        result &= _context->pPortDecoder->RegisterPortHandler(Covox::PORT_RIGHT_B, _covox);
    }

    return result;
}

bool SoundManager::detachFromPorts()
{
    bool result = true;

    //_ay8910->detachFromPorts();
    _turboSound->detachFromPorts();

    // Detach SOUNDRIVE/Covox from port #FB
    if (_covox && _context->pPortDecoder)
    {
        _context->pPortDecoder->UnregisterPortHandler(Covox::PORT_RIGHT_B);
    }

    return result;
}

/// endregion </Port interconnection>