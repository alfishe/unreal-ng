// Lightweight General Sound card tests (GS personalities TDD §10).
//
// Chip-level tests are ROM-free: the LW card is a pure HLE interpreter, so
// protocol, player and audio behavior is exercised directly through the
// ZX-side host ports and the automation actions. The fidelity, cross-
// validation and switch-to-LLE tests boot the real gs105a firmware on the
// LLE card - each carries a runtime justification comment (the POST window
// and ~2 s of playback are the only faithful reference for reply bytes and
// audio level).
//
// Inventory (TDD §10): mailbox parity, reply fidelity vs LLE, memory
// queries, load + parse, start/stop/continue, volumes, tempo, sample-rate
// change, suppression, cross-validation vs LLE, switching, BUG-6 shape,
// TTD round-trip.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

#include "emulator/emulatorcontext.h"
#include "emulator/platform.h"
#include "emulator/sound/audio.h"
#include "emulator/sound/chips/gs/gsmodplayer.h"
#include "emulator/sound/chips/gs/soundchip_gs.h"
#include "emulator/sound/chips/gs/soundchip_gslw.h"
#include "emulator/sound/soundmanager.h"

namespace
{
constexpr uint16_t kPortData = GeneralSoundCard::PORT_DATA;
constexpr uint16_t kPortCommand = GeneralSoundCard::PORT_COMMAND;

void putBeWord(std::vector<uint8_t>& m, size_t offset, uint16_t value)
{
    m[offset] = static_cast<uint8_t>(value >> 8);
    m[offset + 1] = static_cast<uint8_t>(value);
}

// One note cell into a pattern slot: 4 bytes per channel per row
void putCell(std::vector<uint8_t>& m, int pattern, int channel, int row,
             uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3)
{
    const size_t at = 1084 + static_cast<size_t>(pattern) * 1024 + static_cast<size_t>(row) * 16 + channel * 4;
    m[at] = b0;
    m[at + 1] = b1;
    m[at + 2] = b2;
    m[at + 3] = b3;
}

// Synthetic ProTracker module: 1084-byte header + 2 patterns + 64 sample
// bytes = 3196 total (parse()'s expectedSize check is exact). Sample 1 is a
// 64-byte looped square wave (16-byte halves 0xB0/0x30, volume 63) so the
// channel has a stable 50%-duty output for RMS measurements. Pattern 0 row 0
// channel 0 holds a sample-1 note (period 428); with speedOneEffect the F01
// cell on channel 3 sets speed 1 (one row per tempo tick) for the row-rate
// assertions.
std::vector<uint8_t> buildTestModule(bool speedOneEffect)
{
    std::vector<uint8_t> m(1084 + 2 * 1024 + 64, 0x00);

    // Sample 1 header (offset 20 + 30*0): BE words at +22 length, +26 loop
    // start, +28 loop length; volume byte at +25
    putBeWord(m, 20 + 22, 32); // 32 words = 64 bytes
    m[20 + 25] = 63;
    putBeWord(m, 20 + 26, 0);
    putBeWord(m, 20 + 28, 32); // 32 words: the whole sample loops

    m[950] = 2;      // song length (2 positions)
    m[952] = 0;      // order table
    m[953] = 1;
    m[1080] = 'M';   // the 31-sample ProTracker tag
    m[1081] = '.';
    m[1082] = 'K';
    m[1083] = '.';

    // Channel 0, row 0, pattern 0: sample 1, period 428, no effect
    // (period = ((b0 & 0x0F) << 8) | b1 = 0x1AC, sample = (b0 & 0xF0) | (b2 >> 4) = 1)
    putCell(m, 0, 0, 0, 0x01, 0xAC, 0x10, 0x00);
    if (speedOneEffect)
        putCell(m, 0, 3, 0, 0x00, 0x00, 0x0F, 0x01); // F01: speed 1

    // Sample data: 64-byte looped square wave
    const size_t off = 1084 + 2 * 1024;
    for (size_t i = 0; i < 64; i++)
        m[off + i] = (i / 16) % 2 ? 0x30 : 0xB0;

    return m;
}

// Song position * 64 + row (COM62 domain); wraps at the 2-position song
// length (128 rows)
int mod128(int delta)
{
    return ((delta % 128) + 128) % 128;
}
} // namespace

class SoundChip_GSLightweight_Test : public ::testing::Test
{
protected:
    EmulatorContext* ctx = nullptr;
    std::unique_ptr<SoundChip_GSLightweight> chip;

    void SetUp() override
    {
        ctx = new EmulatorContext(LoggerLevel::LogError);
        ctx->config.sound.gs_vol = 8000;
        // Pentagon frame geometry: 3.5 MHz base clock, 12 MHz GS clock ->
        // llround(69888 * 12 / 3.5) = 239602 GS cycles per frame
        ctx->config.frame = 69888;
        ctx->config.frame_duration_us = 19968;
        ctx->emulatorState.current_z80_frequency_multiplier = 1;
        ctx->emulatorState.hw_turbo_shift_applied = 0;
        chip = std::make_unique<SoundChip_GSLightweight>(ctx, 512);
    }

    void TearDown() override
    {
        chip.reset();
        delete ctx;
    }

    void runFrames(int n)
    {
        for (int i = 0; i < n; i++)
        {
            chip->handleFrameStart();
            chip->handleFrameEnd(SAMPLES_PER_FRAME);
        }
    }

    // ZX-side OUT paths (exercise PortDevice + counter accounting)
    void outData(uint8_t value) { chip->portDeviceOutMethod(kPortData, value); }
    void outCmd(uint8_t command) { chip->portDeviceOutMethod(kPortCommand, command); }

    // GS->host byte via the ZX-side IN path. Exact original semantics: a
    // reply posted before the handler's own DATRG param read loses its bit7
    // flag (shared flip-flop) - the value lives in the #B3 latch, so the
    // read is unconditional, like real software that knows a reply follows
    int readReply()
    {
        return chip->portDeviceInMethod(kPortData);
    }

    // COM30 upload with the firmware's reset-and-retry: a COM30 with
    // CNTMOD != 0 soft-resets and expects the command again. Returns the
    // assigned slot reply (1 on success)
    int uploadModule(const std::vector<uint8_t>& bytes)
    {
        // The slot reply's bit7 flag is consumed by the handler's own param
        // read (shared flip-flop, same as the LLE firmware) - the reply is
        // read from the #B3 latch
        int slot = -1;
        for (int attempt = 0; attempt < 3; attempt++)
        {
            chip->sendData(0x01);
            chip->sendCommand(0x30);
            slot = chip->readData();
            if (slot == 1)
                break;
        }
        if (slot != 1)
            return -1;
        for (uint8_t b : bytes)
            chip->sendData(b);
        chip->sendCommand(0xD2);
        return slot;
    }

    // COM31 selector 0 = play current module; returns the reply byte
    int startPlayback()
    {
        chip->sendData(0x00);
        chip->sendCommand(0x31);
        return chip->readData();
    }

    int linearPosition()
    {
        chip->sendCommand(0x62);
        const int v = chip->readData();
        return ((v >> 6) & 0x03) * 64 + (v & 0x3F);
    }

    uint64_t dacFetches() const { return chip->getActivityCounters().dacFetches; }

    int bufferPeak() const
    {
        const int16_t* samples = chip->getBuffer();
        int peak = 0;
        for (int i = 0; i < 2 * SAMPLES_PER_FRAME; i++)
            peak = std::max(peak, std::abs(static_cast<int>(samples[i])));
        return peak;
    }

    // RMS of one output channel accumulated over n frames (drives the frames)
    double rmsOverFrames(int n, int channel)
    {
        double acc = 0.0;
        size_t count = 0;
        for (int f = 0; f < n; f++)
        {
            chip->handleFrameStart();
            chip->handleFrameEnd(SAMPLES_PER_FRAME);
            const int16_t* samples = chip->getBuffer();
            for (int i = 0; i < SAMPLES_PER_FRAME; i++)
            {
                const double v = samples[2 * i + channel];
                acc += v * v;
            }
            count += SAMPLES_PER_FRAME;
        }
        return std::sqrt(acc / static_cast<double>(count));
    }
};

/// region <Protocol>

TEST_F(SoundChip_GSLightweight_Test, Bug6_IdleSignaturePromptAfterF4)
{
    // The trainer-style probe polls IN (#BB) for the clean idle signature
    // 0x7E (bits 1-6 pull-ups high, bit0/bit7 clear) right after the soft
    // reset. The LLE card needs the 0.3-1.1 s POST window for this; the LW
    // card must show it with ZERO frames elapsed (BUG-6 fix, TDD §4.6)
    outCmd(0xF4);
    EXPECT_EQ(chip->readStatus(), 0x7E);
}

TEST_F(SoundChip_GSLightweight_Test, Protocol_BurstLoadSurvivesResetAndDrain)
{
    // Nether Earth GS shape (bootdiag 6): F4, 30, D1 written to #BB back to
    // back with no polling. Everything dispatches in write order: F4 resets,
    // 30 opens the load and replies the slot, D1 is drained by the load
    // machine (LOADCM). Then a payload stream + D2, and COM2C confirms the
    // module slot - exactly the LLE behavior
    outCmd(0xF4);
    outCmd(0x30);
    outCmd(0xD1);
    EXPECT_EQ(chip->getCommandQueueCount(), 0u) << "burst commands not dispatched in order";
    EXPECT_EQ(readReply(), 1) << "COM30 never replied the assigned slot";

    for (int i = 0; i < 16; i++)
        outData(static_cast<uint8_t>(0x40 + i));
    outCmd(0xD2);

    outData(0x00);
    outCmd(0x2C);
    EXPECT_EQ(readReply(), 1) << "CURMOD != 1: the queued #30 was lost";

    const auto& c = chip->getActivityCounters();
    EXPECT_EQ(c.hostCommandsDropped, 0u);
    EXPECT_EQ(c.hostDataDropped, 0u);
    EXPECT_EQ(chip->getDataQueueCount(), 0u) << "load stream not fully consumed";
}

TEST_F(SoundChip_GSLightweight_Test, Protocol_ParamFirstTrioDispatchesInOrder)
{
    // The game's closing trio (bootdiag 7): each parameter on #B3 BEFORE its
    // command on #BB, zero handshake. COM31 gets 0x00 ("play current"), COM2B
    // gets 0x40 -> FXVOL, COM2A gets 0x25 -> MODVOL. A single-latch model
    // would feed COM31 the 0x40 and take the error path
    ASSERT_EQ(uploadModule(buildTestModule(false)), 1);

    // Exact original semantics: an unread reply is discarded by the next
    // command (single hardware latch), so a host that wants the values
    // reads each one before the next command - like real GS software
    outData(0x00);
    outCmd(0x31);
    EXPECT_EQ(readReply(), 1) << "COM31 took the error path: params lost in transit";
    outData(0x40);
    outCmd(0x2B);
    EXPECT_EQ(readReply(), 0x40) << "COM2B did not reply the old FXVOL";
    outData(0x25);
    outCmd(0x2A);
    EXPECT_EQ(readReply(), 0x40) << "COM2A did not reply the old MODVOL";
    EXPECT_EQ(chip->getDataQueueCount(), 0u) << "trio params not consumed";

    // MODVOL = 0x25 reached the engine: the channel latch is
    // (63 * 0x25 * 0x40) >> 12 = 36 while playing
    runFrames(2);
    EXPECT_EQ(chip->getChannelVolume(0), 36) << "MODVOL 0x25 did not scale the channel";
    EXPECT_EQ(chip->getChannelVolume(1), 0);
    EXPECT_GT(dacFetches(), 1000u) << "COM31 did not start playback";

    // A follow-up COM2A query confirms 0x25 landed (and keeps it there)
    chip->sendData(0x25);
    chip->sendCommand(0x2A);
    EXPECT_EQ(chip->readData(), 0x25);
}

TEST_F(SoundChip_GSLightweight_Test, Protocol_NewCommandDiscardsUnreadReply)
{
    // Exact original semantics (gshle.cpp resets the reply state on every
    // command write; the LLE's single latch is overwritten the same way):
    // an unread reply does not survive the next command, and never goes
    // stale into later reads
    chip->sendCommand(0x36); // query: replies 0xFF
    chip->sendCommand(0xF0); // ERRCODE: replies 0x00 - discards the 0xFF
    EXPECT_EQ(readReply(), 0x00) << "stale reply survived a new command";
    EXPECT_EQ(chip->readStatus() & 0x80, 0) << "reply flag left up after the read";
}

/// region <v1 module handoff capture/replay>

TEST_F(SoundChip_GSLightweight_Test, ModuleCapture_ByteExactAfterD2)
{
    const auto module = buildTestModule(false);
    ASSERT_EQ(uploadModule(module), 1);

    std::vector<uint8_t> bytes;
    bool playing = true;
    ASSERT_TRUE(chip->captureModuleUpload(bytes, playing));
    EXPECT_EQ(bytes, module) << "captured stream must match the parsed module byte-for-byte";
    EXPECT_FALSE(playing) << "uploadModule() does not start playback";
}

TEST_F(SoundChip_GSLightweight_Test, ModuleCapture_PlayingFlagTracksStartStop)
{
    ASSERT_EQ(uploadModule(buildTestModule(false)), 1);
    std::vector<uint8_t> bytes;
    bool playing = false;

    ASSERT_EQ(startPlayback(), 1);
    ASSERT_TRUE(chip->captureModuleUpload(bytes, playing));
    EXPECT_TRUE(playing);

    outData(0x00);
    outCmd(0x32); // stop
    ASSERT_TRUE(chip->captureModuleUpload(bytes, playing));
    EXPECT_FALSE(playing);
}

TEST_F(SoundChip_GSLightweight_Test, ModuleCapture_ResetClearsStore)
{
    ASSERT_EQ(uploadModule(buildTestModule(false)), 1);
    std::vector<uint8_t> bytes;
    bool playing = true;
    ASSERT_TRUE(chip->captureModuleUpload(bytes, playing)) << "precondition: module captured";

    outCmd(0xF4); // COMF4 soft reset
    EXPECT_FALSE(chip->captureModuleUpload(bytes, playing))
        << "a reset clears CNTMOD - the stale module must not be handed off";
}

TEST(GSLightweight_ModuleHandoff, ReplayRoundTripByteExactAndPlaysBack)
{
    // Direct receiving-side test (no SoundManager switch involved): feed a
    // captured stream straight into a fresh card's replayModuleUpload and
    // verify it plays back exactly as a normal COM30 upload would
    EmulatorContext ctx(LoggerLevel::LogError);
    ctx.config.sound.gs_vol = 8000;
    ctx.config.frame = 69888;
    ctx.config.frame_duration_us = 19968;
    ctx.emulatorState.current_z80_frequency_multiplier = 1;
    ctx.emulatorState.hw_turbo_shift_applied = 0;
    SoundChip_GSLightweight chip(&ctx, 512);

    const auto module = buildTestModule(false);
    chip.replayModuleUpload(module, true);

    std::vector<uint8_t> bytes;
    bool playing = false;
    ASSERT_TRUE(chip.captureModuleUpload(bytes, playing));
    EXPECT_EQ(bytes, module) << "the replayed card must re-capture the same bytes";
    EXPECT_TRUE(playing) << "startPlayback=true must leave the card playing";

    const uint64_t dac0 = chip.getActivityCounters().dacFetches;
    for (int i = 0; i < 3; i++)
    {
        chip.handleFrameStart();
        chip.handleFrameEnd(SAMPLES_PER_FRAME);
    }
    EXPECT_GT(chip.getActivityCounters().dacFetches, dac0) << "playback did not actually start";
}

TEST(GSLightweight_ModuleHandoff, ReplayEmptyBytesIsNoOp)
{
    EmulatorContext ctx(LoggerLevel::LogError);
    ctx.config.sound.gs_vol = 8000;
    ctx.config.frame = 69888;
    ctx.config.frame_duration_us = 19968;
    ctx.emulatorState.current_z80_frequency_multiplier = 1;
    ctx.emulatorState.hw_turbo_shift_applied = 0;
    SoundChip_GSLightweight chip(&ctx, 512);

    chip.replayModuleUpload({}, true);

    std::vector<uint8_t> bytes;
    bool playing = true;
    EXPECT_FALSE(chip.captureModuleUpload(bytes, playing)) << "an empty replay must leave the card untouched";
}

/// endregion </v1 module handoff capture/replay>

TEST_F(SoundChip_GSLightweight_Test, MemoryQueries_VirtualGeometry)
{
    // COM20: total virtual RAM (512 KB = 0x080000), 3 bytes L, H, C
    chip->sendCommand(0x20);
    EXPECT_EQ(chip->readData(), 0x00);
    EXPECT_EQ(chip->readData(), 0x00);
    EXPECT_EQ(chip->readData(), 0x08);

    // COM23: NUMPG = 32 KB pages minus the system page
    chip->sendCommand(0x23);
    EXPECT_EQ(chip->readData(), 15);

    // COM21 before any upload: free = total
    chip->sendCommand(0x21);
    EXPECT_EQ(chip->readData(), 0x00);
    EXPECT_EQ(chip->readData(), 0x00);
    EXPECT_EQ(chip->readData(), 0x08);

    // After the module upload the store shrinks free by exactly its size
    // (524288 - 3196 = 521092 = 0x07F384)
    ASSERT_EQ(uploadModule(buildTestModule(false)), 1);
    chip->sendCommand(0x21);
    EXPECT_EQ(chip->readData(), 0x84);
    EXPECT_EQ(chip->readData(), 0xF3);
    EXPECT_EQ(chip->readData(), 0x07);
    chip->sendCommand(0x23);
    EXPECT_EQ(chip->readData(), 15) << "page count must not depend on the store";
}

/// endregion </Protocol>

/// region <Player>

TEST_F(SoundChip_GSLightweight_Test, LoadParse_ValidModuleAndGarbageRejection)
{
    // Valid upload: ERRCODE stays clear, the slot registers, the player
    // reports the channel mapping (sample 1 on channel 0, none elsewhere)
    ASSERT_EQ(uploadModule(buildTestModule(false)), 1);
    chip->sendCommand(0xF0);
    EXPECT_EQ(chip->readData(), 0x00) << "valid module raised ERRCODE";

    ASSERT_EQ(startPlayback(), 1);
    runFrames(2);
    chip->sendCommand(0x63); // 4x CHREAL (0x7F = no sample)
    EXPECT_EQ(chip->readData(), 0x01);
    EXPECT_EQ(chip->readData(), 0x7F);
    EXPECT_EQ(chip->readData(), 0x7F);
    EXPECT_EQ(chip->readData(), 0x7F);
    chip->sendCommand(0x64); // 4x CHMVOL (row volume 0-63)
    EXPECT_EQ(chip->readData(), 63);
    EXPECT_EQ(chip->readData(), 0x00);
    EXPECT_EQ(chip->readData(), 0x00);
    EXPECT_EQ(chip->readData(), 0x00);

    // Garbage upload: the LW parser rejects it (firmware-parity divergence
    // documented in TDD 4.6 - ERRCODE 0x10 instead of playing nonsense)
    ASSERT_EQ(uploadModule(std::vector<uint8_t>(2048, 0x00)), 1);
    chip->sendCommand(0xF0);
    EXPECT_EQ(chip->readData(), 0x10) << "malformed upload must set ERRCODE 0x10";
    EXPECT_EQ(startPlayback(), 0) << "start must fail without a parsed module";

    // COM21 clears ERRCODE (dispatch table), and a fresh valid upload
    // recovers playback without a card reset
    chip->sendCommand(0x21);
    (void)chip->readData();
    (void)chip->readData();
    (void)chip->readData();
    chip->sendCommand(0xF0);
    EXPECT_EQ(chip->readData(), 0x00) << "COM21 must clear ERRCODE";

    ASSERT_EQ(uploadModule(buildTestModule(false)), 1);
    EXPECT_EQ(startPlayback(), 1) << "no recovery after a rejected upload";
}

TEST_F(SoundChip_GSLightweight_Test, Playback_StartStopContinue)
{
    // Pentagon geometry: ~749 quanta per frame, so a playing card fetches
    // ~749 DAC samples per frame and a stopped card exactly zero
    ASSERT_EQ(uploadModule(buildTestModule(false)), 1);
    ASSERT_EQ(startPlayback(), 1);
    runFrames(3);

    uint64_t before = dacFetches();
    runFrames(5);
    EXPECT_GT(dacFetches() - before, 3000u) << "playing card must fetch ~749/frame";

    // COM32 freezes the position: fetches stop, the row counter holds
    const int frozenAt = linearPosition();
    chip->sendData(0x00);
    chip->sendCommand(0x32);
    EXPECT_EQ(chip->readData(), 1) << "COM32 must reply the old MODULE";
    before = dacFetches();
    runFrames(5);
    EXPECT_EQ(dacFetches() - before, 0u) << "stopped card keeps fetching";
    EXPECT_EQ(linearPosition(), frozenAt) << "COM32 must freeze the position";

    // COM33 resumes
    chip->sendData(0x00);
    chip->sendCommand(0x33);
    EXPECT_EQ(chip->readData(), 1) << "COM33 must reply the old MODULE";
    before = dacFetches();
    runFrames(5);
    EXPECT_GT(dacFetches() - before, 3000u) << "COM33 did not resume";
    EXPECT_NE(linearPosition(), frozenAt) << "position still frozen after COM33";
}

TEST_F(SoundChip_GSLightweight_Test, Playback_VolumeScaling)
{
    ASSERT_EQ(uploadModule(buildTestModule(false)), 1);
    ASSERT_EQ(startPlayback(), 1);
    runFrames(3); // settle onto the square wave
    const double full = rmsOverFrames(5, 0);
    ASSERT_GT(full, 100.0) << "full-volume playback produced no audio";

    // MODVOL 0x40 -> 0x20: music scales by half (latch 63 -> 31)
    chip->sendData(0x20);
    chip->sendCommand(0x2A);
    EXPECT_EQ(chip->readData(), 0x40);
    EXPECT_NEAR(rmsOverFrames(5, 0) / full, 0.5, 0.15) << "MODVOL does not halve music";

    chip->sendData(0x40);
    chip->sendCommand(0x2A);
    (void)chip->readData();

    // MTVOL 0x40 -> 0x20: the module master scales by half too
    chip->sendData(0x20);
    chip->sendCommand(0x35);
    EXPECT_EQ(chip->readData(), 0x40);
    EXPECT_NEAR(rmsOverFrames(5, 0) / full, 0.5, 0.15) << "MTVOL does not halve music";

    chip->sendData(0x40);
    chip->sendCommand(0x35);
    (void)chip->readData();

    // FXVOL 0x40 -> 0x20: SFX-only scaling must leave music untouched
    // (master volume lives on COM35, music scaling on COM2A - the guide's
    // "#50 set global volume" table row does not match the firmware)
    chip->sendData(0x20);
    chip->sendCommand(0x2B);
    (void)chip->readData();
    EXPECT_NEAR(rmsOverFrames(5, 0) / full, 1.0, 0.2) << "FXVOL leaked into music";
}

TEST_F(SoundChip_GSLightweight_Test, Playback_ExternalTempo)
{
    // Speed 1 + 125 BPM: one row per 750 quanta = ~1 row/frame. COM66 is the
    // external tempo (FXF): 254 BPM rescales the tick to 37500*5/(2*254) =
    // 369 quanta = ~2 rows/frame
    ASSERT_EQ(uploadModule(buildTestModule(true)), 1);
    ASSERT_EQ(startPlayback(), 1);
    runFrames(3);
    chip->sendCommand(0x67);
    ASSERT_EQ(chip->readData(), 1) << "F01 did not set speed 1";

    const int p0 = linearPosition();
    runFrames(12);
    EXPECT_NEAR(mod128(linearPosition() - p0), 12, 2) << "row rate wrong at 125 BPM";

    chip->sendData(0xFE);
    chip->sendCommand(0x66);
    chip->sendCommand(0x68);
    ASSERT_EQ(chip->readData(), 254) << "COM66 did not set 254 BPM";

    const int p1 = linearPosition();
    runFrames(12);
    EXPECT_NEAR(mod128(linearPosition() - p1), 24, 3) << "row rate wrong at 254 BPM";
}

TEST_F(SoundChip_GSLightweight_Test, Playback_SampleRateChangeMidPlay)
{
    // setSampleRate rebuilds the blips only - the musical position lives in
    // the row/tick/quantum domain and must keep advancing at the same rate
    ASSERT_EQ(uploadModule(buildTestModule(true)), 1);
    ASSERT_EQ(startPlayback(), 1);
    runFrames(3);

    const int p0 = linearPosition();
    chip->setSampleRate(48000);
    runFrames(10);
    EXPECT_NEAR(mod128(linearPosition() - p0), 10, 2) << "rate change disturbed the position";
    EXPECT_GT(bufferPeak(), 100) << "audio died after the rate change";
}

TEST_F(SoundChip_GSLightweight_Test, Playback_SuppressionHoldsLevelButTracks)
{
    // Turbo path: emitSample adds no deltas, so the blip integrator
    // DC-holds the last pre-suppression level (TDD 6.3) - every suppressed
    // frame is that flat level per channel, never new audio - while the
    // interpreter keeps advancing so the resync after re-enabling is seamless
    ASSERT_EQ(uploadModule(buildTestModule(false)), 1);
    ASSERT_EQ(startPlayback(), 1);
    runFrames(3);
    EXPECT_GT(bufferPeak(), 100);

    chip->setSynthesisSuppressed(true);
    const uint64_t before = dacFetches();
    // The last pre-suppression step's kernel rings ~16 samples into the
    // first suppressed frame, and the frame tail can carry 1-2 zero-fill
    // samples (the GS frame rounds to ~880.6 of the 882 nominal) - the
    // held level is captured mid-frame, away from both edges
    constexpr int kKernelSettlePairs = 32;
    constexpr int kZeroFillTailPairs = 8;
    const int probePair = SAMPLES_PER_FRAME / 2;
    int16_t heldL = 0;
    int16_t heldR = 0;
    for (int i = 0; i < 5; i++)
    {
        runFrames(1);
        const int16_t* samples = chip->getBuffer();
        if (i == 0)
        {
            heldL = samples[2 * probePair];
            heldR = samples[2 * probePair + 1];
        }
        const int from = i == 0 ? kKernelSettlePairs : 0;
        for (int j = from; j < SAMPLES_PER_FRAME - kZeroFillTailPairs; j++)
        {
            ASSERT_EQ(samples[2 * j], heldL) << "L deltas leaked into suppressed frame " << i;
            ASSERT_EQ(samples[2 * j + 1], heldR) << "R deltas leaked into suppressed frame " << i;
        }
    }
    EXPECT_GT(dacFetches() - before, 3000u) << "suppression stopped the interpreter";

    chip->setSynthesisSuppressed(false);
    runFrames(3);
    EXPECT_GT(bufferPeak(), 100) << "no seamless resync after suppression";
}

TEST_F(SoundChip_GSLightweight_Test, TTD_RoundTripDeterministic)
{
    ASSERT_EQ(uploadModule(buildTestModule(false)), 1);
    ASSERT_EQ(startPlayback(), 1);
    runFrames(30);

    // Save at a frame boundary: two saves must be identical (no hidden
    // nondeterminism in the blob)
    std::vector<uint8_t> blob(chip->TTDStateSize());
    chip->TTDSaveState(blob.data());
    std::vector<uint8_t> again(chip->TTDStateSize());
    chip->TTDSaveState(again.data());
    EXPECT_EQ(blob, again) << "TTD blob not deterministic";

    // Layout: fixed 95-byte header + store + player runtime
    EXPECT_GE(blob.size(), SoundChip_GSLightweight::TTD_FIXED_STATE_SIZE + 4 + 3196 + 4);

    // A restored card must replay the same 10 frames bit-identically
    runFrames(10);
    const uint64_t finalHash = chip->TTDHashState();

    SoundChip_GSLightweight second(ctx, 512);
    second.TTDLoadState(blob.data());
    for (int i = 0; i < 10; i++)
    {
        second.handleFrameStart();
        second.handleFrameEnd(SAMPLES_PER_FRAME);
    }
    EXPECT_EQ(second.TTDHashState(), finalHash) << "restored card diverged";
    // Audio compare: the blob drops the blip integrator DC (the load
    // reseeds _lastL/_lastR but clears the blips, so the restored output
    // = true level - seed) and restarts the fractional clock accumulator
    // (kernel phases shift by sub-sample amounts, +-a few LSB). The LLE
    // restores its blips the same way. Contract: identical waveform up to
    // one constant per channel - compare the DC-removed RMS per channel
    const int16_t* a = chip->getBuffer();
    const int16_t* b = second.getBuffer();
    double meanA[2] = {0.0, 0.0};
    double meanB[2] = {0.0, 0.0};
    for (int j = 0; j < SAMPLES_PER_FRAME; j++)
        for (int c = 0; c < 2; c++)
        {
            meanA[c] += a[2 * j + c];
            meanB[c] += b[2 * j + c];
        }
    double accA[2] = {0.0, 0.0};
    double accB[2] = {0.0, 0.0};
    for (int j = 0; j < SAMPLES_PER_FRAME; j++)
        for (int c = 0; c < 2; c++)
        {
            const double va = a[2 * j + c] - meanA[c] / SAMPLES_PER_FRAME;
            const double vb = b[2 * j + c] - meanB[c] / SAMPLES_PER_FRAME;
            accA[c] += va * va;
            accB[c] += vb * vb;
        }
    for (int c = 0; c < 2; c++)
    {
        const double rmsA = std::sqrt(accA[c] / SAMPLES_PER_FRAME);
        const double rmsB = std::sqrt(accB[c] / SAMPLES_PER_FRAME);
        EXPECT_GT(rmsA, 100.0) << "original card went silent";
        EXPECT_NEAR(rmsB / rmsA, 1.0, 0.03) << "restored waveform diverged beyond the DC offset";
    }
}

TEST(GSModPlayer_LargeSample, OneShotPositionReachesTrueEndBeyond64KB)
{
    // Regression for the 2026-09-21 live-demo distortion: ChannelState's
    // 16.16 fixed-point position used to live in a 32-bit container, whose
    // 16-bit integer part tops out at 65535 bytes. ProTracker samples run
    // up to 131070 bytes (16-bit word length field), and any real GS demo
    // content over 64 KB per sample wrapped the position math well before
    // reaching the sample's true end. This module's one sample is ~100 KB,
    // filled with a byte ramp so the final DAC byte pins down exactly where
    // playback actually ended up.
    constexpr uint32_t kSampleBytes = 100000; // even (word length), > 64 KB
    std::vector<uint8_t> m(1084 + 1 * 1024 + kSampleBytes, 0x00);

    putBeWord(m, 20 + 22, static_cast<uint16_t>(kSampleBytes / 2)); // length words
    m[20 + 25] = 63; // volume
    putBeWord(m, 20 + 26, 0); // loop start words
    putBeWord(m, 20 + 28, 0); // loop length words (<2 -> one-shot)

    m[950] = 1; // song length
    m[952] = 0; // order[0] = pattern 0
    m[1080] = 'M'; m[1081] = '.'; m[1082] = 'K'; m[1083] = '.';

    // Channel 0, row 0: sample 1, period 113 (0x071 - fastest note in range,
    // maximizes bytes/quantum so the test reaches the sample end quickly)
    putCell(m, 0, 0, 0, 0x00, 0x71, 0x10, 0x00); // period=0x071=113, sample=1

    const size_t sampleOff = 1084 + 1024;
    for (uint32_t i = 0; i < kSampleBytes; i++)
        m[sampleOff + i] = static_cast<uint8_t>(i);

    GSModPlayer player;
    const char* reason = nullptr;
    ASSERT_TRUE(player.parse(m.data(), m.size(), &reason)) << (reason ? reason : "");
    player.start(0);

    uint8_t out[GSModPlayer::kChannels];
    uint8_t vols[GSModPlayer::kChannels];
    // ~0.84 bytes/quantum at period 113 - 200000 quanta comfortably clears
    // the full 100000-byte sample (a pre-fix build wraps and goes silent or
    // resets long before this point)
    for (int q = 0; q < 200000; q++)
        player.advanceQuantum(out, vols);

    EXPECT_EQ(out[0], static_cast<uint8_t>((kSampleBytes - 1) & 0xFF))
        << "playback did not reach the true end of a >64KB one-shot sample "
        << "(32-bit position wraparound regression)";
}

TEST(GSModPlayer_LargeSample, LoopStaysInsideRegionBeyond64KB)
{
    // Same root cause as above, isolated to the loop-wrap branch:
    // loopEnd = (loopStart+loopLength)<<16 overflows a 32-bit container
    // once loopStart alone exceeds ~65535 bytes. The sample is 0x00 before
    // the loop and 0xFF for the entire loop region (looping forever once
    // reached) - a byte outside {0x00, 0xFF} after settling proves the
    // loop math read outside the intended [loopStart, loopStart+loopLength)
    // window.
    constexpr uint32_t kLoopStart = 90000;  // past the 64 KB boundary
    constexpr uint32_t kLoopLength = 8000;  // even (word length)
    constexpr uint32_t kSampleBytes = kLoopStart + kLoopLength;
    std::vector<uint8_t> m(1084 + 1 * 1024 + kSampleBytes, 0x00);

    putBeWord(m, 20 + 22, static_cast<uint16_t>(kSampleBytes / 2));
    m[20 + 25] = 63;
    putBeWord(m, 20 + 26, static_cast<uint16_t>(kLoopStart / 2));
    putBeWord(m, 20 + 28, static_cast<uint16_t>(kLoopLength / 2));

    m[950] = 1;
    m[952] = 0;
    m[1080] = 'M'; m[1081] = '.'; m[1082] = 'K'; m[1083] = '.';
    putCell(m, 0, 0, 0, 0x00, 0x71, 0x10, 0x00); // period=0x071=113, sample=1

    const size_t sampleOff = 1084 + 1024;
    for (uint32_t i = 0; i < kLoopStart; i++)
        m[sampleOff + i] = 0x00;
    for (uint32_t i = kLoopStart; i < kSampleBytes; i++)
        m[sampleOff + i] = 0xFF;

    GSModPlayer player;
    const char* reason = nullptr;
    ASSERT_TRUE(player.parse(m.data(), m.size(), &reason)) << (reason ? reason : "");
    player.start(0);

    uint8_t out[GSModPlayer::kChannels];
    uint8_t vols[GSModPlayer::kChannels];
    // Reach the loop (~107500 quanta) and traverse it several times
    // (~0.84 B/quantum: loop alone takes ~9500 quanta per pass), but stay
    // under one full pattern repeat (64 rows * speed 6 * 750 quanta/tick =
    // 288000 quanta) - the single note re-triggers on the repeat and would
    // reset position back to 0, defeating the test
    for (int q = 0; q < 200000; q++)
        player.advanceQuantum(out, vols);

    EXPECT_EQ(out[0], 0xFF)
        << "loop position escaped [loopStart, loopStart+loopLength) on a "
        << ">64KB sample (32-bit loopEnd wraparound regression)";
}

/// endregion </Player>

/// region <Fidelity and cross-validation vs LLE>

namespace
{
// Real-firmware reference card (the shapes mirror the bootdiag harness)
struct LleHarness
{
    EmulatorContext ctx;
    std::unique_ptr<SoundChip_GeneralSound> chip;

    explicit LleHarness(size_t ramKB = 512)
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

    bool sendDataWait(uint8_t value)
    {
        chip->portDeviceOutMethod(kPortData, value);
        return waitFlagClear(0x80, 1000);
    }

    bool sendCommandWait(uint8_t cmd)
    {
        chip->portDeviceOutMethod(kPortCommand, cmd);
        return waitFlagClear(0x01, 1000);
    }

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
};

bool bootToPost(LleHarness& h)
{
    for (int sec = 0; sec < 20; sec++)
    {
        h.runFrames(50);
        if (h.chip->getActivityCounters().volumeLatchWrites >= 4)
        {
            // The POST leaves an unread NUMPG reply in the latch - consume it
            // so the paced helper's bit7 polling does not stall on it
            if (h.chip->readStatus() & 0x80)
                (void)h.chip->portDeviceInMethod(kPortData);
            return true;
        }
    }
    return false;
}

// Paced COM30 upload through the live firmware (fresh post-POST state: a
// single COM30 opens the load)
bool uploadModuleLle(LleHarness& h, const std::vector<uint8_t>& bytes)
{
    h.chip->portDeviceOutMethod(kPortData, 0x01);
    if (!h.sendCommandWait(0x30))
        return false;
    h.runFrames(1);
    if (h.chip->portDeviceInMethod(kPortData) != 1) // slot reply latch
        return false;
    for (uint8_t b : bytes)
    {
        if (!h.sendDataWait(b))
            return false;
    }
    h.chip->portDeviceOutMethod(kPortCommand, 0xD2);
    return h.waitFlagClear(0x01, 1000);
}

// RMS of both output channels accumulated over n frames, card-agnostic
void accumulateRms(GeneralSoundCard& card, int frames, double out[2])
{
    double acc[2] = {0.0, 0.0};
    size_t count = 0;
    for (int f = 0; f < frames; f++)
    {
        card.handleFrameStart();
        card.handleFrameEnd(SAMPLES_PER_FRAME);
        const int16_t* samples = card.getBuffer();
        for (int i = 0; i < SAMPLES_PER_FRAME; i++)
        {
            const double l = samples[2 * i];
            const double r = samples[2 * i + 1];
            acc[0] += l * l;
            acc[1] += r * r;
        }
        count += SAMPLES_PER_FRAME;
    }
    out[0] = std::sqrt(acc[0] / static_cast<double>(count));
    out[1] = std::sqrt(acc[1] / static_cast<double>(count));
}
} // namespace

TEST(SoundChip_GSLightweight_Fidelity, ReplyBytesMatchLle)
{
    // Runtime justification: the gs105a firmware's reply bytes ARE the
    // contract the LW card reproduces bit-exactly (TDD 1.1); one real POST
    // (~0.3-1.1 s emulated) plus a paced upload is the only faithful source.
    // The script walks the get-then-set volume family, the queries, a module
    // upload + start + stop; every #B3-readable byte must match.
    const auto module = buildTestModule(false);
    std::vector<uint8_t> lle;
    std::vector<uint8_t> lw;

    {
        LleHarness h;
        ASSERT_TRUE(h.chip->isROMLoaded());
        ASSERT_TRUE(bootToPost(h));

        const auto paramCmd = [&](uint8_t param, uint8_t cmd) {
            h.chip->portDeviceOutMethod(kPortData, param);
            ASSERT_TRUE(h.sendCommandWait(cmd));
            // Get-then-set: the firmware posts the old value BEFORE its
            // DATRG param read, so the shared bit7 is consumed by the
            // firmware itself - the reply lives in the #B3 latch (exact
            // original semantics, no flag to wait on)
            h.runFrames(1);
            lle.push_back(h.chip->portDeviceInMethod(kPortData));
        };
        const auto query = [&](uint8_t cmd) {
            ASSERT_TRUE(h.sendCommandWait(cmd));
            lle.push_back(static_cast<uint8_t>(h.readGsByte(200)));
        };

        paramCmd(0x40, 0x2A); // MODVOL old
        paramCmd(0x40, 0x2B); // FXVOL old
        paramCmd(0x00, 0x34); // MODFADE old
        paramCmd(0x40, 0x35); // MTVOL old
        paramCmd(0x00, 0x3B); // FXFADE old
        paramCmd(0x40, 0x3D); // FXMVOL old
        query(0x36);
        query(0xF0); // ERRCODE

        h.chip->portDeviceOutMethod(kPortData, 0x01);
        ASSERT_TRUE(h.sendCommandWait(0x30));
        h.runFrames(1);
        lle.push_back(h.chip->portDeviceInMethod(kPortData)); // assigned slot (latch)
        for (uint8_t b : module)
            ASSERT_TRUE(h.sendDataWait(b));
        h.chip->portDeviceOutMethod(kPortCommand, 0xD2);
        ASSERT_TRUE(h.waitFlagClear(0x01, 1000));

        paramCmd(0x00, 0x31); // start current module
        paramCmd(0x00, 0x32); // stop
    }

    {
        EmulatorContext ctx(LoggerLevel::LogError);
        ctx.config.sound.gs_vol = 8000;
        ctx.config.frame = 69888;
        ctx.config.frame_duration_us = 19968;
        ctx.emulatorState.current_z80_frequency_multiplier = 1;
        ctx.emulatorState.hw_turbo_shift_applied = 0;
        SoundChip_GSLightweight chip(&ctx, 512);

        const auto paramCmd = [&](uint8_t param, uint8_t cmd) {
            chip.sendData(param);
            chip.sendCommand(cmd);
            lw.push_back(chip.readData());
        };
        const auto query = [&](uint8_t cmd) {
            chip.sendCommand(cmd);
            lw.push_back(chip.readData());
        };

        paramCmd(0x40, 0x2A);
        paramCmd(0x40, 0x2B);
        paramCmd(0x00, 0x34);
        paramCmd(0x40, 0x35);
        paramCmd(0x00, 0x3B);
        paramCmd(0x40, 0x3D);
        query(0x36);
        query(0xF0);

        chip.sendData(0x01);
        chip.sendCommand(0x30);
        lw.push_back(chip.readData());
        for (uint8_t b : module)
            chip.sendData(b);
        chip.sendCommand(0xD2);

        paramCmd(0x00, 0x31);
        paramCmd(0x00, 0x32);
    }

    ASSERT_EQ(lw.size(), lle.size()) << "reply-count divergence LW vs LLE";
    for (size_t i = 0; i < lle.size() && i < lw.size(); i++)
        EXPECT_EQ(lw[i], lle[i]) << "reply byte " << i << " (script position " << i << ")";
}

TEST(SoundChip_GSLightweight_Fidelity, AudioRmsWithinToleranceOfLle)
{
    // Runtime justification: the fidelity contract compares audio by
    // per-channel RMS tolerance, never bit-exactly (TDD 1.1) - both cards
    // must play the same module for ~2 s to produce a stable RMS. The
    // firmware's POST + upload dominates the wall time.
    const auto module = buildTestModule(false);
    double lleRms[2] = {0.0, 0.0};
    double lwRms[2] = {0.0, 0.0};

    {
        LleHarness h;
        ASSERT_TRUE(h.chip->isROMLoaded());
        ASSERT_TRUE(bootToPost(h));
        ASSERT_TRUE(uploadModuleLle(h, module));

        h.chip->portDeviceOutMethod(kPortData, 0x00);
        ASSERT_TRUE(h.sendCommandWait(0x31));
        h.runFrames(1);
        EXPECT_EQ(h.chip->portDeviceInMethod(kPortData), 1) << "LLE COM31 failed";
        h.runFrames(3); // settle past the start
        accumulateRms(*h.chip, 100, lleRms);
    }

    {
        EmulatorContext ctx(LoggerLevel::LogError);
        ctx.config.sound.gs_vol = 8000;
        ctx.config.frame = 69888;
        ctx.config.frame_duration_us = 19968;
        ctx.emulatorState.current_z80_frequency_multiplier = 1;
        ctx.emulatorState.hw_turbo_shift_applied = 0;
        SoundChip_GSLightweight chip(&ctx, 512);

        chip.sendData(0x01);
        chip.sendCommand(0x30);
        ASSERT_EQ(chip.readData(), 1);
        for (uint8_t b : module)
            chip.sendData(b);
        chip.sendCommand(0xD2);
        chip.sendData(0x00);
        chip.sendCommand(0x31);
        ASSERT_EQ(chip.readData(), 1) << "LW COM31 failed";
        chip.handleFrameStart();
        chip.handleFrameEnd(SAMPLES_PER_FRAME);
        chip.handleFrameStart();
        chip.handleFrameEnd(SAMPLES_PER_FRAME);
        chip.handleFrameStart();
        chip.handleFrameEnd(SAMPLES_PER_FRAME);
        accumulateRms(chip, 100, lwRms);
    }

    ASSERT_GT(lleRms[0], 100.0) << "the LLE reference produced no audio";
    ASSERT_GT(lwRms[0], 100.0) << "the LW card produced no audio";
    for (int ch = 0; ch < 2; ch++)
    {
        const double ratio = lleRms[ch] / lwRms[ch];
        EXPECT_GT(ratio, 0.5) << "channel " << ch << ": LW far louder than LLE";
        EXPECT_LT(ratio, 2.0) << "channel " << ch << ": LW far quieter than LLE";
    }
}

/// endregion </Fidelity and cross-validation vs LLE>

/// region <Runtime personality switching (SoundManager)>

class GSLightweight_Switch_Test : public ::testing::Test
{
protected:
    EmulatorContext* ctx = nullptr;
    SoundManager* sm = nullptr;

    void SetUp() override
    {
        ctx = new EmulatorContext(LoggerLevel::LogError);
        ctx->config.sound.gs_vol = 8000;
        ctx->config.sound.gsTypeKind = GSTypeKind::LW;
        ctx->config.sound.gsRamKB = 512;
        // The switch target LLE factory loads the firmware from the config
        // path; a bare context defaults to an empty string (no ROM)
        strcpy(ctx->config.gs_rom_path, "rom/gs105a.rom");
        ctx->config.frame = 69888;
        ctx->config.frame_duration_us = 19968;
        ctx->emulatorState.current_z80_frequency_multiplier = 1;
        ctx->emulatorState.hw_turbo_shift_applied = 0;
    }

    void TearDown() override
    {
        delete sm;
        delete ctx;
    }

    void createManager()
    {
        sm = new SoundManager(ctx);
        sm->reset();
    }

    // Frame cycling straight through the card (the switch itself is a
    // SoundManager call; playback keeps running card-side)
    void runCardFrames(GeneralSoundCard& card, int n)
    {
        for (int i = 0; i < n; i++)
        {
            card.handleFrameStart();
            card.handleFrameEnd(SAMPLES_PER_FRAME);
        }
    }
};

TEST_F(GSLightweight_Switch_Test, LwToLleReplaysModuleAndResumes)
{
    // Runtime justification: the v1 module handoff replays the upload
    // through a real gs105a POST + paced load (~1 s emulated, burned inside
    // the switch call itself).
    createManager();
    auto* card = sm->getGeneralSound();
    ASSERT_NE(card, nullptr);
    ASSERT_EQ(card->implementation(), GSCardImplementation::LW);

    // Load + start the module, leave it playing, and queue one unread reply
    card->sendData(0x01);
    card->sendCommand(0x30);
    ASSERT_EQ(card->readData(), 1);
    for (uint8_t b : buildTestModule(false))
        card->sendData(b);
    card->sendCommand(0xD2);
    card->sendData(0x00);
    card->sendCommand(0x31);
    ASSERT_EQ(card->readData(), 1);
    runCardFrames(*card, 5);
    // Queue one unread reply over the real host port: portDeviceOutMethod is
    // what bumps the triage counters - the automation sendCommand path does
    // not count traffic on either personality (verified parity)
    card->portDeviceOutMethod(0x00BB, 0x36); // reply 0xFF, deliberately left unread
    const uint64_t cmdsBefore = card->getActivityCounters().hostCommandsReceived;
    ASSERT_GE(cmdsBefore, 1u);

    ASSERT_TRUE(sm->switchGeneralSoundCard(GSTypeKind::Z80));
    auto* lle = sm->getGeneralSound();
    ASSERT_NE(lle, nullptr);
    EXPECT_NE(lle, card) << "the switch must replace the card object";
    EXPECT_EQ(lle->implementation(), GSCardImplementation::LLE);
    EXPECT_TRUE(lle->hasCoprocessor());

    // Mailbox survived: the unread 0xFF is still the pending #B3 byte
    EXPECT_NE(lle->readStatus() & 0x80, 0) << "mailbox snapshot lost";
    EXPECT_EQ(lle->readData(), 0xFF);

    // Module handoff: the replayed upload registered the module and the
    // switch resumed playback (COM2C -> CURMOD 1, DAC fetches flowing).
    // The reply's bit7 flag is consumed by the handler's own DATRG param
    // read (shared flip-flop) - wait for the bit0 ack, then read the latch
    lle->sendData(0x00);
    lle->sendCommand(0x2C);
    for (int i = 0; i < 200 && (lle->readStatus() & 0x01) != 0; i++)
        runCardFrames(*lle, 1);
    ASSERT_EQ(lle->readStatus() & 0x01, 0) << "COM2C never dispatched";
    runCardFrames(*lle, 1);
    EXPECT_EQ(lle->readData(), 1) << "module not registered after the replay";

    const uint64_t dac0 = lle->getActivityCounters().dacFetches;
    runCardFrames(*lle, 60);
    EXPECT_GT(lle->getActivityCounters().dacFetches, dac0 + 1000)
        << "playback did not resume on the LLE card";

    // Triage totals survive the handoff (the switch accumulates the outgoing
    // card's counters onto the new one)
    EXPECT_GE(lle->getActivityCounters().hostCommandsReceived, cmdsBefore);
}

TEST_F(GSLightweight_Switch_Test, LleToLwReplaysModuleAndResumes)
{
    // Runtime justification: switching FROM the LLE must start from a live
    // firmware (real POST ~0.3-1.1 s emulated) with a real COM30 load, so
    // the host-port capture mirrors an actual gs105a upload stream.
    ctx->config.sound.gsTypeKind = GSTypeKind::Z80;
    createManager();
    auto* lle = sm->getGeneralSound();
    ASSERT_NE(lle, nullptr);
    ASSERT_TRUE(lle->isROMLoaded());

    for (int guard = 0; guard < 1000; guard++)
    {
        runCardFrames(*lle, 5);
        if (lle->getActivityCounters().volumeLatchWrites >= 4)
            break;
    }
    ASSERT_GE(lle->getActivityCounters().volumeLatchWrites, 4u) << "POST never completed";
    if (lle->readStatus() & 0x80)
        (void)lle->readData(); // drain the phantom NUMPG reply

    // Real COM30 load over the host ports (this is what the capture layer
    // mirrors), then COM31 to start playback before the switch
    const auto waitFlagClear = [&](uint8_t mask) {
        for (int guard = 0; guard < 1000 && (lle->readStatus() & mask) != 0; guard++)
            runCardFrames(*lle, 1);
        return (lle->readStatus() & mask) == 0;
    };
    lle->portDeviceOutMethod(0x00B3, 0x01);
    lle->portDeviceOutMethod(0x00BB, 0x30);
    ASSERT_TRUE(waitFlagClear(0x01)) << "COM30 never dispatched";
    for (uint8_t b : buildTestModule(false))
    {
        lle->portDeviceOutMethod(0x00B3, b);
        ASSERT_TRUE(waitFlagClear(0x80)) << "payload byte stalled";
    }
    lle->portDeviceOutMethod(0x00BB, 0xD2);
    ASSERT_TRUE(waitFlagClear(0x01)) << "D2 never dispatched";
    lle->portDeviceOutMethod(0x00B3, 0x00);
    lle->portDeviceOutMethod(0x00BB, 0x31);
    ASSERT_TRUE(waitFlagClear(0x01)) << "COM31 never dispatched";
    runCardFrames(*lle, 3);
    if (lle->readStatus() & 0x80)
        (void)lle->readData(); // drain COM31's reply latch

    // One unread reply in flight, then the switch
    lle->sendCommand(0x36);
    for (int guard = 0; guard < 200 && !(lle->readStatus() & 0x80); guard++)
        runCardFrames(*lle, 1);
    ASSERT_NE(lle->readStatus() & 0x80, 0) << "COM36 never replied";

    const uint64_t commandsBefore = lle->getActivityCounters().hostCommandsReceived;
    ASSERT_TRUE(sm->switchGeneralSoundCard(GSTypeKind::LW));
    auto* lw = sm->getGeneralSound();
    ASSERT_NE(lw, nullptr);
    EXPECT_EQ(lw->implementation(), GSCardImplementation::LW);
    EXPECT_FALSE(lw->hasCoprocessor());

    // Mailbox kept: the unread reply survives on the LW card
    EXPECT_NE(lw->readStatus() & 0x80, 0) << "mailbox snapshot lost";
    EXPECT_EQ(lw->readData(), 0xFF);

    // Module handoff: the replayed upload parsed on the LW interpreter and
    // playback resumed (COM31 selector 0 replies the current module)
    lw->sendData(0x00);
    lw->sendCommand(0x31);
    EXPECT_EQ(lw->readData(), 0x01) << "module not registered after the replay";

    const uint64_t dac0 = lw->getActivityCounters().dacFetches;
    runCardFrames(*lw, 3);
    EXPECT_GT(lw->getActivityCounters().dacFetches, dac0)
        << "playback did not resume on the LW card";

    // Triage totals survive the handoff
    EXPECT_GE(lw->getActivityCounters().hostCommandsReceived, commandsBefore);
}

TEST_F(GSLightweight_Switch_Test, NoOpAndRejections)
{
    createManager();
    auto* card = sm->getGeneralSound();
    ASSERT_NE(card, nullptr);

    // Same personality: documented no-op, keeps the card object
    EXPECT_TRUE(sm->switchGeneralSoundCard(GSTypeKind::LW));
    EXPECT_EQ(sm->getGeneralSound(), card);

    // Unswitchable kinds are rejected, card untouched
    EXPECT_FALSE(sm->switchGeneralSoundCard(GSTypeKind::NONE));
    EXPECT_FALSE(sm->switchGeneralSoundCard(GSTypeKind::NGS));
    EXPECT_EQ(sm->getGeneralSound(), card);

    // No card fitted: rejected
    delete sm;
    sm = nullptr;
    ctx->config.sound.gsTypeKind = GSTypeKind::NONE;
    createManager();
    EXPECT_EQ(sm->getGeneralSound(), nullptr);
    EXPECT_FALSE(sm->switchGeneralSoundCard(GSTypeKind::Z80));
}

TEST_F(GSLightweight_Switch_Test, RequestAppliedAtFrameBoundary)
{
    // requestGeneralSoundCardSwitch only queues; the emulation thread's
    // handleFrameStart is the single point where the card may be deleted and
    // recreated (same ownership rule as the core-rate change)
    createManager();
    auto* card = sm->getGeneralSound();
    ASSERT_NE(card, nullptr);
    ASSERT_EQ(card->implementation(), GSCardImplementation::LW);

    EXPECT_FALSE(sm->requestGeneralSoundCardSwitch(GSTypeKind::NGS));
    EXPECT_TRUE(sm->requestGeneralSoundCardSwitch(GSTypeKind::Z80));

    // Queued, not applied: the card survives until the frame boundary
    EXPECT_EQ(sm->getGeneralSound(), card);
    EXPECT_EQ(card->implementation(), GSCardImplementation::LW);

    sm->handleFrameStart(); // the apply point (soundmanager.cpp handleFrameStart)
    auto* lle = sm->getGeneralSound();
    ASSERT_NE(lle, nullptr);
    EXPECT_NE(lle, card);
    EXPECT_EQ(lle->implementation(), GSCardImplementation::LLE);

    // A second frame start with nothing queued must not touch the card
    sm->handleFrameStart();
    EXPECT_EQ(sm->getGeneralSound(), lle);
}

/// endregion </Runtime personality switching (SoundManager)>
