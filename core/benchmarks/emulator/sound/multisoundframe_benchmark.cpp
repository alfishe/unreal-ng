#include <benchmark/benchmark.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

#include "emulator/emulatorcontext.h"
#include "emulator/slots/cards/multisound/multisoundcard.h"
#include "emulator/sound/chips/gs/soundchip_gs.h"
#include "emulator/sound/chips/tsfm/ym2203pair.h"

/// ZX-MultiSound per-frame cost (docs/inprogress/2026-10-03-zx-multisound/TODO.md, "Profile the card's frame cost").
///
/// One Pentagon frame (71680 ticks of 3.5 MHz) of the card alone, no machine: FrameStart, the frame's port writes,
/// FrameEnd (every module run to the frame end, the seven rows rendered at 44.1 kHz). The argument is a mask of the
/// sources that play, the others stay in their reset state (the GS firmware always runs, it is the board's):
///   1  YM2203 pair: six FM channels (both chips, all four operators keyed) and six SSG tones
///   2  SAA1099: six voices at full amplitude
///   4  SounDrive: 200 writes per channel per frame (10 kHz sample playback on all four DACs)
///   8  SAM2695: 16 sustained notes, one per MIDI channel (drums on 10), effects on (the chip's default path)
///   16 General Sound: a looped ProTracker module (the GS benchmark's), GS firmware playing
/// 0 = idle (FM muted and the SAA clock stopped by the reset, the GS firmware in its command loop), 31 = everything.
/// The marginal cost of a source is its row minus the idle row.
namespace
{
constexpr uint64_t kFrameTicks = 71680;   // Pentagon
constexpr uint16_t kYmAddress = 0xFFFD;
constexpr uint16_t kYmData = 0xBFFD;
constexpr uint16_t kSaaAddress = 0x01FF;
constexpr uint16_t kSaaData = 0x00FF;
constexpr uint16_t kGsData = 0xB3;
constexpr uint16_t kGsCommand = 0xBB;
constexpr uint16_t kSoundrive[4] = {0x0F, 0x1F, 0x4F, 0x5F};

enum Source : int
{
    kYm = 1,
    kSaa = 2,
    kSd = 4,
    kMidi = 8,
    kGs = 16,
};

/// The GS benchmark's module: one pattern, a looped square sample on channel 1
std::vector<uint8_t> BuildModule()
{
    std::vector<uint8_t> m(1084 + 2 * 1024 + 64, 0x00);
    m[20 + 23] = 32;
    m[20 + 25] = 63;
    m[20 + 29] = 32;
    m[950] = 2;
    m[953] = 1;
    m[1080] = 'M';
    m[1081] = '.';
    m[1082] = 'K';
    m[1083] = '.';
    m[1084 + 0] = 0x01;
    m[1084 + 1] = 0xAC;
    m[1084 + 2] = 0x10;
    for (size_t i = 0; i < 64; i++)
        m[1084 + 2 * 1024 + i] = (i / 16) % 2 ? 0x30 : 0xB0;
    return m;
}

class Board
{
public:
    Board()
    {
        _ctx.config.frame = static_cast<uint32_t>(kFrameTicks);
        _ctx.config.frame_duration_us = 20480;
        _ctx.emulatorState.current_z80_frequency_multiplier = 1;
        _ctx.emulatorState.hw_turbo_ratio_applied = 1;
        _card = std::make_unique<MultiSoundCard>(&_ctx);
    }

    MultiSoundCard& Card() { return *_card; }

    /// One frame; `writes` runs the frame's port writes (times inside the frame)
    template <typename Writes>
    void Frame(Writes&& writes)
    {
        _card->FrameStart(_t, kFrameTicks);
        _tick = 0;
        writes(_t);
        _t += kFrameTicks;
        _tick = 0;
        _card->FrameEnd(_t);
    }
    void Frame()
    {
        Frame([](uint64_t) {});
    }

    /// Bus cycles 80 ticks apart from the frame start (monotonic; the last ones of a long burst share the frame's
    /// last tick)
    void Out(uint16_t port, uint8_t value) { _card->Out(port, value, NextTime()); }
    /// A bus cycle at `offset` ticks into the next frame (the MIDI bits need exact times)
    void OutAt(uint16_t port, uint8_t value, uint64_t offset) { _card->Out(port, value, _t + offset); }
    uint8_t In(uint16_t port)
    {
        bool drives = false;
        return _card->In(port, NextTime(), drives);
    }

    bool GsBoot()
    {
        for (int i = 0; i < 1000; i++)
        {
            Frame();
            if (_card->Gs().isReadyForCommands())
            {
                Frame();
                if (In(kGsCommand) & 0x80)
                    (void)In(kGsData);
                return true;
            }
        }
        return false;
    }

    bool GsWaitFlagClear(uint8_t mask)
    {
        for (int i = 0; i < 1000; i++)
        {
            if (!(In(kGsCommand) & mask))
                return true;
            Frame();
        }
        return false;
    }

    bool GsPlay()
    {
        Out(kGsData, 0x01);
        Out(kGsCommand, 0x30);
        if (!GsWaitFlagClear(0x01))
            return false;
        Frame();
        (void)In(kGsData);
        for (uint8_t b : BuildModule())
        {
            Out(kGsData, b);
            if (!GsWaitFlagClear(0x80))
                return false;
        }
        Out(kGsCommand, 0xD2);
        if (!GsWaitFlagClear(0x01))
            return false;
        Out(kGsData, 0x00);
        Out(kGsCommand, 0x31);
        return GsWaitFlagClear(0x01);
    }

    uint64_t Now() const { return _t; }

private:
    uint64_t NextTime()
    {
        const uint64_t at = _t + std::min<uint64_t>(_tick, kFrameTicks - 1);
        _tick += 80;
        return at;
    }

    EmulatorContext _ctx{LoggerLevel::LogError};
    std::unique_ptr<MultiSoundCard> _card;
    uint64_t _t = 0;
    uint64_t _tick = 0;
};

/// Both YM2203: control byte (chip, FM on, SAA clock on), three FM channels and three SSG tones each
void StartYm(Board& b)
{
    for (int chip = 0; chip < 2; chip++)
    {
        b.Out(kYmAddress, static_cast<uint8_t>(0xF0 | chip));   // chip select, FM on, SAA clock on
        auto reg = [&](uint8_t r, uint8_t v)
        {
            b.Out(kYmAddress, r);
            b.Out(kYmData, v);
        };
        for (uint8_t ch = 0; ch < 3; ch++)
        {
            for (uint8_t op = 0; op < 4; op++)
            {
                const uint8_t o = static_cast<uint8_t>(op * 4 + ch);
                reg(static_cast<uint8_t>(0x30 + o), 0x01);           // DT / MUL
                reg(static_cast<uint8_t>(0x40 + o), op == 3 ? 0x00 : 0x20);   // TL
                reg(static_cast<uint8_t>(0x50 + o), 0x1F);           // KS / AR
                reg(static_cast<uint8_t>(0x60 + o), 0x00);           // DR
                reg(static_cast<uint8_t>(0x80 + o), 0x0F);           // SL / RR
            }
            reg(static_cast<uint8_t>(0xB0 + ch), 0x04);               // FB 0, algorithm 4
            reg(static_cast<uint8_t>(0xA4 + ch), static_cast<uint8_t>(0x20 | ch));   // block 4
            reg(static_cast<uint8_t>(0xA0 + ch), static_cast<uint8_t>(0x40 + ch * 0x30));
            reg(0x28, static_cast<uint8_t>(0xF0 | ch));               // key on, all four operators
            reg(static_cast<uint8_t>(ch * 2), static_cast<uint8_t>(0x80 + ch * 0x20));   // SSG tone period
            reg(static_cast<uint8_t>(ch * 2 + 1), static_cast<uint8_t>(chip));
            reg(static_cast<uint8_t>(0x08 + ch), 0x0F);
        }
        reg(0x07, 0x38);                                               // tones A, B, C
    }
}

void StartSaa(Board& b)
{
    b.Out(kYmAddress, 0xF0);   // SAA clock on (bit 3 = 0)
    auto reg = [&](uint8_t r, uint8_t v)
    {
        b.Out(kSaaAddress, r);
        b.Out(kSaaData, v);
    };
    reg(0x1C, 0x02);           // reset frequency generators
    reg(0x1C, 0x01);           // sound enable
    for (uint8_t v = 0; v < 6; v++)
    {
        reg(v, 0xFF);                                         // amplitude L / R
        reg(static_cast<uint8_t>(0x08 + v), static_cast<uint8_t>(0x20 + v * 0x21));
    }
    reg(0x10, 0x33);
    reg(0x11, 0x44);
    reg(0x12, 0x55);
    reg(0x14, 0x3F);           // frequency enable, all six
}

/// 16 notes, one per MIDI channel (program change + note on), bit-banged on U4's IOA2 as a Z80 player does: 31 250
/// baud = 112 ticks a bit, LSB first, one start and one stop bit
void StartMidi(Board& b)
{
    std::vector<uint8_t> bytes;
    for (uint8_t ch = 0; ch < 16; ch++)
    {
        bytes.insert(bytes.end(), {static_cast<uint8_t>(0xC0 | ch), static_cast<uint8_t>(ch * 7 + 48)});
        bytes.insert(bytes.end(), {static_cast<uint8_t>(0x90 | ch), static_cast<uint8_t>(ch == 9 ? 42 : 48 + ch * 2), 0x64});
    }
    constexpr uint8_t kHigh = 0xFF;   // R14, IOA2 = 1: the line idle
    constexpr uint8_t kLow = 0xFB;
    // 40 bytes a frame (44 800 ticks of a 71 680-tick frame)
    for (size_t first = 0; first < bytes.size(); first += 40)
    {
        b.Frame([&](uint64_t)
        {
            uint64_t at = 100;
            auto out = [&](uint16_t port, uint8_t value) { b.OutAt(port, value, at++); };
            out(kYmAddress, 0xF2);   // U4, FM on, SAA clock on
            out(kYmAddress, 0x0E);
            out(kYmData, kHigh);
            out(kYmAddress, 0x07);
            out(kYmData, 0x78);      // tones A-C, IOA an output
            out(kYmAddress, 0x0E);
            at += 2 * 112;
            for (size_t i = first; i < std::min(bytes.size(), first + 40); i++)
            {
                b.OutAt(kYmData, kLow, at);
                at += 112;
                for (int bit = 0; bit < 8; bit++, at += 112)
                    b.OutAt(kYmData, (bytes[i] >> bit) & 1 ? kHigh : kLow, at);
                b.OutAt(kYmData, kHigh, at);
                at += 112;
            }
        });
    }
}

void RunMultiSoundFrames(benchmark::State& state)
{
    const int sources = static_cast<int>(state.range(0));
    Board b;
    if (!b.Card().Gs().isROMLoaded() || !b.GsBoot())
    {
        state.SkipWithError("GS firmware did not boot");
        return;
    }
    if ((sources & kMidi) && !b.Card().MidiBankLoaded())
    {
        state.SkipWithError("no MIDI bank (data/midi next to the executable)");
        return;
    }
    // The synthesizer ignores MIDI for 50 ms after the board reset: the GS boot is longer
    if ((sources & kGs) && !b.GsPlay())
    {
        state.SkipWithError("GS module did not start");
        return;
    }
    // Each setup in a frame of its own: the card's times only move forward
    if (sources & kYm)
        b.Frame([&](uint64_t) { StartYm(b); });
    if (sources & kSaa)
        b.Frame([&](uint64_t) { StartSaa(b); });
    if (sources & kMidi)
        StartMidi(b);

    uint8_t sample = 0;
    auto writes = [&](uint64_t)
    {
        if (!(sources & kSd))
            return;
        for (int i = 0; i < 200; i++)
        {
            for (int ch = 0; ch < 4; ch++)
                b.Out(kSoundrive[ch], static_cast<uint8_t>(sample + ch * 64));
            sample = static_cast<uint8_t>(sample + 7);
        }
    };

    // Warm-up: the voices' attack, the effects' tails, the decimators' history
    for (int i = 0; i < 50; i++)
        b.Frame(writes);

    for (auto _ : state)
    {
        b.Frame(writes);
        int16_t first = b.Card().Row(MultiSoundRow::Midi)[0];
        benchmark::DoNotOptimize(first);
    }

    MultiSoundCardReport report;
    b.Card().Describe(report);
    state.counters["voices"] = static_cast<double>(report.midi.activeVoices);
    state.counters["midiBytes"] = static_cast<double>(report.midi.bytesReceived);
    state.counters["framingErr"] = static_cast<double>(report.midi.framingErrors);
    state.SetLabel(sources == 0 ? "idle" : sources == 31 ? "all" : "");
}
} // namespace

static void BM_MultiSoundFrame(benchmark::State& state)
{
    RunMultiSoundFrames(state);
}
BENCHMARK(BM_MultiSoundFrame)
    ->Arg(0)
    ->Arg(kYm)
    ->Arg(kSaa)
    ->Arg(kSd)
    ->Arg(kMidi)
    ->Arg(kGs)
    ->Arg(kYm | kSaa | kSd | kMidi | kGs)
    ->Iterations(1000)
    ->Unit(benchmark::kMicrosecond);
