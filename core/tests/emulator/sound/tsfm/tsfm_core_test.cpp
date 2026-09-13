#include "stdafx.h"
#include "pch.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

#include <ymfm_opn.h>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "_helpers/tsfmplayerharness.h"
#include "base/featuremanager.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/sound/chips/soundchip_turbosoundfm.h"
#include "emulator/sound/soundmanager.h"

/// TSFM chip core tests (TSFM design §12.1, implementation plan P4).
///
/// The unit fixtures drive the device directly: an explicit T-state is poked
/// into the Z80 time counter (on Pentagon AudioTstate(t) == t), port callbacks
/// are invoked by hand, and the word queues are drained for assertions. No CPU
/// instruction executes, which is exactly what "unit, no CPU" in §12.1 means.
///
/// Timing facts asserted here come from the design §4 table (1 master clock =
/// 1 T-state): FM sample period 12·p, busy 32·p, timer A (1024−TA)·12·p,
/// timer B 16·(256−TB)·12·p, and ymfm's timer-B first-load −(m_total_clocks&15)
/// term (ymfm_fm.ipp engine_mode_write).
///
/// The E2E tests at the bottom boot the real TFM Music Compiler player (the
/// sanctioned "booting a real ROM" exception to the 50 ms budget) to prove the
/// busy flag unblocks its WaitStatus poll and that the core hash is identical
/// with synthesis on, in turbo mode, and with the sound feature off.

namespace
{

/// Pentagon frame length in T-states (the ini's config.frame)
constexpr uint32_t PENTAGON_FRAME = 71680;

/// FNV-1a over raw bytes (core-state digest for the output-stage independence
/// checks — TTDHashState itself arrives in P5)
uint64_t Fnv1a(const void* data, size_t size, uint64_t hash = 1469598103934665603ULL)
{
    const uint8_t* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; i++)
    {
        hash ^= p[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

/// Digest of everything guest-visible in the chip core: board latches plus,
/// per chip, the address latch, FM clock phase, timer/busy counters and the
/// full patched-ymfm serialized state (494 bytes, side-effect-free per P1)
uint64_t CoreHash(SoundChip_TurboSoundFM& device)
{
    uint64_t hash = 1469598103934665603ULL;
    const TsfmBoard& board = device.board();
    const uint8_t boardByte = uint8_t(board.chip | (board.statusRead ? 2 : 0) | (board.fmEnabled ? 4 : 0));
    hash = Fnv1a(&boardByte, 1, hash);

    std::vector<uint8_t> ymfmState;
    for (int i = 0; i < 2; i++)
    {
        TsfmChip* c = device.chip(i);
        hash = Fnv1a(&c->address, 1, hash);
        hash = Fnv1a(&c->fmClockPhase, sizeof(c->fmClockPhase), hash);
        hash = Fnv1a(c->intf._timer, sizeof(c->intf._timer), hash);
        hash = Fnv1a(&c->intf._busy, sizeof(c->intf._busy), hash);

        ymfmState.clear();
        ymfm::ymfm_saved_state state(ymfmState, true);
        c->fm.save_restore(state);
        hash = Fnv1a(ymfmState.data(), ymfmState.size(), hash);
    }
    return hash;
}

/// Drain and return the queued FM words of one chip
std::vector<FmWord> DrainWords(TsfmChip& c)
{
    std::vector<FmWord> out;
    out.reserve(c.words.size());
    while (!c.words.empty())
    {
        out.push_back(c.words.front());
        c.words.pop();
    }
    return out;
}

/// Boot a Pentagon emulator whose TurboSound slot is the TSFM device: the
/// shipped pentagon128k ini is copied to unique scratch space with
/// TurboSound=FM, and loaded as the custom config. Caller owns the emulator.
Emulator* CreateFmEmulator(LoggerLevel level)
{
    namespace fs = std::filesystem;
    const fs::path source = TestPathHelper::FindProjectRoot() / "data/configs/pentagon128k/unreal.ini";
    std::string ini;
    {
        std::ifstream in(source, std::ios::binary);
        ini.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    // The shipped slot kind flips between releases (FM ships enabled now);
    // force FM from whichever value the ini carries
    const std::string to = "TurboSound=FM";
    const size_t atAy = ini.find("TurboSound=AY");
    if (ini.empty() || (atAy == std::string::npos && ini.find(to) == std::string::npos))
        return nullptr;
    if (atAy != std::string::npos)
        ini.replace(atAy, to.size(), to);  // both slot literals are the same length

    const fs::path target = TestPathHelper::GetUniqueTestScratchPath("tsfm-fm-pentagon.ini");
    {
        std::ofstream out(target, std::ios::binary);
        out.write(ini.data(), static_cast<std::streamsize>(ini.size()));
    }

    Emulator* emulator = new Emulator(level);
    emulator->SetCustomConfigPath(target.string());
    if (!emulator->Init())
    {
        emulator->Release();
        delete emulator;
        return nullptr;
    }
    return emulator;
}

void ReleaseFmEmulator(Emulator* emulator)
{
    if (emulator)
    {
        emulator->GetContext()->pAudioCallback.store(nullptr, std::memory_order_release);
        emulator->GetContext()->pAudioManagerObj.store(nullptr, std::memory_order_release);
        EmulatorTestHelper::CleanupEmulator(emulator);
    }
}

}  // namespace

/// region <Unit fixture: direct device, no CPU>

class TsfmDeviceTestBase : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    std::unique_ptr<SoundChip_TurboSoundFM> _device;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr) << "Failed to create emulator";
        _context = _emulator->GetContext();

        // Pentagon has no hardware turbo, so AudioTstate(t) == t: the raw
        // time counter is the device's T-state axis. tt's low byte is the
        // fractional part, kept at zero.
        _context->pCore->GetZ80()->tt = 0;

        _device = std::make_unique<SoundChip_TurboSoundFM>(_context);
    }

    void TearDown() override
    {
        _device.reset();
        if (_emulator)
        {
            _context->pAudioCallback.store(nullptr, std::memory_order_release);
            _context->pAudioManagerObj.store(nullptr, std::memory_order_release);
            EmulatorTestHelper::CleanupEmulator(_emulator);
            _emulator = nullptr;
        }
    }

    /// Park the CPU time counter at integer T-state t (port callbacks sync
    /// the core to exactly this position)
    void SetT(uint64_t t)
    {
        _context->pCore->GetZ80()->tt = uint32_t(t) << 8;
    }

    void Out(uint16_t port, uint8_t value)
    {
        _device->portDeviceOutMethod(port, value);
    }

    uint8_t In(uint16_t port)
    {
        return _device->portDeviceInMethod(port);
    }

    TsfmChip& C0()
    {
        return *_device->chip(0);
    }

    TsfmChip& C1()
    {
        return *_device->chip(1);
    }
};

class TsfmPort_Test : public TsfmDeviceTestBase
{
};

/// endregion

/// region <TsfmPort>

TEST_F(TsfmPort_Test, ControlWordTable)
{
    // Distinct sentinel addresses on both chips (0xFF selects chip 1)
    Out(PORT_FFFD, 0xFE);  // chip 0, register read, FM muted
    Out(PORT_FFFD, 0x11);
    Out(PORT_FFFD, 0xFF);  // chip 1
    Out(PORT_FFFD, 0x22);
    ASSERT_EQ(C0().address, 0x11);
    ASSERT_EQ(C1().address, 0x22);

    // All 8 control words set the three latches per hardware-reference §3.2
    // and never touch either chip's address latch (_WR held inactive)
    for (uint8_t value = 0xF8;; value++)
    {
        SCOPED_TRACE(testing::Message() << "control word 0x" << std::hex << int(value));
        Out(PORT_FFFD, value);
        EXPECT_EQ(_device->board().chip, uint8_t(value & 0x01));
        EXPECT_EQ(_device->board().statusRead, !(value & 0x02));
        EXPECT_EQ(_device->board().fmEnabled, !(value & 0x04));
        EXPECT_EQ(C0().address, 0x11);
        EXPECT_EQ(C1().address, 0x22);
        if (value == 0xFF)
            break;
    }
}

TEST_F(TsfmPort_Test, ResetState)
{
    _device->reset();

    // CPLD reset state = OUT #FFFD,#FE (design §5.4): first chip (the 0xFE
    // chip), register read, FM muted
    EXPECT_EQ(_device->board().chip, 0);
    EXPECT_FALSE(_device->board().statusRead);
    EXPECT_FALSE(_device->board().fmEnabled);

    for (int i = 0; i < 2; i++)
    {
        TsfmChip* c = _device->chip(i);
        ASSERT_NE(c, nullptr);
        EXPECT_EQ(c->address, 0);
        EXPECT_EQ(c->fmClockPhase, 0);
        EXPECT_EQ(c->fm.fmClockPrescale(), 6u) << "reset must return the prescaler to /6";
        EXPECT_EQ(c->intf._timer[0], -1);
        EXPECT_EQ(c->intf._timer[1], -1);
        EXPECT_EQ(c->intf._busy, 0);
        EXPECT_TRUE(c->words.empty());
    }

    // The next sync adopts the CPU's T-state instead of advancing (§5.4)
    EXPECT_EQ(_device->syncedT(), 0);
}

TEST_F(TsfmPort_Test, AddressLatchedWhileMuted)
{
    Out(PORT_FFFD, 0xFE);  // chip 0, register read, FM muted

    uint8_t ssgBefore[16];
    for (int reg = 0; reg < 16; reg++)
        ssgBefore[reg] = C0().ssg.readRegister(uint8_t(reg));

    // FM register address + data with the FM path muted: registers are still
    // latched and written (hardware §3.4)
    Out(PORT_FFFD, 0xA0);
    Out(PORT_BFFD, 0x55);
    EXPECT_EQ(C0().fm.fmReg(0xA0), 0x55);

    for (int reg = 0; reg < 16; reg++)
        EXPECT_EQ(C0().ssg.readRegister(uint8_t(reg)), ssgBefore[reg])
            << "SSG register " << reg << " changed by an FM write";
}

TEST_F(TsfmPort_Test, SsgIgnoresDataWithFmAddress)
{
    Out(PORT_FFFD, 0xFE);
    Out(PORT_FFFD, 0x07);
    Out(PORT_BFFD, 0x3F);  // mixer: known value
    ASSERT_EQ(C0().ssg.readRegister(7), 0x3F);

    // Latch FM address 0x28, write data: the SSG half must not see it
    Out(PORT_FFFD, 0x28);
    Out(PORT_BFFD, 0xF0);
    EXPECT_EQ(C0().ssg.readRegister(7), 0x3F);
}

TEST_F(TsfmPort_Test, ReadWithFmAddress)
{
    Out(PORT_FFFD, 0xFE);  // register read mode

    // FM address latched: both ports read back 0xFF (hardware-reference H2)
    Out(PORT_FFFD, 0xA0);
    EXPECT_EQ(In(PORT_FFFD), 0xFF);
    EXPECT_EQ(In(PORT_BFFD), 0xFF);

    // SSG address latched: the register path answers (legacy parity — IN
    // #BFFD writes through to the selected register)
    Out(PORT_FFFD, 0x0E);
    Out(PORT_BFFD, 0x20);
    EXPECT_EQ(In(PORT_BFFD), 0x20);
}

/// endregion

/// region <TsfmBusy>

class TsfmBusy_Test : public TsfmDeviceTestBase
{
};

TEST_F(TsfmBusy_Test, ExactTiming)
{
    // FM data write at /6: busy is 32·6 = 192 T-states (design §4). The status
    // read itself syncs the core, so reading at T+191 / T+192 observes the
    // exact last busy / first free T-state
    Out(PORT_FFFD, 0xFE);  // register mode
    SetT(1000);
    Out(PORT_FFFD, 0x30);
    Out(PORT_BFFD, 0x04);  // data write -> busy until T+192
    Out(PORT_FFFD, 0xFC);  // chip 0, status read

    SetT(1000 + 191);
    EXPECT_NE(In(PORT_FFFD) & 0x80, 0) << "busy must still be set at T+191";
    SetT(1000 + 192);
    EXPECT_EQ(In(PORT_FFFD) & 0x80, 0) << "busy must clear exactly at T+192";

    // SSG data write sets the same 192 T-states (ymfm write_data does it for
    // both halves; the device's SSG path mirrors it, §5.3)
    Out(PORT_FFFD, 0xFE);
    SetT(2000);
    Out(PORT_FFFD, 0x08);
    Out(PORT_BFFD, 0x0A);
    Out(PORT_FFFD, 0xFC);
    SetT(2000 + 191);
    EXPECT_NE(In(PORT_FFFD) & 0x80, 0);
    SetT(2000 + 192);
    EXPECT_EQ(In(PORT_FFFD) & 0x80, 0);

    // Prescaler /3: busy is 32·3 = 96 T-states (0x2E works from the reset /6)
    Out(PORT_FFFD, 0xFE);
    Out(PORT_FFFD, 0x2E);
    SetT(3000);
    Out(PORT_FFFD, 0x30);
    Out(PORT_BFFD, 0x04);
    Out(PORT_FFFD, 0xFC);
    SetT(3000 + 95);
    EXPECT_NE(In(PORT_FFFD) & 0x80, 0) << "busy must still be set at T+95 (/3)";
    SetT(3000 + 96);
    EXPECT_EQ(In(PORT_FFFD) & 0x80, 0) << "busy must clear exactly at T+96 (/3)";
}

TEST_F(TsfmBusy_Test, PlayerWaitLoop)
{
    // The TFM Compiler WaitStatus poll (player-entry-points.md, sub_628A at
    // 0x6293): `in f,(c)` + `jp m` — 22 T per iteration. Prime the chip busy
    // with a data write, enter the loop on the real Z80 and count instructions
    // until it falls through. §12.1: terminates, in a fixed instruction count,
    // on a real Z80 instance — run twice on fresh instances for determinism.
    auto runOnce = [this](int& instructionCount)
    {
        Emulator* fm = CreateFmEmulator(LoggerLevel::LogError);
        ASSERT_NE(fm, nullptr) << "failed to boot a TurboSound=FM emulator";
        Z80* z80 = fm->GetContext()->pCore->GetZ80();
        auto* device = static_cast<SoundChip_TurboSoundFM*>(fm->GetContext()->pSoundManager->getTurboSound());
        ASSERT_EQ(device->TTDPeripheralId(), ttd::PeripheralId::TSFM);

        // 0x8000: in f,(c) / jp m,0x8000 / ret — the player's poll shape
        const uint8_t program[] = {0xED, 0x70, 0xFA, 0x00, 0x80, 0xC9};
        for (size_t i = 0; i < sizeof(program); i++)
            z80->DirectWrite(uint16_t(0x8000 + i), program[i]);

        // Prime: status mode on chip 0, FM data write at T=1000 -> busy 192 T
        z80->tt = 1000 << 8;
        device->portDeviceOutMethod(PORT_FFFD, 0xFC);
        device->portDeviceOutMethod(PORT_FFFD, 0x30);
        device->portDeviceOutMethod(PORT_BFFD, 0x04);

        z80->b = 0xFF;
        z80->c = 0xFD;
        z80->pc = 0x8000;
        z80->iff1 = 0;
        z80->iff2 = 0;

        instructionCount = 0;
        while (z80->pc < 0x8005 && instructionCount < 64)
        {
            fm->RunSingleCPUCycle(true);
            instructionCount++;
        }

        // The loop must have actually polled a busy chip: 192 T of busy over
        // 22 T iterations guarantees several taken branches
        int exitPc = z80->pc;
        uint8_t status = device->chip(0)->fm.read_status();
        ReleaseFmEmulator(fm);
        ASSERT_GE(exitPc, 0x8005) << "WaitStatus loop did not terminate";
        EXPECT_EQ(status & 0x80, 0) << "chip still busy after the wait";
    };

    int first = -1;
    int second = -1;
    runOnce(first);
    runOnce(second);
    ASSERT_GE(first, 4) << "loop exited too early to have observed busy";
    EXPECT_LE(first, 20) << "loop spun far longer than the 192 T busy window";
    EXPECT_EQ(first, second) << "instruction count differs between fresh instances";
}

/// endregion

/// region <TsfmTimer>

class TsfmTimer_Test : public TsfmDeviceTestBase
{
protected:
    /// Program timer A: TA = reg24<<2 | reg25&3, period (1024−TA)·72 T at /6
    void ProgramTimerA(uint8_t reg24, uint8_t reg25)
    {
        Out(PORT_FFFD, 0x24);
        Out(PORT_BFFD, reg24);
        Out(PORT_FFFD, 0x25);
        Out(PORT_BFFD, reg25);
    }

    /// 0x27 = load|enable A (0x05); 0x15 = load|enable|reset-flag A — the
    /// load bit must stay set or ymfm's update_timer stops the counter
    void ArmTimerA(uint8_t mode)
    {
        Out(PORT_FFFD, 0x27);
        Out(PORT_BFFD, mode);
    }
};

TEST_F(TsfmTimer_Test, APeriod)
{
    Out(PORT_FFFD, 0xFE);  // chip 0, register mode
    SetT(10000);
    ProgramTimerA(0x80, 0x00);  // TA = 512 -> period 512·72 = 36864 T
    ArmTimerA(0x05);
    Out(PORT_FFFD, 0xFC);  // status read

    // Flag sets exactly at T + 36864, not one T earlier
    SetT(10000 + 36864 - 1);
    EXPECT_EQ(In(PORT_FFFD) & 0x01, 0) << "timer A flag early";
    SetT(10000 + 36864);
    EXPECT_NE(In(PORT_FFFD) & 0x01, 0) << "timer A flag missing at expiry";

    // engine_timer_expired reloads the full period; clearing the flag needs
    // load|enable|reset (0x15) — a write without the load bit stops the timer
    // in ymfm's update_timer. 1000 periods without drift. z80->t is a 24-bit
    // field (2^24 = 16777216 T), so the loop crosses the wrap the way the
    // machine does: subtract a frame length from the CPU counter and rebase
    // the device (AdjustFrameCounters + handleFrameStart, §5.2) - which also
    // proves the rebase leaves a running timer unharmed
    uint64_t expiry = 10000 + 36864;
    uint64_t offset = 0;
    for (int i = 1; i <= 1000; i++)
    {
        const uint64_t next = 10000 + 36864 * uint64_t(i + 1);
        ArmTimerA(0x15);  // clear the overflow flag, timer keeps running
        SetT(next - 1 - offset);
        EXPECT_EQ(In(PORT_FFFD) & 0x01, 0) << "drift at period " << i;
        SetT(next - offset);
        EXPECT_NE(In(PORT_FFFD) & 0x01, 0) << "flag missing at period " << i;
        expiry = next;

        if (next - offset > 12000000)
        {
            SetT(next - offset - 71680);  // Pentagon frame length
            _device->handleFrameStart();
            offset += 71680;
        }
    }
    EXPECT_EQ(expiry, 10000 + 36864 * 1001);
}

TEST_F(TsfmTimer_Test, BPeriod)
{
    Out(PORT_FFFD, 0xFE);
    SetT(10000);
    Out(PORT_FFFD, 0x26);
    Out(PORT_BFFD, 0x80);  // TB = 128 -> raw period 16·128 = 2048 subperiods

    // ymfm's first timer-B load subtracts (m_total_clocks & 15) subperiods
    // (engine_mode_write) because the ×16 prescaler free-runs. On a fresh
    // device driven from t=0, the engine has clocked exactly floor(t/72)
    // FM samples when the write lands — count them from the word queue
    _device->syncTo(10000);
    const uint64_t samplesAtWrite = 10000 / 72;  // 138 (139th sample pending)
    ASSERT_EQ(DrainWords(C0()).size(), samplesAtWrite);

    Out(PORT_FFFD, 0x27);
    Out(PORT_BFFD, 0x0A);  // load B | enable B (bits 1|3)
    Out(PORT_FFFD, 0xFC);

    const int32_t firstLoadAdjust = int32_t(samplesAtWrite & 15);  // 138 & 15 = 10
    const uint64_t firstExpiry = 10000 + (2048 - firstLoadAdjust) * 72ULL;
    const uint64_t naiveExpiry = 10000 + 2048 * 72ULL;
    ASSERT_NE(firstExpiry, naiveExpiry) << "test degenerated: adjustment term is zero";

    SetT(firstExpiry - 1);
    EXPECT_EQ(In(PORT_FFFD) & 0x02, 0) << "timer B flag early";
    SetT(firstExpiry);
    EXPECT_NE(In(PORT_FFFD) & 0x02, 0) << "timer B flag missing at first expiry";

    // Clear the overflow flag without stopping the timer: load|enable|reset-B
    // (0x2A). ymfm's update_timer keys on the load bit, so a flag reset
    // without it (0x28) would stop timer B
    Out(PORT_FFFD, 0x27);
    Out(PORT_BFFD, 0x2A);

    // Reloads carry no adjustment: the second expiry is one raw period later
    const uint64_t secondExpiry = firstExpiry + 2048 * 72ULL;
    SetT(secondExpiry - 1);
    EXPECT_EQ(In(PORT_FFFD) & 0x02, 0);
    SetT(secondExpiry);
    EXPECT_NE(In(PORT_FFFD) & 0x02, 0) << "timer B reload drifted";
}

TEST_F(TsfmTimer_Test, CsmKeyOnSampleAligned)
{
    // Timer A periods are whole multiples of the FM sample period, so a CSM
    // key-on fires exactly on a sample boundary. The §5.2 ordering rule
    // (expiry before the sample of the same T-state) means the word AT the
    // expiry T-state is the first one influenced. A direct key-on write at a
    // known T calibrates how many samples pass until the envelope shows up;
    // the CSM run must show the same offset from its expiry sample.
    //
    // Channel 2 is the OPN CSM channel (CSM_TRIGGER_MASK 1<<2). Its operators
    // are engine ops {2,5,8,11}, whose register slots are opoffs 2/6/10/14
    // (ymfm remaps engine opnum through operator_offset = opnum + opnum/3), so
    // per-operator writes must target the scattered 0x42/0x46/0x4A/0x4E slots -
    // NOT the contiguous 0x48-0x4B block, which hits channels 0/1 and dead
    // slots (0x4B/0x5B belong to no operator; algorithm 0's output carrier
    // O4 is engine op 11 behind TL 0x4E / AR 0x5E)
    auto program = [this](SoundChip_TurboSoundFM& dev)
    {
        SetT(10000);
        dev.portDeviceOutMethod(PORT_FFFD, 0xFE);
        const uint8_t setup[][2] = {
            {0x42, 0x00}, {0x46, 0x00}, {0x4A, 0x00}, {0x4E, 0x00},  // TL: loudest
            {0x52, 0x1F}, {0x56, 0x1F}, {0x5A, 0x1F}, {0x5E, 0x1F},  // AR: fastest
            // ch2 fnum 0x100 - the 0xA0 region is a latched pair: the upper
            // write only latches, the lower write commits both halves, so the
            // latch must be written first or the commit pairs with a stale 0
            // and the channel stays at phase step 0 (silence)
            {0xA6, 0x41},
            {0xA2, 0x00},
        };
        for (const auto& [reg, data] : setup)
        {
            dev.portDeviceOutMethod(PORT_FFFD, reg);
            dev.portDeviceOutMethod(PORT_BFFD, data);
        }
    };

    auto runTo = [](SoundChip_TurboSoundFM& dev, uint64_t t)
    {
        dev.syncTo(t);
        return DrainWords(*dev.chip(0));
    };

    auto firstNonzero = [](const std::vector<FmWord>& words) -> uint64_t
    {
        for (const FmWord& w : words)
            if (w.word != 0)
                return w.t;
        return UINT64_MAX;
    };

    // Baseline: no key-on ever — the FM half stays silent
    {
        auto dev = std::make_unique<SoundChip_TurboSoundFM>(_context);
        dev->syncTo(0);  // anchor the sample grid at T=0 (fresh devices adopt their first sync)
        program(*dev);
        const auto words = runTo(*dev, 40000);
        ASSERT_FALSE(words.empty());
        EXPECT_EQ(firstNonzero(words), UINT64_MAX) << "FM produced sound without a key-on";
    }

    // Calibration: key-on write (0x28, data 0xF2 = ch2, ALL FOUR operators -
    // an opmask of 1 keys only the modulator and algorithm 0's carrier stays
    // silent) at T=20000. The sample at 20000 has already been produced when
    // the port handler syncs, so the first boundary AFTER the write is 20016
    uint64_t calibOffset = UINT64_MAX;
    {
        auto dev = std::make_unique<SoundChip_TurboSoundFM>(_context);
        dev->syncTo(0);
        program(*dev);
        SetT(20000);
        dev->portDeviceOutMethod(PORT_FFFD, 0x28);
        dev->portDeviceOutMethod(PORT_BFFD, 0xF2);
        const auto words = runTo(*dev, 40000);
        const uint64_t first = firstNonzero(words);
        ASSERT_NE(first, UINT64_MAX) << "key-on write produced no sound";
        calibOffset = first - 20016;
        ASSERT_LE(calibOffset, 72u) << "envelope needed more than one extra sample";
    }

    // CSM run: timer A at TA=1023 (one-sample period) armed exactly on the
    // 20016 boundary expires at 20088 — also a boundary. The first sounded
    // word must sit at the SAME offset from 20088 as the calibration's from
    // its first post-write sample; a sample-then-expiry ordering would push
    // it exactly one sample (72 T) later
    {
        auto dev = std::make_unique<SoundChip_TurboSoundFM>(_context);
        dev->syncTo(0);
        program(*dev);
        SetT(20016);
        dev->portDeviceOutMethod(PORT_FFFD, 0x24);
        dev->portDeviceOutMethod(PORT_BFFD, 0xFF);  // TA = 0xFF<<2 | 3 = 1023
        dev->portDeviceOutMethod(PORT_FFFD, 0x25);
        dev->portDeviceOutMethod(PORT_BFFD, 0x03);
        dev->portDeviceOutMethod(PORT_FFFD, 0x27);
        dev->portDeviceOutMethod(PORT_BFFD, 0x85);  // CSM | load A | enable A
        const auto words = runTo(*dev, 40000);
        const uint64_t first = firstNonzero(words);
        ASSERT_NE(first, UINT64_MAX) << "CSM key-on produced no sound";
        EXPECT_EQ(first, 20088 + calibOffset)
            << "CSM key-on did not land on the expiry sample (ordering/drift)";
    }
}

/// endregion

/// region <TsfmPrescaler>

class TsfmPrescaler_Test : public TsfmDeviceTestBase
{
protected:
    /// Assert that every drained word timestamp is exactly spacing apart,
    /// starting at `first`
    static void ExpectSpacing(const std::vector<FmWord>& words, uint64_t first, uint64_t spacing)
    {
        ASSERT_FALSE(words.empty());
        for (size_t i = 0; i < words.size(); i++)
            EXPECT_EQ(words[i].t, first + spacing * i) << "word " << i;
    }
};

TEST_F(TsfmPrescaler_Test, PeriodFollows)
{
    // 0x2F -> prescaler /2: words every 24 T from the very first sample
    {
        auto dev = std::make_unique<SoundChip_TurboSoundFM>(_context);
        SetT(0);
        dev->portDeviceOutMethod(PORT_FFFD, 0x2F);
        dev->syncTo(240);
        ExpectSpacing(DrainWords(*dev->chip(0)), 24, 24);
    }

    // 0x2E from the reset /6 -> /3: words every 36 T; then 0x2D -> back to 72
    {
        auto dev = std::make_unique<SoundChip_TurboSoundFM>(_context);
        SetT(0);
        dev->portDeviceOutMethod(PORT_FFFD, 0x2E);
        dev->syncTo(360);
        ExpectSpacing(DrainWords(*dev->chip(0)), 36, 36);

        // 0x2D takes effect on the next core iteration: 360 is a /3 boundary
        // (phase 0), so the /6 grid resumes from 360 + 72
        SetT(360);
        dev->portDeviceOutMethod(PORT_FFFD, 0x2D);
        dev->syncTo(504);
        ExpectSpacing(DrainWords(*dev->chip(0)), 432, 72);
    }

    // Shrinking period mid-cycle: at phase 70 of /6, 0x2F clamps the phase to
    // /2's period-1 so the next sample is not skipped (design §5.2). The
    // syncTo(0) anchor matters here: a fresh device ADOPTS its first sync
    // target, so without it phase would still be 0 at T=70
    {
        auto dev = std::make_unique<SoundChip_TurboSoundFM>(_context);
        dev->syncTo(0);
        dev->syncTo(70);  // phase 70 of 72, no sample yet
        ASSERT_TRUE(dev->chip(0)->words.empty());
        SetT(70);
        dev->portDeviceOutMethod(PORT_FFFD, 0x2F);
        dev->syncTo(119);
        ExpectSpacing(DrainWords(*dev->chip(0)), 71, 24);
    }
}

TEST_F(TsfmPrescaler_Test, NoWarningWhenFrameEndsAtDefault)
{
    // Players' init writes 0x2F then 0x2D within one frame: the §9.4 warning
    // is polled at frame start and must stay silent
    auto dev = std::make_unique<SoundChip_TurboSoundFM>(_context);
    SetT(0);
    dev->portDeviceOutMethod(PORT_FFFD, 0x2F);
    dev->portDeviceOutMethod(PORT_FFFD, 0x2D);
    dev->handleFrameStart();
    EXPECT_FALSE(dev->prescalerWarned());

    // A frame that actually ends on /2 warns once and only once
    auto warned = std::make_unique<SoundChip_TurboSoundFM>(_context);
    SetT(0);
    warned->portDeviceOutMethod(PORT_FFFD, 0x2F);
    warned->syncTo(100);
    SetT(100);
    warned->handleFrameStart();
    EXPECT_TRUE(warned->prescalerWarned());
    SetT(200);
    warned->handleFrameStart();
    EXPECT_TRUE(warned->prescalerWarned()) << "warning must fire once per instance";
}

/// endregion

/// region <TsfmCore>

class TsfmCore_Test : public TsfmDeviceTestBase
{
};

TEST_F(TsfmCore_Test, WordCountPerFrame)
{
    // Pentagon frame = 71680 T. At /6 the FM period is 72 T: 71680/72 = 995.5'
    // samples per frame, so a frame produces 995 or 996 words and 72 frames
    // produce exactly 71680 (the phase carries across the frame boundary)
    _device->syncTo(0);  // adopt the origin

    uint64_t total = 0;
    std::vector<uint64_t> lastTimestamps;
    for (int frame = 0; frame < 72; frame++)
    {
        _device->syncTo(uint64_t(frame + 1) * 71680);
        const std::vector<FmWord> words = DrainWords(C0());
        ASSERT_GE(words.size(), 995u) << "frame " << frame;
        ASSERT_LE(words.size(), 996u) << "frame " << frame;
        total += words.size();
        lastTimestamps.push_back(words.back().t);

        // The second chip advances in lockstep
        EXPECT_EQ(DrainWords(C1()).size(), words.size());
    }
    EXPECT_EQ(total, 71680u);

    // Spot-check frame 0: first word at 72, last at 995*72
    EXPECT_EQ(lastTimestamps[0], 71640);
}

TEST_F(TsfmCore_Test, FrameRolloverRebase)
{
    // §5.2 frame rollover: AdjustFrameCounters subtracts the frame length
    // from z80->t, handleFrameStart shifts _syncedT and every pending word
    // timestamp by the same delta, and the FM phase carries over — no sample
    // is lost or duplicated across the boundary
    _device->syncTo(0);
    _device->syncTo(71720);  // frame 71680 plus a 40 T overrun

    // Keep only the newest word queued (the output stage has drained the
    // rest); it was produced at 71712, i.e. 32 T into the new frame's axis
    while (C0().words.size() > 1)
        C0().words.pop();
    ASSERT_EQ(C0().words.size(), 1u);
    ASSERT_EQ(C0().words.front().t, 71712u);
    const int16_t lastWord = C0().words.front().word;

    SetT(40);  // what z80->t reads after the frame adjust
    _device->handleFrameStart();
    EXPECT_EQ(_device->syncedT(), 40u);
    ASSERT_EQ(C0().words.size(), 1u);
    EXPECT_EQ(C0().words.front().t, 32u);
    EXPECT_EQ(C0().words.front().word, lastWord);
    C0().words.pop();  // the output stage consumes the carried word

    // The next sample lands at 32 + 72 = 104 on the new axis
    _device->syncTo(104);
    const std::vector<FmWord> words = DrainWords(C0());
    ASSERT_EQ(words.size(), 1u);
    EXPECT_EQ(words[0].t, 104u);
}

TEST_F(TsfmCore_Test, IndependentOfOutputStage)
{
    // §12.1: the core is a pure function of the (T-state, port write) stream.
    // Host speed deliberately does not appear: it enters only through
    // z80->t -> AudioTstate (the legacy device's host-multiplier scaling is
    // tracked separately, verification report C10), and the unit driver feeds
    // explicit audio-T positions. What MUST NOT leak into the core is the
    // output-stage configuration: suppression, HQ mode and core rate.
    struct Event
    {
        uint64_t t;
        uint16_t port;
        uint8_t value;
    };

    // Deterministic LCG traffic: control words, SSG and FM register pairs,
    // key-ons, timer loads and prescaler flips
    std::vector<Event> events;
    {
        uint32_t lcg = 0x12345678u;
        auto next = [&lcg](uint32_t bound)
        {
            lcg = lcg * 1664525u + 1013904223u;
            return lcg % bound;
        };
        uint64_t t = 40;
        for (int i = 0; i < 400; i++)
        {
            t += 29 + next(200);
            const uint32_t kind = next(8);
            if (kind == 0)
                events.push_back({t, PORT_FFFD, uint8_t(0xF8 + next(8))});
            else if (kind == 1)
                events.push_back({t, PORT_FFFD, uint8_t(0x2D + next(3))});  // prescaler
            else if (kind < 5)
                events.push_back({t, PORT_FFFD, uint8_t(next(0xB4))});     // address < 0xB4
            else
                events.push_back({t, PORT_BFFD, uint8_t(next(256))});       // data
        }
    }
    const uint64_t endT = events.back().t + 5000;

    struct Outcome
    {
        uint64_t coreHash;
        std::vector<FmWord> words[2];
    };

    auto runWith = [&](bool suppressed, bool hq, size_t rate) -> Outcome
    {
        auto dev = std::make_unique<SoundChip_TurboSoundFM>(_context);
        dev->setSynthesisSuppressed(suppressed);
        dev->setHQEnabled(hq);
        dev->setCoreRate(rate);

        for (const Event& event : events)
        {
            SetT(event.t);
            dev->portDeviceOutMethod(event.port, event.value);
        }
        dev->syncTo(endT);

        Outcome outcome;
        outcome.coreHash = CoreHash(*dev);
        for (int i = 0; i < 2; i++)
            outcome.words[i] = DrainWords(*dev->chip(i));
        return outcome;
    };

    const struct
    {
        bool suppressed;
        bool hq;
        size_t rate;
    } configs[] = {
        {false, true, 44100},   // plain HQ 44.1 k
        {true, false, 48000},   // suppressed (turbo / sound off), LQ 48 k
        {true, true, 192000},   // suppressed, HQ 192 k
        {false, false, 96000},  // synthesis on, LQ 96 k
    };

    Outcome reference = runWith(configs[0].suppressed, configs[0].hq, configs[0].rate);
    ASSERT_FALSE(reference.words[0].empty()) << "traffic produced no FM words";
    for (size_t i = 1; i < sizeof(configs) / sizeof(configs[0]); i++)
    {
        const Outcome other = runWith(configs[i].suppressed, configs[i].hq, configs[i].rate);
        EXPECT_EQ(other.coreHash, reference.coreHash) << "core state depends on config " << i;
        for (int chipIndex = 0; chipIndex < 2; chipIndex++)
        {
            ASSERT_EQ(other.words[chipIndex].size(), reference.words[chipIndex].size());
            for (size_t w = 0; w < reference.words[chipIndex].size(); w++)
            {
                ASSERT_EQ(other.words[chipIndex][w].t, reference.words[chipIndex][w].t)
                    << "word timestamp differs (chip " << chipIndex << ", word " << w << ")";
                ASSERT_EQ(other.words[chipIndex][w].word, reference.words[chipIndex][w].word)
                    << "word value differs (chip " << chipIndex << ", word " << w << ")";
            }
        }
    }
}

/// endregion

/// region <E2E: real player on the FM device>

class TsfmPlayer_Test : public ::testing::Test
{
protected:
    /// One run-mode session over the real player: kFrames frames, per-frame
    /// core hash and drained word count, plus a post-session output-stage
    /// liveness probe (RunNFrames crosses the frame boundary at exit, so
    /// per-frame render counters are already reset when it returns)
    struct SessionOutcome
    {
        std::vector<uint64_t> coreHashes;
        std::vector<uint32_t> wordCounts;
        std::vector<uint64_t> maxWordT;
        uint32_t probeRenderedSamples = 0;
    };

    enum class RunMode
    {
        Normal,
        Turbo,
        SoundOff
    };

    // The P4 gate text pins the cross-mode comparison at 300 frames
    static constexpr int kFrames = 300;

    /// Boots the real TFM player on a fresh TurboSound=FM Pentagon and runs
    /// kFrames frames. Three emulator boots with real player code inside —
    /// the sanctioned ROM-boot exception to the 50 ms per-test budget.
    SessionOutcome RunPlayerSession(RunMode mode)
    {
        Emulator* fm = CreateFmEmulator(LoggerLevel::LogError);
        EXPECT_NE(fm, nullptr) << "failed to boot a TurboSound=FM emulator";
        if (!fm)
            return {};

        if (mode == RunMode::Turbo)
            fm->EnableTurboMode();
        if (mode == RunMode::SoundOff)
            fm->GetFeatureManager()->setFeature(Features::kSoundGeneration, false);

        TsfmPlayerHarness harness;
        EXPECT_TRUE(harness.Setup(fm, 0));
        auto* device = static_cast<SoundChip_TurboSoundFM*>(fm->GetContext()->pSoundManager->getTurboSound());
        EXPECT_EQ(device->TTDPeripheralId(), ttd::PeripheralId::TSFM);

        SessionOutcome outcome;
        for (int frame = 0; frame < kFrames; frame++)
        {
            harness.RunFrames(1);

            // Words drained this frame (both chips). The queue is an
            // output-stage buffer: suppressed modes clear it at every frame
            // start (§6.1), while the live output stage (§6) consumes the
            // production as it renders and drains the rest at frame end -
            // only stragglers remain here
            uint32_t wordCount = 0;
            uint64_t maxWordT = 0;
            for (int chipIndex = 0; chipIndex < 2; chipIndex++)
                for (const FmWord& w : DrainWords(*device->chip(chipIndex)))
                {
                    wordCount++;
                    if (w.t > maxWordT)
                        maxWordT = w.t;
                }
            outcome.wordCounts.push_back(wordCount);
            outcome.maxWordT.push_back(maxWordT);
            outcome.coreHashes.push_back(CoreHash(*device));
        }

        // Output-stage liveness probe: RunNFrames left the device at the
        // START of the next frame (the boundary crossing ran OnFrameStart,
        // which reset the per-frame render cursor), so the probe parks the
        // CPU mid-frame and steps the device once by hand. The suppression
        // flag SoundManager pushed for this frame is still live: a normal
        // session must render a half frame's worth of pairs, turbo and
        // sound-off none (§6.1)
        Z80* z80 = fm->GetContext()->pCore->GetZ80();
        z80->t = PENTAGON_FRAME / 2;
        z80->tt = uint32_t(PENTAGON_FRAME / 2) << 8;
        device->handleStep();
        outcome.probeRenderedSamples = uint32_t(device->getRenderedSamplesThisFrame());

        harness.Detach();
        ReleaseFmEmulator(fm);
        return outcome;
    }
};

TEST_F(TsfmPlayer_Test, PlayerCompletesInitAndKeepsPlaying)
{
    // P4 gate: with TurboSound = FM the P0 harness runs the player without
    // hanging. The legacy device parks init in the WaitStatus poll after ~15
    // writes; the TSFM busy flag must carry init to completion inside frame 0
    // and the frame routine must then write every frame. Turbo mode doubles as
    // the busy-under-suppression check (the core must keep answering polls)
    Emulator* fm = CreateFmEmulator(LoggerLevel::LogError);
    ASSERT_NE(fm, nullptr) << "failed to boot a TurboSound=FM emulator";

    TsfmPlayerHarness harness;
    ASSERT_TRUE(harness.Setup(fm, 0));
    fm->EnableTurboMode();

    harness.RunFrames(120);

    const auto& perFrame = harness.GetPerFrameWriteCounts();
    ASSERT_EQ(perFrame.size(), 120u);

    // Frame 0 hosts init: far more traffic than the legacy 15-write park
    EXPECT_GT(perFrame[0], 30u) << "init did not run to completion (WaitStatus park?)";

    // Every later frame plays the tune (version '2' players output per frame)
    for (size_t frame = 1; frame < perFrame.size(); frame++)
        EXPECT_GT(perFrame[frame], 0u) << "player went silent on frame " << frame;
    EXPECT_EQ(harness.GetFramesSinceLastWrite(), 0u) << "player stopped writing";

    // The frame routine's per-frame control words are all present
    EXPECT_GE(harness.GetControlWordCount(0xF8), 100u);
    EXPECT_GE(harness.GetControlWordCount(0xF9), 1u) << "chip reset never selected chip 1";

    harness.Detach();
    ReleaseFmEmulator(fm);
}

TEST_F(TsfmPlayer_Test, CoreHashSameInTurboAndSoundOff)
{
    // P4 gate, design D3: turbo and sound-off switch the output stage off
    // only - the core keeps answering busy polls and running timers on the
    // same T-state axis. The per-frame core hash must equal the normal
    // session's, or a TTD recording made in one mode would replay
    // divergently in another. Since P6 the output stage renders in the
    // normal session (its sample count is asserted below) and consumes the
    // word production; the suppressed modes render nothing and clear the
    // queues at every frame start (§6.1)
    const SessionOutcome normal = RunPlayerSession(RunMode::Normal);
    const SessionOutcome turbo = RunPlayerSession(RunMode::Turbo);
    const SessionOutcome soundOff = RunPlayerSession(RunMode::SoundOff);

    ASSERT_EQ(normal.coreHashes.size(), size_t(kFrames)) << "normal session truncated";
    ASSERT_EQ(turbo.coreHashes.size(), size_t(kFrames)) << "turbo session truncated";
    ASSERT_EQ(soundOff.coreHashes.size(), size_t(kFrames)) << "sound-off session truncated";
    EXPECT_GT(normal.probeRenderedSamples, 400u) << "normal session renders no audio (output stage dead?)";
    EXPECT_EQ(turbo.probeRenderedSamples, 0u) << "turbo session rendered audio";
    EXPECT_EQ(soundOff.probeRenderedSamples, 0u) << "sound-off session rendered audio";

    for (int frame = 0; frame < kFrames; frame++)
    {
        ASSERT_EQ(turbo.coreHashes[frame], normal.coreHashes[frame]) << "turbo core diverged at frame " << frame;
        ASSERT_EQ(soundOff.coreHashes[frame], normal.coreHashes[frame])
            << "sound-off core diverged at frame " << frame;
        // Words still queued after a suppressed frame must all have been
        // produced after the frame-start clear: on the rebased T axis (§5.2
        // rollover) their timestamps are intra-frame positions, bounded by
        // the RunNFrames exit-overshoot drift (a few T per frame, ~1 kT by
        // session end). A clear that failed to run would leave a whole frame
        // queued with timestamps spread up to 71680. Both suppressed modes
        // share the T axis, so they must drain identically
        EXPECT_LT(turbo.maxWordT[frame], 8192u) << "turbo drained stale words after frame " << frame;
        EXPECT_LT(soundOff.maxWordT[frame], 8192u) << "sound-off drained stale words after frame " << frame;
        EXPECT_EQ(turbo.wordCounts[frame], soundOff.wordCounts[frame])
            << "suppressed modes straggled differently at frame " << frame;
    }
}

/// endregion
