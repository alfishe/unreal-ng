// TS-Conf interrupt controller (TSConf implementation-plan phase 2,
// hardware-spec §5): event positions, pulse length, priority, masking, clock.
// The controller is driven directly with frame T-states, as the Z80 drives it.

#include "tsconffixture.h"

#include "emulator/emulator.h"
#include "emulator/emulatormanager.h"

class TsConfInterrupts_Test : public TsConfFixture
{
protected:
    TsConfInterrupts& Ints() { return _decoder->GetInterrupts(); }

    void SetMultiplier(uint8_t multiplier) { _context->emulatorState.current_z80_frequency_multiplier = multiplier; }

    /// Frame T-states [from, to) at which /INT is asserted, acknowledging none
    std::vector<uint32_t> AssertedAt(uint32_t from, uint32_t to)
    {
        std::vector<uint32_t> result;
        for (uint32_t t = from; t < to; t++)
            if (Ints().IsIntAsserted(t))
                result.push_back(t);
        return result;
    }
};

/// The decoder installs the controller on the CPU
TEST_F(TsConfInterrupts_Test, InstalledOnTheCpu)
{
    EXPECT_EQ(_z80->GetInterruptSource(), &Ints());
    EXPECT_EQ(_z80->GetMachineStepHook(), &_decoder->GetEngine()) << "the engine drives the interrupt controller";
}

/// INT-1: after reset one frame INT at tact 1, vector 0xFF, a 32-clock pulse
TEST_F(TsConfInterrupts_Test, INT1_ResetFramePulse)
{
    const std::vector<uint32_t> asserted = AssertedAt(0, 200);
    ASSERT_EQ(asserted.size(), 32u);
    EXPECT_EQ(asserted.front(), 1u);
    EXPECT_EQ(asserted.back(), 32u) << "an EI taking effect at tact 33 misses it";
    EXPECT_TRUE(AssertedAt(200, TsConfInterrupts::kFrameTacts).empty());
}

TEST_F(TsConfInterrupts_Test, INT1_AcknowledgeGivesVectorFFAndClears)
{
    ASSERT_TRUE(Ints().IsIntAsserted(5));
    EXPECT_EQ(Ints().AcknowledgeInterrupt(5), 0xFF);
    EXPECT_FALSE(Ints().IsIntAsserted(6));
}

/// INT-2: the frame INT position; out-of-range positions never match
TEST_F(TsConfInterrupts_Test, INT2_FramePosition)
{
    Reg(TsConfReg::VsIntL, 100);
    Reg(TsConfReg::HsInt, 10);
    EXPECT_FALSE(Ints().IsIntAsserted(22409));
    EXPECT_TRUE(Ints().IsIntAsserted(22410));

    Ints().Reset();
    Reg(TsConfReg::HsInt, 224);
    EXPECT_TRUE(AssertedAt(0, TsConfInterrupts::kFrameTacts).empty());

    Ints().Reset();
    Reg(TsConfReg::HsInt, 0);
    Reg(TsConfReg::VsIntL, 0x40);
    Reg(TsConfReg::VsIntH, 0x01);  // 320
    EXPECT_TRUE(AssertedAt(0, TsConfInterrupts::kFrameTacts).empty());
}

/// INT-3: the line INT, 320 per frame, at the end of every line: raster tact 224 n ([V] video_sync.v:125
/// line_start_s = the line's last fclk, int_lin set from the next tact; [U] line_t = 0, 224, ...). Was 224 n - 1:
/// raster code timed to the line INT ran a tact early. The last line's event is tact 0 of the next frame
TEST_F(TsConfInterrupts_Test, INT3_LineInterrupts)
{
    Reg(TsConfReg::IntMask, TsConfInt::Line);
    for (uint32_t t = 0; t < TsConfInterrupts::kFrameTacts; t += 7)  // the frame the mask was set in
        if (Ints().IsIntAsserted(t))
            Ints().AcknowledgeInterrupt(t);
    _z80->tt = 0;
    Ints().OnMachineFrameRollover(TsConfInterrupts::kFrameTacts);

    std::vector<uint32_t> taken;
    for (uint32_t t = 0; t < TsConfInterrupts::kFrameTacts; t++)
    {
        if (Ints().IsIntAsserted(t))
        {
            EXPECT_EQ(Ints().AcknowledgeInterrupt(t), 0xFD);
            taken.push_back(t);
        }
    }
    ASSERT_EQ(taken.size(), 320u);
    EXPECT_EQ(taken.front(), 0u) << "the previous frame's last line";
    EXPECT_EQ(taken[1], 224u);
    EXPECT_EQ(taken[2], 448u);
    EXPECT_EQ(taken.back(), TsConfInterrupts::kFrameTacts - TsConfInterrupts::kLineTacts);
}

/// INT-3b: the line INT is not up during the line's last tact, only from the next line's first
TEST_F(TsConfInterrupts_Test, INT3b_LineIntRisesAtTheNextLinesFirstTact)
{
    Reg(TsConfReg::IntMask, TsConfInt::Line);
    EXPECT_FALSE(Ints().IsIntAsserted(223)) << "the last tact of line 0";
    EXPECT_TRUE(Ints().IsIntAsserted(224));
}

/// INT-4: frame and line together: frame first, only the served source clears
TEST_F(TsConfInterrupts_Test, INT4_PriorityAndSelectiveClear)
{
    Reg(TsConfReg::IntMask, TsConfInt::Frame | TsConfInt::Line);
    Reg(TsConfReg::HsInt, 223);
    ASSERT_TRUE(Ints().IsIntAsserted(223));
    EXPECT_EQ(Ints().AcknowledgeInterrupt(223), 0xFF);
    ASSERT_TRUE(Ints().IsIntAsserted(224));
    EXPECT_EQ(Ints().AcknowledgeInterrupt(224), 0xFD);
    EXPECT_FALSE(Ints().IsIntAsserted(225));
}

/// INT-5: masking clears the latch; unmasking does not create one
TEST_F(TsConfInterrupts_Test, INT5_MaskClearsPending)
{
    Reg(TsConfReg::IntMask, TsConfInt::Line);
    ASSERT_TRUE(Ints().IsIntAsserted(300));  // latched at 224, not acknowledged (DI)
    Reg(TsConfReg::IntMask, 0x00);
    Reg(TsConfReg::IntMask, TsConfInt::Line);
    EXPECT_FALSE(Ints().IsIntAsserted(301));
    EXPECT_FALSE(Ints().IsIntAsserted(447));
    EXPECT_TRUE(Ints().IsIntAsserted(448));
}

/// INT-7: at 14 MHz the pulse is 32 CPU clocks = 8 raster tacts
TEST_F(TsConfInterrupts_Test, INT7_PulseIsCpuClocks)
{
    SetMultiplier(4);
    const std::vector<uint32_t> asserted = AssertedAt(0, 400);
    ASSERT_EQ(asserted.size(), 32u);
    EXPECT_EQ(asserted.front(), 4u) << "raster tact 1";
    EXPECT_EQ(asserted.back(), 35u);
}

/// A frame pulse that starts on the last tact runs on into the next frame
TEST_F(TsConfInterrupts_Test, FramePulseCrossesTheFrameEnd)
{
    Reg(TsConfReg::VsIntL, 0x3F);
    Reg(TsConfReg::VsIntH, 0x01);  // line 319
    Reg(TsConfReg::HsInt, 223);
    EXPECT_TRUE(AssertedAt(0, TsConfInterrupts::kFrameTacts - 1).empty());
    EXPECT_TRUE(Ints().IsIntAsserted(TsConfInterrupts::kFrameTacts - 1));
    Ints().OnMachineFrameRollover(TsConfInterrupts::kFrameTacts);
    const std::vector<uint32_t> asserted = AssertedAt(0, 100);
    ASSERT_EQ(asserted.size(), 31u) << "32 clocks in all";
    EXPECT_EQ(asserted.back(), 30u);
}

/// Events on the frame's last tact are not lost at the rollover (the step
/// hook may not have seen that tact)
TEST_F(TsConfInterrupts_Test, RolloverLatchesTheLastLineEvent)
{
    Reg(TsConfReg::IntMask, TsConfInt::Line);
    ASSERT_TRUE(Ints().IsIntAsserted(71600));
    Ints().AcknowledgeInterrupt(71600);
    Ints().OnMachineFrameRollover(TsConfInterrupts::kFrameTacts);
    EXPECT_TRUE(Ints().IsIntAsserted(0)) << "the last line's end, tact 0 of the new frame";
}

/// INT-8 (partial, before vdos exists): the output is gated, the latch stays
TEST_F(TsConfInterrupts_Test, INT8_VdosGatesWithoutLosing)
{
    _decoder->GetState().vdos = 1;
    EXPECT_FALSE(Ints().IsIntAsserted(5));
    _decoder->GetState().vdos = 0;
    EXPECT_TRUE(Ints().IsIntAsserted(6));
}

/// INT-11: vdos freezes the frame pulse ([V] zint.v:194: intctr counts on `zpos && !intctr_fin && !wait_r && !vdos`)
/// and gates the output from the trapped access on ([V] top.v:1106: the controller's vdos is `pre_vdos`, set during
/// the trapped I/O cycle). The pulse went on counting through vdos and was gone after 32 clocks: a long virtual-drive
/// session lost the frame INT. [U] tsconf.cpp:940 freezes it too
class TsConfVdosInt_Test : public TsConfInterrupts_Test
{
protected:
    /// Drive B virtual, in DOS, B selected: the next VG93 access is trapped
    void ArmVirtualDrive()
    {
        Reg(TsConfReg::FddVirt, 0x02);
        _decoder->GetState().dos = 1;
        _decoder->ApplyState();
        Out(0x00FF, 0x01);
    }
    /// A trapped VG93 access at CPU clock t (vdos starts at the next M1)
    void EnterVdos(uint32_t t)
    {
        _z80->tt = t << 8;
        In(0x001F);
        _decoder->BeforeMachineM1(0x3D30);
        ASSERT_EQ(_decoder->GetState().vdos, 1);
    }
    /// A VG93 register access inside vdos at CPU clock t ends it
    void LeaveVdos(uint32_t t)
    {
        _z80->tt = t << 8;
        In(0x003F);
        ASSERT_EQ(_decoder->GetState().vdos, 0);
    }
};

TEST_F(TsConfVdosInt_Test, INT11a_TheTrappedAccessGatesAtOnce)
{
    ArmVirtualDrive();
    ASSERT_TRUE(Ints().IsIntAsserted(10)) << "the reset pulse, clocks 1..32";
    _z80->tt = 10u << 8;
    In(0x001F);
    ASSERT_EQ(_decoder->GetState().preVdos, 1);
    EXPECT_FALSE(Ints().IsIntAsserted(10)) << "gated by pre_vdos, before the M1 that maps the virtual drive";
}

TEST_F(TsConfVdosInt_Test, INT11b_VdosFreezesARunningPulse)
{
    ArmVirtualDrive();
    ASSERT_TRUE(Ints().IsIntAsserted(10));
    EnterVdos(10);  // 9 clocks of the pulse are over
    EXPECT_FALSE(Ints().IsIntAsserted(400));
    LeaveVdos(500);  // + the vdos-exit stall (4 fclk, TIM-7), which freezes the pulse too: one more clock
    EXPECT_TRUE(Ints().IsIntAsserted(501)) << "the pulse was frozen, not used up";
    EXPECT_TRUE(Ints().IsIntAsserted(523)) << "23 clocks were left";
    EXPECT_FALSE(Ints().IsIntAsserted(524));
}

TEST_F(TsConfVdosInt_Test, INT11c_APulseThatStartsInsideVdosBeginsWhenItEnds)
{
    Reg(TsConfReg::VsIntL, 1);
    Reg(TsConfReg::HsInt, 0);  // raster tact 224
    ArmVirtualDrive();
    EnterVdos(100);
    EXPECT_FALSE(Ints().IsIntAsserted(300));
    LeaveVdos(1000);  // + the vdos-exit stall (one clock here)
    EXPECT_TRUE(Ints().IsIntAsserted(1001));
    EXPECT_TRUE(Ints().IsIntAsserted(1032));
    EXPECT_FALSE(Ints().IsIntAsserted(1033)) << "32 clocks after vdos and its stall ended";
}

TEST_F(TsConfVdosInt_Test, INT11d_VdosAcrossTheFrameEnd)
{
    Reg(TsConfReg::VsIntL, 0x3F);
    Reg(TsConfReg::VsIntH, 0x01);
    Reg(TsConfReg::HsInt, 223);  // raster tact 71679, the frame's last
    ArmVirtualDrive();
    EnterVdos(71600);
    _z80->tt = 0;  // the rebase of the frame end
    Ints().OnMachineFrameRollover(TsConfInterrupts::kFrameTacts);
    EXPECT_FALSE(Ints().IsIntAsserted(20));
    LeaveVdos(50);  // + the vdos-exit stall (one clock here)
    EXPECT_TRUE(Ints().IsIntAsserted(51)) << "the event of the old frame's last tact, deferred";
    EXPECT_TRUE(Ints().IsIntAsserted(82));
    EXPECT_FALSE(Ints().IsIntAsserted(83));
}

TEST_F(TsConfVdosInt_Test, INT11e_AWaitInsideVdosDoesNotShiftTwice)
{
    ArmVirtualDrive();
    EnterVdos(10);
    Ints().OnWait(100u << 8, 200u << 8);  // the counter already stands still
    LeaveVdos(500);  // + the vdos-exit stall (one clock here)
    EXPECT_TRUE(Ints().IsIntAsserted(523));
    EXPECT_FALSE(Ints().IsIntAsserted(524));
}

/// TTD-2: the latches live in the TTD blob
TEST_F(TsConfInterrupts_Test, TTD2_LatchesAreState)
{
    ASSERT_TRUE(Ints().IsIntAsserted(10));
    const TsConfState saved = _decoder->GetState();
    Ints().AcknowledgeInterrupt(10);
    EXPECT_FALSE(Ints().IsIntAsserted(11));
    _decoder->GetState() = saved;
    EXPECT_TRUE(Ints().IsIntAsserted(11));
}

/// INT-6: through the CPU - IM1 ignores the vector and jumps to #0038, IM0
/// executes the #FF on the bus (RST 38), IM2 reads the vector #FF from I;
/// each acknowledge clears the latch it answered. A whole machine: the CPU's
/// step needs the main loop the port-level fixture does not have
TEST(TsConfInterruptsCpu_Test, INT6_InterruptModesThroughTheCpu)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    auto emulator = manager->CreateEmulatorWithModelAndRAM("tsconf-int6", "TSL", 4096, LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    auto* decoder = dynamic_cast<PortDecoder_TSConf*>(context->pPortDecoder);
    ASSERT_NE(decoder, nullptr);
    Z80& z80 = *context->pCore->GetZ80();
    Memory& memory = *context->pMemory;
    decoder->WriteRegister(TsConfReg::Page2, 0x02);  // RAM at #8000
    decoder->WriteRegister(TsConfReg::Page3, 0x03);  // RAM at #C000 (the stack and the IM2 table)

    for (uint8_t mode : {uint8_t(1), uint8_t(0), uint8_t(2)})
    {
        SCOPED_TRACE(int(mode));
        decoder->GetInterrupts().Reset();
        memory.DirectWriteToZ80Memory(0x8000, 0x00);  // NOP
        memory.DirectWriteToZ80Memory(0xC0FF, 0x34);  // IM2 table entry for vector #FF
        memory.DirectWriteToZ80Memory(0xC100, 0x92);  // -> #9234
        z80.i = 0xC0;
        z80.im = mode;
        z80.iff1 = z80.iff2 = 1;
        z80.pc = 0x8000;
        z80.sp = 0xF000;
        z80.t = 5;  // inside the reset frame pulse (tacts 1..32)
        ASSERT_TRUE(decoder->GetInterrupts().IsIntAsserted(5));
        const Z80::StepResult result = z80.StepInstruction(true);
        EXPECT_TRUE(result.intAccepted);
        EXPECT_EQ(z80.pc, mode == 2 ? 0x9234 : 0x0038);
        EXPECT_EQ(memory.DirectReadFromZ80Memory(0xEFFE), 0x00) << "return address low";
        EXPECT_EQ(memory.DirectReadFromZ80Memory(0xEFFF), 0x80) << "return address high";
        EXPECT_EQ(decoder->GetState().intPending & TsConfInt::Frame, 0) << "the latch was answered";
    }
    manager->RemoveEmulator(emulator->GetUUID());
}

/// CLK-2: a clock switch mid-frame keeps the raster events where they are -
/// the frame INT at line 100, tact 10 asserts at that raster tact before and
/// after 3.5 -> 14 MHz (the CPU clock count at that instant scales 4x)
TEST_F(TsConfInterrupts_Test, CLK2_ClockSwitchKeepsRasterEvents)
{
    Reg(TsConfReg::VsIntL, 100);
    Reg(TsConfReg::HsInt, 10);
    const uint32_t tact = 100 * TsConfInterrupts::kLineTacts + 10;
    EXPECT_FALSE(Ints().IsIntAsserted(1000));
    Reg(TsConfReg::SysConfig, 0x02);  // 14 MHz, mid-frame
    EXPECT_EQ(_context->emulatorState.current_z80_frequency_multiplier, 4);
    EXPECT_EQ(_context->emulatorState.hw_turbo_ratio_applied, 4) << "audio / video descale by the applied ratio";
    EXPECT_FALSE(Ints().IsIntAsserted(tact * 4 - 1));
    EXPECT_TRUE(Ints().IsIntAsserted(tact * 4)) << "the same raster tact at 4 clocks per tact";
}

/// INT-9: the frame pulse counts CPU clocks without /WAIT ([V] zint.v: intctr counts on zpos && !wait_r). The
/// AVR UART's 400-clock access stretched the instruction an event fell in, the 32-clock pulse expired inside the
/// stall and the program lost the interrupt (zifi.spg: every other frame, the screen jerked)
TEST_F(TsConfInterrupts_Test, INT9a_AWaitFreezesARunningPulse)
{
    SetMultiplier(4);
    // The reset event: raster tact 1, asserted from CPU clock 4. 16 clocks of the pulse are over at clock 20
    ASSERT_TRUE(Ints().IsIntAsserted(20));
    Ints().OnWait(20u << 8, 420u << 8);  // a 420-clock stall
    EXPECT_TRUE(Ints().IsIntAsserted(440)) << "the stall did not use the pulse up";
    EXPECT_TRUE(Ints().IsIntAsserted(455)) << "16 clocks were left";
    EXPECT_FALSE(Ints().IsIntAsserted(456));
}

TEST_F(TsConfInterrupts_Test, INT9b_APulseThatStartsInsideAWaitBeginsWhenItEnds)
{
    SetMultiplier(4);
    Reg(TsConfReg::VsIntL, 10);
    Reg(TsConfReg::HsInt, 0);          // raster tact 2240 = CPU clock 8960
    ASSERT_FALSE(Ints().IsIntAsserted(8940));
    Ints().OnWait(8940u << 8, 100u << 8);  // the stall covers 8940..9040: the event is in it
    EXPECT_TRUE(Ints().IsIntAsserted(9040)) << "without the freeze 80 clocks are gone, the pulse over";
    EXPECT_TRUE(Ints().IsIntAsserted(9071));
    EXPECT_FALSE(Ints().IsIntAsserted(9072)) << "32 clocks after the stall";
}

TEST_F(TsConfInterrupts_Test, INT9c_APulseThatIsOverStaysOver)
{
    SetMultiplier(4);
    ASSERT_TRUE(Ints().IsIntAsserted(20));
    ASSERT_FALSE(Ints().IsIntAsserted(100)) << "32 clocks from clock 4";
    Ints().OnWait(200u << 8, 400u << 8);
    EXPECT_FALSE(Ints().IsIntAsserted(600)) << "a stall later does not bring it back";
}

TEST_F(TsConfInterrupts_Test, INT9d_TheCpuTellsTheControllerAboutItsWaits)
{
    SetMultiplier(4);
    Reg(TsConfReg::VsIntL, 10);
    Reg(TsConfReg::HsInt, 0);
    _z80->tt = 8940u << 8;
    _z80->AddWaitStates(100);  // a device's /WAIT, as the UART access does
    ASSERT_EQ(_z80->tt >> 8, 9040u);
    EXPECT_TRUE(Ints().IsIntAsserted(_z80->tt >> 8));

    // And the fractional waits of the DRAM (fclk ticks)
    Reg(TsConfReg::VsIntL, 20);
    _z80->tt = (17900u << 8);
    _z80->AddWaitTicks(100u << 8);
    EXPECT_TRUE(Ints().IsIntAsserted(_z80->tt >> 8));
}

TEST_F(TsConfInterrupts_Test, INT9e_OtherMachinesAreNotNotified)
{
    EXPECT_TRUE(Ints().ObservesWaits());
    class Plain : public IInterruptSource
    {
    public:
        bool IsIntAsserted(uint32_t) override { return false; }
        uint8_t AcknowledgeInterrupt(uint32_t) override { return 0xFF; }
    } plain;
    EXPECT_FALSE(plain.ObservesWaits()) << "the default: a source that does not ask is never called on a wait";
}

/// INT-10: a /WAIT that straddles the frame end carries a rest into the new frame (Z80::t after the rebase); a
/// frame INT whose event fell in that rest starts its pulse where the clock ran again. The first INT of the frame
/// was lost whenever a 400-clock UART access straddled the frame end (zifi.spg's polling: every few frames)
TEST_F(TsConfInterrupts_Test, INT10_AWaitAcrossTheFrameEndKeepsTheFirstPulse)
{
    SetMultiplier(4);
    const uint32_t frameClocks = TsConfInterrupts::kFrameTacts * 4;
    // The last wait of the frame: the CPU is at the end of it, 400 clocks into the new frame
    _z80->tt = (frameClocks + 400u) << 8;  // the rebase has not happened yet ...
    _z80->tt -= frameClocks << 8;          // ... and now has: Z80::t = 400
    Ints().OnMachineFrameRollover(frameClocks);
    EXPECT_TRUE(Ints().IsIntAsserted(400)) << "the new frame's INT (clock 4) fell inside the rest of the stall";
    EXPECT_TRUE(Ints().IsIntAsserted(431));
    EXPECT_FALSE(Ints().IsIntAsserted(432)) << "32 clocks after the stall";
}

TEST_F(TsConfInterrupts_Test, INT10b_AShortRestChangesNothing)
{
    SetMultiplier(4);
    const uint32_t frameClocks = TsConfInterrupts::kFrameTacts * 4;
    _z80->tt = 12u << 8;  // an ordinary instruction ran over the frame end by 12 clocks
    Ints().OnMachineFrameRollover(frameClocks);
    EXPECT_TRUE(Ints().IsIntAsserted(12));
    EXPECT_TRUE(Ints().IsIntAsserted(35));
    EXPECT_FALSE(Ints().IsIntAsserted(36)) << "the pulse is still 32 clocks from clock 4";
}

/// INT-3c: an event before a mask write in the same tact happened under the old mask ([V] the latch takes
/// int_start_lin at its clock; the OUT comes later): the line end at tact 0 is gone when the mask is set in tact 0
TEST_F(TsConfInterrupts_Test, INT3c_TheMaskWriteSeesTheEventsBeforeIt)
{
    _z80->tt = 0;
    Reg(TsConfReg::IntMask, TsConfInt::Line);
    EXPECT_FALSE(Ints().IsIntAsserted(0)) << "the previous frame's last line ended before the write";
    EXPECT_FALSE(Ints().IsIntAsserted(223));
    EXPECT_TRUE(Ints().IsIntAsserted(224));
}

/// INT-12: the source is chosen at the acknowledge ([V] zint.v:117-132: int_sel latches at intack_s, the IORQ of the
/// INTA cycle about 3 clocks after /INT was sampled): a frame pulse that ends in between is gone, the line INT gives
/// the vector. With nothing left, int_sel keeps its last value (no else, no reset). The source was chosen at the
/// sampling instant (TS-Conf audit, interrupts row 20)
TEST_F(TsConfInterrupts_Test, INT12_ThePulseEndingBeforeTheAcknowledge)
{
    Reg(TsConfReg::IntMask, TsConfInt::Frame | TsConfInt::Line);
    Reg(TsConfReg::VsIntL, 1);
    Reg(TsConfReg::HsInt, 76);  // raster tact 300: the pulse runs on clocks 300..331
    ASSERT_TRUE(Ints().IsIntAsserted(330)) << "the line event at 224 and the frame pulse";
    EXPECT_EQ(Ints().AcknowledgeInterrupt(330), 0xFD) << "at the IORQ (clock 333) the frame pulse is over";
    EXPECT_FALSE(Ints().IsIntAsserted(334)) << "both latches are clear";
}

TEST_F(TsConfInterrupts_Test, INT12b_NothingLeftKeepsTheLastSource)
{
    Reg(TsConfReg::IntMask, TsConfInt::Frame | TsConfInt::Line);
    Reg(TsConfReg::VsIntL, 2);
    Reg(TsConfReg::HsInt, 0);  // raster tact 448: the pulse on 448..479
    ASSERT_TRUE(Ints().IsIntAsserted(230));
    ASSERT_EQ(Ints().AcknowledgeInterrupt(230), 0xFD) << "the line event at 224";
    ASSERT_TRUE(Ints().IsIntAsserted(478)) << "the frame pulse (and the line event at 448)";
    ASSERT_EQ(Ints().AcknowledgeInterrupt(478), 0xFD) << "the frame pulse ends before the IORQ: the line at 448";
    Reg(TsConfReg::IntMask, TsConfInt::Frame);
    Reg(TsConfReg::VsIntL, 3);
    Reg(TsConfReg::HsInt, 0);  // tact 672: 672..703
    ASSERT_TRUE(Ints().IsIntAsserted(702));
    EXPECT_EQ(Ints().AcknowledgeInterrupt(702), 0xFD) << "nothing at the IORQ: int_sel is still the line's";
}
