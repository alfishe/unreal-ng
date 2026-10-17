// The MultiSound's quiet runs (Ym2203Pair::renderQuietChannels) against every tick rendered: two Pentagons with the card
// run the same register traffic, one with the quiet runs off, and the sound each sends to the host must be the same
// sample for sample - through silences long enough for the quiet runs to take over and out of them again (SSG tone,
// noise and envelope on both YM2203s, an FM note alone), ending on an envelope that ran through the silences unwritten.

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <functional>
#include <memory>
#include <vector>

#include "multisoundstagedmachine.h"
#include "emulator/mainloop.h"
#include "emulator/slots/cards/multisound/multisoundcard.h"
#include "emulator/sound/chips/tsfm/ym2203pair.h"

using namespace multisoundtest;

namespace
{
void Capture(void* obj, int16_t* samples, size_t numSamples)
{
    auto* sink = static_cast<std::vector<int16_t>*>(obj);
    sink->insert(sink->end(), samples, samples + numSamples);
}

class Twin
{
public:
    explicit Twin(bool quiet) : _m(std::make_unique<StagedMachine>("pentagon128k", "zxbus.1 = multisound"))
    {
        if (!_m->Ok() || !_m->Card())
            return;
        _ok = true;
        _m->Context()->pFeatureManager->setFeature(Features::kSoundGeneration, true);
        _m->Card()->Card().Ym().setQuietTicksEnabled(quiet);
        ParkCpu(*_m);
        _m->Context()->pAudioCallback.store(&Capture, std::memory_order_release);
        _m->Context()->pAudioManagerObj.store(&_sound, std::memory_order_release);
    }
    ~Twin()
    {
        if (!_ok)
            return;
        _m->Context()->pAudioCallback.store(nullptr, std::memory_order_release);
        _m->Context()->pAudioManagerObj.store(nullptr, std::memory_order_release);
    }

    bool Ready() const { return _ok; }
    void Frame() { reinterpret_cast<MainLoop_CUT*>(_m->Context()->pMainLoop)->RunFrame(); }
    void Reg(uint8_t reg, uint8_t value)
    {
        Out(*_m, 0xFFFD, reg);
        Out(*_m, 0xBFFD, value);
    }
    void Control(uint8_t value) { Out(*_m, 0xFFFD, value); }
    uint64_t QuietTicks() const { return _m->Card()->Card().Ym().quietTicks(); }
    const std::vector<int16_t>& Sound() const { return _sound; }

private:
    std::unique_ptr<StagedMachine> _m;
    bool _ok = false;
    std::vector<int16_t> _sound;
};
}  // namespace

// Machine-bound: two machines with the card run until the chips' output filters settle (two seconds of emulated silence
// and more after the FM) three times, far longer than 50 ms
TEST(MultiSoundQuietTicks_Test, SoundThroughSilenceAndOutMatchesEveryTickRendered)
{
    Twin quiet(true);
    Twin every(false);
    ASSERT_TRUE(quiet.Ready());
    ASSERT_TRUE(every.Ready());

    int frame = 0;
    auto frames = [&](int count) {
        for (int i = 0; i < count && !::testing::Test::HasFailure(); i++, frame++)
        {
            quiet.Frame();
            every.Frame();
            ASSERT_EQ(quiet.Sound().size(), every.Sound().size()) << "frame " << frame;
            if (quiet.Sound() != every.Sound())
            {
                size_t first = 0;
                while (quiet.Sound()[first] == every.Sound()[first])
                    first++;
                FAIL() << "frame " << frame << " differs first at sample " << first;
            }
        }
    };
    // The traffic of a stretch must be heard: some sample of it off zero
    auto audible = [&](const std::function<void()>& stretch) {
        const size_t from = quiet.Sound().size();
        stretch();
        int peak = 0;
        for (size_t i = from; i < quiet.Sound().size(); i++)
            peak = std::max(peak, std::abs(int(quiet.Sound()[i])));
        EXPECT_GT(peak, 0) << "nothing was heard (frame " << frame << ")";
    };
    auto both = [&](const std::function<void(Twin&)>& traffic) {
        traffic(quiet);
        traffic(every);
    };
    auto untilQuiet = [&]() {
        const uint64_t before = quiet.QuietTicks();
        for (int i = 0; i < 1500 && quiet.QuietTicks() == before && !::testing::Test::HasFailure(); i++)
            frames(1);
        frames(5);
        EXPECT_GT(quiet.QuietTicks(), before) << "the quiet runs never took over again (frame " << frame << ")";
    };

    untilQuiet();  // the card's own silence
    EXPECT_EQ(every.QuietTicks(), 0u);

    // YM 0, FM on: a tone on A and an envelope shaped (heard at the end), then silence
    both([](Twin& t) {
        t.Control(0xFA);
        t.Reg(0x00, 0x40);
        t.Reg(0x01, 0x01);
        t.Reg(0x07, 0x3E);
        t.Reg(0x08, 0x0F);
        t.Reg(0x0B, 0x00);
        t.Reg(0x0C, 0x04);
        t.Reg(0x0D, 0x0A);
    });
    audible([&]() { frames(15); });
    both([](Twin& t) { t.Reg(0x08, 0x00); });
    untilQuiet();

    // YM 1: noise on B, an envelope on C; then silence
    both([](Twin& t) {
        t.Control(0xFB);
        t.Reg(0x06, 0x05);
        t.Reg(0x07, 0x2D);
        t.Reg(0x09, 0x0C);
        t.Reg(0x0A, 0x10);
        t.Reg(0x0B, 0x00);
        t.Reg(0x0C, 0x08);
        t.Reg(0x0D, 0x0E);
    });
    audible([&]() { frames(15); });
    both([](Twin& t) {
        t.Reg(0x09, 0x00);
        t.Reg(0x0A, 0x00);
    });
    untilQuiet();

    // YM 0 alone after the silence: an FM note (algorithm 7) and no SSG write - its first words land in a quiet run's
    // span, which must give way to them; released, then silence
    both([](Twin& t) {
        t.Control(0xFA);
        t.Reg(0xB0, 0x07);
        for (uint8_t op : {0x00, 0x04, 0x08, 0x0C})
        {
            t.Reg(static_cast<uint8_t>(0x30 + op), 0x01);
            t.Reg(static_cast<uint8_t>(0x40 + op), 0x10);
            t.Reg(static_cast<uint8_t>(0x50 + op), 0x1F);
            t.Reg(static_cast<uint8_t>(0x60 + op), 0x05);
            t.Reg(static_cast<uint8_t>(0x70 + op), 0x05);
            t.Reg(static_cast<uint8_t>(0x80 + op), 0x2F);
        }
        t.Reg(0xA4, 0x22);
        t.Reg(0xA0, 0x69);
        t.Reg(0x28, 0xF0);
    });
    audible([&]() { frames(15); });
    both([](Twin& t) { t.Reg(0x28, 0x00); });
    untilQuiet();

    // YM 0 once more: A's tone and B on the envelope that ran through the silences unwritten (no R13 write)
    both([](Twin& t) {
        t.Control(0xFA);
        t.Reg(0x08, 0x0B);
        t.Reg(0x09, 0x10);
    });
    audible([&]() { frames(10); });
}
