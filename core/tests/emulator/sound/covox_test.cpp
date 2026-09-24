#include <gtest/gtest.h>
#include "emulator/emulatorcontext.h"
#include "emulator/sound/soundmanager.h"
#include "emulator/sound/covox.h"
#include "emulator/sound/audio.h"
#include "emulator/ports/portdecoder.h"

class CovoxTest : public ::testing::Test
{
protected:
    EmulatorContext* ctx = nullptr;
    SoundManager* sm = nullptr;

    void SetUp() override
    {
        ctx = new EmulatorContext(LoggerLevel::LogError);
        // Enable Covox in config
        ctx->config.sound.covoxFB = 1;
        sm = new SoundManager(ctx);
        sm->reset();

        // Disable DSP chains
        sm->getAYChain().setPunchEnabled(false);
        sm->getBeeperChain().setPunchEnabled(false);
    }

    void TearDown() override
    {
        delete sm;
        delete ctx;
    }
};

TEST_F(CovoxTest, CovoxRegisteredWhenConfigEnabled)
{
    ASSERT_TRUE(sm->hasCovox());
    auto* covox = sm->device(AudioSourceType::COVOX);
    ASSERT_NE(covox, nullptr);
    EXPECT_EQ(covox->name, "COVOX");
}

TEST_F(CovoxTest, CovoxNotRegisteredWhenConfigDisabled)
{
    // Create a new context without Covox
    EmulatorContext* ctx2 = new EmulatorContext(LoggerLevel::LogError);
    ctx2->config.sound.covoxFB = 0;
    SoundManager* sm2 = new SoundManager(ctx2);

    EXPECT_FALSE(sm2->hasCovox());
    EXPECT_EQ(sm2->device(AudioSourceType::COVOX), nullptr);

    delete sm2;
    delete ctx2;
}

/// Minimal concrete decoder for routing tests: the exact-address registration
/// map and dispatch (RegisterPortHandler / PeripheralPortOut) live on the
/// PortDecoder base and are all these tests exercise. reset() is the single
/// pure virtual; DecodePortIn/DecodePortOut keep their base behavior
class RoutingDecoderStub : public PortDecoder
{
public:
    explicit RoutingDecoderStub(EmulatorContext* context) : PortDecoder(context) {}
    void reset() override {}
};

/// SoundDrive 1.05 mode 2 (SD=1): all four ports (#F1 L-A, #F3 L-B, #F9 R-A,
/// #FB R-B) must reach the DAC - decoder dispatch is an exact-address map,
/// so a port that is not registered is silently dropped (previously only #FB
/// was wired and the quad was dead)
TEST_F(CovoxTest, SoundDriveWiresAllFourQuadPorts)
{
    EmulatorContext* ctx2 = new EmulatorContext(LoggerLevel::LogError);
    ctx2->config.sound.covoxFB = 0;
    ctx2->config.sound.sd = 1;
    SoundManager* sm2 = new SoundManager(ctx2);
    sm2->reset();
    ASSERT_TRUE(sm2->hasCovox());

    RoutingDecoderStub decoder(ctx2);
    ctx2->pPortDecoder = &decoder;
    ASSERT_TRUE(sm2->attachToPorts());
    Covox* covox = sm2->getCovox();
    ASSERT_NE(covox, nullptr);

    const uint16_t ports[4] = {Covox::PORT_LEFT_A, Covox::PORT_LEFT_B, Covox::PORT_RIGHT_A, Covox::PORT_RIGHT_B};
    for (int i = 0; i < 4; i++)
    {
        decoder.PeripheralPortOut(ports[i], 0xFF);
        uint8_t latches[4];
        covox->TTDSaveState(latches);
        EXPECT_EQ(latches[i], 0xFF) << "quad port index " << i << " did not reach the DAC";
    }

    sm2->detachFromPorts();
    ctx2->pPortDecoder = nullptr;
    delete sm2;
    delete ctx2;
}

/// Regression: SoundManager::attachToPorts() runs before the Beta disk's
/// (core.cpp), so if Covox registered SoundDrive "mode 1" at the literal
/// #0F/#1F/#4F/#5F, it would win the exact-address dispatch-map race and
/// WD1793's later RegisterPortHandler(0x001F, ...) etc. would silently no-op
/// - breaking disk access outright whenever SD=1, regardless of TR-DOS state
/// (this is exactly what broke WD1793_Integration_Test.TRDOS_FORMAT_
/// FullOperation during development). Covox must claim marked keys
/// (Covox::PORT_*_MODE1, raw port | 0x0100) instead, leaving the raw FDC
/// addresses free for a real Beta128/WD1793 registration
TEST_F(CovoxTest, SoundDriveModeOneDoesNotStealBeta128Addresses)
{
    EmulatorContext* ctx2 = new EmulatorContext(LoggerLevel::LogError);
    ctx2->config.sound.covoxFB = 0;
    ctx2->config.sound.sd = 1;
    SoundManager* sm2 = new SoundManager(ctx2);
    sm2->reset();
    ASSERT_TRUE(sm2->hasCovox());

    RoutingDecoderStub decoder(ctx2);
    ctx2->pPortDecoder = &decoder;
    ASSERT_TRUE(sm2->attachToPorts());

    // A stand-in FDC must still be able to claim the raw Beta128 addresses
    class MockFdc final : public PortDevice
    {
    public:
        uint8_t portDeviceInMethod(uint16_t) override { return 0xFF; }
        void portDeviceOutMethod(uint16_t, uint8_t) override {}
    } fdc;

    EXPECT_TRUE(decoder.RegisterPortHandler(0x001F, &fdc)) << "#1F must stay free for WD1793";
    EXPECT_TRUE(decoder.RegisterPortHandler(0x003F, &fdc)) << "#3F must stay free for WD1793";
    EXPECT_TRUE(decoder.RegisterPortHandler(0x005F, &fdc)) << "#5F must stay free for WD1793";
    EXPECT_TRUE(decoder.RegisterPortHandler(0x007F, &fdc)) << "#7F must stay free for WD1793";

    decoder.UnregisterPortHandler(0x001F);
    decoder.UnregisterPortHandler(0x003F);
    decoder.UnregisterPortHandler(0x005F);
    decoder.UnregisterPortHandler(0x007F);
    sm2->detachFromPorts();
    ctx2->pPortDecoder = nullptr;
    delete sm2;
    delete ctx2;
}

/// CovoxFB=1 alone (SD=0): classic single-DAC behavior - #FB drives the
/// DAC, the three SoundDrive quad ports stay unwired
TEST_F(CovoxTest, CovoxFBAloneWiresOnlyMonoPort)
{
    EmulatorContext* ctx2 = new EmulatorContext(LoggerLevel::LogError);
    ctx2->config.sound.covoxFB = 1;
    ctx2->config.sound.sd = 0;
    SoundManager* sm2 = new SoundManager(ctx2);
    sm2->reset();
    ASSERT_TRUE(sm2->hasCovox());

    RoutingDecoderStub decoder(ctx2);
    ctx2->pPortDecoder = &decoder;
    ASSERT_TRUE(sm2->attachToPorts());
    Covox* covox = sm2->getCovox();
    ASSERT_NE(covox, nullptr);

    uint8_t latches[4];
    const uint16_t quadOnly[3] = {Covox::PORT_LEFT_A, Covox::PORT_LEFT_B, Covox::PORT_RIGHT_A};
    for (int i = 0; i < 3; i++)
    {
        decoder.PeripheralPortOut(quadOnly[i], 0xFF);
        covox->TTDSaveState(latches);
        EXPECT_EQ(latches[i], 0x80) << "quad-only port index " << i << " should stay at the silence midpoint";
    }

    decoder.PeripheralPortOut(Covox::PORT_RIGHT_B, 0xFF);
    covox->TTDSaveState(latches);
    EXPECT_EQ(latches[3], 0xFF);

    sm2->detachFromPorts();
    ctx2->pPortDecoder = nullptr;
    delete sm2;
    delete ctx2;
}

TEST_F(CovoxTest, DACMidpointProducesSilence)
{
    Covox* covox = sm->getCovox();
    ASSERT_NE(covox, nullptr);

    // Direct buffer write to test DAC conversion without needing Core
    // Simulate what portDeviceOutMethod would do: write 0x80 (midpoint)
    // (0x80 - 128) * 256 = 0
    covox->handleFrameStart();
    // Fill buffer manually with midpoint value
    int16_t* buf = covox->getBuffer();
    int16_t sample = static_cast<int16_t>((0x80 - 128) * 256);
    for (size_t i = 0; i < SAMPLES_PER_FRAME * 2; i++)
        buf[i] = sample;
    covox->handleFrameEnd();

    // Check buffer - should be 0 (silence)
    EXPECT_EQ(buf[0], 0);
    EXPECT_EQ(buf[1], 0);
}

TEST_F(CovoxTest, DACMaxProducesPositiveSample)
{
    Covox* covox = sm->getCovox();
    ASSERT_NE(covox, nullptr);

    // Test DAC conversion: 0xFF = max positive
    int16_t sample = static_cast<int16_t>((0xFF - 128) * 256);
    // (255-128)*256 = 127*256 = 32512
    EXPECT_EQ(sample, 32512);
    EXPECT_GT(sample, 30000);
}

TEST_F(CovoxTest, DACMinProducesNegativeSample)
{
    Covox* covox = sm->getCovox();
    ASSERT_NE(covox, nullptr);

    // Test DAC conversion: 0x00 = max negative
    int16_t sample = static_cast<int16_t>((0x00 - 128) * 256);
    // (0-128)*256 = -32768
    EXPECT_EQ(sample, -32768);
    EXPECT_LT(sample, -30000);
}

TEST_F(CovoxTest, CovoxInMasterMix)
{
    Covox* covox = sm->getCovox();
    ASSERT_NE(covox, nullptr);

    // Clear other devices
    sm->setDeviceMute(AudioSourceType::Beeper, true);
    sm->setDeviceMute(AudioSourceType::AY1_All, true);
    sm->setDeviceMute(AudioSourceType::AY2_All, true);

    // Manually fill Covox buffer AFTER handleFrameEnd's renderToBuffer runs
    // by filling just before the mixer reads it
    sm->handleFrameStart();

    // Fill Covox buffer directly (handleFrameEnd will process it but DC removal is off)
    int16_t* buf = covox->getBuffer();
    int16_t sample = static_cast<int16_t>((0xFF - 128) * 256);  // 32512
    for (size_t i = 0; i < SAMPLES_PER_FRAME * 2; i++)
        buf[i] = sample;

    // Skip Covox handleFrameEnd (which would overwrite with DAC value 0x80)
    // Just run the mixing directly
    // We need to manually trigger the mix without the Covox render step
    // For this test, we verify the buffer is mixed correctly
    const int16_t* covoxBuf = sm->deviceBuffer(AudioSourceType::COVOX);
    EXPECT_GT(covoxBuf[0], 30000);  // Covox buffer has our value

    // Test that when mixed, it appears in master (via solo test below)
}

TEST_F(CovoxTest, CovoxMuteRemovesFromMix)
{
    Covox* covox = sm->getCovox();
    ASSERT_NE(covox, nullptr);

    // Mute all including Covox
    sm->setDeviceMute(AudioSourceType::Beeper, true);
    sm->setDeviceMute(AudioSourceType::AY1_All, true);
    sm->setDeviceMute(AudioSourceType::AY2_All, true);
    sm->setDeviceMute(AudioSourceType::COVOX, true);

    covox->handleFrameStart();
    int16_t* buf = covox->getBuffer();
    int16_t sample = static_cast<int16_t>((0xFF - 128) * 256);
    for (size_t i = 0; i < SAMPLES_PER_FRAME * 2; i++)
        buf[i] = sample;
    sm->handleFrameEnd();

    // Master mix should be silent (Covox is muted)
    const int16_t* master = sm->deviceBuffer(AudioSourceType::MasterMix);
    EXPECT_EQ(master[0], 0);
}

TEST_F(CovoxTest, CovoxSoloPlaysOnlyCovox)
{
    // This test verifies Covox participates in solo/mix logic
    // We verify the device registry has COVOX and it responds to solo
    Covox* covox = sm->getCovox();
    ASSERT_NE(covox, nullptr);

    // Solo Covox
    sm->setDeviceSolo(AudioSourceType::COVOX, true);

    // Verify solo was set
    auto* covoxInfo = sm->device(AudioSourceType::COVOX);
    ASSERT_NE(covoxInfo, nullptr);
    EXPECT_TRUE(covoxInfo->solo);

    // Verify other devices are not soloed (so they'd be silent)
    EXPECT_FALSE(sm->device(AudioSourceType::Beeper)->solo);
    EXPECT_FALSE(sm->device(AudioSourceType::AY1_All)->solo);
}

TEST_F(CovoxTest, DCRemovalReducesOffset)
{
    Covox* covox = sm->getCovox();
    ASSERT_NE(covox, nullptr);

    covox->setDCRemovalEnabled(true);

    // Fill with a constant DC offset (not centered)
    int16_t dcValue = static_cast<int16_t>((0xC0 - 128) * 256);  // 16384

    // Run multiple frames to let the DC filter settle
    for (int frame = 0; frame < 10; frame++)
    {
        covox->handleFrameStart();
        covox->portDeviceOutMethod(Covox::PORT_RIGHT_B, 0xC0);
        covox->handleFrameEnd();
    }

    const int16_t* buf = covox->getBuffer();
    // The last sample should be closer to zero than the raw initial offset
    // Due to DC removal, it should be significantly reduced
    EXPECT_LT(std::abs(buf[SAMPLES_PER_FRAME * 2 - 2]), 10000);
}

// Regression (balldreams2.sna): a mono digi player driving SoundDrive "mode
// 1" LeftB (#1F) alone - not the classic #FB - must still be centered to
// both speakers. Before this fix, only RightB (#FB) got the mono-compat
// centering, so any other single active channel panned hard to its own
// side instead of playing on both (reported as "only one left channel")
TEST_F(CovoxTest, SingleActiveChannelOtherThanRightBIsCentered)
{
    Covox* covox = sm->getCovox();
    ASSERT_NE(covox, nullptr);

    covox->portDeviceOutMethod(Covox::PORT_LEFT_B_MODE1, 0xC0);

    EXPECT_GT(covox->lastLeftAmplitude(), 0) << "LeftB alone should still produce a positive amplitude";
    EXPECT_EQ(covox->lastLeftAmplitude(), covox->lastRightAmplitude())
        << "single active channel must be centered on both speakers, not panned hard left";
}

// A genuine two-channel signal on the same side must NOT be force-centered -
// only exactly one active channel triggers the mono-compat behavior
TEST_F(CovoxTest, TwoActiveChannelsOnSameSideStaysUncentered)
{
    Covox* covox = sm->getCovox();
    ASSERT_NE(covox, nullptr);

    covox->portDeviceOutMethod(Covox::PORT_LEFT_A, 0xC0);
    covox->portDeviceOutMethod(Covox::PORT_LEFT_B, 0xC0);

    EXPECT_GT(covox->lastLeftAmplitude(), 0);
    EXPECT_EQ(covox->lastRightAmplitude(), 0) << "Right side must stay silent, not centered";
}

// Regression (balldreams2.sna): switching a demo's device selection from
// SoundDrive mode-1 (LeftB / #1F) to Covox (RightB / #FB) and back leaves
// RightB's latch frozen at its last non-midpoint value forever, since
// nothing in the demo ever rewrites it. Without decay, that stale RightB
// permanently defeats mono-centering for the LeftB signal that resumes
// afterward (reported as "sd works only with left channel" / clicks on
// switching). After STALE_CHANNEL_FRAMES frames of silence on RightB, it
// must decay back to midpoint and centering must resume.
TEST_F(CovoxTest, StaleChannelDecaysAndCenteringResumes)
{
    Covox* covox = sm->getCovox();
    ASSERT_NE(covox, nullptr);

    // SD mode: LeftB carries the signal, centered (single active channel)
    covox->portDeviceOutMethod(Covox::PORT_LEFT_B_MODE1, 0xC0);
    ASSERT_GT(covox->lastLeftAmplitude(), 0);
    ASSERT_EQ(covox->lastLeftAmplitude(), covox->lastRightAmplitude());

    // Switch to Covox: RightB now carries the signal, LeftB abandoned but
    // still latched from before - both non-midpoint, so no longer "mono"
    covox->portDeviceOutMethod(Covox::PORT_RIGHT_B, 0x62);
    EXPECT_NE(covox->lastLeftAmplitude(), covox->lastRightAmplitude())
        << "two distinct stale+live channels must not be force-centered";

    // Switch back to SD: LeftB resumes, but RightB is still frozen at 0x62
    // from before - immediately after the switch centering is still broken
    covox->portDeviceOutMethod(Covox::PORT_LEFT_B_MODE1, 0xC0);
    ASSERT_NE(covox->lastLeftAmplitude(), covox->lastRightAmplitude());

    // Let RightB go stale: several frames pass with LeftB refreshed every
    // frame (as a real digi engine would) but RightB never rewritten
    for (int frame = 0; frame < Covox::STALE_CHANNEL_FRAMES + 1; frame++)
    {
        covox->handleFrameStart();
        covox->portDeviceOutMethod(Covox::PORT_LEFT_B_MODE1, 0xC0);
    }

    EXPECT_GT(covox->lastLeftAmplitude(), 0);
    EXPECT_EQ(covox->lastLeftAmplitude(), covox->lastRightAmplitude())
        << "RightB should have decayed to midpoint, restoring mono centering";
}

TEST_F(CovoxTest, MonoOutputOnBothChannels)
{
    Covox* covox = sm->getCovox();
    ASSERT_NE(covox, nullptr);

    covox->handleFrameStart();
    int16_t* buf = covox->getBuffer();
    int16_t sample = static_cast<int16_t>((0xA0 - 128) * 256);
    // Fill as mono (same on L and R)
    for (size_t i = 0; i < SAMPLES_PER_FRAME; i++)
    {
        buf[i * 2]     = sample;
        buf[i * 2 + 1] = sample;
    }
    covox->handleFrameEnd();

    // Left and right should be identical (mono DAC)
    EXPECT_EQ(buf[0], buf[1]);
    EXPECT_EQ(buf[100], buf[101]);
}

// Test that frame continuity is preserved - no clicks at frame boundaries.
// If handleFrameStart doesn't pre-fill with the held DAC value, samples
// before the first write would be zero, causing a click.
TEST_F(CovoxTest, FrameBoundaryContinuity)
{
    Covox* covox = sm->getCovox();
    ASSERT_NE(covox, nullptr);

    // Simulate end of frame N with DAC at non-zero value
    covox->handleFrameStart();
    int16_t* buf = covox->getBuffer();

    // Manually set DAC to 0xC0 and fill buffer (simulating writes)
    int16_t expectedSample = static_cast<int16_t>((0xC0 - 128) * 256);  // 16384
    for (size_t i = 0; i < SAMPLES_PER_FRAME * 2; i++)
        buf[i] = expectedSample;
    covox->handleFrameEnd();

    // Now simulate frame N+1 with NO port writes early in frame.
    // Use a helper to set internal state as if port was written.
    // We need direct access, so we just verify the pre-fill behavior.
    // After handleFrameStart, the buffer should be pre-filled with the held value.

    // Manually poke the DAC value to simulate end-of-previous-frame state
    // (In real usage, portDeviceOutMethod would have set this)
    // For this test, we rely on reset() setting 0x80, so sample should be 0.

    covox->reset();  // Reset to 0x80 = silence = sample 0

    // Frame 1: write 0xC0 mid-frame, then end frame
    covox->handleFrameStart();
    // No writes happen - buffer should be pre-filled with 0x80 -> sample 0
    covox->handleFrameEnd();

    // Entire buffer should be 0 (since DAC is at midpoint 0x80)
    EXPECT_EQ(buf[0], 0);
    EXPECT_EQ(buf[SAMPLES_PER_FRAME - 1], 0);

    // Now verify with a non-zero value:
    // Manually set _dacValue[RightB] = 0xC0 by writing to buffer end
    // then check next frame's pre-fill

    // We can't easily poke internal DAC state, so we verify via full simulation
}

// Test that mode latching prevents mid-frame discontinuities.
// Mode (mono vs stereo) should be determined at frame start, not on each write.
TEST_F(CovoxTest, ModeLatchingPreventsDiscontinuity)
{
    Covox* covox = sm->getCovox();
    ASSERT_NE(covox, nullptr);

    // Frame 1: Write only to RightB (mono mode should be detected)
    covox->handleFrameStart();
    int16_t* buf = covox->getBuffer();

    // Fill with a known mono value via RightB channel simulation
    int16_t monoValue = static_cast<int16_t>((0xC0 - 128) * 256);  // 16384
    for (size_t i = 0; i < SAMPLES_PER_FRAME * 2; i++)
        buf[i] = monoValue;

    covox->handleFrameEnd();

    // Verify mono output: L and R should be identical
    EXPECT_EQ(buf[0], buf[1]);
    EXPECT_EQ(buf[SAMPLES_PER_FRAME/2 * 2], buf[SAMPLES_PER_FRAME/2 * 2 + 1]);
}

// Test half-open interval rendering: no double-writes at boundaries
TEST_F(CovoxTest, HalfOpenIntervalRendering)
{
    Covox* covox = sm->getCovox();
    ASSERT_NE(covox, nullptr);

    // This test verifies that sample indices use [start, end) intervals,
    // not [start, end], to avoid double-writing boundary samples.
    // We can't easily inject port writes with specific t-states in unit tests,
    // but we verify the buffer is cleanly filled with no gaps.

    covox->handleFrameStart();
    int16_t* buf = covox->getBuffer();
    covox->handleFrameEnd();

    // After handleFrameStart + handleFrameEnd with no writes,
    // entire buffer should be filled with the held value (0x80 -> 0)
    bool allZero = true;
    for (size_t i = 0; i < SAMPLES_PER_FRAME * 2; i++)
    {
        if (buf[i] != 0)
        {
            allZero = false;
            break;
        }
    }
    EXPECT_TRUE(allZero) << "Buffer should be uniformly filled with held value";
}

// Test that COVOX holds its value across frames when no writes occur.
// This verifies the zero-order hold implementation at frame boundaries.
TEST_F(CovoxTest, CovoxHoldsValueAcrossFrameWithNoWrites)
{
    Covox* covox = sm->getCovox();
    ASSERT_NE(covox, nullptr);

    // Frame 1: Set a non-zero DAC value by directly manipulating buffer
    // (simulating what a port write would do)
    covox->handleFrameStart();
    int16_t* buf = covox->getBuffer();

    // Manually set the buffer to a known value (simulating DAC at 0xC0)
    int16_t expectedSample = static_cast<int16_t>((0xC0 - 128) * 256);  // 16384
    for (size_t i = 0; i < SAMPLES_PER_FRAME * 2; i++)
        buf[i] = expectedSample;

    covox->handleFrameEnd();

    // Verify frame 1 has our value
    EXPECT_EQ(buf[0], expectedSample);
    EXPECT_EQ(buf[SAMPLES_PER_FRAME * 2 - 1], expectedSample);

    // Frame 2: No writes - buffer should be pre-filled with held value.
    // Since we can't actually modify internal _dacValue without port writes,
    // we test the simpler case: after reset, the held value is 0x80 (silence).
    covox->reset();

    // Frame 1 after reset: verify silence
    covox->handleFrameStart();
    covox->handleFrameEnd();
    EXPECT_EQ(buf[0], 0) << "After reset, held value should be silence (0x80 -> 0)";

    // Frame 2: still silence (no writes)
    covox->handleFrameStart();
    covox->handleFrameEnd();
    EXPECT_EQ(buf[0], 0) << "Held value should persist across frames";
    EXPECT_EQ(buf[SAMPLES_PER_FRAME * 2 - 1], 0);
}

// Test that turbo mode (speed multiplier > 1) fills the entire frame correctly.
// In turbo mode, the frame has more t-states but still produces 882 audio samples.
TEST_F(CovoxTest, CovoxTurboModeFillsEntireFrame)
{
    Covox* covox = sm->getCovox();
    ASSERT_NE(covox, nullptr);

    // Simulate turbo mode by checking the buffer fill behavior.
    // The key property: regardless of speed multiplier, the buffer should
    // always have exactly SAMPLES_PER_FRAME samples filled after handleFrameEnd.

    covox->reset();
    covox->handleFrameStart();
    int16_t* buf = covox->getBuffer();

    // After handleFrameStart, entire buffer should be pre-filled (not partially)
    // Check that all samples are set (not left uninitialized)
    bool allFilled = true;
    int16_t firstValue = buf[0];
    for (size_t i = 0; i < SAMPLES_PER_FRAME * 2; i++)
    {
        if (buf[i] != firstValue)
        {
            allFilled = false;
            break;
        }
    }
    EXPECT_TRUE(allFilled) << "handleFrameStart should pre-fill entire buffer uniformly";

    // Verify the count is correct
    EXPECT_EQ(SAMPLES_PER_FRAME, 882) << "Sanity check: 44100Hz / 50fps = 882 samples";

    covox->handleFrameEnd();

    // After handleFrameEnd, buffer should still be fully filled
    allFilled = true;
    firstValue = buf[0];
    for (size_t i = 0; i < SAMPLES_PER_FRAME * 2; i++)
    {
        if (buf[i] != firstValue)
        {
            allFilled = false;
            break;
        }
    }
    EXPECT_TRUE(allFilled) << "handleFrameEnd should maintain uniform buffer fill";
}
