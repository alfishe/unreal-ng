// The TSFM's step gate (SoundChip_TurboSoundFM::handleStep, settleSteps) against every step taken the full way: two
// machines with a TurboSound FM run the same program - random register writes to both chips at random places in the
// frame, FM key-on writes, dense and sparse (long silences between bursts) - one with the gate closed, and the sound each
// sends to the host must be the same sample for sample, and the device state (the TTD blob) the same at the end; HQ, LQ
// and a 2x host speed (the gate holds only at 1x), and turbo without audio (the core only notes the position, every
// step deferred) with the way back to sound. And the quiet deferral (the device asleep through silence, the
// outputs rendered in one run at the next entry) against a machine rendering every tick with no gate and no quiet runs.

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
#include "emulator/sound/chips/soundchip_turbosoundfm.h"
#include "emulator/sound/soundmanager.h"

namespace
{
void Capture(void* obj, int16_t* samples, size_t numSamples)
{
    auto* sink = static_cast<std::vector<int16_t>*>(obj);
    sink->insert(sink->end(), samples, samples + numSamples);
}

/// A loop writing a random register of a random chip with a random value, then waiting a random while (sparse: up to a
/// third of a frame and more), with an FM key-on write (#28) now and then. Interrupts off
std::vector<uint8_t> Program(bool sparse)
{
    std::vector<uint8_t> code = {
        0xF3,              // di
        0x31, 0xF0, 0xBF,  // ld sp,#BFF0
    };
    const size_t loop = code.size();
    code.insert(code.end(), {
                                0xED, 0x5F, 0xE6, 0x01, 0xF6, 0xFA,  // ld a,r : and 1 : or #FA (chip 0 / 1, FM on)
                                0x01, 0xFD, 0xFF, 0xED, 0x79,        // ld bc,#FFFD : out (c),a
                                0xED, 0x5F, 0xE6, 0x0F,              // ld a,r : and #0F (an SSG register)
                                0xED, 0x79,                          // out (c),a
                                0xED, 0x5F, 0x06, 0xBF, 0xED, 0x79,  // ld a,r : ld b,#BF : out (c),a (its value)
                                0xED, 0x5F, 0xE6, 0x07, 0x20, 0x0C,  // ld a,r : and 7 : jr nz,+12 (FM key on 1 in 8)
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

/// `ld bc,#FFFD : ld a,reg : out (c),a : ld b,#BF : ld a,value : out (c),a` - a register of the selected chip
void Out(std::vector<uint8_t>& code, uint8_t reg, uint8_t value)
{
    code.insert(code.end(), {0x01, 0xFD, 0xFF, 0x3E, reg, 0xED, 0x79, 0x06, 0xBF, 0x3E, value, 0xED, 0x79});
}

/// A ~0.27 s wait (`ld hl,#9000 : dec hl : ld a,h : or l : jr nz`)
void Wait(std::vector<uint8_t>& code)
{
    code.insert(code.end(), {0x21, 0x00, 0x90, 0x2B, 0x7C, 0xB5, 0x20, 0xFB});
}

/// `ld bc,#FFFD : ld a,value : out (c),a` - a board control word
void Control(std::vector<uint8_t>& code, uint8_t value)
{
    code.insert(code.end(), {0x01, 0xFD, 0xFF, 0x3E, value, 0xED, 0x79});
}

/// Chip 0, the device asleep at each event (falling asleep once the SSG and FM filters are back at exactly 0: some two
/// seconds after an SSG sound, eight after an FM one): an SSG tone for about a frame and 3.4 s of silence; the FM timer A
/// in CSM mode with the FM part muted - its expiries key channel 3 on, the first one about a frame after the write - for
/// ~0.27 s and 3.4 s of silence; the same audible. A random wait between the parts. Interrupts off
std::vector<uint8_t> BeepsAfterSilence()
{
    std::vector<uint8_t> code = {
        0xF3,              // di
        0x31, 0xF0, 0xBF,  // ld sp,#BFF0
    };
    Control(code, 0xFA);  // chip 0, FM on
    Out(code, 0x00, 0x40);
    Out(code, 0x01, 0x01);
    Out(code, 0x07, 0x3E);
    Out(code, 0xB2, 0x07);  // channel 3: every operator a carrier
    for (uint8_t op : {uint8_t(0), uint8_t(4), uint8_t(8), uint8_t(12)})
    {
        Out(code, uint8_t(0x32 + op), 0x01);  // MUL 1
        Out(code, uint8_t(0x42 + op), 0x10);  // TL
        Out(code, uint8_t(0x52 + op), 0x1F);  // AR 31
        Out(code, uint8_t(0x82 + op), 0x0F);  // SL 0, RR 15
    }
    Out(code, 0xA6, 0x22);
    Out(code, 0xA2, 0x69);
    Out(code, 0x24, 0x00);  // timer A: the longest period (about a frame)
    Out(code, 0x25, 0x00);
    const auto silence = [&code]() {
        code.insert(code.end(), {
                                    0x16, 0x07,                                      // ld d,7
                                    0x21, 0x00, 0x00, 0x2B, 0x7C, 0xB5, 0x20, 0xFB,  // ld hl,0 : 65536 turns (1.7 M T)
                                    0x15, 0x20, 0xF5,                                // dec d : jr nz (3.4 s in all)
                                    0xED, 0x5F, 0x67, 0x2E, 0x00, 0x2B, 0x7C, 0xB5, 0x20, 0xFB,  // a random wait
                                });
    };
    const auto csm = [&code](uint8_t control) {
        Control(code, control);
        Out(code, 0x27, 0x85);  // CSM, timer A loaded, its flag on
        Wait(code);
        Out(code, 0x27, 0x30);  // timers stopped, flags reset
        Control(code, 0xFA);
    };
    const size_t loop = code.size();
    Out(code, 0x08, 0x0F);  // SSG A volume 15
    code.insert(code.end(), {0x21, 0x00, 0x08, 0x2B, 0x7C, 0xB5, 0x20, 0xFB});  // ~53 000 T
    Out(code, 0x08, 0x00);
    silence();
    csm(0xFE);  // FM muted
    silence();
    csm(0xFA);
    silence();
    silence();
    silence();
    code.push_back(0xC3);  // jp loop
    code.push_back(static_cast<uint8_t>((0x8000 + loop) & 0xFF));
    code.push_back(static_cast<uint8_t>((0x8000 + loop) >> 8));
    return code;
}

class Twin
{
public:
    Twin(bool gate, bool hq, uint8_t speed, bool sparse, bool beeps = false)
    {
        _emulator = EmulatorTestHelper::CreateEmulatorWithTurboSoundKind("PENTAGON", TurboSoundKind::FM, LoggerLevel::LogError);
        EXPECT_NE(_emulator, nullptr);
        if (!_emulator)
            return;
        _context = _emulator->GetContext();
        _context->pFeatureManager->setFeature(Features::kSoundGeneration, true);
        _context->pFeatureManager->setFeature(Features::kSoundHQ, hq);
        _tsfm = dynamic_cast<SoundChip_TurboSoundFM*>(_context->pSoundManager->getTurboSound());
        EXPECT_NE(_tsfm, nullptr);
        if (_tsfm)
        {
            _tsfm->setStepGate(gate);
            if (beeps)
                _tsfm->pair().setQuietTicksEnabled(gate);  // the full way renders every tick: no quiet runs either
        }
        if (speed != 1)
            EXPECT_TRUE(_emulator->SetSpeedMultiplier(speed));
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
    void Turbo(bool on)
    {
        if (on)
            _context->pCore->EnableTurboMode();
        else
            _context->pCore->DisableTurboMode();
    }
    const std::vector<int16_t>& Sound() const { return _sound; }
    size_t LongestQuietRun() const { return _tsfm->longestQuietRun(); }
    std::vector<uint8_t> State() const
    {
        std::vector<uint8_t> blob(_tsfm->TTDStateSize());
        _tsfm->TTDSaveState(blob.data());
        return blob;
    }

private:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    SoundChip_TurboSoundFM* _tsfm = nullptr;
    std::vector<int16_t> _sound;
};

/// `turbo`: the frames from a quarter to a half of them run in turbo without audio
void Lockstep(bool hq, uint8_t speed, bool sparse, int frames, bool beeps = false, bool turbo = false)
{
    Twin gated(true, hq, speed, sparse, beeps);
    Twin full(false, hq, speed, sparse, beeps);
    ASSERT_TRUE(gated.Ready());
    ASSERT_TRUE(full.Ready());
    for (int frame = 0; frame < frames; frame++)
    {
        if (turbo && (frame == frames / 4 || frame == frames / 2))
        {
            gated.Turbo(frame == frames / 4);
            full.Turbo(frame == frames / 4);
        }
        gated.Frame();
        full.Frame();
        ASSERT_EQ(gated.Sound().size(), full.Sound().size()) << "frame " << frame;
        if (gated.Sound() != full.Sound())
        {
            size_t first = 0;
            while (gated.Sound()[first] == full.Sound()[first])
                first++;
            FAIL() << (hq ? "HQ" : "LQ") << " x" << int(speed) << (sparse ? " sparse" : " dense") << ": frame " << frame
                   << " differs first at sample " << first;
        }
        if (turbo && frame == frames / 2 - 1)
            ASSERT_EQ(gated.State(), full.State()) << "the device state at the end of the turbo frames";
    }
    EXPECT_EQ(gated.State(), full.State()) << "the device state at the end";
    if (beeps)
        EXPECT_GT(gated.LongestQuietRun(), 100u) << "the device never slept (a run of a step's few outputs at most)";
}
}  // namespace

// Machine-bound: two machines with the TurboSound FM run whole frames of sound (~1-2 ms each), longer than 50 ms
TEST(TsfmStepGate_Test, DenseWritesMatchEveryStepTakenTheFullWay)
{
    Lockstep(true, 1, false, 40);
    Lockstep(false, 1, false, 20);
}

TEST(TsfmStepGate_Test, SparseWritesAndSilencesMatchEveryStepTakenTheFullWay)
{
    Lockstep(true, 1, true, 80);
    Lockstep(false, 1, true, 30);
}

TEST(TsfmStepGate_Test, AFasterHostClockMatchesEveryStepTakenTheFullWay)
{
    Lockstep(true, 2, false, 20);
    Lockstep(true, 2, true, 30);
}

TEST(TsfmStepGate_Test, TurboWithoutAudioMatchesEveryStepTakenTheFullWay)
{
    Lockstep(true, 1, false, 40, false, true);
    Lockstep(false, 1, true, 40, false, true);
}

// ~420 frames: asleep, an SSG write wakes the device, then a CSM expiry with FM muted and one audible (no write)
TEST(TsfmStepGate_Test, BeepsAfterSilencesMatchEveryStepTakenTheFullWay)
{
    Lockstep(true, 1, false, 420, true);
}
