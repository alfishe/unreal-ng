// libopl4 — top-level chip (core TDD §3, §7, §9, §10).
//
// Drives both grids from one 64-bit master-clock position derived from the
// host tick axis with a persistent remainder (§3.1 — never a float). FM
// boundaries (every 684 clocks) and output boundaries (every 768 clocks) are
// processed in master-clock order; that interleaving *is* the HoldDrop
// reducer (§4.3): 1 or 2 FM ticks fall inside each 768-clock window — the
// same cadence as the silicon phase accumulator stepping 21/171 — and the DAC
// latches the last tick (HoldDrop) or the window mean (LinearBlend).
//
// Mix order is fixed and observable (§7): reducer FM + PCM slot sum, block
// mix 0xF8 (FM L/R = bits 5:3 / 2:0) and 0xF9 (PCM), then one 16-bit
// saturated add per side (D10). Mute/peaks act on the tap-sum path only and
// are never serialised (R7, D11).
#include "opl4/opl4.h"

#include "opl4fm.h"
#include "opl4pcm.h"
#include "opl4render.h"
#include "opl4/wavememory.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

namespace opl4
{

namespace
{

constexpr size_t kTopStateSize = 92;
constexpr uint32_t kStateVersion = 1;
constexpr uint64_t kStreamReserveFrames = 4410; // ~100 ms of chip audio

int16_t Clamp16(int32_t v)
{
    return static_cast<int16_t>(std::max(-32768, std::min(32767, v)));
}

} // namespace

struct Opl4::Impl
{
    Opl4Config cfg;

    // Time model (§3.1)
    uint64_t hostTicks = 0;     // last host timestamp synced
    uint64_t hostRemainder = 0; // fractional master clocks carried over
    uint64_t masterPos = 0;
    uint64_t fmTicks = 0;   // FM boundaries processed (= masterPos / 684)
    uint64_t outSteps = 0;  // output boundaries processed (= masterPos / 768)

    // Reducer window state (§4.3) — the kernel-dependent remainder §9.1
    uint32_t windowTicks = 0;
    int32_t windowSumL = 0, windowSumR = 0;
    int32_t heldL = 0, heldR = 0; // last FM tick output (HoldDrop latch)

    // Block mix latches (§7, D9): 0xF8 / 0xF9 full bytes
    uint8_t mixF8 = 0x1B; // FM: L=3 R=3 (-9 dB both) after reset
    uint8_t mixF9 = 0x00; // PCM: unity

    // Bus deadlines (§3.3) in master clocks
    uint64_t busyUntil = 0;
    uint64_t ldUntil = 0;

    // Chip-boundary stream buffers (delivery, not state — §9.1)
    std::vector<int16_t> chipStream; // Authentic: mixed 44100 stereo
    std::vector<int16_t> fmStream;   // HiFi: per-tick 49516.4 stereo / split FM
    std::vector<int16_t> pcmStream;  // HiFi: PCM-only 44100 stereo / split PCM
    bool splitStreams = false;       // host mixer sources (integration D5)

    // Tap scratch + render-side meters/mutes (never serialised)
    std::array<int32_t, Opl4Fm::kChannelCount> fmTaps{};
    std::array<int32_t, Opl4Pcm::kSlotCount> pcmTaps{};
    uint64_t muteMask = 0; // bit i: 0..17 FM, 18..41 PCM
    int32_t fmPeak[Opl4Fm::kChannelCount] = {};
    int32_t pcmPeak[Opl4Pcm::kSlotCount] = {};

    IWaveMemory* mem = nullptr;

    bool AnyMute() const { return muteMask != 0; }

    void ClearStreams()
    {
        chipStream.clear();
        fmStream.clear();
        pcmStream.clear();
    }
};

Opl4::Opl4()
    : _impl(new Impl), _pcm(new Opl4Pcm), _fm(new Opl4Fm), _render(new Opl4Render)
{
    Reset(0);
}

Opl4::~Opl4()
{
    delete _impl;
    delete _pcm;
    delete _fm;
    delete _render;
}

uint64_t Opl4::DebugFmTicks() const
{
    return _impl->fmTicks;
}

uint64_t Opl4::DebugOutSteps() const
{
    return _impl->outSteps;
}

void Opl4::Configure(const Opl4Config& cfg, IWaveMemory* mem)
{
    _impl->cfg = cfg;
    if (_impl->cfg.hostTickRate == 0)
        _impl->cfg.hostTickRate = 1;
    _impl->mem = mem;
    _pcm->SetMemory(mem);
    _render->Configure(cfg);
    _impl->ClearStreams();
    _impl->chipStream.reserve(kStreamReserveFrames * 2);
    _impl->fmStream.reserve(kStreamReserveFrames * 2);
    _impl->pcmStream.reserve(kStreamReserveFrames * 2);
}

void Opl4::Reset(uint64_t time)
{
    _fm->Reset();
    _pcm->Reset();
    Impl& im = *_impl;
    im.hostTicks = time;
    im.hostRemainder = 0;
    im.masterPos = 0;
    im.fmTicks = 0;
    im.outSteps = 0;
    im.windowTicks = 0;
    im.windowSumL = im.windowSumR = 0;
    im.heldL = im.heldR = 0;
    im.mixF8 = 0x1B;
    im.mixF9 = 0x00;
    im.busyUntil = 0;
    im.ldUntil = 0;
    im.fmTaps.fill(0);
    im.pcmTaps.fill(0);
    std::fill(std::begin(im.fmPeak), std::end(im.fmPeak), 0);
    std::fill(std::begin(im.pcmPeak), std::end(im.pcmPeak), 0);
    im.ClearStreams();
    _render->ResetRenderState();
}

// ---------------------------------------------------------------------------
// Grid advance
// ---------------------------------------------------------------------------

void Opl4::AdvanceFmToOutput()
{
    Impl& im = *_impl;
    int32_t fmL = 0, fmR = 0;
    _fm->Advance(fmL, fmR, im.fmTaps);

    const auto& channels = _fm->Channels();
    for (int ch = 0; ch < Opl4Fm::kChannelCount; ch++)
    {
        const int32_t tap = im.fmTaps[ch];
        const int32_t a = tap < 0 ? -tap : tap;
        int32_t& p = im.fmPeak[ch];
        p -= p >> 8; // meter decay, render-side only
        if (a > p)
            p = a;

        if (im.AnyMute() && ((im.muteMask >> ch) & 1))
        {
            // Mirror the engine's routing exactly (§7): subtract the muted
            // channel's contribution from the tap-sum path, never chip state.
            const uint8_t route = channels[ch].cha;
            if (!(route & 0x10))
                fmL -= tap;
            if (!(route & 0x20))
                fmR -= tap;
        }
    }

    // Block mix 0xF8: L = bits 5:3, R = bits 2:0 (§7 step 2).
    fmL = ApplyMixScale(fmL, (im.mixF8 >> 3) & 7);
    fmR = ApplyMixScale(fmR, im.mixF8 & 7);

    im.windowTicks++;
    im.windowSumL += fmL;
    im.windowSumR += fmR;
    im.heldL = fmL;
    im.heldR = fmR;
    im.fmTicks++;

    if (im.cfg.mode == RenderMode::HiFi)
    {
        // Reducer bypassed: every FM tick leaves the chip boundary (§4.3).
        im.fmStream.push_back(Clamp16(fmL));
        im.fmStream.push_back(Clamp16(fmR));
    }
}

void Opl4::AdvanceOutputStep()
{
    Impl& im = *_impl;

    // Reducer kernel picks the window value presented to the DAC.
    int32_t fmL = im.heldL, fmR = im.heldR;
    if (im.cfg.reducer == ReducerKernel::LinearBlend && im.windowTicks != 0)
    {
        fmL = im.windowSumL / static_cast<int32_t>(im.windowTicks);
        fmR = im.windowSumR / static_cast<int32_t>(im.windowTicks);
    }
    im.windowTicks = 0;
    im.windowSumL = 0;
    im.windowSumR = 0;

    int32_t pcmL = 0, pcmR = 0;
    _pcm->Advance(pcmL, pcmR, im.pcmTaps);

    const auto& slots = _pcm->Slots();
    for (int i = 0; i < Opl4Pcm::kSlotCount; i++)
    {
        const int32_t tap = im.pcmTaps[i];
        const int32_t a = tap < 0 ? -tap : tap;
        int32_t& p = im.pcmPeak[i];
        p -= p >> 8;
        if (a > p)
            p = a;

        if (im.AnyMute() && ((im.muteMask >> (18 + i)) & 1))
        {
            // Taps are pre-pan (post envelope, post TL): re-pan the muted
            // slot's contribution to subtract it exactly (§5).
            const PanPair pp = kPanTable[slots[i].pan & 0x0F];
            pcmL -= VolFactor(tap, pp.left);
            pcmR -= VolFactor(tap, pp.right);
        }
    }

    pcmL = ApplyMixScale(pcmL, (im.mixF9 >> 3) & 7);
    pcmR = ApplyMixScale(pcmR, im.mixF9 & 7);
    im.outSteps++;

    if (im.cfg.mode == RenderMode::HiFi)
    {
        im.pcmStream.push_back(Clamp16(pcmL));
        im.pcmStream.push_back(Clamp16(pcmR));
    }
    else
    {
        // §7 step 3: one 16-bit saturated add per side (D10).
        im.chipStream.push_back(Clamp16(fmL + pcmL));
        im.chipStream.push_back(Clamp16(fmR + pcmR));
        if (im.splitStreams)
        {
            // Host mixer sources (integration D5): the two pre-sum group
            // streams, each saturated to 16 bits like the DAC would see it.
            im.fmStream.push_back(Clamp16(fmL));
            im.fmStream.push_back(Clamp16(fmR));
            im.pcmStream.push_back(Clamp16(pcmL));
            im.pcmStream.push_back(Clamp16(pcmR));
        }
    }
}

void Opl4::SyncTo(uint64_t time)
{
    Impl& im = *_impl;
    if (time <= im.hostTicks)
        return; // contract: monotonically increasing timestamps

    const uint64_t delta = time - im.hostTicks;
    const uint64_t num = delta * kMasterClockHz + im.hostRemainder;
    im.hostRemainder = num % im.cfg.hostTickRate;
    im.masterPos += num / im.cfg.hostTickRate;
    im.hostTicks = time;

    for (;;)
    {
        const uint64_t nextFm = (im.fmTicks + 1) * kFmDivider;
        const uint64_t nextOut = (im.outSteps + 1) * kOutDivider;
        // FM first on a tie: the tick lands on the boundary clock, then the
        // DAC latches — matches silicon sample/hold ordering.
        if (nextFm <= im.masterPos && nextFm <= nextOut)
            AdvanceFmToOutput();
        else if (nextOut <= im.masterPos)
            AdvanceOutputStep();
        else
            break;
    }
}

// ---------------------------------------------------------------------------
// Guest-visible bus (§3.2, §3.3)
// ---------------------------------------------------------------------------

void Opl4::WriteFm(uint64_t time, int bank, uint8_t addr, uint8_t data)
{
    SyncTo(time);
    _fm->WriteReg(static_cast<uint8_t>(bank & 1), addr, data);
    _impl->busyUntil = std::max(_impl->busyUntil, _impl->masterPos + kBusyFmWriteClocks);
}

void Opl4::WriteWave(uint64_t time, uint8_t addr, uint8_t data)
{
    SyncTo(time);
    Impl& im = *_impl;
    const bool toneLoad = _pcm->WriteReg(addr, data);
    if (addr == 0xF8)
        im.mixF8 = data;
    else if (addr == 0xF9)
        im.mixF9 = data;

    const bool memPath = (addr >= 0x03 && addr <= 0x06);
    const uint64_t busy = memPath ? kBusyMemWriteClocks : kBusyWaveRegWriteClocks;
    im.busyUntil = std::max(im.busyUntil, im.masterPos + busy);
    if (addr >= 0x02 && addr <= 0x06)
        im.ldUntil = std::max(im.ldUntil, im.masterPos + kLdMinClocks);
    if (toneLoad)
        im.ldUntil = std::max(im.ldUntil, im.masterPos + Opl4Pcm::ToneLoadClocks());
}

uint8_t Opl4::ReadStatus(uint64_t time)
{
    SyncTo(time);
    Impl& im = *_impl;
    uint8_t s = _fm->Status() & (Opl4Fm::kStatusT1 | Opl4Fm::kStatusT2);
    const bool busy = im.masterPos < im.busyUntil || im.masterPos < im.ldUntil;
    if (busy)
        s |= 0x01; // BUSY
    if (im.masterPos < im.ldUntil)
        s |= 0x80; // LD (memory load window)
    return s;
}

uint8_t Opl4::ReadWave(uint64_t time, uint8_t addr)
{
    SyncTo(time);
    Impl& im = *_impl;
    const uint8_t v = _pcm->ReadReg(addr);
    if (addr == 0x06)
    {
        // Direct memory read: 38 master clocks on the bus regardless of MA
        // (§3.3); the auto-increment inside ReadReg stays MA-gated.
        im.busyUntil = std::max(im.busyUntil, im.masterPos + kBusyMemReadClocks);
    }
    return v;
}

void Opl4::Run(uint64_t time)
{
    SyncTo(time);
}

bool Opl4::NewMode() const
{
    return _fm->NewMode();
}

// ---------------------------------------------------------------------------
// Render pull + taps
// ---------------------------------------------------------------------------

size_t Opl4::Render(float* interleavedStereo, size_t maxFrames)
{
    Impl& im = *_impl;
    if (im.cfg.mode == RenderMode::Authentic)
    {
        const size_t frames = im.chipStream.size() / 2;
        if (frames == 0)
            return 0;
        size_t consumed = 0;
        const size_t written = _render->ProcessChip(im.chipStream.data(), frames,
                                                    interleavedStereo, maxFrames,
                                                    &consumed);
        im.chipStream.erase(im.chipStream.begin(),
                            im.chipStream.begin() + static_cast<std::ptrdiff_t>(consumed * 2));
        return written;
    }

    const size_t fmFrames = im.fmStream.size() / 2;
    const size_t pcmFrames = im.pcmStream.size() / 2;
    if (fmFrames == 0 && pcmFrames == 0)
        return 0;
    size_t consumedFm = 0, consumedPcm = 0;
    // Offer the full captured counts: ProcessSplit stages resampler output
    // persistently, so the two grids need not advance in lockstep. Passing
    // min(fm, pcm) for both truncated the faster grid and dropped the excess
    // (an ~11% per-frame FM loss at output 44100).
    const size_t written = _render->ProcessSplit(im.fmStream.data(), fmFrames,
                                                 im.pcmStream.data(), pcmFrames,
                                                 interleavedStereo, maxFrames,
                                                 &consumedFm, &consumedPcm);
    im.fmStream.erase(im.fmStream.begin(),
                      im.fmStream.begin() + static_cast<std::ptrdiff_t>(consumedFm * 2));
    im.pcmStream.erase(im.pcmStream.begin(),
                       im.pcmStream.begin() + static_cast<std::ptrdiff_t>(consumedPcm * 2));
    return written;
}

void Opl4::SetChannelMute(ChannelId id, bool mute)
{
    const unsigned idx = (id.group == ChannelGroup::Fm) ? id.index
                                                        : static_cast<unsigned>(18 + id.index);
    if (idx >= 42)
        return;
    if (mute)
        _impl->muteMask |= (1ull << idx);
    else
        _impl->muteMask &= ~(1ull << idx);
}

void Opl4::EnableSplitStreams(bool on)
{
    if (_impl->splitStreams == on)
        return;
    _impl->splitStreams = on;
    _impl->ClearStreams(); // the emitted stream set changes; chip state untouched
}

bool Opl4::SplitStreamsEnabled() const
{
    return _impl->splitStreams;
}

size_t Opl4::RenderSplit(float* fmOut, float* pcmOut, size_t maxFrames)
{
    Impl& im = *_impl;
    const size_t fmFrames = im.fmStream.size() / 2;
    const size_t pcmFrames = im.pcmStream.size() / 2;
    if (fmFrames == 0 && pcmFrames == 0)
        return 0;

    size_t consumedFm = 0, consumedPcm = 0;
    const size_t wFm = _render->ProcessGroup(ChannelGroup::Fm, im.fmStream.data(),
                                              fmFrames, fmOut, maxFrames, &consumedFm);
    const size_t wPcm = _render->ProcessGroup(ChannelGroup::Pcm, im.pcmStream.data(),
                                               pcmFrames, pcmOut, maxFrames, &consumedPcm);
    if (consumedFm > 0)
        im.fmStream.erase(im.fmStream.begin(),
                          im.fmStream.begin() + static_cast<std::ptrdiff_t>(consumedFm * 2));
    if (consumedPcm > 0)
        im.pcmStream.erase(im.pcmStream.begin(),
                           im.pcmStream.begin() + static_cast<std::ptrdiff_t>(consumedPcm * 2));
    return std::min(wFm, wPcm);
}

void Opl4::DiscardPendingAudio()
{
    _impl->ClearStreams(); // delivery buffers only; render filter state stays
}

float Opl4::ChannelPeak(ChannelId id) const
{
    if (id.group == ChannelGroup::Fm)
    {
        if (id.index >= Opl4Fm::kChannelCount)
            return 0.0f;
        return static_cast<float>(_impl->fmPeak[id.index]) / 32768.0f;
    }
    if (id.index >= Opl4Pcm::kSlotCount)
        return 0.0f;
    return static_cast<float>(_impl->pcmPeak[id.index]) / 32768.0f;
}

size_t Opl4::ChannelCount(ChannelGroup g) const
{
    return (g == ChannelGroup::Fm) ? static_cast<size_t>(Opl4Fm::kChannelCount)
                                   : static_cast<size_t>(Opl4Pcm::kSlotCount);
}

void Opl4::SetRenderMode(RenderMode m)
{
    _impl->cfg.mode = m;
    _impl->ClearStreams(); // stream format changes; chip state untouched
    _render->Configure(_impl->cfg);
}

void Opl4::SetQuality(Quality q)
{
    _impl->cfg.quality = q;
    _render->Configure(_impl->cfg);
}

void Opl4::SetBoardAnalog(bool on)
{
    _render->SetBoardAnalog(on);
}

void Opl4::SetPunch(ChannelGroup g, PunchPreset p)
{
    _render->SetPunchPreset(g, p);
}

void Opl4::SetRoom(RoomMode m)
{
    _render->SetRoom(m);
}

// ---------------------------------------------------------------------------
// POD state pair (§9): top chunk | FM chunk | PCM chunk.
// Streams, meters and mutes are deliberately not saved.
// ---------------------------------------------------------------------------

size_t Opl4::StateSize() const
{
    return kTopStateSize + Opl4Fm::kStateSize + Opl4Pcm::kStateSize;
}

void Opl4::SaveState(uint8_t* dst) const
{
    const Impl& im = *_impl;
    std::memset(dst, 0, kTopStateSize);
    dst[0] = 'O'; dst[1] = 'P'; dst[2] = 'L'; dst[3] = '4';
    uint32_t ver = kStateVersion;
    std::memcpy(dst + 4, &ver, 4);
    std::memcpy(dst + 8, &im.masterPos, 8);
    std::memcpy(dst + 16, &im.hostTicks, 8);
    std::memcpy(dst + 24, &im.hostRemainder, 8);
    std::memcpy(dst + 32, &im.busyUntil, 8);
    std::memcpy(dst + 40, &im.ldUntil, 8);
    std::memcpy(dst + 48, &im.fmTicks, 8);
    std::memcpy(dst + 56, &im.outSteps, 8);
    std::memcpy(dst + 64, &im.windowTicks, 4);
    std::memcpy(dst + 68, &im.windowSumL, 4);
    std::memcpy(dst + 72, &im.windowSumR, 4);
    std::memcpy(dst + 76, &im.heldL, 4);
    std::memcpy(dst + 80, &im.heldR, 4);
    dst[84] = im.mixF8;
    dst[85] = im.mixF9;
    _fm->SaveState(dst + kTopStateSize);
    _pcm->SaveState(dst + kTopStateSize + Opl4Fm::kStateSize);
}

void Opl4::LoadState(const uint8_t* src)
{
    Impl& im = *_impl;
    uint32_t ver = 0;
    std::memcpy(&ver, src + 4, 4);
    if (src[0] != 'O' || src[1] != 'P' || src[2] != 'L' || src[3] != '4' || ver != kStateVersion)
        return; // refuse foreign blobs (TTD D5 pairing contract)
    std::memcpy(&im.masterPos, src + 8, 8);
    std::memcpy(&im.hostTicks, src + 16, 8);
    std::memcpy(&im.hostRemainder, src + 24, 8);
    std::memcpy(&im.busyUntil, src + 32, 8);
    std::memcpy(&im.ldUntil, src + 40, 8);
    std::memcpy(&im.fmTicks, src + 48, 8);
    std::memcpy(&im.outSteps, src + 56, 8);
    std::memcpy(&im.windowTicks, src + 64, 4);
    std::memcpy(&im.windowSumL, src + 68, 4);
    std::memcpy(&im.windowSumR, src + 72, 4);
    std::memcpy(&im.heldL, src + 76, 4);
    std::memcpy(&im.heldR, src + 80, 4);
    im.mixF8 = src[84];
    im.mixF9 = src[85];
    _fm->LoadState(src + kTopStateSize);
    _pcm->LoadState(src + kTopStateSize + Opl4Fm::kStateSize);
    im.ClearStreams(); // delivery buffers resume from the restore point
}

void Opl4::ResetRenderState()
{
    _render->ResetRenderState();
}

const uint8_t* Opl4::RamDirtyBitmap(size_t* bytes) const
{
    const WaveMemory* wm = dynamic_cast<const WaveMemory*>(_impl->mem);
    if (wm == nullptr)
    {
        if (bytes)
            *bytes = 0;
        return nullptr;
    }
    if (bytes)
        *bytes = WaveMemory::kMaxDirtyPages;
    return wm->DirtyBitmap();
}

void Opl4::ClearRamDirty()
{
    WaveMemory* wm = dynamic_cast<WaveMemory*>(_impl->mem);
    if (wm != nullptr)
        wm->ClearDirty();
}

} // namespace opl4
