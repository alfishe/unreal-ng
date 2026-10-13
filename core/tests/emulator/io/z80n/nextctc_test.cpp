// The Next's CTC (core/src/emulator/io/z80n/nextctc.h): the per-instruction Advance returns at once while no zero count and no
// ZC/TO pulse is due. That gate must not change anything the machine can see

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <vector>

#include "emulator/io/z80n/nextctc.h"

// Two CTCs get the same random programming and the same clock; one walks its channels on every Advance, the other uses the
// "nothing due before" gate. Counts, zero counts, run states and the order of interrupts and ZC/TO pulses are compared after
// every step
TEST(NextCtc_Test, EventGateMatchesTheChannelWalkOnEveryAdvance)
{
    NextCtc reference, gated;
    reference.SetEventGate(false);
    struct Event
    {
        unsigned step, channel;
        bool interrupt;
        bool operator==(const Event&) const = default;
    };
    std::vector<Event> referenceEvents, gatedEvents;
    unsigned step = 0;
    reference.onInterrupt = [&](unsigned channel) { referenceEvents.push_back({step, channel, true}); };
    reference.onZeroCount = [&](unsigned channel) { referenceEvents.push_back({step, channel, false}); };
    gated.onInterrupt = [&](unsigned channel) { gatedEvents.push_back({step, channel, true}); };
    gated.onZeroCount = [&](unsigned channel) { gatedEvents.push_back({step, channel, false}); };

    uint32_t seed = 0xC7C0001u;
    auto next = [&]() {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        return seed;
    };
    uint64_t now = 0;
    for (step = 0; step < 60000; step++)
    {
        now += 32 + next() % 160;  // an instruction at 3.5 MHz in 28 MHz clocks
        const unsigned action = next() % 400;
        const unsigned channel = next() % NextCtc::kChannels;
        if (action < 6)
        {
            // a control word: timers and counters, both prescalers, triggers, with and without a time constant
            const uint8_t control = static_cast<uint8_t>((next() & 0xFC) | NextCtc::kControl | (next() % 4 == 0 ? NextCtc::kSoftReset : 0));
            reference.Write(channel, control, now);
            gated.Write(channel, control, now);
            if (control & NextCtc::kTimeConstantFollows)
            {
                const uint8_t tc = static_cast<uint8_t>(next() % 3 == 0 ? 1 + next() % 4 : next());
                reference.Write(channel, tc, now);
                gated.Write(channel, tc, now);
            }
        }
        else if (action == 6)
        {
            reference.Trigger(channel, now);
            gated.Trigger(channel, now);
        }
        else if (action == 7)
        {
            const uint8_t bits = static_cast<uint8_t>(next() & 0x0F);
            reference.SetInterruptEnables(bits);
            gated.SetInterruptEnables(bits);
        }
        else if (action == 8 && next() % 16 == 0)
        {
            reference.Reset();
            gated.Reset();
        }
        reference.Advance(now);
        gated.Advance(now);
        if (action < 40 || step % 64 == 0)
        {
            for (unsigned c = 0; c < NextCtc::kChannels; c++)
            {
                ASSERT_EQ(gated.Read(c, now), reference.Read(c, now)) << "step " << step << " channel " << c;
                ASSERT_EQ(gated.Zeros(c), reference.Zeros(c)) << "step " << step << " channel " << c;
                ASSERT_EQ(gated.Running(c), reference.Running(c)) << "step " << step << " channel " << c;
                ASSERT_EQ(gated.Control(c), reference.Control(c)) << "step " << step << " channel " << c;
            }
        }
        ASSERT_EQ(gatedEvents.size(), referenceEvents.size()) << "step " << step;
    }
    EXPECT_EQ(gatedEvents, referenceEvents);
    EXPECT_GT(referenceEvents.size(), 1000u) << "the script made the timers run";
}
