#pragma once

#include <stdafx.h>

#include <memory>

#include "debugger/ttd/ttdserializable.h"
#include "emulator/ports/portdecoder.h"
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
    /// Advances the core; renders unless suppressed (design §6.1)
    virtual void handleStep() = 0;
    /// Output stage drain; skipped in turbo. Called from
    /// SoundManager::handleFrameEnd - mind the frame-end axis trap
    /// (design §6.1): drain to the device's own end-of-frame position,
    /// never to AudioTstate(z80->t), which already reads into the new frame
    virtual void handleFrameEnd() = 0;

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
    virtual void setCoreRate(size_t rate) = 0;
    virtual size_t getCoreRate() const = 0;
    virtual void setHQEnabled(bool enabled) = 0;
    /// endregion </Rate / quality>

    /// region <Frame buffers (interleaved int16 stereo)>
    /// Number of stereo sample pairs rendered into the frame buffers so far
    /// this frame
    virtual size_t getRenderedSamplesThisFrame() const = 0;
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
};
