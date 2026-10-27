// The Next's interrupt, NMI and reset journal and the NMI report (docs/inprogress/2026-10-07-zx-next/automation-gaps-2026-10-11.md, P4)
// Source: zxnext.vhd (nmi_assert_mf, nmi_sw_gen_*, nr_02_reset_type), im2_control.vhd / the IM2 controller of NextInterruptSource;
// multiface.vhd for the session; the vector is NR #C0 bits 7:5 + the source number x 2

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "../../../automation/cli/src/commands/cli-next-format.h"
#include "_helpers/emulatortesthelper.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/z80n/nextintjournal.h"
#include "emulator/ports/models/portdecoder_next.h"
#include "emulator/state/devicestate.h"

namespace
{
// NextREG numbers (the register map)
constexpr uint8_t kRegNmiEnables = 0x06;      // bit 3 the M1 button, bit 4 the DRIVE button
constexpr uint8_t kRegResetType = NextBoard::kRegResetType;  // NR #02: writing bit 3 presses the M1 button, bit 2 the DRIVE button; they read 1 once taken
constexpr uint8_t kPressM1 = 0x08;
constexpr uint8_t kPressDrive = 0x04;
constexpr uint8_t kNmiM1ButtonBit = 0x08;
constexpr uint8_t kNmiDriveButtonBit = 0x10;
constexpr uint8_t kRegInterruptControl = NextInterruptSource::kRegControl;   // NR #C0: bit 0 hardware IM2, bits 7:5 the vector base
constexpr uint8_t kRegInterruptEnable0 = NextInterruptSource::kRegEnable0;   // NR #C4: bit 0 ULA, bit 1 line
constexpr uint8_t kHardwareIm2 = 0x01;
constexpr uint8_t kVectorBase = 0xE0;
constexpr uint8_t kEnableUla = 0x01;
constexpr uint8_t kEnableLine = 0x02;
// The vector of a source: the base plus the source number x 2 (VectorOf)
constexpr uint8_t VectorOf(unsigned source) { return static_cast<uint8_t>(kVectorBase | (source << 1)); }
}  // namespace

TEST(NextIntJournal_Test, RingQueryAndNames)
{
    NextIntJournal journal(3);
    for (uint8_t i = 0; i < 5; i++)
    {
        NextIntEvent e;
        e.kind = i % 2 ? NextIntKind::Reti : NextIntKind::IntAck;
        e.frame = i;
        journal.Record(e);
    }
    EXPECT_EQ(journal.Size(), 3u);
    EXPECT_EQ(journal.Evicted(), 2u);
    NextIntJournalQuery q;
    std::string error;
    ASSERT_TRUE(NextIntJournalQueryFromStrings("reti", "", "", "", "", q, error)) << error;
    EXPECT_EQ(journal.Query(q).size(), 1u) << "frames 2, 3, 4 are held; only frame 3 is a reti";
    ASSERT_TRUE(NextIntJournalQueryFromStrings("", "", "3", "4", "", q, error)) << error;
    EXPECT_EQ(journal.Query(q).size(), 2u);
    EXPECT_FALSE(NextIntJournalQueryFromStrings("nmi", "", "", "", "", q, error));
    EXPECT_FALSE(NextIntJournalQueryFromStrings("", "-1", "", "", "", q, error));
    EXPECT_STREQ(NextIntKindName(NextIntKind::NmiRequest), "nmi_request");
    EXPECT_STREQ(NextInterruptSource::SourceName(NextInterruptSource::kCtc0 + 7), "ctc7");
    EXPECT_STREQ(NextInterruptSource::SourceName(NextInterruptSource::kUla), "ula");
    EXPECT_EQ(NextInterruptSource::SourceNames((1u << NextInterruptSource::kLine) | (1u << NextInterruptSource::kUla)).size(), 2u);
}

class NextIntJournalMachine_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    Z80* _z80 = nullptr;
    PortDecoder_Next* _ports = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("NEXT", LoggerLevel::LogError, RamPowerOn::Zero);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _z80 = _context->pCore->GetZ80();
        _ports = dynamic_cast<PortDecoder_Next*>(_context->pPortDecoder);
        ASSERT_NE(_ports, nullptr);
    }
    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }
    NextBoard& Board() { return _ports->Board(); }
    /// Presses a board button the way a program or the host key does (NR #02); true when the board took the NMI request
    bool Press(uint8_t bit)
    {
        Board().WriteNextReg(kRegResetType, 0);
        Board().WriteNextReg(kRegResetType, bit);
        return (Board().Read(kRegResetType) & bit) != 0;
    }
    StateNode Report() { return DeviceState::NextNmiReport(_context, NextIntJournalQuery{}); }
    const StateNode& Event(const StateNode& report, size_t index)
    {
        static const StateNode none;
        const StateNode* events = report.find("events");
        return events && index < events->items.size() ? events->items[index] : none;
    }
};

TEST_F(NextIntJournalMachine_Test, TheMaskableInterruptsAreNamedWithTheirVectorsAndEndWithReti)
{
    NextInterruptSource& irq = _ports->Interrupts();
    // pulse mode (the reset default): the bus carries #FF, the source is the ULA's frame pulse
    DeviceState::NextNmiJournalControl(_context, 1, true);
    (void)irq.AcknowledgeInterrupt(0);
    // hardware IM2 mode: the line and the ULA both ask, the line (bit 0) is served first with its vector, then the ULA after RETI
    Board().WriteNextReg(kRegInterruptControl, kHardwareIm2 | kVectorBase);
    Board().WriteNextReg(kRegInterruptEnable0, kEnableUla | kEnableLine);
    irq.Raise(NextInterruptSource::kLine);
    irq.Raise(NextInterruptSource::kUla);
    EXPECT_EQ(irq.AcknowledgeInterrupt(0), VectorOf(NextInterruptSource::kLine));
    irq.OnReti();
    EXPECT_EQ(irq.AcknowledgeInterrupt(0), VectorOf(NextInterruptSource::kUla));
    irq.OnReti();
    (void)irq.AcknowledgeInterrupt(0);  // nothing asks: the controller answers with the ULA's vector

    const StateNode report = Report();
    const StateNode* events = report.find("events");
    ASSERT_NE(events, nullptr);
    ASSERT_EQ(events->items.size(), 6u);
    EXPECT_EQ(Event(report, 0).find("source")->s, "ula");
    EXPECT_EQ(Event(report, 0).find("vector"), nullptr) << "pulse mode: no vector from the controller";
    EXPECT_EQ(Event(report, 1).find("source")->s, "line");
    EXPECT_EQ(Event(report, 1).find("vector")->s, "0xE0");
    EXPECT_EQ(Event(report, 2).find("kind")->s, "reti");
    EXPECT_EQ(Event(report, 2).find("source")->s, "line");
    EXPECT_EQ(Event(report, 3).find("source")->s, "ula");
    EXPECT_EQ(Event(report, 3).find("vector")->s, "0xF6");
    EXPECT_EQ(Event(report, 5).find("source")->s, "spurious");
    EXPECT_EQ(report.find("interrupts")->find("mode")->s, "hardware im2");
}

TEST_F(NextIntJournalMachine_Test, AButtonThatTheBoardRefusesSaysWhyAndTheSessionEndsWithRetn)
{
    DeviceState::NextNmiJournalControl(_context, 1, true);
    EXPECT_FALSE(Press(kPressM1));
    StateNode report = Report();
    EXPECT_FALSE(report.find("sources")->items[0].find("would_fire")->b);
    EXPECT_NE(report.find("sources")->items[0].find("blocked_by")->s.find("NR #06 bit 3"), std::string::npos);

    Board().WriteNextReg(kRegNmiEnables, kNmiM1ButtonBit | kNmiDriveButtonBit);
    report = Report();
    EXPECT_TRUE(report.find("sources")->items[0].find("would_fire")->b);
    EXPECT_TRUE(Press(kPressM1));
    EXPECT_TRUE(_ports->Multiface().NmiHold());
    EXPECT_FALSE(Press(kPressM1)) << "the Multiface session is open";
    EXPECT_FALSE(Press(kPressDrive)) << "the DRIVE button waits for the Multiface session";
    report = Report();
    EXPECT_TRUE(report.find("in_service")->find("multiface_session")->b);
    EXPECT_TRUE(report.find("multiface")->find("nmi_active")->b);

    _ports->OnRetn();
    EXPECT_FALSE(_ports->Multiface().NmiHold());
    EXPECT_TRUE(_ports->RequestBoardNmi()) << "the magic button (Emulator::RequestMNI, POST /nmi {magic}) is the DRIVE button";

    report = Report();
    const StateNode* events = report.find("events");
    ASSERT_NE(events, nullptr);
    ASSERT_GE(events->items.size(), 6u);
    EXPECT_EQ(Event(report, 0).find("source")->s, "m1_button");
    EXPECT_FALSE(Event(report, 0).find("accepted")->b);
    EXPECT_NE(Event(report, 0).find("reason")->s.find("NR #06 bit 3"), std::string::npos);
    EXPECT_TRUE(Event(report, 1).find("accepted")->b);
    EXPECT_EQ(Event(report, 2).find("source")->s, "m1_button") << "the second M1 request, refused: the session is open";
    EXPECT_FALSE(Event(report, 2).find("accepted")->b);
    EXPECT_NE(Event(report, 2).find("reason")->s.find("session"), std::string::npos);
    EXPECT_EQ(Event(report, 3).find("source")->s, "drive_button") << "the refused DRIVE button";
    EXPECT_FALSE(Event(report, 3).find("accepted")->b);
    EXPECT_EQ(Event(report, 4).find("kind")->s, "retn");
    EXPECT_EQ(Event(report, 4).find("source")->s, "multiface");
    EXPECT_EQ(Event(report, 5).find("source")->s, "drive_button");
    EXPECT_TRUE(Event(report, 5).find("accepted")->b);
}

TEST_F(NextIntJournalMachine_Test, AResetIsLoggedWithItsBitsAndTheReportShowsTheMultifaceAndTheIoTrap)
{
    DeviceState::NextNmiJournalControl(_context, 1, true);
    _ports->PerformReset(false);
    _ports->PerformReset(true);
    const StateNode report = Report();
    ASSERT_NE(report.find("events"), nullptr);
    ASSERT_EQ(report.find("events")->items.size(), 2u);
    EXPECT_EQ(Event(report, 0).find("kind")->s, "reset");
    EXPECT_EQ(Event(report, 0).find("source")->s, "soft");
    EXPECT_EQ(Event(report, 1).find("source")->s, "hard");
    EXPECT_NE(Event(report, 1).find("reset_last"), nullptr);
    EXPECT_EQ(report.find("reset")->find("bits")->i, Board().ResetBits());

    const StateNode* sources = report.find("sources");
    ASSERT_EQ(sources->items.size(), 3u);
    EXPECT_EQ(sources->items[2].find("name")->s, "io_trap");
    EXPECT_TRUE(sources->items[2].find("modelled")->b) << "the +3 FDC trap (NR #D8-#DA)";
    const StateNode* mf = report.find("multiface");
    EXPECT_EQ(mf->find("enable_port")->s.size(), 4u);
    EXPECT_FALSE(mf->find("active")->b);
    EXPECT_FALSE(report.find("stackless_nmi")->find("enabled")->b);
}

TEST_F(NextIntJournalMachine_Test, TheCliPrintsTheReportAndTheJournal)
{
    Board().WriteNextReg(kRegNmiEnables, kNmiM1ButtonBit);
    auto run = [&](const std::vector<std::string>& args) { return CliNext::StateText(_context, args, "\n"); };
    EXPECT_NE(run({"next", "nmi", "on"}).find("NMI journal on"), std::string::npos);
    EXPECT_TRUE(Press(kPressM1));
    _ports->OnRetn();
    EXPECT_FALSE(Press(kPressDrive)) << "the DRIVE button is off in NR #06";
    const std::string text = run({"next", "nmi"});
    EXPECT_NE(text.find("NMI source m1_button:"), std::string::npos) << text;
    EXPECT_NE(text.find("NMI source io_trap: blocked - the trap is off (NR #D8 bit 0)"), std::string::npos) << text;
    EXPECT_NE(text.find("Last reset: "), std::string::npos) << text;
    EXPECT_NE(text.find("nmi_request m1_button accepted"), std::string::npos) << text;
    EXPECT_NE(text.find("nmi_request drive_button REFUSED: "), std::string::npos) << text;
    EXPECT_NE(run({"next", "nmi", "kinds=jump"}).find("Error:"), std::string::npos);
    EXPECT_NE(run({"next", "nmi", "colour=1"}).find("unknown option"), std::string::npos);
    EXPECT_NE(run({"next", "nmi", "clear"}).find("0 event(s) held"), std::string::npos);
}
