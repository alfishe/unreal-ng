// libopl4 — YMF278B (OPL4) emulation core + render layer.
// Public configuration types (core TDD §3.1, §8.2).
#pragma once

#include <stdint.h>

namespace opl4
{

// Silicon clock constants (core TDD D1). Fixed properties of the YMF278B:
// the library converts to the host-requested output rate in the render layer.
constexpr uint64_t kMasterClockHz = 33868800; // 33.8688 MHz
constexpr uint64_t kFmDivider = 684;          // FM grid  ≈ 49516.4 Hz
constexpr uint64_t kOutDivider = 768;         // output grid = 44100 Hz exactly

// Bus timing, in master clocks (core TDD §3.3, resolved M3; values
// real-HW-verified via openMSX YMF278B). The LD window length lives in
// Opl4Pcm::ToneLoadClocks (9600 ≈ 283.5 µs; openMSX uses 10000 and notes
// the true value is 2–4% below that).
constexpr uint64_t kBusyFmWriteClocks = 56;
constexpr uint64_t kBusyWaveRegWriteClocks = 88;
constexpr uint64_t kBusyMemWriteClocks = 28;
constexpr uint64_t kBusyMemReadClocks = 38;

// Native chip output grid.
constexpr uint32_t kChipOutputRate = 44100;

enum class RenderMode
{
    Authentic, // chip rate reducer on (HoldDrop), default
    HiFi       // reducer bypassed, FM resampled from the 49516.4 Hz grid
};

enum class Quality
{
    Reference,    // 96 taps, Kaiser beta = 5  (~56 dB stopband)
    HighFidelity  // 192 taps, Kaiser beta = 9 (~90 dB stopband)
};

enum class ReducerKernel
{
    HoldDrop,    // authentic silicon: ZOH + phase-accumulator drop (D2)
    LinearBlend  // optional software smoothing across the 64/57 phase
};

enum class ChannelGroup
{
    Fm,  // 18 channels
    Pcm  // 24 slots
};

// FM/PCM character-chain presets (core TDD §8.4).
enum class PunchPreset
{
    Off,
    Opl4Fm, // edgeBlend 0.02, transBoost 0.06 — off by default
    Opl4Pcm,// edgeBlend 0.06, transBoost 0.15
    Custom
};

enum class RoomMode
{
    Off,
    Soft
};

struct Opl4Config
{
    uint64_t hostTickRate = 3500000; // e.g. Z80 T-state axis
    uint32_t outputRate = 44100;     // 44100 … 192000
    uint32_t ramSizeBytes = 1u << 20; // ZXM-MoonSound: 1 MiB SRAM
    uint32_t romSizeBytes = 2u << 20; // ZXM-MoonSound: 2 MiB ROM
    RenderMode mode = RenderMode::Authentic;
    Quality quality = Quality::Reference;
    ReducerKernel reducer = ReducerKernel::HoldDrop;
};

// ChannelId indexes a per-channel tap (R7): 18 FM + 24 PCM.
struct ChannelId
{
    ChannelGroup group = ChannelGroup::Fm;
    uint8_t index = 0; // < 18 for Fm, < 24 for Pcm
};

} // namespace opl4
