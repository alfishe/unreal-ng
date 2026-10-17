// The Next's quiet audio runs (NextAudio::QuietTicks) against every tick rendered: two Next machines run the same register
// traffic, one with the quiet runs off, and the sound each sends to the host must be the same sample for sample - through
// silence long enough for the quiet runs to take over (the device's DC filter settles first), out of it again on each of
// the three chips (tone, noise, envelope), with the DAC held off its middle and a pan change, ending on an envelope that
// ran through the silence unwritten.

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
#include "emulator/ports/models/portdecoder_next.h"
#include "emulator/sound/nextaudio.h"

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
    explicit Twin(bool quiet)
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("NEXT", LoggerLevel::LogError, RamPowerOn::Zero);
        EXPECT_NE(_emulator, nullptr);
        if (!_emulator)
            return;
        _context = _emulator->GetContext();
        _context->pFeatureManager->setFeature(Features::kSoundGeneration, true);
        _ports = dynamic_cast<PortDecoder_Next*>(_context->pPortDecoder);
        EXPECT_NE(_ports, nullptr);
        if (!_ports)
            return;
        _ports->Audio().SetQuietTicksEnabled(quiet);
        _ports->Board().Write(0x08, 0x02 | 0x08);  // turbosound and the DAC on
        _context->pAudioCallback.store(&Capture, std::memory_order_release);
        _context->pAudioManagerObj.store(&_sound, std::memory_order_release);

        // The CPU parked (DI : HALT) before the firmware starts: only the test writes to the chips
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

    bool Ready() const { return _emulator && _ports; }
    void Frame() { reinterpret_cast<MainLoop_CUT*>(_context->pMainLoop)->RunFrame(); }
    void Out(uint16_t port, uint8_t value) { _ports->DecodePortOut(port, value, 0); }
    void Reg(uint8_t reg, uint8_t value)
    {
        Out(0xFFFD, reg);
        Out(0xBFFD, value);
    }
    void NextReg(uint8_t reg, uint8_t value) { _ports->Board().Write(reg, value); }
    uint64_t QuietTicks() { return _ports->Audio().QuietTicks(); }
    const std::vector<int16_t>& Sound() const { return _sound; }

private:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    PortDecoder_Next* _ports = nullptr;
    std::vector<int16_t> _sound;
};
}  // namespace

// Machine-bound: two Next machines run until the chips' output filters settle (about two seconds of emulated silence)
// three times, far longer than 50 ms
TEST(NextAudioQuietTicks_Test, SoundThroughSilenceAndOutOnEveryChipMatchesEveryTickRendered)
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

    untilQuiet();  // the machine's own silence
    EXPECT_EQ(every.QuietTicks(), 0u);

    // Chip 0: a tone on A, an envelope shaped on it (heard only at the end), then silence
    both([](Twin& t) {
        t.Out(0xFFFD, 0xFF);
        t.Reg(0x00, 0x40);
        t.Reg(0x01, 0x01);
        t.Reg(0x07, 0x3E);
        t.Reg(0x08, 0x0F);
        t.Reg(0x0B, 0x00);
        t.Reg(0x0C, 0x04);
        t.Reg(0x0D, 0x0A);
    });
    frames(15);
    both([](Twin& t) { t.Reg(0x08, 0x00); });
    untilQuiet();

    // Chips 1 and 2 at once: noise on B and an envelope on C of chip 1, a tone on chip 2 panned left only; then silence with
    // the DAC held off its middle
    both([](Twin& t) {
        t.Out(0xFFFD, 0xFE);
        t.Reg(0x06, 0x05);
        t.Reg(0x07, 0x2D);
        t.Reg(0x09, 0x0C);
        t.Reg(0x0A, 0x10);
        t.Reg(0x0B, 0x00);
        t.Reg(0x0C, 0x08);
        t.Reg(0x0D, 0x0E);
        t.Out(0xFFFD, 0xDD);  // chip 2 (01), pan bit 6 (left) only
        t.Reg(0x02, 0x90);
        t.Reg(0x07, 0x3D);
        t.Reg(0x09, 0x0D);
    });
    frames(15);
    both([](Twin& t) {
        t.Reg(0x09, 0x00);    // chip 2
        t.Out(0xFFFD, 0xFE);  // chip 1 (the pan bits of chip 2 stay)
        t.Reg(0x09, 0x00);
        t.Reg(0x0A, 0x00);
        t.NextReg(0x2D, 0xA0);  // DAC A and D
    });
    untilQuiet();

    // Chip 0 once more: A's tone and B on the envelope that ran through the silences unwritten (no R13 write)
    both([](Twin& t) {
        t.Out(0xFFFD, 0xFF);
        t.Reg(0x08, 0x0B);
        t.Reg(0x09, 0x10);
    });
    frames(10);
}
