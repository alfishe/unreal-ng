// TEMPORARY triage harness for "GS mailbox alive but no playback" (do not
// ship as-is): boots the real gs105a firmware and drives the four links of
// the audio chain separately so the broken link is identifiable:
//   1. POST completion (RAM detect + INITVAR volume/DAC writes)
//   2. High-command mailbox round-trip (#23 = NUMPG) across RAM sizes
//   3. Covox streaming DAC output (#0E, interrupt-independent render path)
//   4. 37.5 kHz interrupt + DAC-fetch-from-handler path (#13 jump to a stub
//      that installs IM2/I=0x17 and reads #6000 from the handler)
//
// Firmware-source protocol notes (v105b sources, same dispatcher family as
// the gs105a ROM):
//   COM16 "put byte": the VALUE byte is consumed by the dispatch-time
//     IN A,(DATRG); WTDTL then drains addr lo and addr hi -> data first.
//   COM13 "jump": dispatch-time IN A,(DATRG) = addr lo, WTDTL = addr hi.
//   COM0E "covox": ack, then a tight loop IN A,(DATRG) -> LD (HL),A /
//     LD (BC),A into DAC0/DAC2, re-read both (the DAC latching fetches),
//     exit on a new command. No flag wait, no interrupts involved.
//   WTDTL: JP P tests the sign flag of AND #81 (IN A,(n) does not set
//     flags) - it aborts only when a command arrived with no data pending.
#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <vector>

#include "emulator/emulatorcontext.h"
#include "emulator/sound/audio.h"
#include "emulator/sound/chips/soundchip_gs.h"

namespace
{
constexpr uint16_t kPortData = SoundChip_GeneralSound::PORT_DATA;
constexpr uint16_t kPortCommand = SoundChip_GeneralSound::PORT_COMMAND;

struct GSHarness
{
    EmulatorContext ctx;
    std::unique_ptr<SoundChip_GeneralSound> chip;

    explicit GSHarness(size_t ramKB = 512)
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
        }
    }

    void dump(const char* tag)
    {
        const auto& c = chip->getActivityCounters();
        printf("%-22s PC=0x%04x I=0x%02x SP=0x%04x mpag=%u halted=%d steps=%llu int=%llu dac=%llu vol=%llu\n",
               tag, chip->getCPUReg(regPC), chip->getCPUReg(regI), chip->getCPUReg(regSP),
               chip->getMPAG(), (int)chip->isCPUHalted(), (unsigned long long)c.cpuSteps,
               (unsigned long long)c.interruptsAccepted, (unsigned long long)c.dacFetches,
               (unsigned long long)c.volumeLatchWrites);
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

    // ZX-side "SD" primitive: OUT #B3 then wait until the GS consumed the byte
    bool sendDataWait(uint8_t value)
    {
        chip->portDeviceOutMethod(kPortData, value);
        return waitFlagClear(0x80, 1000);
    }

    // ZX-side "SC" primitive: OUT #BB then wait for the RSCOM acknowledge
    bool sendCommandWait(uint8_t cmd)
    {
        chip->portDeviceOutMethod(kPortCommand, cmd);
        return waitFlagClear(0x01, 1000);
    }

    // GS->host byte: wait for bit7 then IN #B3 (clears bit7)
    int readGsByte(int maxFrames)
    {
        for (int i = 0; i < maxFrames; i++)
        {
            if (chip->readStatus() & 0x80)
                return chip->portDeviceInMethod(kPortData);
            runFrames(1);
        }
        return -1;
    }

    // COM16 (put byte): SD value (kept pending), SC #16 - the dispatch-time
    // IN A,(DATRG) consumes the value - then WTDTL drains lo and hi
    bool putByte(uint16_t addr, uint8_t value)
    {
        chip->portDeviceOutMethod(kPortData, value);
        if (!sendCommandWait(0x16))
            return false;
        return sendDataWait(addr & 0xFF) && sendDataWait(addr >> 8);
    }

    // COM13 (jump): SD lo (kept pending), SC #13, then SD hi via WTDTL
    bool jumpTo(uint16_t addr)
    {
        chip->portDeviceOutMethod(kPortData, addr & 0xFF);
        if (!sendCommandWait(0x13))
            return false;
        return sendDataWait(addr >> 8);
    }
};

bool bootToPost(GSHarness& h)
{
    for (int sec = 0; sec < 20; sec++)
    {
        h.runFrames(50);
        if (h.chip->getActivityCounters().volumeLatchWrites >= 4)
            return true;
    }
    return false;
}

// Peak of the chip's mixed frame buffer (what SoundManager would consume).
// Must be read in the same frame the audio happens: blip_buf holds the DC
// level, but a later idle frame's read yields only the (silent) remainder.
int bufferPeak(SoundChip_GeneralSound& chip)
{
    const int16_t* samples = chip.getBuffer();
    int peak = 0;
    for (int i = 0; i < 2 * SAMPLES_PER_FRAME; i++)
        peak = std::max(peak, std::abs(static_cast<int>(samples[i])));
    return peak;
}
// Patch one byte of the fixed window (window 1 = RAM page 3) via the TTD
// state blob - used after the COM13 jump, where the firmware dispatcher is
// gone and COM16 can no longer write memory
void patchFixedRam(SoundChip_GeneralSound& chip, uint16_t addr, uint8_t value)
{
    std::vector<uint8_t> blob(chip.TTDStateSize());
    chip.TTDSaveState(blob.data());
    const size_t ramOffset = chip.TTDStateSize() - chip.getRamSizeKB() * 1024;
    blob[ramOffset + 3 * SoundChip_GeneralSound::PAGE_SIZE + (addr - 0x4000)] = value;
    chip.TTDLoadState(blob.data());
}
} // namespace

TEST(SoundChip_GeneralSound_BootDiag, 1_PostCompletes)
{
    GSHarness h;
    ASSERT_TRUE(h.chip->isROMLoaded());
    ASSERT_TRUE(bootToPost(h)) << "INITVAR never ran within 20 emulated seconds";

    h.dump("POST done");
    // I must point at the ROM IM2 vector table (0x1700, INTTAB)
    EXPECT_EQ(h.chip->getCPUReg(regI), 0x17);
    // COMINT_ poll loop lives in the low ROM
    EXPECT_LT(h.chip->getCPUReg(regPC), 0x2000);
    EXPECT_FALSE(h.chip->isCPUHalted());
}

TEST(SoundChip_GeneralSound_BootDiag, 2_HighCommandRoundTrip)
{
    // COM23 replies NUMPG (COM_H.a80: "LD A,(NUMPG); OUT (OUTRG),A; ...").
    // v105b's INIT_L increments E once per verified RAM pair, so NUMPG is
    // either the pair count or pairs-1 depending on the gs105a (v1.04)
    // build - calibrate across sizes to distinguish:
    //   count semantics {4,8,16} / pairs-1 {3,7,15} / broken pair16 {4,8,15}
    static const size_t kSizes[] = {128, 256, 512};
    for (size_t ramKB : kSizes)
    {
        GSHarness h(ramKB);
        ASSERT_TRUE(h.chip->isROMLoaded());
        ASSERT_TRUE(bootToPost(h)) << ramKB << " KB: POST never completed";

        ASSERT_TRUE(h.sendCommandWait(0x23)) << ramKB << " KB: no ack for #23";
        const int pages = h.readGsByte(1000);
        printf("COM23 NUMPG with %3zu KB RAM: %d\n", ramKB, pages);
        EXPECT_GT(pages, 0) << ramKB << " KB: RAM detection replied nothing";
    }
}

TEST(SoundChip_GeneralSound_BootDiag, 3_CovoxStreamProducesAudio)
{
    GSHarness h;
    ASSERT_TRUE(h.chip->isROMLoaded());
    ASSERT_TRUE(bootToPost(h));

    // COM0E (covox mode): stream bytes; the firmware loop latches each one
    // into channels 0+2 via DAC-window reads. Exit by sending a command.
    ASSERT_TRUE(h.sendCommandWait(0x0E)) << "GS never acknowledged command #0E";
    const uint64_t dacBefore = h.chip->getActivityCounters().dacFetches;

    // 0x80 (silence) for 16 frames, then a square wave between 0xD0/0x30
    int peak = 0;
    for (int i = 0; i < 2000; i++)
    {
        const uint8_t sample = static_cast<uint8_t>(0x80 + 80 * ((i / 16) % 2));
        h.chip->portDeviceOutMethod(kPortData, sample);
        h.runFrames(1);
        peak = std::max(peak, bufferPeak(*h.chip));
    }
    h.dump("after covox");
    printf("covox: in-stream peak=%d ch=[%02x %02x %02x %02x] vol=[%02x %02x %02x %02x]\n",
           peak, h.chip->getChannelSample(0), h.chip->getChannelSample(1),
           h.chip->getChannelSample(2), h.chip->getChannelSample(3),
           h.chip->getChannelVolume(0), h.chip->getChannelVolume(1),
           h.chip->getChannelVolume(2), h.chip->getChannelVolume(3));

    EXPECT_GT(h.chip->getActivityCounters().dacFetches, dacBefore + 1000000)
        << "covox streaming produced no DAC fetches";
    EXPECT_GT(peak, 1000) << "DAC activity never reached the mixed frame buffer";
}

TEST(SoundChip_GeneralSound_BootDiag, 4_InterruptPathDacFetch)
{
    GSHarness h;
    ASSERT_TRUE(h.chip->isROMLoaded());
    ASSERT_TRUE(bootToPost(h));

    // Handler at #4040 (IM2 target): LD A,(#6000) [DAC fetch]; EI; RET
    static const uint8_t kHandler[] = {0x3A, 0x00, 0x60, 0xFB, 0xC9};
    for (size_t i = 0; i < sizeof(kHandler); i++)
        ASSERT_TRUE(h.putByte(0x4040 + i, kHandler[i])) << "putByte #" << i;

    // Stub at #5000: DI; IM 2; LD A,#17; LD I,A; LD SP,#4400; EI; JR self
    static const uint8_t kStub[] = {0xF3, 0xED, 0x5E, 0x3E, 0x17, 0xED, 0x47,
                                    0x31, 0x00, 0x44, 0xFB, 0x18, 0xFE};
    for (size_t i = 0; i < sizeof(kStub); i++)
        ASSERT_TRUE(h.putByte(0x5000 + i, kStub[i])) << "putByte #" << i;

    // COM13 (jump to address): lo at dispatch time, hi via WTDTL
    ASSERT_TRUE(h.jumpTo(0x5000));

    // The handler reads a constant #6000 byte, and a constant level means
    // zero deltas (blip is delta-based - DC is silence). Drive the sampled
    // value from the test side: patch #6000 per frame and expect the
    // interrupt handler to render the square wave (0x30/0xD0 on ch0).
    int peak = 0;
    for (int i = 0; i < 10; i++)
    {
        patchFixedRam(*h.chip, 0x6000, static_cast<uint8_t>(0x80 + 80 * (i % 2)));
        h.runFrames(1);
        peak = std::max(peak, bufferPeak(*h.chip));
    }
    h.dump("after jump+10f");

    EXPECT_EQ(h.chip->getCPUReg(regI), 0x17) << "stub never ran (I unchanged)";
    // The stub self-loop is the JR instruction itself (#500B: JR #500B)
    EXPECT_EQ(h.chip->getCPUReg(regPC), 0x500B) << "stub self-loop not observed";

    const uint64_t interrupts = h.chip->getActivityCounters().interruptsAccepted;
    const uint64_t dacFetches = h.chip->getActivityCounters().dacFetches;
    EXPECT_GT(interrupts, 1000) << "37.5 kHz interrupts not accepted with IFF1=1 + IM2";
    EXPECT_GT(dacFetches, 1000) << "interrupt handler DAC reads never reached the DAC hook";
    EXPECT_GT(peak, 100) << "handler DAC fetches produced no samples";
    printf("interrupt path: interrupts=%llu dacFetches=%llu framePeak=%d\n",
           (unsigned long long)interrupts, (unsigned long long)dacFetches, peak);
}

TEST(SoundChip_GeneralSound_BootDiag, 5_InterruptLevelHoldNoLoss)
{
    // Root-cause regression for the 2026-09-20 pitch-drift investigation:
    // the 37.5 kHz INT is level-held (design §2.4, Xpeccy intrq |= Z80_INT)
    // - a boundary request arriving while IFF1=0 (firmware ISR, QTDONE
    // re-programming, DI stretches) stays asserted until the CPU can accept
    // it. The pulse model dropped such requests (740-767 accepted per 768
    // boundaries during module playback): every lost request is one missing
    // sample step, i.e. instantaneous replay-rate wobble - steady tones
    // floated by several cents before this fix.
    GSHarness h;
    ASSERT_TRUE(h.chip->isROMLoaded());
    ASSERT_TRUE(bootToPost(h));

    // Handler at #4040 (IM2 target): LD A,(#6000); EI; RET
    static const uint8_t kHandler[] = {0x3A, 0x00, 0x60, 0xFB, 0xC9};
    for (size_t i = 0; i < sizeof(kHandler); i++)
        ASSERT_TRUE(h.putByte(0x4040 + i, kHandler[i])) << "putByte #" << i;

    // Stub A (#5000): classic park - DI; IM 2; LD A,#17; LD I,A; LD SP,#4400;
    // EI; JR self. Measures the fully-unmasked cadence.
    static const uint8_t kStubA[] = {0xF3, 0xED, 0x5E, 0x3E, 0x17, 0xED, 0x47,
                                     0x31, 0x00, 0x44, 0xFB, 0x18, 0xFE};
    for (size_t i = 0; i < sizeof(kStubA); i++)
        ASSERT_TRUE(h.putByte(0x5000 + i, kStubA[i])) << "putByte #" << i;

    // Stub B (#5100): same init, then an endless loop with a ~193-cycle DI
    // window every ~398 cycles (48% duty). Each window is shorter than the
    // 320-cycle quantum, so a boundary landing inside it must still be
    // delivered right after the EI; the loop period is not a multiple of
    // 320, so the phase drifts through every alignment.
    static const uint8_t kStubB[] = {
        0xF3, 0xED, 0x5E, 0x3E, 0x17, 0xED, 0x47, 0x31, 0x00, 0x44, 0xFB, // init
        0x06, 0x0E, // loop: LD B,14
        0x10, 0xFE, // dj1: DJNZ dj1
        0xF3,       // DI
        0x06, 0x0E, // LD B,14
        0x10, 0xFE, // dj2: DJNZ dj2
        0xFB,       // EI
        0x18, 0xF4, // JR loop (#510B)
    };
    for (size_t i = 0; i < sizeof(kStubB); i++)
        ASSERT_TRUE(h.putByte(0x5100 + i, kStubB[i])) << "putByte #" << i;

    ASSERT_TRUE(h.jumpTo(0x5000));

    // GS cycles per harness frame: 69888 ZX tacts * 12 MHz / 3.5 MHz = 239616
    // (exactly 748.8 interrupt boundaries per frame). Over 25 frames the
    // boundary count is 25*239616/320 = 18720; the tolerance of 1 covers a
    // boundary deferred across a frame edge when the window ends exactly on
    // a quantum edge. The pulse model lost ~48% of the boundaries in B.
    constexpr uint64_t kExpected = 25ULL * 239616 / 320;

    h.runFrames(1); // settle into the self-loop
    uint64_t before = h.chip->getActivityCounters().interruptsAccepted;
    h.runFrames(25);
    uint64_t delta = h.chip->getActivityCounters().interruptsAccepted - before;
    printf("level-hold A (EI park): %llu interrupts in 25 frames (expected ~%llu)\n",
           (unsigned long long)delta, (unsigned long long)kExpected);
    EXPECT_NEAR(static_cast<int64_t>(delta), static_cast<int64_t>(kExpected), 1)
        << "unmasked cadence: interrupt requests lost";

    // Switch to the masking stub by patching PC through the TTD blob (the
    // mailbox dispatcher is gone after the COM13 jump). Z80 pc lives at
    // fixed-state offset 24+22.
    std::vector<uint8_t> blob(h.chip->TTDStateSize());
    h.chip->TTDSaveState(blob.data());
    blob[24 + 22] = 0x00; // pc <- #5100
    blob[24 + 23] = 0x51;
    h.chip->TTDLoadState(blob.data());

    h.runFrames(1); // settle into the masked loop
    before = h.chip->getActivityCounters().interruptsAccepted;
    h.runFrames(25);
    delta = h.chip->getActivityCounters().interruptsAccepted - before;
    printf("level-hold B (48%% DI duty): %llu interrupts in 25 frames (expected ~%llu)\n",
           (unsigned long long)delta, (unsigned long long)kExpected);
    EXPECT_NEAR(static_cast<int64_t>(delta), static_cast<int64_t>(kExpected), 1)
        << "masked-window cadence: interrupt requests lost";
}
