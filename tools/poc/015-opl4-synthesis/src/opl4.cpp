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

#include "common/exptable.h"
#include "common/mixtables.h"
#include "fm/fmbus.h"
#include "fm/fmsynthopl4.h"
#if defined(OPL4_FM_YMFM)
#include "fm/fmsynthymfm.h"
#endif
#include "pcm/pcmsynthopl4.h"
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
// Backend-tagged layout: the FM chunk differs between the in-tree model and
// the ymfm verification backend, so sessions never cross builds. Version 4:
// the guest-visible FM state (register shadow, timers, status, routing
// source) moved into the FmBus chunk ahead of a synthesis-only engine chunk
// (rearchitecture Step 2); [88..91] pins the engine layout tag. In-tree
// version 5: FM operators carry the latched key bit (FmOperator::keyReq),
// so older snapshots would key sounding notes off on load.
#if defined(OPL4_FM_YMFM)
constexpr uint32_t kStateVersion = 3;
#else
constexpr uint32_t kStateVersion = 5;
#endif
constexpr uint64_t kStreamReserveFrames = 4410; // ~100 ms of chip audio

// Rail headroom: signals stay at full 16-bit scale (unity ≈ 32767), but the
// Authentic rail is shifted up by kRailShift bits to give multiple voices
// headroom before clipping. The host normalizes by 1/(32768 << kRailShift).
constexpr int kRailShift = 2;
constexpr int32_t kAuthenticRail = 32767 << kRailShift; // ±131068

int32_t ClampRail(int32_t v)
{
    return std::max(-kAuthenticRail - 1, std::min(kAuthenticRail, v));
}

} // namespace

struct Opl4::Impl
{
    Opl4Config cfg;

    // Guest-visible FM bus state (register shadow, timers, status, routing,
    // FM mutes) — rearchitecture §4.1. Wired to the engine in the ctor.
    FmBus fmBus;

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
    // int32 preserves full precision until the render/host conversion point.
    // chipStream clips at ±32767 (authentic 16-bit DAC rail); split streams
    // pass full range to the host mixer which handles headroom.
    std::vector<int32_t> chipStream; // Authentic: mixed 44100 stereo
    std::vector<int32_t> fmStream;   // HiFi: per-tick 49516.4 stereo / split FM
    std::vector<int32_t> pcmStream;  // HiFi: PCM-only 44100 stereo / split PCM
    bool splitStreams = false;       // host mixer sources (integration D5)

    // Tap scratch + render-side meters (never serialised; FM mutes live in
    // fmBus, PCM mutes in pcmMuteMask)
    std::array<int32_t, FmBus::kChannelCount> fmTaps{};
    std::array<int32_t, Opl4Pcm::kSlotCount> pcmTaps{};
    uint32_t pcmMuteMask = 0; // bit i: PCM slot i
    int32_t fmPeak[FmBus::kChannelCount] = {};
    int32_t pcmPeak[Opl4Pcm::kSlotCount] = {};

    IWaveMemory* mem = nullptr;

    bool AnyPcmMute() const { return pcmMuteMask != 0; }

    void ClearStreams()
    {
        chipStream.clear();
        fmStream.clear();
        pcmStream.clear();
    }
};

Opl4::Opl4()
    : _impl(new Impl), _pcm(new Opl4Pcm), _fm(new FmBackend), _render(new Opl4Render)
{
    _impl->fmBus.SetSynth(_fm);
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
    _impl->fmBus.Reset();
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
    // One 684-clock FM step: engine synthesis + bus timers/routing/mute. The
    // bus owns the include-semantics routing (§7) and applies FM mutes on
    // the tap-sum path exactly (summing only unmuted channels).
    im.fmBus.Advance(im.fmTaps, fmL, fmR);

    for (int ch = 0; ch < FmBus::kChannelCount; ch++)
    {
        const int32_t tap = im.fmTaps[ch];
        const int32_t a = tap < 0 ? -tap : tap;
        int32_t& p = im.fmPeak[ch];
        p -= p >> 8; // meter decay, render-side only
        if (a > p)
            p = a;
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
        // No clamp — full precision to the host mixer.
        im.fmStream.push_back(fmL);
        im.fmStream.push_back(fmR);
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

        if (im.AnyPcmMute() && ((im.pcmMuteMask >> i) & 1u))
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
        // No clamp — full precision to the host mixer.
        im.pcmStream.push_back(pcmL);
        im.pcmStream.push_back(pcmR);
    }
    else
    {
        // §7 step 3: saturated add at the widened rail (kRailShift bits of
        // headroom above the original 16-bit DAC rail). Signals stay at full
        // 16-bit scale; the host normalizes by 1/(32768 << kRailShift).
        im.chipStream.push_back(ClampRail(fmL + pcmL));
        im.chipStream.push_back(ClampRail(fmR + pcmR));
        if (im.splitStreams)
        {
            // Host mixer sources (integration D5): full precision to the
            // host mixer which handles headroom in its wide bus.
            im.fmStream.push_back(fmL);
            im.fmStream.push_back(fmR);
            im.pcmStream.push_back(pcmL);
            im.pcmStream.push_back(pcmR);
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
    _impl->fmBus.Write(static_cast<uint8_t>(bank & 1), addr, data);
    _impl->busyUntil = std::max(_impl->busyUntil, _impl->masterPos + kBusyFmWriteClocks);
}

uint8_t Opl4::ReadFm(uint64_t time, int bank, uint8_t addr)
{
    SyncTo(time); // time coherence only - a register read opens no BUSY window
    return _impl->fmBus.Read(static_cast<uint8_t>(bank & 1), addr);
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
    // LD opens only on tone-header fetch writes — bank-0 regs 0x08..0x1F,
    // exactly the wave-number registers (openMSX YMF278B::writeIO gates
    // LOAD_DELAY on the same range; plain register and memory writes extend
    // BUSY only).
    if (toneLoad)
        im.ldUntil = std::max(im.ldUntil, im.masterPos + Opl4Pcm::ToneLoadClocks());
}

uint8_t Opl4::ReadStatus(uint64_t time)
{
    SyncTo(time);
    Impl& im = *_impl;
    uint8_t s = _impl->fmBus.Status() & (FmBus::kStatusT1 | FmBus::kStatusT2);
    // YMF262 status | chip flags (openMSX YMF278B::readYMF278Status,
    // real-HW-verified bit positions): bit 0 BUSY, bit 1 LD. The YMF262
    // status only ever uses bits 6..5 (timer flags), so no collision.
    const bool busy = im.masterPos < im.busyUntil || im.masterPos < im.ldUntil;
    if (busy)
        s |= 0x01; // BUSY
    if (im.masterPos < im.ldUntil)
        s |= 0x02; // LD (wave-table load window)
    return s;
}

uint8_t Opl4::ReadWave(uint64_t time, uint8_t addr)
{
    SyncTo(time);
    Impl& im = *_impl;
    const uint8_t v = _pcm->ReadReg(addr);
    if (addr >= 0x03 && addr <= 0x06)
    {
        // Direct memory read: 38 master clocks on the bus for the memory-path
        // registers (openMSX applies MEM_READ_DELAY for latch 3..6; the
        // auto-increment inside ReadReg stays MA-gated).
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
    return _impl->fmBus.NewMode();
}

bool Opl4::New2Mode() const
{
    return _impl->fmBus.New2();
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
    if (id.group == ChannelGroup::Fm)
        _impl->fmBus.SetChannelMute(static_cast<int>(id.index), mute);
    else if (id.index < Opl4Pcm::kSlotCount)
    {
        if (mute)
            _impl->pcmMuteMask |= (1u << id.index);
        else
            _impl->pcmMuteMask &= ~(1u << id.index);
    }
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
        if (id.index >= FmBus::kChannelCount)
            return 0.0f;
        return static_cast<float>(_impl->fmPeak[id.index]) / 32768.0f;
    }
    if (id.index >= Opl4Pcm::kSlotCount)
        return 0.0f;
    return static_cast<float>(_impl->pcmPeak[id.index]) / 32768.0f;
}

size_t Opl4::ChannelCount(ChannelGroup g) const
{
    return (g == ChannelGroup::Fm) ? static_cast<size_t>(FmBus::kChannelCount)
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
// POD state pair (§9): top chunk | FM bus chunk | FM engine chunk | PCM
// chunk. Streams, meters and mutes are deliberately not saved.
// ---------------------------------------------------------------------------

size_t Opl4::StateSize() const
{
    return kTopStateSize + FmBus::kStateSize + _fm->StateSize() + Opl4Pcm::kStateSize;
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
    const uint32_t fmTag = _fm->LayoutTag();
    std::memcpy(dst + 88, &fmTag, 4);
    im.fmBus.SaveState(dst + kTopStateSize);
    const size_t fmEngineOffset = kTopStateSize + FmBus::kStateSize;
    _fm->SaveState(dst + fmEngineOffset);
    _pcm->SaveState(dst + fmEngineOffset + _fm->StateSize());
}

void Opl4::LoadState(const uint8_t* src)
{
    Impl& im = *_impl;
    uint32_t ver = 0;
    std::memcpy(&ver, src + 4, 4);
    if (src[0] != 'O' || src[1] != 'P' || src[2] != 'L' || src[3] != '4' || ver != kStateVersion)
        return; // refuse foreign blobs (TTD D5 pairing contract)
    uint32_t fmTag = 0;
    std::memcpy(&fmTag, src + 88, 4);
    if (fmTag != _fm->LayoutTag())
        return; // refuse mismatched engine layouts
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
    im.fmBus.LoadState(src + kTopStateSize);
    const size_t fmEngineOffset = kTopStateSize + FmBus::kStateSize;
    _fm->LoadState(src + fmEngineOffset);
    _pcm->LoadState(src + fmEngineOffset + _fm->StateSize());
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
