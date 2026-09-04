#include <gtest/gtest.h>
#include "tapeaudio/taperecognizer.h"

#include <cstdint>
#include <vector>

/// Recognizer tests (tape-audio-bridge design §8.2) over synthetic pulse
/// tracks in T-states — no audio involved, so every stage-1/2/3 decision is
/// tested at exact timing: ROM-standard $10, turbo $11, tone $12, pulse
/// fallback $13, pauses $20, checksum honesty and the pilotless case.

namespace
{
    constexpr uint32_t PILOT = 2168;
    constexpr uint32_t SYNC1 = 667;
    constexpr uint32_t SYNC2 = 735;
    constexpr uint32_t ZERO = 855;
    constexpr uint32_t ONE = 1710;
    constexpr uint32_t GAP_1S = 3500 * 1000;

    /// flag + payload, XOR checksum appended — a valid ROM block body
    std::vector<uint8_t> FramedBlock(std::initializer_list<uint8_t> payload)
    {
        std::vector<uint8_t> bytes = payload;
        uint8_t parity = 0;
        for (uint8_t byte : bytes)
        {
            parity ^= byte;
        }
        bytes.push_back(parity);
        return bytes;
    }

    /// pilot + sync + ROM-encoded bytes [+ trailing gap] — the canonical input
    std::vector<uint32_t> RomTrack(const std::vector<uint8_t>& bytes, size_t pilotPulses = 16,
                                   uint32_t gapAfter = GAP_1S)
    {
        std::vector<uint32_t> track(pilotPulses, PILOT);
        track.push_back(SYNC1);
        track.push_back(SYNC2);
        for (uint8_t byte : bytes)
        {
            for (int bit = 7; bit >= 0; bit--)
            {
                const uint32_t half = (byte & (1u << bit)) ? ONE : ZERO;
                track.push_back(half);
                track.push_back(half);
            }
        }
        if (gapAfter > 0)
        {
            track.push_back(gapAfter);
        }
        return track;
    }
}

TEST(TapeRecognizer_Test, StandardRomBlockRecognized)
{
    const std::vector<uint8_t> body = FramedBlock({0x00, 0x11, 0x22});
    const std::vector<RecognizedBlock> blocks = TapeRecognizer::Recognize(RomTrack(body));

    ASSERT_EQ(blocks.size(), 1u);
    EXPECT_EQ(blocks[0].kind, RecognizedBlockKind::StandardBlock);
    EXPECT_EQ(blocks[0].data, body);
    EXPECT_TRUE(blocks[0].checksumValid);
    EXPECT_EQ(blocks[0].pauseMs, 1000u);
    EXPECT_TRUE(blocks[0].notes.empty());
}

TEST(TapeRecognizer_Test, TwoBlocksBackToBackWithoutGap)
{
    // Block A (no trailing gap) then block B's pilot directly — the data
    // scan must stop at B's pilot, not absorb it as bits
    const std::vector<uint8_t> a = FramedBlock({0x00, 0xAA});
    const std::vector<uint8_t> b = FramedBlock({0xFF, 0x55});

    std::vector<uint32_t> track = RomTrack(a, 16, 0);
    const std::vector<uint32_t> partB = RomTrack(b, 16, 0);
    track.insert(track.end(), partB.begin(), partB.end());

    const std::vector<RecognizedBlock> blocks = TapeRecognizer::Recognize(track);
    ASSERT_EQ(blocks.size(), 2u);
    EXPECT_EQ(blocks[0].kind, RecognizedBlockKind::StandardBlock);
    EXPECT_EQ(blocks[0].data, a);
    EXPECT_EQ(blocks[0].pauseMs, 0u);
    EXPECT_EQ(blocks[1].kind, RecognizedBlockKind::StandardBlock);
    EXPECT_EQ(blocks[1].data, b);
}

TEST(TapeRecognizer_Test, EqualBitRunsDoNotTruncateData)
{
    // 0x00/0xFF bytes are runs of 8 equal bits — 16 equal half-periods that
    // must NOT be mistaken for the next block's pilot (regression guard for
    // the PilotRunAfterData distinctness check)
    const std::vector<uint8_t> body = FramedBlock({0x00, 0x00, 0xFF, 0xFF, 0x00});
    const std::vector<RecognizedBlock> blocks = TapeRecognizer::Recognize(RomTrack(body));

    ASSERT_EQ(blocks.size(), 1u);
    EXPECT_EQ(blocks[0].data, body);
    EXPECT_TRUE(blocks[0].checksumValid);
}

TEST(TapeRecognizer_Test, TurboBlockCarriesMeasuredTiming)
{
    const std::vector<uint8_t> body = FramedBlock({0xFF, 0x12, 0x34});

    std::vector<uint32_t> track(10, 2000);  // pilot run
    track.push_back(600);
    track.push_back(700);
    for (uint8_t byte : body)
    {
        for (int bit = 7; bit >= 0; bit--)
        {
            const uint32_t half = (byte & (1u << bit)) ? 1400 : 700;
            track.push_back(half);
            track.push_back(half);
        }
    }
    track.push_back(GAP_1S);

    const std::vector<RecognizedBlock> blocks = TapeRecognizer::Recognize(track);
    ASSERT_EQ(blocks.size(), 1u);
    EXPECT_EQ(blocks[0].kind, RecognizedBlockKind::TurboBlock);
    EXPECT_EQ(blocks[0].data, body);
    EXPECT_TRUE(blocks[0].checksumValid);

    const TapeTimingProfile& timing = blocks[0].timing;
    EXPECT_EQ(timing.profile, TapeSpeedProfileEnum::Custom);
    EXPECT_EQ(timing.pilotHalfPeriod, 2000u);
    EXPECT_EQ(timing.sync1, 600u);
    EXPECT_EQ(timing.sync2, 700u);
    EXPECT_EQ(timing.zeroHalfPeriod, 700u);
    EXPECT_EQ(timing.oneHalfPeriod, 1400u);
    EXPECT_EQ(timing.pilotPulses, 10u);
    EXPECT_EQ(timing.pauseMs, 1000u);
}

TEST(TapeRecognizer_Test, ChecksumMismatchIsHonest)
{
    std::vector<uint8_t> body = FramedBlock({0x00, 0x11});
    body.back() ^= 0x01;  // break the parity

    const std::vector<RecognizedBlock> blocks = TapeRecognizer::Recognize(RomTrack(body));
    ASSERT_EQ(blocks.size(), 1u);
    EXPECT_EQ(blocks[0].kind, RecognizedBlockKind::StandardBlock);
    EXPECT_FALSE(blocks[0].checksumValid);
    ASSERT_EQ(blocks[0].notes.size(), 1u);
}

TEST(TapeRecognizer_Test, PureToneRecognized)
{
    std::vector<uint32_t> track(50, PILOT);
    track.push_back(GAP_1S / 10);

    const std::vector<RecognizedBlock> blocks = TapeRecognizer::Recognize(track);
    ASSERT_EQ(blocks.size(), 2u);  // tone + the trailing pause gap
    EXPECT_EQ(blocks[0].kind, RecognizedBlockKind::PureTone);
    EXPECT_EQ(blocks[0].tonePeriod, PILOT);
    EXPECT_EQ(blocks[0].tonePulses, 50u);
    EXPECT_EQ(blocks[1].kind, RecognizedBlockKind::PauseGap);
    EXPECT_EQ(blocks[1].pauseMs, 100u);
}

TEST(TapeRecognizer_Test, IrregularSignalFallsBackToPulseSequence)
{
    const std::vector<uint32_t> track = {3000, 400, 5000, 300, 2500, 600};
    const std::vector<RecognizedBlock> blocks = TapeRecognizer::Recognize(track);

    ASSERT_EQ(blocks.size(), 1u);
    EXPECT_EQ(blocks[0].kind, RecognizedBlockKind::PulseSequence);
    EXPECT_EQ(blocks[0].pulses, track);
}

TEST(TapeRecognizer_Test, PilotlessDataBecomesZeroPilotTurbo)
{
    // Leading byte must not be uniform: 16 equal halves at the very start
    // are indistinguishable from a pilot before any data level is
    // established (a documented limitation) — 0xA5 breaks the run.
    const std::vector<uint8_t> body = FramedBlock({0xA5, 0x42});
    std::vector<uint32_t> track;
    for (uint8_t byte : body)
    {
        for (int bit = 7; bit >= 0; bit--)
        {
            const uint32_t half = (byte & (1u << bit)) ? ONE : ZERO;
            track.push_back(half);
            track.push_back(half);
        }
    }
    track.push_back(GAP_1S);

    const std::vector<RecognizedBlock> blocks = TapeRecognizer::Recognize(track);
    ASSERT_EQ(blocks.size(), 1u);
    EXPECT_EQ(blocks[0].kind, RecognizedBlockKind::TurboBlock);
    EXPECT_EQ(blocks[0].data, body);
    EXPECT_EQ(blocks[0].timing.pilotPulses, 0u);
    EXPECT_FALSE(blocks[0].notes.empty());
}

TEST(TapeRecognizer_Test, StandaloneGapIsAPause)
{
    const std::vector<uint32_t> track = {350000u};
    const std::vector<RecognizedBlock> blocks = TapeRecognizer::Recognize(track);
    ASSERT_EQ(blocks.size(), 1u);
    EXPECT_EQ(blocks[0].kind, RecognizedBlockKind::PauseGap);
    EXPECT_EQ(blocks[0].pauseMs, 100u);
}
