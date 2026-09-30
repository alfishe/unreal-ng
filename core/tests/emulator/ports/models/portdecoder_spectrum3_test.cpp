#include <gtest/gtest.h>
#include "_helpers/soundcardscope.h"

#include <vector>

#include "3rdparty/message-center/messagecenter.h"
#include "debugger/ttd/plus3/ttdplus3paging.h"
#include "debugger/ttd/timetravelmanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/io/fdc/fdd.h"
#include "emulator/io/fdc/upd765.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/video/ulacontention.h"

/// ZX Spectrum +2A/+3 paging (PortDecoder_Spectrum3): the four ROMs picked by
/// #1FFD bit 2 and #7FFD bit 4, the all-RAM modes, the #7FFD lock, contention
/// on banks 4-7 and the #1FFD latch in TTD. The per-write bank map is pinned
/// in ModelsRegression_Test.GoldenBankMap_Spectrum3; the menu and the editors
/// are exercised on the real ROMs by the input verification tests.
class Spectrum3Paging_Test : public ::testing::Test
{
protected:
    std::shared_ptr<Emulator> _emulator;
    EmulatorContext* _context = nullptr;

    void Create(ROMModeEnum mode)
    {
        MessageCenter::DisposeDefaultMessageCenter();
        _emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel("plus3", "PLUS3", LoggerLevel::LogError);
        ASSERT_TRUE(_emulator);
        _context = _emulator->GetContext();
        _context->config.reset_rom = mode;
        _emulator->Reset();
    }

    void TearDown() override
    {
        if (_emulator)
            EmulatorManager::GetInstance()->RemoveEmulator(_emulator->GetUUID());
        MessageCenter::DisposeDefaultMessageCenter();
    }

    void Out(uint16_t port, uint8_t value) { _context->pPortDecoder->DecodePortOut(port, value, 0x8000); }
    uint8_t In(uint16_t port) { return _context->pPortDecoder->DecodePortIn(port, 0x8000); }
    uint16_t RomPage() const { return _context->pMemory->GetROMPage(); }
};

TEST_F(Spectrum3Paging_Test, ResetIntoTheMenuMapsTheEditorRom)
{
    Create(RM_128);
    ASSERT_FALSE(HasFatalFailure());
    EXPECT_TRUE(_context->pMemory->IsBank0ROM());
    EXPECT_EQ(RomPage(), 0);
}

TEST_F(Spectrum3Paging_Test, AllFourRomsAreReachable)
{
    Create(RM_128);
    ASSERT_FALSE(HasFatalFailure());

    Out(0x7FFD, 0x10);
    EXPECT_EQ(RomPage(), 1);  // 128 BASIC syntax
    Out(0x1FFD, 0x04);
    EXPECT_EQ(RomPage(), 3);  // 48 BASIC
    Out(0x7FFD, 0x00);
    EXPECT_EQ(RomPage(), 2);  // +3DOS
    Out(0x1FFD, 0x00);
    EXPECT_EQ(RomPage(), 0);  // editor
}

TEST_F(Spectrum3Paging_Test, ResetInto48BasicStaysLocked)
{
    Create(RM_SOS);
    ASSERT_FALSE(HasFatalFailure());
    ASSERT_EQ(RomPage(), 3);

    // 48 BASIC mode is the #7FFD lock: nothing brings another ROM back
    Out(0x7FFD, 0x00);
    Out(0x1FFD, 0x00);
    EXPECT_EQ(RomPage(), 3);
    EXPECT_EQ(_context->emulatorState.p7FFD & 0x20, 0x20);
}

TEST_F(Spectrum3Paging_Test, BanksFourToSevenAreContended)
{
    Create(RM_128);
    ASSERT_FALSE(HasFatalFailure());
    UlaContention* ula = _context->pUlaContention;
    ASSERT_NE(ula, nullptr);
    ula->SetContentionEnabled(true);

    for (uint8_t bank = 0; bank <= 7; bank++)
    {
        Out(0x7FFD, bank);
        EXPECT_EQ(ula->IsAddressContended(0xC000), bank >= 4) << "bank " << int(bank);
    }
}

TEST_F(Spectrum3Paging_Test, Port1FFDRidesItsOwnTtdBlob)
{
    Create(RM_128);
    ASSERT_FALSE(HasFatalFailure());

    ttd::TTDPlus3Paging serializer(_context);
    _context->emulatorState.p1FFD = 0x05;
    std::vector<uint8_t> blob(serializer.TTDStateSize());
    serializer.TTDSaveState(blob.data());

    _context->emulatorState.p1FFD = 0x00;
    serializer.TTDLoadState(blob.data());
    EXPECT_EQ(_context->emulatorState.p1FFD, 0x05);

    // The decoder declares the latch and the floppy controller, so recording on a +3 must be allowed
    const std::vector<ttd::PeripheralId> ids = _context->pPortDecoder->GetTTDModelStateIds();
    ASSERT_EQ(ids.size(), 2u);
    EXPECT_EQ(ids[0], ttd::PeripheralId::Plus3Paging);
    EXPECT_EQ(ids[1], ttd::PeripheralId::Upd765);
    ASSERT_NE(_context->pTimeTravelManager, nullptr);
    EXPECT_TRUE(_context->pTimeTravelManager->StartRecording());
}

TEST_F(Spectrum3Paging_Test, FloppyControllerAnswersOn2FFDAnd3FFD)
{
    Create(RM_128);
    ASSERT_FALSE(HasFatalFailure());
    ASSERT_NE(_context->pUPD765, nullptr);

    // Idle: ready for a command byte
    EXPECT_EQ(In(0x2FFD), UPD765::MSR_RQM);

    // SENSE INTERRUPT STATUS with nothing pending: one result byte #80
    Out(0x3FFD, UPD765::CMD_SENSE_INTERRUPT_STATUS);
    EXPECT_EQ(In(0x2FFD), UPD765::MSR_RQM | UPD765::MSR_DIO | UPD765::MSR_CB);
    EXPECT_EQ(In(0x3FFD), 0x80);
    EXPECT_EQ(In(0x2FFD), UPD765::MSR_RQM);
}

TEST_F(Spectrum3Paging_Test, Port1FFDBit3SwitchesTheDriveMotors)
{
    Create(RM_128);
    ASSERT_FALSE(HasFatalFailure());
    FDD* driveA = _context->coreState.diskDrives[0];
    ASSERT_NE(driveA, nullptr);

    Out(0x1FFD, 0x08);
    EXPECT_TRUE(_context->pUPD765->getMotor());
    EXPECT_TRUE(driveA->getMotor());
    EXPECT_EQ(RomPage(), 0);  // Bit 3 is not a paging bit

    Out(0x1FFD, 0x00);
    EXPECT_FALSE(driveA->getMotor());
}

TEST_F(Spectrum3Paging_Test, OnlyThePlus3HasTheFloppyController)
{
    Create(RM_128);
    ASSERT_FALSE(HasFatalFailure());
    EXPECT_NE(_context->pUPD765, nullptr);

    std::shared_ptr<Emulator> other = EmulatorManager::GetInstance()->CreateEmulatorWithModel("128k", "128k", LoggerLevel::LogError);
    ASSERT_TRUE(other);
    EXPECT_EQ(other->GetContext()->pUPD765, nullptr);
    EmulatorManager::GetInstance()->RemoveEmulator(other->GetUUID());
}

/// The +2A / +3 gate array lets the AY answer a read of #BFFD like #FFFD: the selected register
/// (docs/inprogress/2026-09-30-fusetest-core-defects/research.md claim 3; fusetest "0xbffd read")
TEST_F(Spectrum3Paging_Test, BFFDReadReadsTheSelectedAyRegister)
{
    SoundCardScope ay(TestSound::TurboSound);  // the machine's AY (the test runner leaves the slot empty)
    Create(RM_128);
    Out(0xFFFD, 11);
    Out(0xBFFD, 0x55);
    EXPECT_EQ(In(0xFFFD), 0x55);
    EXPECT_EQ(In(0xBFFD), 0x55);
    EXPECT_TRUE(_context->pPortDecoder->WasLastPortDecoded()) << "the AY drives the bus: no floating bus";
}

