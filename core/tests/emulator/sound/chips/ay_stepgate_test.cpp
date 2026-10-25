// The AY device's step gate (SoundChip_TurboSound::handleStep, settleSteps) against every step taken the full way: two
// machines with the plain AY (one chip, or the two-AY TurboSound) run the same program - random register writes at random
// places in the frame, dense and sparse (long silences between bursts) - one with the gate closed, and the sound each
// sends to the host must be the same sample for sample, and the device state (the TTD blob) the same at the end; HQ, LQ
// and a 2x host speed (the gate holds only at 1x).

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "base/featuremanager.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/mainloop.h"
#include "emulator/memory/memory.h"
#include "emulator/sound/chips/soundchip_turbosound.h"
#include "emulator/sound/soundmanager.h"

namespace
{
void Capture(void* obj, int16_t* samples, size_t numSamples)
{
    auto* sink = static_cast<std::vector<int16_t>*>(obj);
    sink->insert(sink->end(), samples, samples + numSamples);
}

/// A loop writing a random register of a random chip with a random value, then waiting a random while (sparse: up to a
/// third of a frame and more), with a write above the SSG registers (#28, ignored) now and then. Interrupts off
std::vector<uint8_t> Program(bool sparse)
{
    std::vector<uint8_t> code = {
        0xF3,              // di
        0x31, 0xF0, 0xBF,  // ld sp,#BFF0
    };
    const size_t loop = code.size();
    code.insert(code.end(), {
                                0xED, 0x5F, 0xE6, 0x01, 0xF6, 0xFE,  // ld a,r : and 1 : or #FE (chip 1 / 0 on the TurboSound)
                                0x01, 0xFD, 0xFF, 0xED, 0x79,        // ld bc,#FFFD : out (c),a
                                0xED, 0x5F, 0xE6, 0x0F,              // ld a,r : and #0F (an SSG register)
                                0xED, 0x79,                          // out (c),a
                                0xED, 0x5F, 0x06, 0xBF, 0xED, 0x79,  // ld a,r : ld b,#BF : out (c),a (its value)
                                0xED, 0x5F, 0xE6, 0x07, 0x20, 0x0C,  // ld a,r : and 7 : jr nz,+12 (1 in 8)
                                0x3E, 0x28, 0x06, 0xFF, 0xED, 0x79,  // ld a,#28 : ld b,#FF : out (c),a
                                0xED, 0x5F, 0x06, 0xBF, 0xED, 0x79,  // ld a,r : ld b,#BF : out (c),a
                            });
    if (sparse)
        code.insert(code.end(), {
                                    0xED, 0x5F, 0x67, 0x2E, 0x00,  // ld a,r : ld h,a : ld l,0 (up to 255 x 256 turns)
                                    0x2B, 0x7C, 0xB5, 0x20, 0xFB,  // dec hl : ld a,h : or l : jr nz,-5
                                });
    else
        code.insert(code.end(), {
                                    0xED, 0x5F, 0xE6, 0x3F, 0x3C, 0x47,  // ld a,r : and #3F : inc a : ld b,a
                                    0x10, 0xFE,                          // djnz $
                                });
    code.push_back(0x18);  // jr loop
    code.push_back(static_cast<uint8_t>(loop - (code.size() + 1)));
    return code;
}

/// Beeps after silences: a tone on A, then volume 15 for a moment and 0 for some three seconds (the output filters settle
/// and the device sleeps) with a random extra wait, again and again - the writes land mid-frame while the device sleeps
std::vector<uint8_t> BeepsAfterSilence()
{
    std::vector<uint8_t> code = {
        0xF3,              // di
        0x31, 0xF0, 0xBF,  // ld sp,#BFF0
        0x01, 0xFD, 0xFF, 0x3E, 0x00, 0xED, 0x79, 0x06, 0xBF, 0x3E, 0x40, 0xED, 0x79,  // R0 = #40
        0x06, 0xFF, 0x3E, 0x01, 0xED, 0x79, 0x06, 0xBF, 0x3E, 0x01, 0xED, 0x79,        // R1 = #01
        0x06, 0xFF, 0x3E, 0x07, 0xED, 0x79, 0x06, 0xBF, 0x3E, 0x3E, 0xED, 0x79,        // R7 = #3E
    };
    const size_t loop = code.size();
    code.insert(code.end(), {
                                0x06, 0xFF, 0x3E, 0x08, 0xED, 0x79, 0x06, 0xBF, 0x3E, 0x0F, 0xED, 0x79,  // R8 = 15
                                0x21, 0x00, 0x08, 0x2B, 0x7C, 0xB5, 0x20, 0xFB,                          // ~53 000 T
                                0x06, 0xFF, 0x3E, 0x08, 0xED, 0x79, 0x06, 0xBF, 0x3E, 0x00, 0xED, 0x79,  // R8 = 0
                                0x16, 0x07,                                                              // ld d,7
                                0x21, 0x00, 0x00, 0x2B, 0x7C, 0xB5, 0x20, 0xFB,  // ld hl,0 : 65536 turns (1.7 M T)
                                0x15, 0x20, 0xF5,                                // dec d : jr nz (3.4 s in all)
                                0xED, 0x5F, 0x67, 0x2E, 0x00, 0x2B, 0x7C, 0xB5, 0x20, 0xFB,  // ld a,r : ld h,a : a random wait
                                0x01, 0xFD, 0xFF,                                // ld bc,#FFFD
                            });
    code.push_back(0x18);  // jr loop
    code.push_back(static_cast<uint8_t>(loop - (code.size() + 1)));
    return code;
}

class Twin
{
public:
    Twin(TurboSoundKind kind, bool gate, bool hq, uint8_t speed, bool sparse, bool beeps = false)
    {
        _emulator = EmulatorTestHelper::CreateEmulatorWithTurboSoundKind("PENTAGON", kind, LoggerLevel::LogError);
        EXPECT_NE(_emulator, nullptr);
        if (!_emulator)
            return;
        _context = _emulator->GetContext();
        _context->pFeatureManager->setFeature(Features::kSoundGeneration, true);
        _context->pFeatureManager->setFeature(Features::kSoundHQ, hq);
        _tsfm = dynamic_cast<SoundChip_TurboSound*>(_context->pSoundManager->getTurboSound());
        EXPECT_NE(_tsfm, nullptr);
        if (_tsfm)
        {
            _tsfm->setStepGate(gate);
            _tsfm->setQuietTicksEnabled(gate);  // the full way renders every tick: no quiet runs either
        }
        if (speed != 1)
        {
            EXPECT_TRUE(_emulator->SetSpeedMultiplier(speed));
        }
        _context->pAudioCallback.store(&Capture, std::memory_order_release);
        _context->pAudioManagerObj.store(&_sound, std::memory_order_release);

        const std::vector<uint8_t> code = beeps ? BeepsAfterSilence() : Program(sparse);
        for (size_t i = 0; i < code.size(); i++)
            _context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), code[i]);
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

    bool Ready() const { return _emulator && _tsfm; }
    void Frame() { reinterpret_cast<MainLoop_CUT*>(_context->pMainLoop)->RunFrame(); }
    const std::vector<int16_t>& Sound() const { return _sound; }
    uint64_t QuietTicks() const { return _tsfm->quietTicks(); }
    std::vector<uint8_t> State() const
    {
        std::vector<uint8_t> blob(_tsfm->TTDStateSize());
        _tsfm->TTDSaveState(blob.data());
        return blob;
    }

private:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    SoundChip_TurboSound* _tsfm = nullptr;
    std::vector<int16_t> _sound;
};

void Lockstep(TurboSoundKind kind, bool hq, uint8_t speed, bool sparse, int frames, bool beeps = false)
{
    Twin gated(kind, true, hq, speed, sparse, beeps);
    Twin full(kind, false, hq, speed, sparse, beeps);
    ASSERT_TRUE(gated.Ready());
    ASSERT_TRUE(full.Ready());
    for (int frame = 0; frame < frames; frame++)
    {
        gated.Frame();
        full.Frame();
        ASSERT_EQ(gated.Sound().size(), full.Sound().size()) << "frame " << frame;
        if (gated.Sound() != full.Sound())
        {
            size_t first = 0;
            while (gated.Sound()[first] == full.Sound()[first])
                first++;
            FAIL() << (kind == TurboSoundKind::AY ? "TS " : "AY ") << (hq ? "HQ" : "LQ") << " x" << int(speed) << (sparse ? " sparse" : " dense") << ": frame " << frame
                   << " differs first at sample " << first;
        }
    }
    EXPECT_EQ(gated.State(), full.State()) << "the device state at the end";
    if (beeps)
    {
        EXPECT_GT(gated.QuietTicks(), 0u) << "the device never slept";
    }
}
}  // namespace

// Machine-bound: two machines run whole frames of sound (~1 ms each), longer than 50 ms
TEST(AyStepGate_Test, DenseWritesMatchEveryStepTakenTheFullWay)
{
    Lockstep(TurboSoundKind::AY, true, 1, false, 40);
    Lockstep(TurboSoundKind::Single, true, 1, false, 20);
    Lockstep(TurboSoundKind::AY, false, 1, false, 20);
}

TEST(AyStepGate_Test, SparseWritesAndSilencesMatchEveryStepTakenTheFullWay)
{
    Lockstep(TurboSoundKind::AY, true, 1, true, 80);
    Lockstep(TurboSoundKind::Single, false, 1, true, 30);
}

TEST(AyStepGate_Test, AFasterHostClockMatchesEveryStepTakenTheFullWay)
{
    Lockstep(TurboSoundKind::AY, true, 2, false, 20);
    Lockstep(TurboSoundKind::AY, true, 2, true, 30);
}

// The device sleeps through each silence (the quiet deferral: the gate holds to the next entry) and wakes at the CPU's
// write in the middle of a frame; three beeps
TEST(AyStepGate_Test, BeepsAfterSilencesMatchEveryStepTakenTheFullWay)
{
    Lockstep(TurboSoundKind::AY, true, 1, false, 600, true);
}
