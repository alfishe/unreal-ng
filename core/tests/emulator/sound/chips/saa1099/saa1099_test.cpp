// Saa1099 (core/src/emulator/sound/chips/saa1099): the behaviors of
// tdd-saa1099.md §3.3 as the reference consensus fixed them
// (tools/verification/saa1099/README.md, consensus table). Every test runs the chip
// with its host axis equal to the chip clock (1 tick = 1 chip clock) unless it tests
// the axis itself.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "emulator/sound/chips/saa1099/saa1099.h"

namespace
{
constexpr uint32_t kChipClock = 8000000;

uint32_t HalfPeriod(int tone, int octave)
{
    return static_cast<uint32_t>(511 - tone) << (8 - octave);
}

// The noise polynomial measured on a real SAA1099P: x^18 + x^11 + 1 (Galois, right shift)
uint32_t NextLfsr(uint32_t r)
{
    return (r & 1) ? ((r >> 1) ^ 0x20400u) : (r >> 1);
}

class Bench
{
public:
    explicit Bench(Saa1099RenderMode mode = Saa1099RenderMode::HiFi, uint32_t hostTickRate = kChipClock)
    {
        Saa1099Config cfg;
        cfg.hostTickRate = hostTickRate;
        cfg.chipClockHz = kChipClock;
        cfg.renderMode = mode;
        chip.Configure(cfg);
    }

    // Address, 8 clocks, data; the clock stops right after the data write
    void Reg(uint8_t reg, uint8_t value)
    {
        chip.WriteAddress(t, reg);
        t += 8;
        chip.WriteData(t, value);
    }
    void Addr(uint8_t reg)
    {
        chip.WriteAddress(t, reg);
    }
    void Run(uint64_t clocks)
    {
        t += clocks;
        chip.Run(t);
    }
    // RST pulse: every tone generator restarts its half period with its numbers now
    void Sync()
    {
        Reg(0x1C, 0x03);
        Reg(0x1C, 0x01);
    }
    Saa1099Report Report() const
    {
        Saa1099Report r;
        chip.Describe(r);
        return r;
    }

    Saa1099 chip;
    uint64_t t = 0;
};

// Envelope level after `steps` clocks of a freshly started shape (Philips TP231 Fig.3):
// the reference table the tests compare against
uint8_t ShapeLevel(int shape, bool res3, int steps)
{
    const int pos = steps * (res3 ? 2 : 1); // 4-bit position counted from the start
    const bool looping = (shape & 1) != 0;
    const int mask = res3 ? 0x0E : 0x0F;
    int level = 0;
    switch (shape)
    {
        case 0: level = 0; break;
        case 1: level = 15; break;
        case 2:
        case 3:
            level = (!looping && pos >= 16) ? 0 : 15 - (pos % 16);
            break;
        case 4:
        case 5:
        {
            const int p = pos % 32;
            level = (!looping && pos >= 32) ? 0 : (p < 16 ? p : 31 - p);
            break;
        }
        case 6:
        case 7:
            level = (!looping && pos >= 16) ? 0 : pos % 16;
            break;
    }
    return static_cast<uint8_t>(level & mask);
}
} // namespace

TEST(Saa1099_Test, ToneFrequencyAllOctaves)
{
    Bench b;
    const int tones[] = {0, 1, 100, 227, 254, 255};
    for (int octave = 0; octave < 8; octave++)
    {
        for (int tone : tones)
        {
            b.Reg(0x08, static_cast<uint8_t>(tone));
            b.Reg(0x10, static_cast<uint8_t>(octave));
            b.Sync();
            const uint32_t p = HalfPeriod(tone, octave);
            Saa1099Report r = b.Report();
            ASSERT_EQ(r.tones[0].halfPeriod, p) << "octave " << octave << " tone " << tone;
            ASSERT_EQ(r.tones[0].clocksToTransition, p);
            const uint8_t level = r.tones[0].level;

            b.Run(p - 1);
            ASSERT_EQ(b.Report().tones[0].level, level) << "octave " << octave << " tone " << tone;
            b.Run(1);
            r = b.Report();
            ASSERT_NE(r.tones[0].level, level) << "octave " << octave << " tone " << tone;
            ASSERT_EQ(r.tones[0].clocksToTransition, p);
        }
    }

    // The worked example: tone 227 in octave 3 is middle A, 15 625 * 8 / 284 = 440.1 Hz
    b.Reg(0x08, 227);
    b.Reg(0x10, 0x03);
    b.Sync();
    EXPECT_NEAR(b.Report().tones[0].frequencyHz, 440.14, 0.01);
}

TEST(Saa1099_Test, OctaveLatchAtReload)
{
    Bench b;
    b.Reg(0x09, 100);
    b.Reg(0x10, 0x30); // voice 1 octave 3
    b.Sync();
    const uint32_t p = HalfPeriod(100, 3);

    // A new octave waits for the transition
    b.Run(p / 2);
    b.Reg(0x10, 0x50);
    Saa1099Report r = b.Report();
    EXPECT_EQ(r.tones[1].octaveRegister, 5);
    EXPECT_EQ(r.tones[1].octaveLatched, 3);
    EXPECT_EQ(r.tones[1].halfPeriod, p);
    b.Run(r.tones[1].clocksToTransition);
    r = b.Report();
    EXPECT_EQ(r.tones[1].octaveLatched, 5);
    EXPECT_EQ(r.tones[1].clocksToTransition, HalfPeriod(100, 5));

    // Tone number then octave inside one half period: both act at the same transition
    b.Run(10);
    b.Reg(0x09, 200);
    b.Reg(0x10, 0x20);
    r = b.Report();
    EXPECT_EQ(r.tones[1].halfPeriod, HalfPeriod(100, 5));
    b.Run(r.tones[1].clocksToTransition);
    EXPECT_EQ(b.Report().tones[1].halfPeriod, HalfPeriod(200, 2));

    // A tone number alone acts at the next transition too (Philips: "up to half a
    // period"; SAASound's extra transition is not followed)
    b.Run(10);
    b.Reg(0x09, 50);
    r = b.Report();
    EXPECT_EQ(r.tones[1].toneLatched, 200);
    b.Run(r.tones[1].clocksToTransition);
    EXPECT_EQ(b.Report().tones[1].toneLatched, 50);
}

TEST(Saa1099_Test, NoiseLfsrSequence)
{
    // Fixed rates: clock / 256, 512, 1024
    for (uint8_t source = 0; source < 3; source++)
    {
        Bench b;
        b.Reg(0x1C, 0x02);
        b.Reg(0x16, static_cast<uint8_t>(source | (source << 4)));
        b.Reg(0x1C, 0x01); // release: both dividers start a full period
        const uint32_t period = 256u << source;
        uint32_t expected = 0x3FFFF;
        ASSERT_EQ(b.Report().noise[0].lfsr, expected);
        for (int i = 0; i < 64; i++)
        {
            ASSERT_EQ(b.Report().noise[0].clocksToShift, period) << "source " << int(source) << " bit " << i;
            b.Run(period - 1);
            ASSERT_EQ(b.Report().noise[0].lfsr, expected) << "source " << int(source) << " bit " << i;
            b.Run(1);
            expected = NextLfsr(expected);
            const Saa1099Report r = b.Report();
            ASSERT_EQ(r.noise[0].lfsr, expected) << "source " << int(source) << " bit " << i;
            ASSERT_EQ(r.noise[1].lfsr, expected);
            ASSERT_EQ(r.noise[0].output, expected & 1);
        }
    }

    // Source 3: every transition of tone generator 0 / 3 shifts noise generator 0 / 1
    Bench b;
    b.Reg(0x08, 200);
    b.Reg(0x0B, 150);
    b.Reg(0x10, 0x06);
    b.Reg(0x11, 0x50);
    b.Reg(0x16, 0x33);
    b.Sync();
    uint32_t expected0 = 0x3FFFF;
    const uint32_t p0 = HalfPeriod(200, 6);
    for (int i = 0; i < 64; i++)
    {
        b.Run(p0 - 1);
        ASSERT_EQ(b.Report().noise[0].lfsr, expected0) << "bit " << i;
        b.Run(1);
        expected0 = NextLfsr(expected0);
        ASSERT_EQ(b.Report().noise[0].lfsr, expected0) << "bit " << i;
    }
    // Generator 1 followed tone generator 3 (octave 5, tone 150) over the same time
    const uint64_t edges3 = (64ull * p0) / HalfPeriod(150, 5);
    uint32_t expected1 = 0x3FFFF;
    for (uint64_t i = 0; i < edges3; i++)
        expected1 = NextLfsr(expected1);
    EXPECT_EQ(b.Report().noise[1].lfsr, expected1);

    // The measured polynomial has the full period 2^18 - 1
    uint32_t r = 0x3FFFF;
    uint32_t n = 0;
    do
    {
        r = NextLfsr(r);
        n++;
    } while (r != 0x3FFFF && n < (1u << 19));
    EXPECT_EQ(n, (1u << 18) - 1);
}

TEST(Saa1099_Test, ToneAndNoiseCombined)
{
    // Voice 0, left amplitude 15: full level 4 * 15 PDM ones in each of the two periods
    struct Case
    {
        uint8_t toneEnable, noiseEnable;
    };
    const Case cases[] = {{1, 0}, {0, 1}, {1, 1}, {0, 0}};
    for (const Case& c : cases)
    {
        Bench b;
        b.Reg(0x00, 0x0F);
        b.Reg(0x08, 0);
        b.Reg(0x10, 0x07); // fast tone: half period 1022 clocks
        b.Reg(0x16, 0x00); // noise every 256 clocks
        b.Reg(0x14, c.toneEnable);
        b.Reg(0x15, c.noiseEnable);
        b.Sync();
        bool seen[2][2] = {};
        for (int step = 0; step < 4000; step++)
        {
            b.Run(37);
            const Saa1099Report r = b.Report();
            const int tone = r.tones[0].level;
            const int noise = r.noise[0].output;
            seen[tone][noise] = true;
            int expected = 0;
            if (c.toneEnable && c.noiseEnable)
                expected = tone ? (noise ? 60 : 120) : 0; // half while both are high
            else if (c.toneEnable)
                expected = tone ? 120 : 0;
            else if (c.noiseEnable)
                expected = noise ? 120 : 0;
            ASSERT_EQ(r.voices[0].levelLeft, expected) << "tone " << tone << " noise " << noise;
            ASSERT_EQ(r.voices[0].levelRight, 0);
        }
        EXPECT_TRUE(seen[0][0] && seen[0][1] && seen[1][0] && seen[1][1]);
    }
}

TEST(Saa1099_Test, EnvelopeShapesAndResolution)
{
    for (int shape = 0; shape < 8; shape++)
    {
        for (int res3 = 0; res3 < 2; res3++)
        {
            for (int invert = 0; invert < 2; invert++)
            {
                Bench b;
                const uint8_t control =
                    static_cast<uint8_t>(0x80 | 0x20 | (res3 << 4) | (shape << 1) | invert); // external clock
                b.Reg(0x18, control);
                for (int step = 0; step < 40; step++)
                {
                    const Saa1099Report r = b.Report();
                    const uint8_t left = ShapeLevel(shape, res3 != 0, step);
                    const uint8_t right = invert ? static_cast<uint8_t>((res3 ? 14 : 15) - left) : left;
                    ASSERT_EQ(r.envelopes[0].levelLeft, left)
                        << "shape " << shape << " res3 " << res3 << " invert " << invert << " step " << step;
                    ASSERT_EQ(r.envelopes[0].levelRight, right)
                        << "shape " << shape << " res3 " << res3 << " invert " << invert << " step " << step;
                    b.Addr(0x18);
                }
            }
        }
    }
}

TEST(Saa1099_Test, EnvelopeBufferedWrite)
{
    // External clock: every address write of #18 clocks generator 0, including the
    // address cycle of a control write
    Bench b;
    b.Reg(0x18, 0x80 | 0x20 | (3 << 1)); // repetitive decay; it was off, so it acts at once
    for (int i = 0; i < 5; i++)
        b.Addr(0x18);
    EXPECT_EQ(b.Report().envelopes[0].position, 5);
    EXPECT_EQ(b.Report().envelopes[0].levelLeft, 10);

    // Shape, clock and inversion wait for the loop point (4); resolution acts at once
    // (position 6 after this write's own strobe, kept even in 3-bit mode)
    b.Reg(0x18, 0x80 | 0x20 | 0x10 | (7 << 1) | 1);
    Saa1099Report r = b.Report();
    EXPECT_TRUE(r.envelopes[0].pending);
    EXPECT_EQ(r.envelopes[0].shape, 3);
    EXPECT_FALSE(r.envelopes[0].invertRight);
    EXPECT_TRUE(r.envelopes[0].resolution3Bit);
    EXPECT_EQ(r.envelopes[0].position, 6);
    EXPECT_EQ(r.envelopes[0].levelLeft, 8); // (15 - 6) with the LSB dropped
    for (int i = 0; i < 4; i++) // 8, 10, 12, 14
        b.Addr(0x18);
    EXPECT_EQ(b.Report().envelopes[0].shape, 3);
    b.Addr(0x18); // 16: the loop point
    r = b.Report();
    EXPECT_FALSE(r.envelopes[0].pending);
    EXPECT_EQ(r.envelopes[0].shape, 7);
    EXPECT_TRUE(r.envelopes[0].invertRight);
    EXPECT_EQ(r.envelopes[0].levelLeft, 0);
    EXPECT_EQ(r.envelopes[0].levelRight, 14);

    // A single decay, buffered until the attack wraps, then it plays once and ends
    // (point 3); after that a write acts at once
    b.Reg(0x18, 0x80 | 0x20 | (2 << 1));
    int clocks = 0;
    while (b.Report().envelopes[0].pending && clocks < 32)
    {
        b.Addr(0x18);
        clocks++;
    }
    r = b.Report();
    ASSERT_FALSE(r.envelopes[0].pending);
    ASSERT_EQ(r.envelopes[0].shape, 2);
    EXPECT_EQ(r.envelopes[0].levelLeft, 15);
    for (int i = 0; i < 16; i++)
        b.Addr(0x18);
    r = b.Report();
    ASSERT_TRUE(r.envelopes[0].ended);
    EXPECT_EQ(r.envelopes[0].levelLeft, 0);
    b.Reg(0x18, 0x80 | 0x20 | (6 << 1));
    r = b.Report();
    EXPECT_FALSE(r.envelopes[0].pending);
    EXPECT_EQ(r.envelopes[0].shape, 6);

    // Switching off is direct-acting, and the voice plays unshaped
    b.Reg(0x02, 0xFF);
    b.Reg(0x14, 0x04);
    b.Reg(0x1C, 0x01);
    b.Reg(0x18, 0x00);
    r = b.Report();
    EXPECT_FALSE(r.envelopes[0].enabled);
    EXPECT_FALSE(r.voices[2].envelopeShaped);
}

TEST(Saa1099_Test, EnvelopeExternalClockOnAddressWrite)
{
    Bench b;
    b.Reg(0x18, 0x80 | 0x20 | (7 << 1)); // repetitive attack, external
    b.Reg(0x19, 0x80 | 0x20 | (7 << 1));
    // The control writes' own address cycles came while the generators were off
    EXPECT_EQ(b.Report().envelopes[0].position, 0);
    EXPECT_EQ(b.Report().envelopes[1].position, 0);
    b.Addr(0x18);
    EXPECT_EQ(b.Report().envelopes[0].position, 1);
    EXPECT_EQ(b.Report().envelopes[1].position, 0);
    b.Addr(0x19);
    EXPECT_EQ(b.Report().envelopes[0].position, 1);
    EXPECT_EQ(b.Report().envelopes[1].position, 1);
    b.Addr(0x38); // 5-bit address: #38 is #18
    EXPECT_EQ(b.Report().envelopes[0].position, 2);
    b.Addr(0x00);
    EXPECT_EQ(b.Report().envelopes[0].position, 2);

    // Internal clock: address writes do nothing, every edge of tone generator 1 clocks
    Bench i;
    i.Reg(0x09, 255);
    i.Reg(0x10, 0x70); // generator 1: half period 512
    i.Reg(0x18, 0x80 | (7 << 1));
    i.Sync();
    i.Addr(0x18);
    EXPECT_EQ(i.Report().envelopes[0].position, 0);
    i.Run(512);
    EXPECT_EQ(i.Report().envelopes[0].position, 1);
    i.Run(512);
    EXPECT_EQ(i.Report().envelopes[0].position, 2);
}

TEST(Saa1099_Test, SyncResetBit)
{
    Bench b;
    b.Reg(0x00, 0xFF);
    b.Reg(0x08, 100);
    b.Reg(0x10, 0x04);
    b.Reg(0x14, 0x01);
    b.Reg(0x15, 0x01);
    b.Reg(0x18, 0x80 | 0x20 | (3 << 1));
    b.Addr(0x18);
    b.Addr(0x18);
    b.Reg(0x1C, 0x01);
    b.Run(10000);
    const uint32_t p = HalfPeriod(100, 4);

    b.Reg(0x1C, 0x03);
    Saa1099Report r = b.Report();
    EXPECT_TRUE(r.sync);
    EXPECT_EQ(r.tones[0].level, 1);
    EXPECT_EQ(r.tones[0].clocksToTransition, p);
    EXPECT_EQ(b.chip.OutputLeft(), 0); // silent while held
    const uint32_t lfsr = r.noise[0].lfsr;
    const uint8_t envPosition = r.envelopes[0].position;

    b.Run(100000);
    b.Reg(0x08, 10); // written during RST
    r = b.Report();
    EXPECT_EQ(r.tones[0].clocksToTransition, p);
    EXPECT_EQ(r.tones[0].toneLatched, 100);
    EXPECT_EQ(r.noise[0].lfsr, lfsr);
    EXPECT_EQ(r.envelopes[0].position, envPosition); // envelopes are not reset by RST

    // Release: the first half period runs with the numbers held at RST, then the new ones
    b.Reg(0x1C, 0x01);
    b.Run(p - 1);
    EXPECT_EQ(b.Report().tones[0].level, 1);
    b.Run(1);
    r = b.Report();
    EXPECT_EQ(r.tones[0].level, 0);
    EXPECT_EQ(r.tones[0].toneLatched, 10);
    EXPECT_EQ(r.tones[0].clocksToTransition, HalfPeriod(10, 4));
}

TEST(Saa1099_Test, ClockGateFreezes)
{
    Bench b(Saa1099RenderMode::HiFi, 3500000);
    b.Reg(0x00, 0xFF);
    b.Reg(0x08, 0);
    b.Reg(0x10, 0x07);
    b.Reg(0x14, 0x01);
    b.Reg(0x1C, 0x01);
    std::vector<int16_t> frame(882 * 2);
    b.Run(70000);
    b.chip.EndFrame(b.t, frame.data(), 882);

    b.chip.SetClockEnabled(b.t, false);
    const Saa1099Report before = b.Report();
    const int32_t held = b.chip.OutputLeft();
    for (int f = 0; f < 3; f++)
    {
        b.Run(70000);
        b.chip.EndFrame(b.t, frame.data(), 882);
    }
    const Saa1099Report after = b.Report();
    EXPECT_FALSE(after.clockEnabled);
    EXPECT_EQ(after.chipClocks, before.chipClocks);
    EXPECT_EQ(after.tones[0].clocksToTransition, before.tones[0].clocksToTransition);
    EXPECT_EQ(after.tones[0].level, before.tones[0].level);
    EXPECT_EQ(b.chip.OutputLeft(), held);
    // The held level is a constant: no steps, the output only creeps (blip's DC leak)
    for (size_t i = 2; i < frame.size(); i += 2)
        ASSERT_LE(std::abs(frame[i] - frame[i - 2]), 2) << i;

    b.chip.SetClockEnabled(b.t, true);
    b.Run(7000); // 16 000 chip clocks
    EXPECT_EQ(b.Report().chipClocks, before.chipClocks + 16000);
}

TEST(Saa1099_Test, OutputRateChangeKeepsState)
{
    Bench b(Saa1099RenderMode::HiFi, 3500000);
    b.Reg(0x00, 0xFF);
    b.Reg(0x08, 227);
    b.Reg(0x10, 0x03);
    b.Reg(0x14, 0x01);
    b.Reg(0x1C, 0x01);
    std::vector<int16_t> frame(960 * 2);
    for (int f = 0; f < 5; f++)
    {
        b.Run(70000);
        EXPECT_EQ(b.chip.EndFrame(b.t, frame.data(), 882), 882u);
    }
    const Saa1099Report before = b.Report();
    b.chip.SetOutputRate(48000);
    const Saa1099Report after = b.Report();
    EXPECT_EQ(after.outputRate, 48000u);
    EXPECT_EQ(after.chipClocks, before.chipClocks);
    EXPECT_EQ(after.tones[0].clocksToTransition, before.tones[0].clocksToTransition);
    EXPECT_EQ(after.tones[0].level, before.tones[0].level);

    int peak = 0;
    for (int f = 0; f < 3; f++)
    {
        b.Run(70000);
        EXPECT_EQ(b.chip.EndFrame(b.t, frame.data(), 960), 960u);
        for (size_t i = 0; i < frame.size(); i += 2)
            peak = std::max(peak, std::abs(static_cast<int>(frame[i])));
    }
    EXPECT_GT(peak, 1000); // the 440 Hz tone keeps sounding
    EXPECT_EQ(b.Report().chipClocks, before.chipClocks + 3 * 160000);
}

TEST(Saa1099_Test, TtdRoundTrip)
{
    Bench a(Saa1099RenderMode::HiFi, 3500000);
    uint32_t lcg = 12345;
    auto next = [&lcg]() {
        lcg = lcg * 1103515245u + 12345u;
        return static_cast<uint8_t>(lcg >> 16);
    };
    const uint8_t regs[] = {0, 1, 2, 3, 4, 5, 8, 9, 10, 11, 12, 13, 0x10, 0x11, 0x12, 0x14, 0x15, 0x16, 0x18, 0x19};
    auto play = [&](Bench& b, int writes, std::vector<int16_t>* audio) {
        std::vector<int16_t> frame(882 * 2);
        for (int i = 0; i < writes; i++)
        {
            const uint8_t reg = regs[next() % sizeof(regs)];
            b.Reg(reg, next());
            b.Run(3000 + next() * 20);
            if (i % 8 == 7)
            {
                b.chip.EndFrame(b.t, frame.data(), 882);
                if (audio)
                    audio->insert(audio->end(), frame.begin(), frame.end());
            }
        }
    };
    a.Reg(0x1C, 0x01);
    play(a, 64, nullptr);
    std::vector<int16_t> frame(882 * 2);
    a.chip.EndFrame(a.t, frame.data(), 882);

    std::vector<uint8_t> blob(a.chip.TTDStateSize());
    a.chip.TTDSaveState(blob.data());
    EXPECT_EQ(blob[0], Saa1099::kStateVersion);
    EXPECT_EQ(a.chip.TTDPeripheralId(), ttd::PeripheralId::Saa1099);

    Bench b(Saa1099RenderMode::HiFi, 3500000);
    b.chip.TTDLoadState(blob.data());
    b.t = a.t;
    EXPECT_EQ(b.chip.TTDHashState(), a.chip.TTDHashState());

    // A second restore of the same blob replays bit-exactly (TTD's requirement)
    Bench c(Saa1099RenderMode::HiFi, 3500000);
    c.chip.TTDLoadState(blob.data());
    c.t = a.t;

    const uint32_t saved = lcg;
    std::vector<int16_t> audioA, audioB, audioC;
    play(a, 64, &audioA);
    lcg = saved;
    play(b, 64, &audioB);
    lcg = saved;
    play(c, 64, &audioC);
    EXPECT_EQ(a.chip.TTDHashState(), b.chip.TTDHashState());
    EXPECT_EQ(audioB, audioC);
    // The original keeps its output buffer; the restored chip starts a fresh one with a
    // step to the restored level, on a sample grid that may sit up to one sample off
    // (the buffer's sub-sample phase is host state). Past the first samples every
    // restored sample lies within the original's neighbours
    ASSERT_EQ(audioA.size(), audioB.size());
    for (size_t i = 64; i + 2 < audioA.size(); i++)
    {
        const int lo = std::min({audioA[i - 2], audioA[i], audioA[i + 2]});
        const int hi = std::max({audioA[i - 2], audioA[i], audioA[i + 2]});
        ASSERT_GE(audioB[i], lo - 512) << i; // band-limited ringing moves with the phase
        ASSERT_LE(audioB[i], hi + 512) << i;
    }

    // A blob of another layout version is refused and changes nothing
    std::vector<uint8_t> bad = blob;
    bad[0] = 0xEE;
    const uint64_t hash = b.chip.TTDHashState();
    b.chip.TTDLoadState(bad.data());
    EXPECT_EQ(b.chip.TTDHashState(), hash);
}

TEST(Saa1099_Test, EnvelopeAmplitudeFromPdm)
{
    // A voice under its envelope sinks current where the amplitude (LSB dropped) and the
    // envelope PDM patterns measured on a real chip are both 1, while its mixer output
    // is low; 53 of 64 slots at amplitude 15 and envelope 15 (Philips: 7/8)
    Bench b;
    b.Reg(0x02, 0x2F);                   // voice 2: left 15, right 2
    b.Reg(0x18, 0x80 | 0x20 | (1 << 1)); // maximum amplitude
    b.Reg(0x1C, 0x01);
    Saa1099Report r = b.Report();
    EXPECT_TRUE(r.voices[2].envelopeShaped);
    EXPECT_EQ(r.voices[2].levelLeft, 2 * 53);
    EXPECT_EQ(r.voices[2].levelRight, 2 * 8); // amplitude 2 and envelope 15: 8 of 64 slots
    // Tone enabled and high: silent; low: full
    b.Reg(0x14, 0x04);
    b.Sync();
    r = b.Report();
    ASSERT_EQ(r.tones[2].level, 1);
    EXPECT_EQ(r.voices[2].levelLeft, 0);
    b.Run(r.tones[2].clocksToTransition);
    EXPECT_EQ(b.Report().voices[2].levelLeft, 2 * 53);
}

TEST(Saa1099_Test, AuthenticMeanMatchesHiFi)
{
    for (int envelope = 0; envelope < 2; envelope++)
    {
        Bench hifi(Saa1099RenderMode::HiFi);
        Bench pdm(Saa1099RenderMode::Authentic);
        for (Bench* b : {&hifi, &pdm})
        {
            for (uint8_t v = 0; v < 6; v++)
                b->Reg(v, static_cast<uint8_t>(0x37 + v * 0x21));
            b->Reg(0x14, 0x3F);
            b->Reg(0x15, 0x00);
            if (envelope)
                b->Reg(0x18, 0x80 | 0x20 | 0x10 | (3 << 1) | 1); // repetitive decay, at 14 / 0
            b->Reg(0x1C, 0x01);
            b->Run(128 - (b->t % 128)); // align to a PDM period pair
        }
        ASSERT_EQ(hifi.t, pdm.t);
        // Tone generators all start a long first half period after power-on: no event
        // for the next 130 000 clocks, so every 128-clock window has one level
        for (int window = 0; window < 20; window++)
        {
            int64_t sumLeft = 0, sumRight = 0;
            for (int c = 0; c < 128; c++)
            {
                sumLeft += pdm.chip.OutputLeft();
                sumRight += pdm.chip.OutputRight();
                pdm.Run(1);
            }
            hifi.Run(128);
            ASSERT_EQ(sumLeft / 128, hifi.chip.OutputLeft()) << "envelope " << envelope << " window " << window;
            ASSERT_EQ(sumRight / 128, hifi.chip.OutputRight());
            ASSERT_EQ(sumLeft % 128, 0);
        }
    }
}

TEST(Saa1099_Test, TimeAxisRatioIsExact)
{
    Bench b(Saa1099RenderMode::HiFi, 3500000);
    uint64_t t = 0;
    uint32_t lcg = 7;
    for (int i = 0; i < 2000; i++)
    {
        lcg = lcg * 1664525u + 1013904223u;
        t += lcg % 5000;
        b.chip.Run(t);
        ASSERT_EQ(b.chip.ChipClocks(), t * 8000000ull / 3500000ull) << "t " << t;
    }
    // A timestamp that does not move forward advances nothing
    b.chip.Run(t - 100);
    EXPECT_EQ(b.chip.ChipClocks(), t * 8000000ull / 3500000ull);
}
