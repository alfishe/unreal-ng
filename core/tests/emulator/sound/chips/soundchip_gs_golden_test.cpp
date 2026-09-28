// Golden-reference fingerprints of the classic General Sound card (LLE).
//
// NeoGS phase 0 (neogs-tdd.md §5.3, §9) extracts the catch-up loop, the audio
// output stage and the module replay out of SoundChip_GeneralSound. That
// refactor must not change a single cycle: these tests pin the card's
// observable trajectory - every rendered sample, every traced event (with its
// card-cycle timestamp and PC), the complete TTD blob (CPU, timing, RAM) and
// the activity counters - as FNV-1a digests recorded on the pre-refactor code.
//
// Runtime justification: each scenario boots the real gs105a firmware (POST
// is ~1 s of emulated card time) and plays a module for 2 s; that is the only
// faithful workload for the interrupt/DAC path the refactor touches.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#include "emulator/emulatorcontext.h"
#include "emulator/platform.h"
#include "emulator/sound/audio.h"
#include "emulator/sound/chips/gs/soundchip_gs.h"

namespace
{
constexpr uint16_t kPortData = GeneralSoundCard::PORT_DATA;
constexpr uint16_t kPortCommand = GeneralSoundCard::PORT_COMMAND;
constexpr uint16_t kPortControl = GeneralSoundCard::PORT_CONTROL;

struct Fnv
{
    uint64_t h = 14695981039346656037ull;
    void byte(uint8_t b)
    {
        h ^= b;
        h *= 1099511628211ull;
    }
    void bytes(const void* p, size_t n)
    {
        const auto* b = static_cast<const uint8_t*>(p);
        for (size_t i = 0; i < n; i++)
            byte(b[i]);
    }
    template <typename T>
    void value(T v)
    {
        bytes(&v, sizeof(v));
    }
};

// Synthetic ProTracker module (same shape as the LW tests' module): one
// looped 64-byte square wave sample, two patterns, note on channel 0 row 0
std::vector<uint8_t> buildModule()
{
    std::vector<uint8_t> m(1084 + 2 * 1024 + 64, 0x00);
    m[20 + 22] = 0;
    m[20 + 23] = 32; // length 32 words
    m[20 + 25] = 63; // volume
    m[20 + 28] = 0;
    m[20 + 29] = 32; // loop length 32 words
    m[950] = 2;
    m[952] = 0;
    m[953] = 1;
    m[1080] = 'M';
    m[1081] = '.';
    m[1082] = 'K';
    m[1083] = '.';
    const size_t cell = 1084;
    m[cell + 0] = 0x01;
    m[cell + 1] = 0xAC;
    m[cell + 2] = 0x10;
    // a second note on channel 2 (right side) at row 8, pattern 1
    const size_t cell2 = 1084 + 1024 + 8 * 16 + 2 * 4;
    m[cell2 + 0] = 0x01;
    m[cell2 + 1] = 0x40;
    m[cell2 + 2] = 0x10;
    for (size_t i = 0; i < 64; i++)
        m[1084 + 2 * 1024 + i] = (i / 16) % 2 ? 0x30 : 0xB0;
    return m;
}

struct Harness
{
    EmulatorContext ctx;
    std::unique_ptr<SoundChip_GeneralSound> chip;
    Fnv audio;
    size_t nonSilentSamples = 0;

    explicit Harness(size_t ramKB)
        : ctx(LoggerLevel::LogError)
    {
        ctx.config.sound.gs_vol = 8000;
        ctx.config.frame = 69888;
        ctx.config.frame_duration_us = 19968;
        ctx.emulatorState.current_z80_frequency_multiplier = 1;
        ctx.emulatorState.hw_turbo_shift_applied = 0;
        chip = std::make_unique<SoundChip_GeneralSound>(&ctx, ramKB, 44100);
        chip->loadROM("rom/gs105a.rom");
    }

    void runFrames(int n)
    {
        for (int i = 0; i < n; i++)
        {
            chip->handleFrameStart();
            chip->handleFrameEnd(SAMPLES_PER_FRAME);
            const int16_t* samples = chip->getBuffer();
            audio.bytes(samples, SAMPLES_PER_FRAME * 2 * sizeof(int16_t));
            for (int s = 0; s < SAMPLES_PER_FRAME * 2; s++)
                nonSilentSamples += samples[s] != 0 ? 1 : 0;
        }
    }

    bool waitFlagClear(uint8_t mask, int maxFrames)
    {
        for (int i = 0; i < maxFrames; i++)
        {
            if (!(chip->readStatus() & mask))
                return true;
            runFrames(1);
        }
        return false;
    }

    bool bootToPost()
    {
        for (int i = 0; i < 1000; i++)
        {
            runFrames(1);
            if (chip->getActivityCounters().volumeLatchWrites >= 4)
            {
                if (chip->readStatus() & 0x80)
                    (void)chip->portDeviceInMethod(kPortData);
                return true;
            }
        }
        return false;
    }

    bool upload(const std::vector<uint8_t>& bytes)
    {
        chip->portDeviceOutMethod(kPortData, 0x01);
        chip->portDeviceOutMethod(kPortCommand, 0x30);
        if (!waitFlagClear(0x01, 1000))
            return false;
        runFrames(1);
        if (chip->portDeviceInMethod(kPortData) != 1)
            return false;
        for (uint8_t b : bytes)
        {
            chip->portDeviceOutMethod(kPortData, b);
            if (!waitFlagClear(0x80, 1000))
                return false;
        }
        chip->portDeviceOutMethod(kPortCommand, 0xD2);
        return waitFlagClear(0x01, 1000);
    }

    uint64_t stateDigest() const
    {
        std::vector<uint8_t> blob(chip->TTDStateSize());
        chip->TTDSaveState(blob.data());
        Fnv f;
        f.bytes(blob.data(), blob.size());
        return f.h;
    }

    uint64_t traceDigest() const
    {
        Fnv f;
        for (const GSTraceEvent& e : chip->getPortTraceEvents())
        {
            f.value(e.timestamp);
            f.value(e.frameNumber);
            f.value(e.port);
            f.value(e.pc);
            f.value(e.value);
            f.value(e.channel);
            f.value(static_cast<uint8_t>(e.side));
            f.value(e.flags);
        }
        f.value(chip->getPortTraceTotalProduced());
        return f.h;
    }

    uint64_t countersDigest() const
    {
        const GSActivityCounters& c = chip->getActivityCounters();
        Fnv f;
        f.value(c.cpuSteps);
        f.value(c.interruptPeriods);
        f.value(c.interruptsAccepted);
        f.value(c.interruptsCoalesced);
        f.value(c.nmisAccepted);
        f.value(c.dacFetches);
        f.value(c.lastDacFetchGsCycle);
        f.value(c.volumeLatchWrites);
        f.value(c.hostDataWritten);
        f.value(c.hostDataRead);
        f.value(c.hostCommandsReceived);
        return f.h;
    }
};

struct Digests
{
    uint64_t audio, trace, state, counters;
};

// When the pinned values need re-recording (a deliberate behaviour change),
// run with GS_GOLDEN_PRINT=1 and paste the printed values
void check(const char* name, const Digests& got, const Digests& want)
{
    if (std::getenv("GS_GOLDEN_PRINT"))
    {
        std::printf("%s: {0x%016llxull, 0x%016llxull, 0x%016llxull, 0x%016llxull}\n", name,
                    static_cast<unsigned long long>(got.audio), static_cast<unsigned long long>(got.trace),
                    static_cast<unsigned long long>(got.state), static_cast<unsigned long long>(got.counters));
        return;
    }
    EXPECT_EQ(got.audio, want.audio) << name << ": rendered audio changed";
    EXPECT_EQ(got.trace, want.trace) << name << ": event trace (timing/order) changed";
    EXPECT_EQ(got.state, want.state) << name << ": TTD state (CPU/timing/RAM) changed";
    EXPECT_EQ(got.counters, want.counters) << name << ": activity counters changed";
}
} // namespace

TEST(SoundChip_GeneralSound_Golden, BootUploadPlayResetNmi)
{
    Harness h(512);
    ASSERT_TRUE(h.chip->isROMLoaded());
    h.chip->startPortTrace();

    ASSERT_TRUE(h.bootToPost());
    ASSERT_TRUE(h.upload(buildModule()));
    h.chip->portDeviceOutMethod(kPortData, 0x00);
    h.chip->portDeviceOutMethod(kPortCommand, 0x31);
    ASSERT_TRUE(h.waitFlagClear(0x01, 1000));
    h.runFrames(100);

    // Volume query family and a stop/continue mid-play
    h.chip->portDeviceOutMethod(kPortCommand, 0x32);
    ASSERT_TRUE(h.waitFlagClear(0x01, 1000));
    h.runFrames(5);
    h.chip->portDeviceOutMethod(kPortCommand, 0x33);
    ASSERT_TRUE(h.waitFlagClear(0x01, 1000));
    h.runFrames(20);

    // NMI and a card reset (#33): the firmware reboots, POST runs again
    h.chip->portDeviceOutMethod(kPortControl, 0x40);
    h.runFrames(3);
    h.chip->portDeviceOutMethod(kPortControl, 0x80);
    h.runFrames(60);

    EXPECT_GT(h.nonSilentSamples, 10000u) << "the scenario must actually play";
    check("BootUploadPlayResetNmi", {h.audio.h, h.traceDigest(), h.stateDigest(), h.countersDigest()},
          {0xea0ddb3e02932de2ull, 0xa45e8dfaf7ee07cfull, 0xc2ce10e377b16e60ull, 0x520ff92dc262e2c5ull});
}

TEST(SoundChip_GeneralSound_Golden, ModuleReplayOntoFreshCard)
{
    // The personality-switch replay path (moved to GSModuleReplay in phase 0)
    Harness h(256);
    ASSERT_TRUE(h.chip->isROMLoaded());
    h.chip->startPortTrace();
    h.chip->replayModuleUpload(buildModule(), true);
    h.runFrames(50);

    EXPECT_GT(h.nonSilentSamples, 1000u) << "the replayed module must play";
    check("ModuleReplayOntoFreshCard", {h.audio.h, h.traceDigest(), h.stateDigest(), h.countersDigest()},
          {0xdcfb25d66fe02eb2ull, 0x9ce33a7772a420b8ull, 0xa8214a9045e28767ull, 0xe1b4c967605dc9afull});
}
