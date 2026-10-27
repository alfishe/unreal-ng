// The AY device's quiet ticks (SoundChip_TurboSound::quietTick) against every tick rendered: two machines with the
// plain AY (one chip, or the two-AY TurboSound) run the same register traffic, one with the quiet ticks off, and the
// sound each sends to the host must be the same sample for sample - through silence long enough for the quiet ticks to
// take over, and out of it again (tone, noise, envelope, the second chip), in HQ (the quiet ticks' only mode) and LQ.

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <functional>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "base/featuremanager.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/mainloop.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/sound/chips/soundchip_turbosound.h"
#include "emulator/sound/soundmanager.h"

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
    Twin(TurboSoundKind kind, bool quiet, bool hq)
    {
        _emulator = EmulatorTestHelper::CreateEmulatorWithTurboSoundKind("PENTAGON", kind, LoggerLevel::LogError);
        EXPECT_NE(_emulator, nullptr);
        if (!_emulator)
            return;
        _context = _emulator->GetContext();
        _context->pFeatureManager->setFeature(Features::kSoundGeneration, true);
        _context->pFeatureManager->setFeature(Features::kSoundHQ, hq);
        _ay = dynamic_cast<SoundChip_TurboSound*>(_context->pSoundManager->getTurboSound());
        EXPECT_NE(_ay, nullptr);
        if (_ay)
            _ay->setQuietTicksEnabled(quiet);
        _context->pAudioCallback.store(&Capture, std::memory_order_release);
        _context->pAudioManagerObj.store(&_sound, std::memory_order_release);

        // The CPU parked (DI : HALT) before the ROM starts: only the test writes to the chips
        _context->pMemory->DirectWriteToZ80Memory(0x8000, 0xF3);
        _context->pMemory->DirectWriteToZ80Memory(0x8001, 0x76);
        Z80& z80 = *_context->pCore->GetZ80();
        z80.pc = 0x8000;
        z80.iff1 = z80.iff2 = 0;
        z80.halted = 0;
    }

    ~Twin()
    {
        if (!_emulator)
            return;
        _context->pAudioCallback.store(nullptr, std::memory_order_release);
        _context->pAudioManagerObj.store(nullptr, std::memory_order_release);
        EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    bool Ready() const { return _emulator && _ay; }
    void Frame() { reinterpret_cast<MainLoop_CUT*>(_context->pMainLoop)->RunFrame(); }
    void Out(uint16_t port, uint8_t value) { _context->pPortDecoder->DecodePortOut(port, value, 0); }
    void Reg(uint8_t reg, uint8_t value)
    {
        Out(0xFFFD, reg);
        Out(0xBFFD, value);
    }
    uint64_t QuietTicks() const { return _ay->quietTicks(); }
    const std::vector<int16_t>& Sound() const { return _sound; }

private:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    SoundChip_TurboSound* _ay = nullptr;
    std::vector<int16_t> _sound;
};

void Lockstep(TurboSoundKind kind, bool hq)
{
    Twin quiet(kind, true, hq);
    Twin every(kind, false, hq);
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
                FAIL() << (hq ? "HQ" : "LQ") << ": frame " << frame << " differs first at sample " << first;
            }
        }
    };
    auto both = [&](const std::function<void(Twin&)>& traffic) {
        traffic(quiet);
        traffic(every);
    };
    // Silence until the quiet ticks run again (the output filters settle to exactly zero first: about two seconds)
    auto untilQuiet = [&]() {
        const uint64_t before = quiet.QuietTicks();
        for (int i = 0; i < (hq ? 600 : 150) && quiet.QuietTicks() == before && !::testing::Test::HasFailure(); i++)
            frames(1);
        frames(5);
        if (hq)
        {
            EXPECT_GT(quiet.QuietTicks(), before) << "the quiet ticks never took over again (frame " << frame << ")";
        }
    };

    frames(20);  // the machine's own silence
    if (hq)
    {
        EXPECT_GT(quiet.QuietTicks(), 0u) << "a silent machine renders no quiet tick";
    }
    EXPECT_EQ(every.QuietTicks(), 0u);

    // A tone on A, then silence
    both([](Twin& t) {
        t.Reg(0x00, 0x40);
        t.Reg(0x01, 0x01);
        t.Reg(0x07, 0x3E);
        t.Reg(0x08, 0x0F);
    });
    frames(15);
    both([](Twin& t) { t.Reg(0x08, 0x00); });
    untilQuiet();

    // Out of it: noise on B and an envelope on C, then silence again
    both([](Twin& t) {
        t.Reg(0x06, 0x05);
        t.Reg(0x02, 0x80);
        t.Reg(0x04, 0x20);
        t.Reg(0x07, 0x2C);  // tone A on, tone B off, noise B on, tone C on
        t.Reg(0x09, 0x0C);
        t.Reg(0x0A, 0x10);  // C on the envelope
        t.Reg(0x0B, 0x00);
        t.Reg(0x0C, 0x08);
        t.Reg(0x0D, 0x0E);
    });
    frames(15);
    both([](Twin& t) {
        t.Reg(0x09, 0x00);
        t.Reg(0x0A, 0x00);
    });
    untilQuiet();

    if (kind == TurboSoundKind::AY)
    {
        // The second chip: a tone, then silence (#FE selects chip 1 on the TurboSound, #FF chip 0)
        both([](Twin& t) {
            t.Out(0xFFFD, 0xFE);
            t.Reg(0x00, 0x90);
            t.Reg(0x07, 0x3E);
            t.Reg(0x08, 0x0D);
        });
        frames(15);
        both([](Twin& t) {
            t.Reg(0x08, 0x00);
            t.Out(0xFFFD, 0xFF);
        });
        untilQuiet();
    }

    // A tone once more after the silence, and B on the envelope left running since it was shaped (no R13 write, which
    // would restart it): the phases the generators kept are heard
    both([](Twin& t) {
        t.Reg(0x08, 0x0B);
        t.Reg(0x09, 0x10);
    });
    frames(10);
}
}  // namespace

// Machine-bound: two machines run until the output filters settle (two seconds of emulated silence each time), far
// longer than 50 ms
TEST(AyQuietTicks_Test, OneAySoundThroughSilenceAndOutMatchesEveryTickRendered)
{
    Lockstep(TurboSoundKind::Single, true);
}

TEST(AyQuietTicks_Test, TurboSoundTwoAySoundThroughSilenceAndOutMatchesEveryTickRendered)
{
    Lockstep(TurboSoundKind::AY, true);
}

TEST(AyQuietTicks_Test, LowQualityIsUntouched)
{
    Lockstep(TurboSoundKind::AY, false);
}
