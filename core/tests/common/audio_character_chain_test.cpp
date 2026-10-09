#include "stdafx.h"
#include "pch.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

#include <gtest/gtest.h>

#include "common/sound/filters/audio_character_chain.h"

/// AudioCharacterChain room-mode IDs: the stable text form used by settings,
/// automation and frontend persistence. Every mode round-trips; the parser
/// also takes the dB forms people type ("-15dB", "15").

using RoomMode = AudioCharacterChain::RoomMode;

TEST(AudioCharacterChain_Test, RoomModeIdsRoundTrip)
{
    for (int i = 0; i < static_cast<int>(RoomMode::COUNT); i++)
    {
        const auto mode = static_cast<RoomMode>(i);
        RoomMode parsed = RoomMode::COUNT;
        ASSERT_TRUE(AudioCharacterChain::parseRoomMode(AudioCharacterChain::roomModeId(mode), parsed))
            << AudioCharacterChain::roomModeId(mode);
        EXPECT_EQ(parsed, mode);
    }
}

TEST(AudioCharacterChain_Test, RoomModeParseAcceptsDbForms)
{
    RoomMode mode = RoomMode::Off;
    EXPECT_TRUE(AudioCharacterChain::parseRoomMode("-15dB", mode));
    EXPECT_EQ(mode, RoomMode::Room_15dB);
    EXPECT_TRUE(AudioCharacterChain::parseRoomMode("9", mode));
    EXPECT_EQ(mode, RoomMode::Room_9dB);
    EXPECT_TRUE(AudioCharacterChain::parseRoomMode("OFF", mode));
    EXPECT_EQ(mode, RoomMode::Off);

    mode = RoomMode::Room_6dB;
    EXPECT_FALSE(AudioCharacterChain::parseRoomMode("7db", mode));
    EXPECT_FALSE(AudioCharacterChain::parseRoomMode("", mode));
    EXPECT_FALSE(AudioCharacterChain::parseRoomMode("loud", mode));
    EXPECT_EQ(mode, RoomMode::Room_6dB) << "a failed parse must not touch the output";
}

/// Bypass and live switching (owner decision 2026-10-07): a chain with every
/// effect off - or gated off (Sound HQ off) - leaves its buffer bit-identical;
/// switching an effect ramps over one frame with no step beyond the signal's
/// own; an effect switched on again never replays audio from before it was off.

namespace
{
constexpr double kRate = 44100.0;
constexpr double kPi = 3.14159265358979323846;
constexpr int32_t kFrame = 882;   // one 50 Hz frame at 44.1 kHz

/// Stereo test signal: different sines left / right (smooth, so a switch step stands out against the signal's own)
std::vector<int16_t> Sines(int32_t frames, int32_t offset = 0, double amplitude = 12000.0)
{
    std::vector<int16_t> out(static_cast<size_t>(frames) * 2);
    for (int32_t n = 0; n < frames; n++)
    {
        const double t = static_cast<double>(n + offset) / kRate;
        out[n * 2] = static_cast<int16_t>(amplitude * std::sin(2.0 * kPi * 440.0 * t));
        out[n * 2 + 1] = static_cast<int16_t>(0.75 * amplitude * std::sin(2.0 * kPi * 660.0 * t + 1.0));
    }
    return out;
}

/// A chain at the AY settings unreal-ng ships (AY chip type and punch preset)
void ConfigureAy(AudioCharacterChain& chain, bool punch, RoomMode room)
{
    chain.setup(kRate);
    chain.setChipType(AudioCharacterChain::ChipType::AY);
    chain.setPunchPreset(AudioCharacterChain::PunchPreset::AY);
    chain.setPunchEnabled(punch);
    chain.setRoomMode(room);
}

/// Largest sample-to-sample step on either channel
int MaxStep(const std::vector<int16_t>& x)
{
    int worst = 0;
    for (size_t i = 2; i < x.size(); i++)
        worst = std::max(worst, std::abs(int(x[i]) - int(x[i - 2])));
    return worst;
}

/// Run `frames` frames of the sine signal through the chain, calling `atFrame(k)` before frame k
template <typename F>
std::vector<int16_t> RunFrames(AudioCharacterChain& chain, int frames, F atFrame)
{
    std::vector<int16_t> out;
    for (int k = 0; k < frames; k++)
    {
        atFrame(k);
        std::vector<int16_t> frame = Sines(kFrame, k * kFrame);
        chain.processInt16(frame.data(), kFrame);
        out.insert(out.end(), frame.begin(), frame.end());
    }
    return out;
}
}  // namespace

TEST(AudioCharacterChain_Test, ChainOffLeavesBufferBitIdentical)
{
    // Every int16 value, extremes included: the processed path would scale them by 32767 / 32768 and truncate
    std::vector<int16_t> input(65536 * 2);
    for (int32_t v = 0; v < 65536; v++)
    {
        input[v * 2] = static_cast<int16_t>(v - 32768);
        input[v * 2 + 1] = static_cast<int16_t>(32767 - v);
    }

    // All effects off
    {
        AudioCharacterChain chain;
        ConfigureAy(chain, false, RoomMode::Off);
        EXPECT_TRUE(chain.isBypassed());
        std::vector<int16_t> buffer = input;
        for (int k = 0; k < 3; k++)
            chain.processInt16(buffer.data(), 65536);
        EXPECT_EQ(buffer, input) << "a chain with every effect off changed the audio";
    }

    // Effects on, the chain gated off (Sound HQ off) before its first frame: untouched from the first frame
    {
        AudioCharacterChain chain;
        ConfigureAy(chain, true, RoomMode::Room_9dB);
        chain.setActive(false);
        EXPECT_TRUE(chain.isBypassed());
        std::vector<int16_t> buffer = input;
        chain.processInt16(buffer.data(), 65536);
        EXPECT_EQ(buffer, input) << "a gated-off chain changed the audio";
    }

    // Effects on, then switched off while running: one ramp frame, then untouched
    {
        AudioCharacterChain chain;
        ConfigureAy(chain, true, RoomMode::Room_9dB);
        std::vector<int16_t> frame = Sines(kFrame);
        chain.processInt16(frame.data(), kFrame);
        ASSERT_NE(frame, Sines(kFrame)) << "the chain must process while on";
        chain.setPunchEnabled(false);
        chain.setRoomMode(RoomMode::Off);
        EXPECT_FALSE(chain.isBypassed()) << "the switch frame ramps out";
        frame = Sines(kFrame, kFrame);
        chain.processInt16(frame.data(), kFrame);
        EXPECT_TRUE(chain.isBypassed());
        std::vector<int16_t> buffer = input;
        chain.processInt16(buffer.data(), 65536);
        EXPECT_EQ(buffer, input) << "after the ramp-out frame the chain must pass audio untouched";
    }
}

TEST(AudioCharacterChain_Test, SwitchingRampsWithoutStep)
{
    struct Case
    {
        const char* name;
        bool punchFrom, punchTo;
        RoomMode roomFrom, roomTo;
        bool gateTo;   // the chain's host gate after the switch
    };
    const Case cases[] = {
        {"punch on", false, true, RoomMode::Off, RoomMode::Off, true},
        {"punch off", true, false, RoomMode::Off, RoomMode::Off, true},
        {"room on", false, false, RoomMode::Off, RoomMode::Room_6dB, true},
        {"room off", false, false, RoomMode::Room_6dB, RoomMode::Off, true},
        {"both on", false, true, RoomMode::Off, RoomMode::Room_6dB, true},
        {"both off", true, false, RoomMode::Room_6dB, RoomMode::Off, true},
        {"room on, punch stays on", true, true, RoomMode::Off, RoomMode::Room_6dB, true},
        {"punch off, room stays on", true, false, RoomMode::Room_6dB, RoomMode::Room_6dB, true},
        {"room level -9 -> -3 dB", true, true, RoomMode::Room_9dB, RoomMode::Room_3dB, true},
        {"gate off (Sound HQ off)", true, true, RoomMode::Room_6dB, RoomMode::Room_6dB, false},
    };
    constexpr int kFrames = 8;
    for (const Case& c : cases)
    {
        // The signal's own steps: dry, and fully processed at the "from" and "to" settings
        std::vector<int16_t> dry;
        for (int k = 0; k < kFrames; k++)
        {
            const std::vector<int16_t> frame = Sines(kFrame, k * kFrame);
            dry.insert(dry.end(), frame.begin(), frame.end());
        }
        AudioCharacterChain fromChain, toChain;
        ConfigureAy(fromChain, c.punchFrom, c.roomFrom);
        ConfigureAy(toChain, c.punchTo, c.roomTo);
        toChain.setActive(c.gateTo);
        const std::vector<int16_t> fromOut = RunFrames(fromChain, kFrames, [](int) {});
        const std::vector<int16_t> toOut = RunFrames(toChain, kFrames, [](int) {});
        const int ownStep = std::max({MaxStep(dry), MaxStep(fromOut), MaxStep(toOut)});

        for (const int switchFrame : {1, 3, 6})
        {
            AudioCharacterChain chain;
            ConfigureAy(chain, c.punchFrom, c.roomFrom);
            const std::vector<int16_t> out = RunFrames(chain, kFrames, [&](int k) {
                if (k == switchFrame)
                {
                    chain.setPunchEnabled(c.punchTo);
                    chain.setRoomMode(c.roomTo);
                    chain.setActive(c.gateTo);
                }
            });
            // A hard switch steps by up to the whole effect (thousands of LSB here); the ramp spreads it over the
            // frame: the effect / kFrame per sample on top of the signal's own step
            EXPECT_LE(MaxStep(out), ownStep + 16) << c.name << ", switch at frame " << switchFrame;

            // The ramp spans exactly the switch frame: the frame before it is the "from" setting, the switch frame
            // differs from both ends, and from the frame after it on the chain is steady at the "to" setting
            const size_t frameValues = static_cast<size_t>(kFrame) * 2;
            auto frameOf = [&](const std::vector<int16_t>& x, int k) {
                return std::vector<int16_t>(x.begin() + static_cast<std::ptrdiff_t>(k * frameValues),
                                            x.begin() + static_cast<std::ptrdiff_t>((k + 1) * frameValues));
            };
            EXPECT_EQ(frameOf(out, switchFrame - 1), frameOf(fromOut, switchFrame - 1)) << c.name;
            EXPECT_NE(frameOf(out, switchFrame), frameOf(fromOut, switchFrame)) << c.name << ": no ramp";
            // The midpoint of the switch frame sits between the two ends (on the side-band the effect changes)
            const size_t mid = static_cast<size_t>(switchFrame) * frameValues + frameValues / 2;
            for (size_t ch = 0; ch < 2; ch++)
            {
                const int a = fromOut[mid + ch], b = toOut[mid + ch], m = out[mid + ch];
                EXPECT_LE(m, std::max(a, b) + 2) << c.name << " ch " << ch;
                EXPECT_GE(m, std::min(a, b) - 2) << c.name << " ch " << ch;
            }
            // Room only: the delay line holds the same audio as a chain that was always on, so from the frame
            // after the switch on the output is the steady "to" output sample for sample (the punch envelope
            // started from rest, it converges instead)
            if (!c.punchFrom && !c.punchTo)
                EXPECT_EQ(frameOf(out, switchFrame + 1), frameOf(toOut, switchFrame + 1)) << c.name;
        }
    }
}

TEST(AudioCharacterChain_Test, SwitchedOnAgainReplaysNothing)
{
    // A marker burst on the left only, then silence. The room puts the left on the right 2 ms late: a stale delay
    // line would put the marker on the right after re-enabling; a stale punch follower would put a step there
    auto marker = [] {
        std::vector<int16_t> x(static_cast<size_t>(kFrame) * 2, 0);
        for (int32_t n = 0; n < kFrame; n++)
            x[n * 2] = static_cast<int16_t>((n % 7 < 3) ? 20000 : -20000);
        return x;
    };
    const std::vector<int16_t> silence(static_cast<size_t>(kFrame) * 2, 0);

    enum class Off
    {
        Effects,     // punch and room switched off
        Gate,        // Sound HQ off
        Reset,       // a gap (sound off, TTD restore)
        RateChange,  // setup() at another rate
    };
    for (const Off how : {Off::Effects, Off::Gate, Off::Reset, Off::RateChange})
    for (const int offFrames : {0, 1, 4})
    {
        // Effects / gate off for no frame at all never switched anything: the room's tail of the marker in the next
        // frame is its normal echo. A gap (reset, rate change) cuts it at once
        if (offFrames == 0 && (how == Off::Effects || how == Off::Gate))
            continue;
        AudioCharacterChain chain;
        ConfigureAy(chain, true, RoomMode::Room_3dB);
        std::vector<int16_t> frame = marker();
        chain.processInt16(frame.data(), kFrame);
        frame = marker();
        chain.processInt16(frame.data(), kFrame);

        switch (how)
        {
            case Off::Effects:
                chain.setPunchEnabled(false);
                chain.setRoomMode(RoomMode::Off);
                break;
            case Off::Gate:
                chain.setActive(false);
                break;
            case Off::Reset:
                chain.reset();
                break;
            case Off::RateChange:
                chain.setup(48000.0);
                chain.setRoomMode(RoomMode::Room_3dB);
                break;
        }
        for (int k = 0; k < offFrames; k++)
        {
            frame = silence;
            chain.processInt16(frame.data(), kFrame);
        }
        chain.setPunchEnabled(true);
        chain.setRoomMode(RoomMode::Room_3dB);
        chain.setActive(true);

        // Back on over silence: the output is silence (no marker on either channel, no step)
        for (int k = 0; k < 3; k++)
        {
            frame = silence;
            chain.processInt16(frame.data(), kFrame);
            EXPECT_EQ(frame, silence) << "mode " << int(how) << ", " << offFrames << " frames off, frame " << k
                                      << " after re-enabling: old audio replayed";
        }
    }
}
