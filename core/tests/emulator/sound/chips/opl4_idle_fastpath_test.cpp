// libopl4 idle fast path (Opl4::AdvanceIdle): a silent chip - every FM operator off at full attenuation, every PCM
// slot off - moves only counters (operator phases, LFOs, the noise LFSR, envelope and TL counters, timers, the
// reducer window), so SyncTo advances it in one go. Two chips in lockstep, one with the fast path and one stepping
// every FM tick and output step, get the same random register traffic and runs of random length, notes that start
// and release into silence, timers, vibrato, rhythm, 4-op pairs and PCM TL ramps and LFOs on silent slots: the
// whole chip state (SaveState), the status register and the rendered audio must stay equal after every action.

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <random>
#include <vector>

#include "opl4/opl4.h"
#include "opl4/wavememory.h"

namespace
{
constexpr uint32_t kHdrBase = 0x200000;  // tone headers in SRAM (header base 4)
constexpr uint32_t kSmpBase = 0x200100;

struct Chip
{
    opl4::WaveMemory mem;
    opl4::Opl4 chip;
    opl4::Opl4Config cfg;

    Chip(bool fastPath, opl4::RenderMode mode, opl4::ReducerKernel reducer, bool split)
    {
        cfg.hostTickRate = opl4::kMasterClockHz;
        cfg.mode = mode;
        cfg.reducer = reducer;
        mem.Configure(2u << 20, 1u << 20);
        // One 16-bit tone at wave 384: a short looped square
        uint8_t header[12] = {static_cast<uint8_t>((2 << 6) | ((kSmpBase >> 16) & 0x3F)),
                              static_cast<uint8_t>((kSmpBase >> 8) & 0xFF), static_cast<uint8_t>(kSmpBase & 0xFF),
                              0, 0, 0xFF, 0xF8, 0, 0, 0, 0, 0};
        mem.WriteSram(kHdrBase, header, sizeof(header));
        uint8_t samples[16];
        for (int i = 0; i < 8; i++)
        {
            const uint16_t v = i < 4 ? 0x6000 : 0xA000;
            samples[2 * i] = static_cast<uint8_t>(v >> 8);
            samples[2 * i + 1] = static_cast<uint8_t>(v & 0xFF);
        }
        mem.WriteSram(kSmpBase, samples, sizeof(samples));
        mem.ClearDirty();
        chip.Configure(cfg, &mem);
        chip.EnableSplitStreams(split);
        chip.Reset(0);
        chip.SetIdleFastPath(fastPath);
    }

    std::vector<uint8_t> state() const
    {
        std::vector<uint8_t> s(chip.StateSize());
        chip.SaveState(s.data());
        return s;
    }

    std::vector<float> drain()
    {
        std::vector<float> out;
        float buffer[2 * 4096];
        size_t n;
        while ((n = chip.Render(buffer, 4096)) > 0)
            out.insert(out.end(), buffer, buffer + 2 * n);
        return out;
    }
};

struct Lockstep
{
    Chip fast;
    Chip stepping;
    uint64_t t = 1000;
    int actions = 0;

    Lockstep(opl4::RenderMode mode, opl4::ReducerKernel reducer, bool split)
        : fast(true, mode, reducer, split), stepping(false, mode, reducer, split)
    {
    }

    template <typename F>
    void both(F&& f)
    {
        f(fast.chip);
        f(stepping.chip);
        actions++;
    }

    ::testing::AssertionResult equal()
    {
        if (fast.state() != stepping.state())
            return ::testing::AssertionFailure() << "chip state differs after action " << actions;
        const uint8_t a = fast.chip.ReadStatus(t);
        const uint8_t b = stepping.chip.ReadStatus(t);
        if (a != b)
            return ::testing::AssertionFailure() << "status " << int(a) << " vs " << int(b) << " after action " << actions;
        if (fast.drain() != stepping.drain())
            return ::testing::AssertionFailure() << "audio differs after action " << actions;
        return ::testing::AssertionSuccess();
    }
};

// The operator register offsets of the classic map (slots 0..21 per bank, gaps at 6, 7, 14, 15)
constexpr uint8_t kOpOffsets[18] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x08, 0x09, 0x0A,
                                    0x0B, 0x0C, 0x0D, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15};

void RandomFmWrite(Lockstep& l, std::mt19937& rng)
{
    const int bank = static_cast<int>(rng() % 2);
    const uint8_t op = kOpOffsets[rng() % 18];
    const uint8_t ch = static_cast<uint8_t>(rng() % 9);
    const uint8_t value = static_cast<uint8_t>(rng());
    uint8_t reg = 0;
    uint8_t data = value;
    switch (rng() % 12)
    {
        case 0: reg = static_cast<uint8_t>(0x20 + op); break;                       // AM VIB EGT KSR MULT
        case 1: reg = static_cast<uint8_t>(0x40 + op); break;                       // KSL TL
        case 2: reg = static_cast<uint8_t>(0x60 + op); data |= 0x80; break;         // AR (fast) DR
        case 3: reg = static_cast<uint8_t>(0x80 + op); data |= 0x08; break;         // SL RR (releases end)
        case 4: reg = static_cast<uint8_t>(0xA0 + ch); break;                       // F-number low
        case 5: reg = static_cast<uint8_t>(0xB0 + ch); break;                       // key, block, F-number high
        case 6: reg = static_cast<uint8_t>(0xC0 + ch); data |= 0x30; break;         // routing, FB, CNT
        case 7: reg = static_cast<uint8_t>(0xE0 + op); break;                       // waveform
        case 8: reg = 0xBD; break;                                                  // AM/VIB depth, rhythm
        case 9: reg = static_cast<uint8_t>(0x02 + rng() % 2); break;                // timer loads
        case 10: reg = 0x04; break;                                                 // timer control / 4-op (bank 1)
        default: reg = 0x05; data = static_cast<uint8_t>(value & 0x03); break;      // NEW, NEW2 (bank 1)
    }
    l.both([&](opl4::Opl4& c) { c.WriteFm(l.t, bank, reg, data); });
}

// Every FM key off, fast release on every operator: the chip falls silent
void FmAllOff(Lockstep& l)
{
    l.both([&](opl4::Opl4& c) {
        for (int bank = 0; bank < 2; bank++)
        {
            for (uint8_t op : kOpOffsets)
                c.WriteFm(l.t, bank, static_cast<uint8_t>(0x80 + op), 0x0F);
            for (uint8_t ch = 0; ch < 9; ch++)
                c.WriteFm(l.t, bank, static_cast<uint8_t>(0xB0 + ch), 0x00);
        }
        c.WriteFm(l.t, 0, 0xBD, 0xC0);  // rhythm drums off, depths kept
    });
}

void RandomPcm(Lockstep& l, std::mt19937& rng)
{
    const uint8_t slot = static_cast<uint8_t>(rng() % 24);
    switch (rng() % 5)
    {
        case 0:  // key on at the square, random TL, LFO, vibrato and AM
        {
            const uint8_t tl = static_cast<uint8_t>(rng() & 0xFE);
            const uint8_t lfo = static_cast<uint8_t>(rng());
            const uint8_t am = static_cast<uint8_t>(rng() & 0x07);
            l.both([&](opl4::Opl4& c) {
                c.WriteWave(l.t, 0x02, 0x10);
                c.WriteWave(l.t, static_cast<uint8_t>(0x08 + slot + 24), 0xFF);
                c.WriteWave(l.t, static_cast<uint8_t>(0x08 + slot), 0x80);
                c.WriteWave(l.t, static_cast<uint8_t>(0x08 + slot + 24 * 2), 0x07);
                c.WriteWave(l.t, static_cast<uint8_t>(0x08 + slot + 24 * 6), 0xF4);
                c.WriteWave(l.t, static_cast<uint8_t>(0x08 + slot + 24 * 7), 0x00);
                c.WriteWave(l.t, static_cast<uint8_t>(0x08 + slot + 24 * 8), 0x0E);  // fast release
                c.WriteWave(l.t, static_cast<uint8_t>(0x08 + slot + 24 * 5), lfo);
                c.WriteWave(l.t, static_cast<uint8_t>(0x08 + slot + 24 * 9), am);
                c.WriteWave(l.t, static_cast<uint8_t>(0x08 + slot + 24 * 3), tl);
                c.WriteWave(l.t, static_cast<uint8_t>(0x08 + slot + 24 * 4), 0x80);
            });
            break;
        }
        case 1:  // key off
            l.both([&](opl4::Opl4& c) { c.WriteWave(l.t, static_cast<uint8_t>(0x08 + slot + 24 * 4), 0x00); });
            break;
        case 2:  // a TL ramp (interpolated, also on a silent slot)
        {
            const uint8_t tl = static_cast<uint8_t>(rng() & 0xFE);
            l.both([&](opl4::Opl4& c) { c.WriteWave(l.t, static_cast<uint8_t>(0x08 + slot + 24 * 3), tl); });
            break;
        }
        case 3:  // LFO speed / vibrato (the counter turns on a silent slot too)
        {
            const uint8_t lfo = static_cast<uint8_t>(rng());
            l.both([&](opl4::Opl4& c) { c.WriteWave(l.t, static_cast<uint8_t>(0x08 + slot + 24 * 5), lfo); });
            break;
        }
        default:  // LFO reset bit toggled with the key off
        {
            const uint8_t v = static_cast<uint8_t>(rng() & 0x20);
            l.both([&](opl4::Opl4& c) { c.WriteWave(l.t, static_cast<uint8_t>(0x08 + slot + 24 * 4), v); });
            break;
        }
    }
}

void PcmAllOff(Lockstep& l)
{
    l.both([&](opl4::Opl4& c) {
        for (uint8_t slot = 0; slot < 24; slot++)
        {
            c.WriteWave(l.t, static_cast<uint8_t>(0x08 + slot + 24 * 8), 0x0F);
            c.WriteWave(l.t, static_cast<uint8_t>(0x08 + slot + 24 * 4), 0x00);
        }
    });
}

void RunScenario(opl4::RenderMode mode, opl4::ReducerKernel reducer, bool split, uint32_t seed)
{
    Lockstep l(mode, reducer, split);
    std::mt19937 rng(seed);
    l.both([&](opl4::Opl4& c) { c.WriteFm(l.t, 1, 0x05, 0x03); });  // NEW, NEW2: the whole chip
    for (int i = 0; i < 600; i++)
    {
        switch (rng() % 10)
        {
            case 0:
            case 1:
            case 2:
                RandomFmWrite(l, rng);
                break;
            case 3:
                RandomPcm(l, rng);
                break;
            case 4:
                if (rng() % 3 == 0)
                    FmAllOff(l);
                else
                    PcmAllOff(l);
                break;
            case 5:
                l.both([&](opl4::Opl4& c) { (void)c.ReadStatus(l.t); });
                break;
            default:
            {
                // Short runs (inside one window) up to several frames of silence
                const uint64_t clocks = (rng() % 4 == 0) ? 1 + rng() % 2000 : 1 + rng() % 3000000;
                l.t += clocks;
                const uint64_t to = l.t;
                l.both([&](opl4::Opl4& c) { c.Run(to); });
                break;
            }
        }
        ASSERT_TRUE(l.equal()) << "seed " << seed;
    }
    EXPECT_GT(l.fast.chip.IdleFmTicks(), 0u) << "the silent stretches took the fast path";
    EXPECT_EQ(l.stepping.chip.IdleFmTicks(), 0u);
}
}  // namespace

TEST(Opl4_IdleFastPath, AuthenticSplitHoldDropMatchesTheSteppingChip)
{
    RunScenario(opl4::RenderMode::Authentic, opl4::ReducerKernel::HoldDrop, true, 20261012);
}

TEST(Opl4_IdleFastPath, AuthenticLinearBlendMatchesTheSteppingChip)
{
    RunScenario(opl4::RenderMode::Authentic, opl4::ReducerKernel::LinearBlend, false, 12102026);
}

TEST(Opl4_IdleFastPath, HiFiMatchesTheSteppingChip)
{
    RunScenario(opl4::RenderMode::HiFi, opl4::ReducerKernel::HoldDrop, false, 777);
}

// A whole silent second from power-on: one SyncTo, the same state as 49516 FM ticks one by one
TEST(Opl4_IdleFastPath, PowerOnSilenceInOneStep)
{
    Lockstep l(opl4::RenderMode::Authentic, opl4::ReducerKernel::HoldDrop, true);
    l.both([&](opl4::Opl4& c) {
        c.WriteFm(l.t, 0, 0x02, 0x80);  // timer 1 load
        c.WriteFm(l.t, 0, 0x03, 0x10);  // timer 2 load
        c.WriteFm(l.t, 0, 0x04, 0x03);  // both running, unmasked
    });
    l.t += opl4::kMasterClockHz;
    l.both([&](opl4::Opl4& c) { c.Run(l.t); });
    ASSERT_TRUE(l.equal());
    EXPECT_GT(l.fast.chip.IdleFmTicks(), 49000u);
}
