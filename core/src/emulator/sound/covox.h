#pragma once

#include <cstdint>
#include <cstddef>
#include "emulator/sound/audio.h"
#include "emulator/ports/portdecoder.h"
#include "common/modulelogger.h"
#include "debugger/ttd/ttdserializable.h"  // TTDSerializable (P1.5 peripheral serializer)

class EmulatorContext;
struct blip_t;

/// SOUNDRIVE 1.05 / COVOX - 4-channel 8-bit DAC with band-limited synthesis.
///
/// Ports: #F1 (Left A), #F3 (Left B), #F9 (Right A), #FB (Right B)
/// Decoding: bits[7:4]=1111, bit2=0, bit0=1
///
/// Each DAC write computes a stereo delta and inserts it into blip_buf
/// at the exact T-state position. At frame end, blip_buf produces
/// alias-free 44.1 kHz output via windowed-sinc interpolation.
///
/// Stereo mixing: Left = (LeftA + LeftB) * 128, Right = (RightA + RightB) * 128
/// The ×128 scaling (instead of ×256) provides 0.5× headroom per channel-pair,
/// preventing hard clipping when both channels on one side are at full amplitude.
///
/// Mono COVOX compatibility: When only #FB (RightB) is written (other channels
/// at midpoint), the output is automatically centered — RightB goes to both
/// speakers. This matches the behavior of simple single-DAC Covox hardware.
///
/// Hardware references (mode 2 port map):
/// - VELESOFT DAC-for-ZX database, SoundDrive 1.05:
///   https://velesoft.speccy.cz/da_for_zx-cz.htm
/// - BC Info Guide #4, ZX Spectrum Ports Guide (Black_Cat, 2008) — decode
///   pattern 1111B0A1; transcribed in docs/ports/zx-ports-full-table.md
///
/// Self-decoding device (PortDevice::tryClaimOut/In, PortDecoder::
/// RegisterSelfDecodingDevice): Fitment (Mono vs Quad) is baked in at
/// construction from config, so SoundManager just "plugs the card in" -
/// no per-port dispatch-map wiring, no per-model mask/match duplication.
/// This mirrors how multi-machine Speccy-clone emulators structure the
/// same peripheral - e.g. Xpeccy's `sdrvCreate(type)` + one shared
/// `zx_dev_wr()` dispatch tried by every machine after its own FDC-
/// precedence gate (`/Volumes/TB4-4Tb/Projects/emulators/github/Xpeccy/
/// src/libxpeccy/hardware/common.c`) - adopted here after
/// `PortDecoder_Scorpion256` turned out to have none of
/// `PortDecoder_Pentagon128`'s per-model SoundDrive fix (see
/// docs/inprogress/2026-09-23-sounddrive-quad-wiring/DONE.md, session 3).
class Covox : public PortDevice, public ttd::TTDSerializable
{
public:
    // Port addresses for 4 channels - SoundDrive 1.05 "mode 2" mirror set
    static constexpr uint16_t PORT_LEFT_A  = 0x00F1;  // Left channel A
    static constexpr uint16_t PORT_LEFT_B  = 0x00F3;  // Left channel B
    static constexpr uint16_t PORT_RIGHT_A = 0x00F9;  // Right channel A
    static constexpr uint16_t PORT_RIGHT_B = 0x00FB;  // Right channel B

    // Port decoding mask/match (catches all 4 mode-2 ports)
    static constexpr uint8_t PORT_MASK  = 0xF5;  // Check bits 7-4, 2, 0
    static constexpr uint8_t PORT_MATCH = 0xF1;  // 1111x0x1

    // SoundDrive 1.05 "mode 1" primary port set (#0F/#1F/#4F/#5F). These
    // addresses alias into the Beta128 FDC's wide mirror decode (bits
    // 0,1=1, bit7=0) on Pentagon/Scorpion, so a machine can only have one
    // peripheral answering there at a time. Covox is a *self-decoding*
    // device (PortDevice::tryClaimOut/In, registered via
    // PortDecoder::RegisterSelfDecodingDevice) rather than being wired into
    // any model's exact-address port-device map, so it never competes for a
    // dispatch-map slot with WD1793 or any other exact-match peripheral;
    // each model's DecodePortOut/In only offers a raw port to the
    // self-decoding chain once its own higher-priority decode (Beta128
    // precedence while TR-DOS is paged in, memory latches, AY, ...) has
    // already declined it - matching the reference implementations
    // (pentevo/Unreal io.cpp: `conf.sound.sd && (port & 0xAF) == 0x0F`;
    // Xpeccy soundrive.c SDRV_105_1/SDRV_105_2, dispatched from one shared
    // `zx_dev_wr()` tried by every machine after its own `flgBDI` gate).
    static constexpr uint16_t PORT_LEFT_A_MODE1  = 0x000F;
    static constexpr uint16_t PORT_LEFT_B_MODE1  = 0x001F;
    static constexpr uint16_t PORT_RIGHT_A_MODE1 = 0x004F;
    static constexpr uint16_t PORT_RIGHT_B_MODE1 = 0x005F;

    static constexpr uint8_t PORT_MASK_MODE1  = 0xAF;
    static constexpr uint8_t PORT_MATCH_MODE1 = 0x0F;

    /// Fitment baked in at construction from config (mirrors Xpeccy's
    /// sdrvCreate(type) - the card itself knows what it is, not the
    /// machine): Quad answers both mode-1 and mode-2 addresses (SD=1),
    /// Mono answers only the exact #FB Covox port (CovoxFB=1, SD=0)
    enum class Fitment { Mono, Quad };

    enum class Channel { LeftA = 0, LeftB = 1, RightA = 2, RightB = 3, Count = 4 };

    // Stale-channel decay: mode-1 and mode-2 addresses are two different bus
    // schemes for the SAME 4 physical DAC latches, so software that switches
    // between them (e.g. a demo's device-selection menu) commonly abandons
    // the previously-used channel without ever rewriting it back to silence.
    // Left frozen forever, that stale non-midpoint latch permanently defeats
    // the single-channel mono-centering in computeStereoAmplitudes() and
    // reintroduces the exact wrong-side-panning/click behavior the centering
    // was meant to fix (see balldreams2.sna SD1.05<->Covox switching). Digi/
    // PCM engines refresh an active channel far more than once per video
    // frame, so a channel that goes STALE_CHANNEL_FRAMES whole frames
    // without a single write is safe to treat as abandoned and silently
    // decay back to the 0x80 midpoint (see handleFrameStart()).
    static constexpr int STALE_CHANNEL_FRAMES = 3;

protected:
    EmulatorContext* _context;

    // Fitment baked in at construction (see Fitment doc, public section)
    Fitment _fitment;

    // Audio buffer (one frame of stereo int16)
    AudioFrameDescriptor _audioDescriptor;
    int16_t* const _buffer = reinterpret_cast<int16_t*>(_audioDescriptor.memoryBuffer);

    // blip_buf accumulators (one per stereo output channel)
    blip_t* _blipL = nullptr;
    blip_t* _blipR = nullptr;
    bool _synthesisSuppressed = false;

    // Per-channel DAC state
    uint8_t _dacValue[4] = {0x80, 0x80, 0x80, 0x80};  // Start at midpoint (silence)

    // Last stereo amplitudes written to blip_buf (for computing deltas)
    int32_t _lastL = 0;
    int32_t _lastR = 0;

    // Per-channel mute (for UI)
    bool _channelMute[4] = {false, false, false, false};

    // Stale-channel decay state (see STALE_CHANNEL_FRAMES doc, public section)
    bool _writtenThisFrame[4] = {false, false, false, false};
    int _staleFrameCount[4] = {0, 0, 0, 0};

    /// Synthetically silence one abandoned channel through the normal
    /// delta/blip path (called from handleFrameStart, T-state 0 - before any
    /// real writes this frame - so the decay never clips real playback)
    void decayStaleChannel(Channel ch);

    // DC offset removal (optional, applied post-blip)
    bool _dcRemovalEnabled = false;
    float _dcAccumL = 0.0f;
    float _dcAccumR = 0.0f;

    // Activity tracking for HUD notification
    bool _frameHadActivity = false;
    bool _wasActive = false;
    static constexpr float DC_COEF = 0.995f;  // ~7 Hz cutoff @ 44.1 kHz
    float _dcCoefEff = DC_COEF;               // DC_COEF^(44100/fs): same cutoff Hz at every core rate

    // Core output rate (multirate plan phase 6)
    size_t _sampleRate;

    // Input clock (T-states fed to blip): base CPU clock x frequency
    // multiplier - the T-state position is descaled by EmulatorState::AudioTstate
    size_t _clockRate = CPU_CLOCK_RATE;

public:
    Covox() = delete;
    explicit Covox(EmulatorContext* context, size_t sampleRate = 44100);
    virtual ~Covox();

    // Buffer access for registry
    int16_t* getBuffer() { return _buffer; }
    const int16_t* getBuffer() const { return _buffer; }

    /// Live core-rate change (device reroute with CoreRate=auto)
    void setSampleRate(size_t sampleRate);

    /// CPU clock change (turbo / speed multiplier): Z80::t counts multiplied
    /// cycles, so the blip input clock must track base x multiplier to keep
    /// the output sample count realtime. Frame boundary only
    void setClockRate(size_t clockRate);

    // Frame lifecycle
    void reset();
    void handleFrameStart();

    /// Turbo mode: keep DAC register / level tracking, skip blip deltas (see Beeper)
    void setSynthesisSuppressed(bool suppressed);
    bool isSynthesisSuppressed() const { return _synthesisSuppressed; }
    /// @param expectedSamples Exact per-frame sample count from SoundManager's
    ///        accumulator (0 = compute locally via rounding, legacy behavior).
    ///        Passing it keeps the covox stream in lockstep with the mixer.
    void handleFrameEnd(size_t expectedSamples = 0);

    // DC removal control (for UI section)
    void setDCRemovalEnabled(bool enabled) { _dcRemovalEnabled = enabled; }
    bool isDCRemovalEnabled() const { return _dcRemovalEnabled; }

    // Per-channel mute control
    void setChannelMute(Channel ch, bool mute) { _channelMute[static_cast<int>(ch)] = mute; }
    bool isChannelMuted(Channel ch) const { return _channelMute[static_cast<int>(ch)]; }

    /// The four DAC latches (LeftA, LeftB, RightA, RightB order of Channel)
    void getDacLatches(uint8_t (&out)[4]) const
    {
        for (int i = 0; i < 4; i++)
            out[i] = _dacValue[i];
    }

    /// Last stereo amplitude computed from the DAC latches (updated
    /// synchronously in portDeviceOutMethod(), independent of blip_buf/frame
    /// timing) - lets tests assert on the mono-vs-stereo mixing decision
    /// without depending on a fully wired frame/clock pipeline.
    int32_t lastLeftAmplitude() const { return _lastL; }
    int32_t lastRightAmplitude() const { return _lastR; }

    // PortDevice interface
    uint8_t portDeviceInMethod(uint16_t port) override;
    void portDeviceOutMethod(uint16_t port, uint8_t value) override;

    /// Self-decoding: recognizes mode-1/mode-2 SoundDrive addresses (Quad
    /// fitment) or the exact #FB port (Mono fitment) and, if the raw port
    /// matches, forwards to portDeviceOutMethod()/portDeviceInMethod().
    /// See PortDevice::tryClaimOut/In and the Fitment doc above.
    bool tryClaimOut(uint16_t rawPort, uint8_t value) override;
    bool tryClaimIn(uint16_t rawPort, uint8_t& outValue) override;

    // Determine which channel a port address maps to
    static Channel portToChannel(uint16_t port);

private:
    /// Compute stereo amplitudes from current DAC values.
    /// Each side sums two channels with 0.5× scaling to prevent clipping.
    void computeStereoAmplitudes(int32_t& outL, int32_t& outR) const;


public:
    /// region <TTDSerializable interface (P1.5 - parent TDD 6.4)>
    ///
    /// The Covox/Soundrive is a 4-channel 8-bit DAC. The only machine state is
    /// the four DAC latches (_dacValue[4]) - everything else is host-side
    /// audio pipeline (rebuilt by handleFrameStart) or user config (mute,
    /// DC-removal toggle).
    ///
    /// Layout: 4 bytes - _dacValue[0..3] (LeftA, LeftB, RightA, RightB).
    size_t TTDStateSize() const override;
    void   TTDSaveState(uint8_t* dst) const override;
    void   TTDLoadState(const uint8_t* src) override;

    /// Identity used by TTDPeripheralRegistry. Without it the base class
    /// returns PeripheralId::Count and the device cannot be indexed in a
    /// checkpoint's blob map.
    ttd::PeripheralId TTDPeripheralId() const override { return ttd::PeripheralId::Covox; }
    std::string TTDDeviceName() const override { return "Covox"; }
    /// endregion </TTDSerializable interface>
};
