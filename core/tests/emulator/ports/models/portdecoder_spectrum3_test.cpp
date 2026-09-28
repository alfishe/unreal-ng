#include <gtest/gtest.h>

#include <vector>

#include "3rdparty/message-center/messagecenter.h"
#include "debugger/ttd/plus3/ttdplus3paging.h"
#include "debugger/ttd/timetravelmanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
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

    // The decoder declares the latch, so recording on a +3 must be allowed
    const std::vector<ttd::PeripheralId> ids = _context->pPortDecoder->GetTTDModelStateIds();
    ASSERT_EQ(ids.size(), 1u);
    EXPECT_EQ(ids[0], ttd::PeripheralId::Plus3Paging);
    ASSERT_NE(_context->pTimeTravelManager, nullptr);
    EXPECT_TRUE(_context->pTimeTravelManager->StartRecording());
}
