// Classic General Sound idle sleep (GSCardRunner): after boot the gs105a firmware polls FLAGS with interrupts off
// (COMINT_ at #026E); the runner sleeps through that loop and runs the 37.5 kHz period boundaries in one go, as if
// the CPU had polled all along. Two cards in lockstep, one sleeping and one stepping every instruction, get the same
// host traffic at random moments: the whole card state (TTD blob: CPU registers with R, RAM, timing, mailbox,
// DAC latches), the activity counters and the audio must stay equal after every action.
//
// Runtime justification: the real firmware boots (POST is ~1 s of card time) and plays a module; the stepping card
// runs every poll-loop instruction. That is the only faithful check that sleeping changes nothing.

#include <gtest/gtest.h>

#include <cstring>
#include <iterator>
#include <memory>
#include <random>
#include <vector>

#include "emulator/emulatorcontext.h"
#include "emulator/sound/audio.h"
#include "emulator/sound/chips/gs/soundchip_gs.h"

namespace
{
constexpr uint16_t kPortData = GeneralSoundCard::PORT_DATA;
constexpr uint16_t kPortCommand = GeneralSoundCard::PORT_COMMAND;

struct SleepCard
{
    EmulatorContext ctx{LoggerLevel::LogError};
    std::unique_ptr<SoundChip_GeneralSound> chip;
    uint64_t audioHash = 14695981039346656037ull;

    explicit SleepCard(bool sleep)
    {
        ctx.config.sound.gs_vol = 8000;
        ctx.config.frame = 69888;
        ctx.config.frame_duration_us = 19968;
        ctx.emulatorState.current_z80_frequency_multiplier = 1;
        ctx.emulatorState.hw_turbo_ratio_applied = 1;
        chip = std::make_unique<SoundChip_GeneralSound>(&ctx, 512, 44100);
        chip->loadROM("rom/gs105a.rom");
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

    std::vector<uint8_t> blob() const
    {
        std::vector<uint8_t> b(chip->TTDStateSize());
        chip->TTDSaveState(b.data());
        return b;
    }
};

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
        const SoundChip_GeneralSound& a = *sleeping.chip;
        const SoundChip_GeneralSound& b = *stepping.chip;
        if (a.cardUnits() != b.cardUnits())
            return ::testing::AssertionFailure() << "card time " << a.cardUnits() << " vs " << b.cardUnits() << " after action " << actions;
        const GSActivityCounters& ca = a.getActivityCounters();
        const GSActivityCounters& cb = b.getActivityCounters();
        if (ca.cpuSteps != cb.cpuSteps || ca.interruptPeriods != cb.interruptPeriods ||
            ca.interruptsAccepted != cb.interruptsAccepted || ca.interruptsCoalesced != cb.interruptsCoalesced ||
            ca.dacFetches != cb.dacFetches || ca.volumeLatchWrites != cb.volumeLatchWrites ||
            ca.lastDacFetchGsCycle != cb.lastDacFetchGsCycle)
            return ::testing::AssertionFailure() << "activity counters differ after action " << actions << " (steps "
                                                 << ca.cpuSteps << " vs " << cb.cpuSteps << ")";
        if (sleeping.blob() != stepping.blob())
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
            const int64_t step = 20 + rng() % 1200;
            both([&](SleepCard& c) { c.chip->runFor(step); });
        }
        return false;
    }

    void boot()
    {
        for (int i = 0; i < 200 && sleeping.chip->getActivityCounters().volumeLatchWrites < 4; i++)
            both([](SleepCard& c) { c.frame(); });
        both([](SleepCard& c) {
            c.frame();
            if (c.chip->readStatus() & 0x80)
                (void)c.chip->portDeviceInMethod(kPortData);
        });
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

TEST(SoundChip_GS_IdleSleep, BootAndRandomHostTrafficMatchTheSteppingCard)
{
    Lockstep l;
    std::mt19937 rng(20261012);
    l.boot();
    ASSERT_TRUE(l.equal());
    ASSERT_GE(l.sleeping.chip->getActivityCounters().volumeLatchWrites, 4u);

    // Commands, data, status and data reads, NMIs and runs of random length, frames in between
    const uint8_t commands[] = {0x20, 0x21, 0x22, 0x23, 0x2A, 0x2E, 0xF3, 0x00, 0x40, 0x41};
    for (int i = 0; i < 400; i++)
    {
        switch (rng() % 7)
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
            case 4:
                l.both([](SleepCard& c) { c.frame(); });
                break;
            default:
            {
                const int64_t units = 1 + rng() % 40000;
                l.both([&](SleepCard& c) { c.chip->runFor(units); });
                break;
            }
        }
        ASSERT_TRUE(l.equal());
    }
    EXPECT_GT(l.sleeping.chip->stepsSlept(), 0u) << "the poll loop was slept through";
    EXPECT_EQ(l.stepping.chip->stepsSlept(), 0u);
}

TEST(SoundChip_GS_IdleSleep, ModulePlaybackAndStopMatchTheSteppingCard)
{
    Lockstep l;
    std::mt19937 rng(12102026);
    l.boot();
    ASSERT_TRUE(l.equal());

    // Upload (COM30), play (COM31), then stop (COM32) and fall idle again
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

    l.both([](SleepCard& c) { c.chip->portDeviceOutMethod(kPortCommand, 0x31); });
    for (int i = 0; i < 40; i++)
    {
        l.both([](SleepCard& c) { c.frame(); });
        ASSERT_TRUE(l.equal());
    }
    const uint64_t sleptBeforeStop = l.sleeping.chip->stepsSlept();
    l.both([](SleepCard& c) { c.chip->portDeviceOutMethod(kPortCommand, 0x32); });
    for (int i = 0; i < 40; i++)
    {
        l.both([](SleepCard& c) { c.frame(); });
        ASSERT_TRUE(l.equal());
    }
    EXPECT_GT(l.sleeping.chip->stepsSlept(), sleptBeforeStop) << "the card sleeps again once the music stops";
}
