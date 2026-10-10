// NeoGS idle sleep (GSCardRunner): the card's firmware polls its mailbox in a short loop; the runner sleeps through
// it and wakes at the host's access, its frame end or a card event as if the CPU had polled all along. Two cards in
// lockstep, one sleeping and one stepping every instruction, get the same host traffic at random moments: the
// whole card state (TTD hash: CPU registers with R and T, RAM, timers, DAC, mailbox), the step count and the audio
// must stay equal after every action. The card is the same on every machine (the host side is only its ports and
// its frame clock), so a standalone card covers the Pentagon, the Sprinter and the rest alike.
//
// Runtime justification: the real flash image boots (~10 frames of card time at 20 MHz) and plays a module; the
// stepping card runs every poll-loop instruction. That is the only faithful check that sleeping changes nothing.

#include <gtest/gtest.h>

#include <cstring>
#include <iterator>
#include <memory>
#include <random>
#include <vector>

#include "emulator/emulatorcontext.h"
#include "emulator/sound/audio.h"
#include "emulator/sound/chips/neogs/soundchip_neogs.h"

namespace
{
constexpr uint16_t kPortData = GeneralSoundCard::PORT_DATA;
constexpr uint16_t kPortCommand = GeneralSoundCard::PORT_COMMAND;

struct SleepCard
{
    EmulatorContext ctx{LoggerLevel::LogError};
    NeoGSConfig config;
    std::unique_ptr<SoundChip_NeoGS> chip;
    uint64_t audioHash = 14695981039346656037ull;

    explicit SleepCard(bool sleep)
    {
        ctx.config.frame = 69888;
        ctx.config.frame_duration_us = 19968;
        ctx.emulatorState.current_z80_frequency_multiplier = 1;
        ctx.emulatorState.hw_turbo_ratio_applied = 1;
        chip = std::make_unique<SoundChip_NeoGS>(&ctx, config, 44100);
        chip->loadROM("rom/neogs/full_ngs.rom");
        chip->setIdleSleep(sleep);
    }

    void frame()
    {
        chip->handleFrameStart();
        chip->handleFrameEnd(SAMPLES_PER_FRAME);
        const int16_t* samples = chip->getBuffer();
        for (int s = 0; s < SAMPLES_PER_FRAME * 2; s++)
        {
            audioHash ^= static_cast<uint16_t>(samples[s]);
            audioHash *= 1099511628211ull;
        }
    }
};

/// The two cards: every action goes to both, then their states are compared
struct Lockstep
{
    SleepCard sleeping{true};
    SleepCard stepping{false};
    int actions = 0;

    template <typename F>
    void both(F&& f)
    {
        f(sleeping);
        f(stepping);
        actions++;
    }

    ::testing::AssertionResult equal() const
    {
        const SoundChip_NeoGS& a = *sleeping.chip;
        const SoundChip_NeoGS& b = *stepping.chip;
        if (a.cardTicks() != b.cardTicks())
            return ::testing::AssertionFailure() << "card time " << a.cardTicks() << " vs " << b.cardTicks() << " after action " << actions;
        if (a.getActivityCounters().cpuSteps != b.getActivityCounters().cpuSteps)
            return ::testing::AssertionFailure() << "CPU steps " << a.getActivityCounters().cpuSteps << " vs "
                                                 << b.getActivityCounters().cpuSteps << " after action " << actions;
        if (a.TTDHashState() != b.TTDHashState())
            return ::testing::AssertionFailure() << "card state differs after action " << actions << " (PC "
                                                 << a.getCPUReg(GSCpuRegister::PC) << " vs " << b.getCPUReg(GSCpuRegister::PC) << ")";
        if (sleeping.audioHash != stepping.audioHash)
            return ::testing::AssertionFailure() << "audio differs after action " << actions;
        return ::testing::AssertionSuccess();
    }

    /// Host-side wait for a mailbox flag to clear, polling in steps of random length (the same for both cards)
    bool waitFlagClear(std::mt19937& rng, uint8_t mask)
    {
        for (int i = 0; i < 20000; i++)
        {
            const uint8_t s = sleeping.chip->readStatus();
            const uint8_t t = stepping.chip->readStatus();
            actions++;
            if (s != t)
                return false;
            if (!(s & mask))
                return true;
            const int64_t step = 200 + rng() % 12000;
            both([&](SleepCard& c) { c.chip->runFor(step); });
        }
        return false;
    }
};

std::vector<uint8_t> buildModule()
{
    std::vector<uint8_t> m(1084 + 2 * 1024 + 64, 0x00);
    m[20 + 23] = 32;
    m[20 + 25] = 63;
    m[20 + 29] = 32;
    m[950] = 2;
    m[953] = 1;
    memcpy(&m[1080], "M.K.", 4);
    m[1084 + 0] = 0x01;
    m[1084 + 1] = 0xAC;
    m[1084 + 2] = 0x10;
    for (size_t i = 0; i < 64; i++)
        m[1084 + 2 * 1024 + i] = (i / 16) % 2 ? 0x30 : 0xB0;
    return m;
}
}  // namespace

TEST(SoundChip_NeoGS_IdleSleep, BootAndRandomHostTrafficMatchTheSteppingCard)
{
    Lockstep l;
    std::mt19937 rng(20261009);
    for (int i = 0; i < 12 && !l.sleeping.chip->isReadyForCommands(); i++)
    {
        l.both([](SleepCard& c) { c.frame(); });
        ASSERT_TRUE(l.equal());
    }
    ASSERT_TRUE(l.sleeping.chip->isReadyForCommands());

    // Commands, data, status and data reads and runs of random length, frames in between
    const uint8_t commands[] = {0x23, 0x20, 0x21, 0x2A, 0x2E, 0x1D, 0x00};
    for (int i = 0; i < 400; i++)
    {
        switch (rng() % 6)
        {
            case 0:
            {
                const uint8_t command = commands[rng() % std::size(commands)];
                l.both([&](SleepCard& c) { c.chip->portDeviceOutMethod(kPortCommand, command); });
                break;
            }
            case 1:
            {
                const uint8_t data = static_cast<uint8_t>(rng());
                l.both([&](SleepCard& c) { c.chip->portDeviceOutMethod(kPortData, data); });
                break;
            }
            case 2:
                l.both([](SleepCard& c) { (void)c.chip->portDeviceInMethod(kPortData); });
                break;
            case 3:
                l.both([](SleepCard& c) { c.frame(); });
                break;
            default:
            {
                const int64_t ticks = 1 + rng() % 400000;
                l.both([&](SleepCard& c) { c.chip->runFor(ticks); });
                break;
            }
        }
        ASSERT_TRUE(l.equal());
    }
    EXPECT_GT(l.sleeping.chip->stepsSlept(), 0u) << "the poll loop was slept through";
    EXPECT_EQ(l.stepping.chip->stepsSlept(), 0u);
}

TEST(SoundChip_NeoGS_IdleSleep, ModulePlaybackMatchesTheSteppingCard)
{
    Lockstep l;
    std::mt19937 rng(9102026);
    for (int i = 0; i < 12 && !l.sleeping.chip->isReadyForCommands(); i++)
        l.both([](SleepCard& c) { c.frame(); });
    ASSERT_TRUE(l.equal());
    l.both([](SleepCard& c) {
        c.frame();
        if (c.chip->readStatus() & 0x80)
            (void)c.chip->portDeviceInMethod(kPortData);  // the boot NUMPG reply
    });

    // Upload (COM30), play (COM31): the host polls the flags in steps of random length
    l.both([](SleepCard& c) {
        c.chip->portDeviceOutMethod(kPortData, 0x01);
        c.chip->portDeviceOutMethod(kPortCommand, 0x30);
    });
    ASSERT_TRUE(l.waitFlagClear(rng, 0x01));
    l.both([](SleepCard& c) {
        c.frame();
        (void)c.chip->portDeviceInMethod(kPortData);
    });
    for (const uint8_t b : buildModule())
    {
        l.both([&](SleepCard& c) { c.chip->portDeviceOutMethod(kPortData, b); });
        ASSERT_TRUE(l.waitFlagClear(rng, 0x80));
    }
    l.both([](SleepCard& c) { c.chip->portDeviceOutMethod(kPortCommand, 0xD2); });
    ASSERT_TRUE(l.waitFlagClear(rng, 0x01));
    ASSERT_TRUE(l.equal());
    l.both([](SleepCard& c) {
        c.chip->portDeviceOutMethod(kPortData, 0x00);
        c.chip->portDeviceOutMethod(kPortCommand, 0x31);
    });
    ASSERT_TRUE(l.waitFlagClear(rng, 0x01));

    // Playing: frames, with status polls at random points inside them
    for (int i = 0; i < 40; i++)
    {
        const int64_t ticks = 1 + rng() % 2000000;
        l.both([&](SleepCard& c) {
            c.chip->runFor(ticks);
            (void)c.chip->readStatus();
            c.frame();
        });
        ASSERT_TRUE(l.equal()) << "frame " << i;
    }
    EXPECT_GT(l.sleeping.chip->getActivityCounters().interruptsAccepted, 20000u) << "the module plays";
}
