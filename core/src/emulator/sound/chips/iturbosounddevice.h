#pragma once

#include <stdafx.h>

#include <memory>

#include "debugger/ttd/ttdserializable.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/sound/audio.h"
#include "emulator/sound/chips/soundchip_ay8910.h"
#include "emulator/sound/native_audio_tap.h"

/// @file iturbosounddevice.h
/// @brief Interface of whatever occupies the TurboSound slot (design §3.3).
///
/// Implemented by the legacy two-AY TurboSound device and, later, by
/// SoundChip_TurboSoundFM. SoundManager holds the slot through this
/// interface only; TTD registers the device under its own
/// TTDPeripheralId() (TurboSound = 0, TSFM = 4), so a session recorded on
/// one device refuses to load on the other.
///
/// Suppression contract (design §6.1): handleFrameStart() and handleStep()
/// are called every frame / every CPU step in every mode (turbo and
/// sound-feature-off included) — a device with an emulated core (TSFM)
/// advances it there even when its output stage is off. The device gates
/// its own rendering on the flag set by setSynthesisSuppressed().
class ITurboSoundDevice : public PortDecoder, public PortDevice, public ttd::TTDSerializable
{
    /// region <Constructors / destructor>
public:
    ITurboSoundDevice(EmulatorContext* context) : PortDecoder(context)
    {
    }

    // PortDevice has no virtual destructor of its own; this one makes
    // deletion through the interface pointer safe for every device
    virtual ~ITurboSoundDevice() = default;
    /// endregion </Constructors / destructor>

    /// region <Interface methods>
public:
    /// region <Lifecycle (emulation thread)>
    /// Runs every frame, even in turbo mode (TSFM rebases the core clock
    /// here, design §5.2); clears the frame buffers
    virtual void handleFrameStart() = 0;
    /// The mixer's frame-start sample phase (T-states x rate, modulo CPU_CLOCK_RATE), pushed by
    /// SoundManager::handleFrameStart right before handleFrameStart: the device renders the frame from it. The
    /// mixer's phase is the authority for how many samples every frame has; at 1x the device's own copy is equal by
    /// construction, after a frame that moved only one of the two (a host speed multiplier renders the multiplied
    /// time, the sound feature off renders nothing while the mixer still counts) taking it back is what keeps the
    /// device's count the mixer's - otherwise a never-rendered or a dropped sample at some frame boundaries for good
    virtual void followSamplePhase(uint64_t phase) = 0;
    /// Advances the core; renders unless suppressed (design §6.1)
    virtual void handleStep() = 0;
    /// Output stage drain; skipped in turbo. Called from
    /// SoundManager::handleFrameEnd - mind the frame-end axis trap
    /// (design §6.1): drain to the device's own end-of-frame position,
    /// never to AudioTstate(z80->t), which already reads into the new frame
    virtual void handleFrameEnd() = 0;
    /// A core whose per-step sync is deferred (TSFM with its synthesis
    /// skipped) catches up with the CPU: the machine stops being driven (a
    /// park, the end of a direct run), so observers read it in step
    virtual void catchUp() {}

    /// Output stage on/off; core unaffected. Pushed once per frame by the
    /// manager (sound feature off counts as suppressed)
    virtual void setSynthesisSuppressed(bool suppressed)
    {
        (void)suppressed;
    }

    /// Sample-generation clocking of the emulated core may be skipped. Pushed once per frame by the
    /// manager: true only while the output stage is suppressed AND time-travel debugging neither records
    /// nor replays. What the CPU can observe (registers, timers, busy, status, IRQ) stays exact; only the
    /// internal synthesis state (e.g. FM operator phases / envelopes), which feeds nothing but the sound
    /// output, is frozen. Under TTD it is part of the hashed core state, so it is never skipped there.
    /// Devices without such state ignore it
    virtual void setCoreSynthesisSkipped(bool skipped)
    {
        (void)skipped;
    }
    /// endregion </Lifecycle>

    /// region <Rate / quality>
    /// Anti-alias FIR design tier ([SOUND] DecimatorQuality). Stored only: it
    /// takes effect at the next setCoreRate(), the one point where the
    /// devices redesign their filters (and restart the sample accumulator in
    /// step with the mixer) - so set it before the first setCoreRate()
    virtual void setDecimatorQuality(FilterDecimator::Quality quality) = 0;
    virtual void setCoreRate(size_t rate) = 0;
    virtual size_t getCoreRate() const = 0;
    virtual void setHQEnabled(bool enabled) = 0;
    /// endregion </Rate / quality>

    /// region <Chip clock>
    /// The AY / SSG input clock in Hz, set by the machine at run time (the
    /// Profi's video divider gives 1.5 MHz in hi-res, design-hires.md H2b).
    /// Takes effect at the current CPU T-state on the device's render
    /// timeline: register writes queued before it keep their timing. Returns
    /// false when the device keeps its fixed clock: the TSFM's YM2203 core and
    /// its FM decimators are built around the 1.75 MHz socket clock, so TSFM
    /// stays at PSG_CLOCK_RATE (a follow-up, the Profi has no TSFM fitted)
    virtual bool SetPsgClock(uint32_t hz)
    {
        return hz == PSG_CLOCK_RATE;
    }
    /// The clock the generators run at now (a requested switch still queued
    /// on the render timeline is not reported until it is reached)
    virtual uint32_t GetPsgClock() const
    {
        return static_cast<uint32_t>(PSG_CLOCK_RATE);
    }
    /// endregion </Chip clock>

    /// region <Frame buffers (interleaved int16 stereo)>
    /// Number of stereo sample pairs rendered into the frame buffers so far
    /// this frame
    virtual size_t getRenderedSamplesThisFrame() const = 0;
    /// The render loop's sample phase (T-states x rate, modulo CPU_CLOCK_RATE; diagnostics and tests). At the end of
    /// a 1x frame it is SoundManager::samplePhase()
    virtual uint64_t getSamplePhase() const = 0;
    /// Interleaved stereo frame buffer of AY chip `chip` (0/1)
    virtual int16_t* getChipBuffer(int chip) = 0;
    /// Interleaved stereo FM-only frame buffer of chip `chip` (TSFM);
    /// nullptr on devices without FM
    virtual int16_t* getFmBuffer(int /*chip*/)
    {
        return nullptr;
    }
    /// True when the device has FM channels (TSFM)
    virtual bool hasFm() const
    {
        return false;
    }
    /// FM loudness trim in dB relative to the hardware-derived default
    /// (TSFM, design §7.1); no-op without FM
    virtual void setFmTrimDb(double /*db*/)
    {
    }
    virtual double fmTrimDb() const
    {
        return 0.0;
    }
    /// endregion </Frame buffers>

    /// region <Chips, taps, log>
    /// AY (SSG) half of chip `chip` for monitoring; nullptr if absent
    virtual SoundChip_AY8910* getChip(int chip) const = 0;
    virtual int getChipCount() const = 0;
    /// The chip the ports #FFFD / #BFFD talk to now (TurboSound: #FF / #FE written to #FFFD; TSFM: the CPLD's
    /// chip bit)
    virtual int getSelectedChip() const = 0;
    /// The SSG register a data read (IN #FFFD) returns now, 0..15; -1 when it returns something else (the TSFM
    /// status byte, #FF with an FM address latched). The controllers' port-read watch asks it while it runs
    virtual int getBusReadRegister() const = 0;
    /// Native-rate (pre-decimation) recording tap of the AY/SSG mix
    virtual std::shared_ptr<NativeAudioTap> getNativeTap() const = 0;
    /// Native-rate FM-only tap of chip `chip` (TSFM); nullptr without FM
    virtual std::shared_ptr<NativeAudioTap> getFmNativeTap(int /*chip*/) const
    {
        return nullptr;
    }
    /// Install/remove the AY port-write log tap; inert while sink == nullptr
    virtual void setLogSink(AYLogSink sink, void* context) = 0;
    /// endregion </Chips, taps, log>

    /// region <Ports>
    virtual bool attachToPorts(PortDecoder* decoder) = 0;
    virtual void detachFromPorts() = 0;
    /// endregion </Ports>

    /// Identity: TTDSerializable::TTDPeripheralId() — TurboSound (0) or
    /// TSFM (4); TTD registration and session-kind guards key on it
    /// endregion </Interface methods>

    /// Render-layer restarts (bumped by every TTD restore, not TTD state): the host's post-processing of the chip
    /// buffers (SoundManager's character chains) restarts with it, so the audio after a restore does not depend on
    /// what played before it
    uint64_t renderEpoch() const
    {
        return _renderEpoch;
    }

    /// The step gate's test (handleStep) without the call, as SoundManager::handleStep makes it: a step that ends at
    /// audio T `now` before the deadline, at 1x host speed, only notes where it ended (settleSteps books it). true:
    /// noted, the device's handleStep is not needed for this step
    bool noteGatedStep(uint64_t now, uint8_t hostSpeed)
    {
        if (now >= _stepDeadline || hostSpeed != 1)
            return false;
        _pendingT = now;
        _stepPending = true;
        return true;
    }

protected:
    uint64_t _renderEpoch = 0;
    // The step gate (handleStep): the audio T before which a step only notes its T (_pendingT) and settleSteps books it
    // once - a step that completes no output sample, or any step while the device sleeps (the quiet deferral, max).
    // 0: every step the full way. Set only by the full way; every other entry clears it first
    uint64_t _stepDeadline = 0;
    uint64_t _pendingT = 0;
    bool _stepPending = false;
};
