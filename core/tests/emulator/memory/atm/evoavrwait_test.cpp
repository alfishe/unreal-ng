// The ZX-Evo AVR's /WAIT ports (EvoAvrWait): the COM port (#xxEF) and the Gluk clock data port (#BFF7 / #BEF7) wait
// for the same AVR main loop (TS-Conf fpga/current/z80/zwait.v:31-43, BaseConf fpga/base/z80/zwait.v:57-67); a wait
// is the interrupt + the loop reaching its flag test + the firmware's service up to the release. The Gluk services
// are counted on the released firmware images (reference-evo-com-port.md §3.1).

#include <gtest/gtest.h>

#include <memory>

#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/io/serial/comport.h"
#include "emulator/memory/atm/evoavr.h"
#include "emulator/memory/atm/evoavrwait.h"
#include "emulator/ports/models/portdecoder_atm3.h"
#include "emulator/ports/models/portdecoder_tsconf.h"

namespace
{
constexpr uint32_t kAvrHz = 11059200;
constexpr uint32_t kBaseHz = 3500000;
constexpr uint32_t kIsr = 37;
constexpr uint32_t kPass = 260;

/// avrNow of `now` base T-states, as the model counts it
uint64_t AvrAt(uint64_t now)
{
    return now * kAvrHz / kBaseHz;
}
}  // namespace

/// The model alone: ISR + phase + service; right behind a release a whole pass is left
TEST(EvoAvrWait_Test, AnAccessWaitsInterruptPlusPhasePlusService)
{
    EvoAvrWait wait;
    const uint64_t now = 1000000;
    wait.SetState(EvoAvrWait::State{AvrAt(now), 0});   // the loop resumed its pass just now
    EXPECT_EQ(wait.Access(300, 0, now, kBaseHz), kIsr + kPass + 300) << "a whole pass to the flag test";

    EvoAvrWait late;
    const uint32_t anywhere = late.Access(300, 0, now, kBaseHz);
    EXPECT_GT(anywhere, kIsr + 300);
    EXPECT_LE(anywhere, kIsr + kPass + 300);

    EvoAvrWait ts;
    ts.SetTiming(EvoAvrWait::Timing{kAvrHz, 37, 260, 8});
    ts.SetState(EvoAvrWait::State{AvrAt(now), 0});
    EXPECT_EQ(ts.Access(100, 0, now, kBaseHz), kIsr + kPass / 8 + 100) << "TS: the flag is looked at after each task";
}

/// Work after the release (a Gluk write's I2C transfer) keeps the AVR from its loop: the next access waits for it
TEST(EvoAvrWait_Test, WorkAfterTheReleaseDelaysTheNextAccess)
{
    EvoAvrWait wait;
    const uint64_t now = 2000000;
    wait.SetState(EvoAvrWait::State{AvrAt(now), 0});
    const uint32_t first = wait.Access(281, 3400, now, kBaseHz);
    const uint64_t release = AvrAt(now) + first;
    EXPECT_EQ(wait.GetState().loopResume, release + 3400);

    // The Z80 comes back 10 AVR cycles after its release: 3390 cycles of I2C left, then a whole pass
    const uint64_t next = (release + 10) * kBaseHz / kAvrHz + 1;
    const uint64_t busy = release + 3400 - AvrAt(next);
    EXPECT_EQ(wait.Access(100, 0, next, kBaseHz), kIsr + busy + kPass + 100);
}

/// The EEPROM: a write starts an 8.5 ms write after the release; the next EEPROM access busy-waits for its end
TEST(EvoAvrWait_Test, AnEepromReadWaitsForTheLastEepromWrite)
{
    EvoAvrWait wait;
    const uint64_t now = 3000000;
    wait.SetState(EvoAvrWait::State{AvrAt(now), 0});
    const uint32_t write = wait.Access(281, 100, now, kBaseHz, EvoAvrWait::Eeprom::Write);
    const uint64_t release = AvrAt(now) + write;
    const uint64_t writeTime = static_cast<uint64_t>(kAvrHz) * EvoAvrWait::kEepromWriteMicros / 1000000;
    EXPECT_EQ(wait.GetState().eepromReadyAt, release + writeTime);

    // 1 ms later the EEPROM is still busy: the read's service waits for the rest of the write
    const uint64_t later = now + kBaseHz / 1000 + (static_cast<uint64_t>(write) * kBaseHz) / kAvrHz;
    const uint32_t read = wait.Access(300, 0, later, kBaseHz, EvoAvrWait::Eeprom::Read);
    EXPECT_GT(read, writeTime * 8 / 10) << "about 7.5 ms of the 8.5 ms write left";
    EXPECT_LT(read, writeTime);

    // A plain access does not care about the EEPROM
    EvoAvrWait plain;
    plain.SetState(EvoAvrWait::State{AvrAt(now), AvrAt(now) + writeTime});
    EXPECT_EQ(plain.Access(300, 0, now, kBaseHz), kIsr + kPass + 300);
}

/// The emulator's time base starts again at a machine reset (and under a snapshot load): a phase anchored far in the
/// future would stall the first access for the whole previous run. Reset() re-anchors; anything more than a second
/// ahead is dropped
TEST(EvoAvrWait_Test, ATimeBaseRestartForgetsThePhase)
{
    EvoAvrWait wait;
    wait.SetState(EvoAvrWait::State{AvrAt(50000000), AvrAt(50000000)});   // anchored 14 s into the old run
    const uint32_t first = wait.Access(300, 0, 1000, kBaseHz);
    EXPECT_LE(first, kIsr + kPass + 300) << "no stall for the old run's time";
    wait.Reset();
    EXPECT_EQ(wait.GetState().loopResume, 0u);
    EXPECT_EQ(wait.GetState().eepromReadyAt, 0u);
}

class EvoAvrGlukWait_Test : public ::testing::Test
{
protected:
    EmulatorManager* _manager = nullptr;
    std::shared_ptr<Emulator> _emulator;
    EmulatorContext* _context = nullptr;
    Z80* _z80 = nullptr;
    EvoAvr* _avr = nullptr;

    void SetUp() override { _manager = EmulatorManager::GetInstance(); }

    void TearDown() override
    {
        if (_emulator)
            _manager->RemoveEmulator(_emulator->GetId());
    }

    void Create(const char* model)
    {
        _emulator = _manager->CreateEmulatorWithModelAndRAM("evoavrwait", model, 4096, LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _z80 = _context->pCore->GetZ80();
        if (auto* tsconf = dynamic_cast<PortDecoder_TSConf*>(_context->pPortDecoder))
            _avr = &tsconf->GetEvoAvr();
        else if (auto* atm3 = dynamic_cast<PortDecoder_ATM3*>(_context->pPortDecoder))
        {
            _avr = &atm3->GetEvoAvr();
            // Outside shadow (TR-DOS off, #BF shaden off, CP/M and the pager on): #BFF7 after #EFF7 bit 7
            EmulatorState& state = _context->emulatorState;
            state.atm.aFF77 = PortDecoder_ATM3::ATM_AFF77_PEN | PortDecoder_ATM3::ATM_AFF77_CPM;
            state.flags &= ~CF_TRDOS;
            state.evo.pBF = 0x00;
        }
        ASSERT_NE(_avr, nullptr);
    }

    /// The AVR cycles in CPU clocks at the current speed (rounded up, as the board waits)
    uint32_t ClocksFor(uint32_t avrCycles) const
    {
        const uint64_t cpuHz = _context->emulatorState.current_z80_frequency;
        return static_cast<uint32_t>((static_cast<uint64_t>(avrCycles) * cpuHz + kAvrHz - 1) / kAvrHz);
    }

    /// The AVR's main loop resumed its pass this very moment: the next access waits a whole pass (TS: a task)
    void LoopResumesNow()
    {
        const uint64_t avrNow = EvoAvrWait::BaseNow(_context) * kAvrHz / _context->emulatorState.base_z80_frequency;
        _avr->SetWaitState(EvoAvrWait::State{avrNow, 0});
    }

    uint32_t TimeIn(uint16_t port)
    {
        const uint32_t before = _z80->t;
        (void)_z80->in(port);
        return _z80->t - before;
    }

    uint32_t TimeOut(uint16_t port, uint8_t value)
    {
        const uint32_t before = _z80->t;
        _z80->out(port, value);
        return _z80->t - before;
    }
};

/// TS-Conf (TS firmware, zx_wait_task since 2021-04-28): wait_start_gluclock = gluclock_on && !a[14] &&
/// (portf7_rd || portf7_wr) (fpga/current/z80/zports.v:763). A cell outside #F0-#FF costs the extra SPI #41
/// exchange; #F0-#FF rides in the status byte
TEST_F(EvoAvrGlukWait_Test, TsConfDataPortWaitsForTheAvr)
{
    Create("TSL");
    constexpr uint32_t kTask = kPass / 8;   // TS: waittask() after each of the 8 tasks
    EXPECT_EQ(TimeOut(0xEFF7, 0x80), 0u) << "#EFF7 is the FPGA's";
    EXPECT_EQ(TimeOut(0xDFF7, 0x00), 0u) << "the address port is the FPGA's latch: no wait";

    LoopResumesNow();
    EXPECT_EQ(TimeIn(0xBFF7), ClocksFor(kIsr + kTask + 151 + 122)) << "seconds, BCD: full address + hex_to_bcd";
    LoopResumesNow();
    EXPECT_EQ(TimeOut(0xDFF7, 0x0B), 0u);
    EXPECT_EQ(TimeOut(0xBFF7, 0x02), ClocksFor(kIsr + kTask + 147)) << "a register write: its work after the release";
    LoopResumesNow();
    (void)_z80->out(0xDFF7, 0xF0);
    EXPECT_EQ(TimeIn(0xBFF7), ClocksFor(kIsr + kTask + 90 + 39)) << "#F0: the short path, the version window";

    // 14 MHz: the same AVR time, four times the clocks
    _z80->out(0x20AF, 0x02);   // SYS_CONFIG: 14 MHz
    ASSERT_EQ(_context->emulatorState.current_z80_frequency, 14000000u);
    (void)_z80->out(0xDFF7, 0x00);
    LoopResumesNow();
    EXPECT_EQ(TimeIn(0xBFF7), ClocksFor(kIsr + kTask + 151 + 122));
    EXPECT_GT(ClocksFor(kIsr + kTask + 151 + 122), 400u);

    // Not reachable: no wait (#EFF7 bit 7 clear outside DOS)
    _z80->out(0xEFF7, 0x00);
    EXPECT_EQ(TimeIn(0xBFF7), 0u);
    EXPECT_EQ(TimeOut(0xBFF7, 0x00), 0u);
}

/// TS-Conf inside vdos: the AVR serves the access (portf7 allows vdos, zports.v:720-721) and the Z80 waits for it,
/// though the read floats (#FF); in plain DOS the clock is not reachable and nothing waits
TEST_F(EvoAvrGlukWait_Test, TsConfWaitsInsideVdosNotInDos)
{
    Create("TSL");
    auto* tsconf = dynamic_cast<PortDecoder_TSConf*>(_context->pPortDecoder);
    TsConfState& ts = tsconf->GetState();
    _z80->out(0xEFF7, 0x80);
    _z80->out(0xDFF7, 0x0A);
    ts.dos = 1;
    EXPECT_EQ(TimeIn(0xBFF7), 0u) << "DOS outside vdos: the clock is closed";
    ts.vdos = 1;
    LoopResumesNow();
    EXPECT_EQ(TimeIn(0xBFF7), ClocksFor(kIsr + kPass / 8 + 151 + 34)) << "register A inside vdos";
    ts.vdos = 0;
    ts.dos = 0;
}

/// ATM3 (BaseConf FPGA + NedoPC firmware): the same gating (fpga/base/z80/zports.v:754, zwait.v:57-61); the firmware
/// fetches the cell with SPI #41 for every access and tests the flag once per pass
TEST_F(EvoAvrGlukWait_Test, Atm3DataPortWaitsForTheAvr)
{
    Create("ATM3");
    _z80->out(0xEFF7, 0x80);
    _z80->out(0xDFF7, 0x00);
    LoopResumesNow();
    EXPECT_EQ(TimeIn(0xBFF7), ClocksFor(kIsr + kPass + 295 + 98)) << "seconds, BCD";
    _z80->out(0xDFF7, 0x0B);
    LoopResumesNow();
    EXPECT_EQ(TimeOut(0xBFF7, 0x02), ClocksFor(kIsr + kPass + 281)) << "a register write";

    // An NVRAM cell is an I2C read of the PCF8583 before the release: about 0.4 ms
    _z80->out(0xDFF7, 0x20);
    LoopResumesNow();
    const uint32_t nvram = TimeIn(0xBFF7);
    EXPECT_EQ(nvram, ClocksFor(kIsr + kPass + 295 + 4427));
    EXPECT_GT(nvram * 1e6 / _context->emulatorState.current_z80_frequency, 400.0);

    _z80->out(0xEFF7, 0x00);
    EXPECT_EQ(TimeIn(0xBFF7), 0u) << "the clock closed: no wait";

    // A reset starts the time base again: the AVR's phase is re-anchored, the first access after it is not stalled
    // for the time before it (the ERS reads the NVRAM right after a reset)
    EXPECT_NE(_avr->GetWaitState().loopResume, 0u);
    _emulator->Reset();
    EXPECT_EQ(_avr->GetWaitState().loopResume, 0u);
}

/// One AVR, one main loop: a CMOS access right behind a COM access finds the loop at the start of a pass
TEST_F(EvoAvrGlukWait_Test, ACmosAccessRightAfterAComAccessSeesTheSharedPhase)
{
    Create("ATM3");
    ASSERT_NE(_context->pComPort, nullptr);
    ASSERT_EQ(_context->pComPort->GetAvrWait(), &_avr->Wait()) << "the COM port waits on the board's AVR";
    _z80->out(0xEFF7, 0x80);
    _z80->out(0xDFF7, 0x0A);

    (void)_z80->in(0xFDEF);   // LSR: the AVR releases the Z80 and starts its pass again
    const uint32_t cmos = TimeIn(0xBFF7);
    const uint32_t wholePass = kIsr + kPass + 295 + 22;   // register A
    EXPECT_GE(cmos, ClocksFor(wholePass - 4)) << "right behind the COM access: a whole pass left";
    EXPECT_LE(cmos, ClocksFor(wholePass + 4));

    // And the other way round: the COM access behind the CMOS one
    const Uart16550::Params& p = _context->pComPort->Uart().GetParams();
    const uint32_t com = TimeIn(0xFDEF);
    EXPECT_GE(com, ClocksFor(p.isrCycles + p.loopCycles + p.serviceRead - 4));
    EXPECT_LE(com, ClocksFor(p.isrCycles + p.loopCycles + p.serviceRead + 44 + 4)) << "the Gluk read's tail on top";
}
