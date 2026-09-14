// libopl4 — public API (core TDD §10).
//
// Determinism contract (§9): chip state is integer-only and side-effect-free
// to save; the render layer is downstream of the chip boundary (D11) and is
// never serialised. run() and render() are separate on purpose (R8): a turbo
// host calls run() and never render().
#pragma once

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

    // Core-only advance — call at least once per host frame even when muted.
    void Run(uint64_t time);

    // Render. Pulls from the chip stream produced since the last call.
    // Interleaved stereo floats in [-1, 1]. Returns frames written.
    size_t Render(float* interleavedStereo, size_t maxFrames);

    // Taps (R7). Mute affects the tap sum path only, never chip state.
    void SetChannelMute(ChannelId id, bool mute);
    float ChannelPeak(ChannelId id) const;
    size_t ChannelCount(ChannelGroup g) const;

    // Render configuration — may change at any time, never affects the core.
    void SetRenderMode(RenderMode m);
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
