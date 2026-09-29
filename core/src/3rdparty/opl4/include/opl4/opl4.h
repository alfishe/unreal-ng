// libopl4 — public API (core TDD §10).
//
// Determinism contract (§9): chip state is integer-only and side-effect-free
// to save; the render layer is downstream of the chip boundary (D11) and is
// never serialised. run() and render() are separate on purpose (R8): a turbo
// host calls run() and never render().
#pragma once

#include <array>

#include "opl4/iwavememory.h"
#include "opl4/opl4config.h"

#include <cstddef>

namespace opl4
{

class Opl4Pcm;
class Opl4Fm;
class Opl4Render;

class Opl4
{
public:
    Opl4();
    ~Opl4();

    Opl4(const Opl4&) = delete;
    Opl4& operator=(const Opl4&) = delete;

    void Configure(const Opl4Config& cfg, IWaveMemory* mem);
    void Reset(uint64_t time);

    // Guest-visible. All advance the core to `time` first (§3.2).
    void WriteFm(uint64_t time, int bank, uint8_t addr, uint8_t data);
    void WriteWave(uint64_t time, uint8_t addr, uint8_t data);
    uint8_t ReadStatus(uint64_t time);
    uint8_t ReadWave(uint64_t time, uint8_t addr);

    // FM data-port read-back (guest-visible): the last-written value of the
    // addressed register, per bank, through the same aliasing the write
    // decode applies. YMF278B silicon answers data-port reads from its
    // register file (openMSX MSXMoonSound::readIO -> readReg); card drivers
    // read-modify-write C0 through it to preserve feedback/connection while
    // changing pan.
    uint8_t ReadFm(uint64_t time, int bank, uint8_t addr);

    // OPL4 NEW mode armed (bank-1 reg 0x05 bit 0). Read-only view for hosts
    // that gate shared-bus behaviour on it (ZX cards vs the Beta-128 FDC).
    bool NewMode() const;

    // NEW2 (bank-1 reg 0x05 bit 1): the chip-internal OPL4-wave gate. While
    // clear the YMF278B ignores wave register writes (openMSX-verified).
    bool New2Mode() const;

    // Core-only advance — call at least once per host frame even when muted.
    void Run(uint64_t time);

    // Render. Pulls from the chip stream produced since the last call.
    // Interleaved stereo floats normalized to full scale +-1.0 (the 44100
    // bypass path emits exact multiples of kNormScale = 1/(32768 << 2)).
    // Returns frames written.
    size_t Render(float* interleavedStereo, size_t maxFrames);

    // Split render (host mixer sources, integration D5). When enabled the
    // chip boundary emits FM-only and PCM-only streams alongside the mixed
    // stream (Authentic mode; HiFi carries them regardless). Render-side
    // only: chip state is untouched and the mixed Render() keeps working
    // (R7/D11). Toggling clears the delivery buffers.
    void EnableSplitStreams(bool on);
    bool SplitStreamsEnabled() const;

    // Render both group streams independently (interleaved stereo floats
    // normalized to full scale +-1.0, resampled to the configured output
    // rate, each through its own character chain / board analog /
    // DC-blocker state). Returns frames
    // written; the count can differ per group across resampler boundaries,
    // so the return is the minimum and each buffer may carry a little more.
    size_t RenderSplit(float* fmOut, float* pcmOut, size_t maxFrames);

    // Drop pending delivery-buffer audio without rendering (turbo hosts
    // that run() the core at multiple realtime but never render). Chip
    // state is unaffected.
    void DiscardPendingAudio();

    // Taps (R7). Mute affects the tap sum path only, never chip state.
    void SetChannelMute(ChannelId id, bool mute);
    float ChannelPeak(ChannelId id) const;
    size_t ChannelCount(ChannelGroup g) const;

    // Render configuration — may change at any time, never affects the core.
    void SetRenderMode(RenderMode m);
    // Live output-rate change (host audio device renegotiation, 44100..192000):
    // only the render layer depends on the output rate. Chip state and the
    // chip-rate streams (44.1 kHz chip / 49516.4 Hz FM grids) are kept, so no
    // pending audio is lost; the resamplers and filters are re-designed and
    // restart (Tier C). Call at a frame boundary.
    void SetOutputRate(uint32_t rate);
    uint32_t OutputRate() const;
    void SetQuality(Quality q);
    void SetBoardAnalog(bool on);
    void SetPunch(ChannelGroup g, PunchPreset p);
    void SetRoom(RoomMode m);

    // POD state pair (TTD integration D5/F5). stateSize() is constant after
    // Configure(); save is side-effect free; restore is exact (§9.2).
    size_t StateSize() const;
    void SaveState(uint8_t* dst) const;
    void LoadState(const uint8_t* src);

    void ResetRenderState(); // drop filter/tap/chain history (Tier C)
    const uint8_t* RamDirtyBitmap(size_t* bytes) const;
    void ClearRamDirty();

    // Read-only snapshots for debuggers and automation. They report the chip
    // as of the last guest access or Run() - they do not advance the core
    // (§3.2) - and have no side effects (unlike ReadFm/ReadWave/ReadStatus).
    struct FmTimerView
    {
        uint16_t count = 0;
        uint16_t load = 0;
        bool enabled = false;
        bool masked = false;
    };
    enum class FmEnvelopePhase : uint8_t
    {
        Off,
        Release,
        Decay,
        Sustain,
        Attack
    };
    /// One operator slot (22 per bank, addressed like the silicon: slot =
    /// bank * 22 + register offset; offsets 6/7/14/15 hold no operator)
    struct FmOperatorView
    {
        uint16_t fnum = 0;
        uint8_t block = 0;
        uint8_t mult = 0;                  // MULT register value 0..15
        uint8_t tl = 0;                    // total level, 6-bit (0.75 dB/step)
        uint8_t ar = 0, dr = 0, sl = 0, rr = 0;
        uint8_t ws = 0;                    // waveform 0..7
        bool ksr = false, am = false, vib = false, egt = false;
        uint8_t kslRegister = 0;           // KSL as written (register 0x40 bits 7:6)
        bool keyOn = false;
        int16_t attenuation = 0;           // envelope attenuation index, 0 .. kFmMaxAttenuation
        FmEnvelopePhase phase = FmEnvelopePhase::Off;
    };
    struct FmChannelView
    {
        uint8_t op1 = 0, op2 = 0;          // operator slots
        bool fourOp = false;               // part of a 4-op pair
        uint8_t connection = 0;            // C0 bit 0
    };
    static constexpr int kFmMaxAttenuation = 0x3FF;
    struct FmView
    {
        std::array<uint8_t, 512> regs{};   // bank 0 at 0x000, bank 1 at 0x100
        uint8_t status = 0;
        bool newMode = false;              // NEW  (bank-1 reg 0x05 bit 0)
        bool new2 = false;                 // NEW2 (bank-1 reg 0x05 bit 1)
        bool rhythm = false;               // bank-0 reg 0xBD bit 5
        FmTimerView timers[2];
        std::array<uint8_t, 18> route{};   // per channel C0 output bits (0x30 outside NEW)
        std::array<FmChannelView, 18> channels{};
        std::array<FmOperatorView, 44> operators{};
    };
    enum class PcmEnvelopePhase : uint8_t
    {
        Off,
        Release,
        Sustain,
        Decay,
        Attack
    };
    struct PcmSlotView
    {
        uint16_t wave = 0;
        int8_t octave = 0;
        uint16_t fnum = 0;
        uint8_t totalLevel = 0;            // current TL, 8-bit internal
        uint8_t pan = 0;
        bool keyOn = false;
        bool damp = false;
        uint8_t bits = 0;                  // 0 = 8-bit, 1 = 12-bit, 2 = 16-bit samples
        uint32_t start = 0;                // 22-bit wave memory address
        uint16_t loop = 0;
        uint16_t endComplement = 0;        // 0x10000 - true end, as the tone header stores it
        uint16_t position = 0;
        int16_t attenuation = 0;           // envelope attenuation index, 0 .. kPcmMaxAttenuation
        PcmEnvelopePhase phase = PcmEnvelopePhase::Off;
        uint8_t ar = 0, d1r = 0, d2r = 0, rr = 0, rc = 0;
        int16_t decayLevel = 0;            // D1 level as an attenuation index
        uint8_t lfo = 0, vib = 0, am = 0;
        bool lfoActive = false;
        uint16_t panLeft = 0, panRight = 0; // pan attenuation, 32 per 3 dB, 1020 = off
    };
    /// PCM LFO speeds 0..7 in Hz (the engine's own table)
    static constexpr double kPcmLfoHz[8] = {0.168, 2.019, 3.196, 4.206, 5.215, 5.888, 6.224, 7.066};
    static constexpr int kPcmMaxAttenuation = 0x280;
    struct PcmView
    {
        std::array<uint8_t, 256> regs{};
        uint32_t memAddress = 0;           // wave memory address register (regs 3..5)
        std::array<PcmSlotView, 24> slots{};
    };
    void PeekFm(FmView& out) const;
    void PeekPcm(PcmView& out) const;
    uint8_t MixFmLatch() const;   // 0xF8: FM block mix (left bits 0-2, right bits 3-5)
    uint8_t MixPcmLatch() const;  // 0xF9: PCM block mix

    // Internal engine access for tests (CUT-style, no production use).
    Opl4Pcm& PcmForTest() { return *_pcm; }
    Opl4Fm& FmForTest() { return *_fm; }
    Opl4Render& RenderForTest() { return *_render; }
    // Grid counters for the reducer-cadence test (§4.3): FM boundaries and
    // output boundaries processed so far.
    uint64_t DebugFmTicks() const;
    uint64_t DebugOutSteps() const;

private:
    void SyncTo(uint64_t time); // lazy advance of both grids (§3.2)
    void AdvanceOutputStep();   // one 44100 Hz chip output sample
    void AdvanceFmToOutput();   // reducer consumption (§4.3)

    struct Impl;
    Impl* _impl;
    Opl4Pcm* _pcm;
    Opl4Fm* _fm;
    Opl4Render* _render;
};

} // namespace opl4
