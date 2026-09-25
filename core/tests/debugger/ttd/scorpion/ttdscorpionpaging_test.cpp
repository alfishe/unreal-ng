#include "stdafx.h"
#include "pch.h"

#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/scorpion/ttdscorpionprofrom.h"
#include "emulator/ports/models/scorpionfixture.h"

/// @brief TTD restore of the plain Scorpion ZS 256 (MM_SCORP) paging state.
///
/// #1FFD drives the Scorpion paging chain on both variants: RAM at #0000
/// (bit 0), the service monitor (bit 1) and the high RAM-bank bits of the #C000
/// window (bits 4, 6, 7). It is not part of the model-agnostic TTDChipsetState,
/// so the model serializer must carry it for the plain Scorpion as well as for
/// the ProfROM variant - otherwise a seek across an OUT #1FFD pages the wrong
/// memory. The DD50.1 magic-button trigger is in the same position.
class TtdScorpionPaging_Test : public ScorpionMachineFixture
{
protected:
    /// Record one checkpoint with the machine as it is, then stop
    uint64_t CaptureCheckpoint(ttd::TimeTravelManager& ttd)
    {
        ttd.OnFrameBoundary();
        const uint64_t frame = _context->emulatorState.frame_counter;
        ttd.StopRecording();
        return frame;
    }

    bool SeekToFrame(ttd::TimeTravelManager& ttd, uint64_t frame)
    {
        ttd::TTDTimePoint target;
        target.frame = frame;
        target.tInFrame = 0;
        return ttd.SeekTo(target);
    }
};

/// The plain Scorpion must register the serializer that carries #1FFD
TEST_F(TtdScorpionPaging_Test, PlainScorpionRecordingCarriesModelState)
{
    ASSERT_EQ(_context->config.mem_model, MM_SCORP);

    ttd::TimeTravelManager ttd(_context);
    ASSERT_TRUE(ttd.StartRecording());
    EXPECT_TRUE(ttd.GetPeripheralRegistry().IsRegistered(ttd::PeripheralId::ScorpionProfROM))
        << "MM_SCORP recording has no serializer for #1FFD / the magic-button trigger";
    ttd.StopRecording();
}

/// #1FFD bit 4 selects the high RAM bank at #C000: a seek must map it back
TEST_F(TtdScorpionPaging_Test, SeekRestoresHighRamBankFrom1FFD)
{
    ttd::TimeTravelManager ttd(_context);
    ASSERT_TRUE(ttd.StartRecording());

    WritePort(0x7FFD, 0x03);                 // bank 3 ...
    WritePort(0x1FFD, 0x10);                 // ... + #1FFD bit 4 -> page 11
    ASSERT_EQ(ScorpionTagPage(BankTag(0xC000)), 11);
    const uint64_t frame = CaptureCheckpoint(ttd);

    WritePort(0x1FFD, 0x00);                 // back to page 3
    ASSERT_EQ(ScorpionTagPage(BankTag(0xC000)), 3);

    ASSERT_TRUE(SeekToFrame(ttd, frame));
    EXPECT_EQ(_context->emulatorState.p1FFD, 0x10);
    EXPECT_TRUE(ScorpionIsRamTag(BankTag(0xC000)));
    EXPECT_EQ(ScorpionTagPage(BankTag(0xC000)), 11) << "the #C000 window was not re-paged from the restored #1FFD";
}

/// #1FFD bit 0 maps RAM page 0 at #0000 above every ROM selection
TEST_F(TtdScorpionPaging_Test, SeekRestoresRamAtZeroFrom1FFD)
{
    ttd::TimeTravelManager ttd(_context);
    ASSERT_TRUE(ttd.StartRecording());

    WritePort(0x1FFD, 0x01);
    ASSERT_TRUE(ScorpionIsRamTag(BankTag(0x0000)));
    const uint64_t frame = CaptureCheckpoint(ttd);

    WritePort(0x1FFD, 0x00);
    ASSERT_TRUE(ScorpionIsRomTag(BankTag(0x0000)));

    ASSERT_TRUE(SeekToFrame(ttd, frame));
    EXPECT_TRUE(ScorpionIsRamTag(BankTag(0x0000))) << "ROM at #0000 after restoring a RAM-at-#0000 checkpoint";
    EXPECT_EQ(ScorpionTagPage(BankTag(0x0000)), 0);
}

/// The other direction: recorded with ROM at #0000, live machine has RAM there
TEST_F(TtdScorpionPaging_Test, SeekClearsRamAtZeroSetAfterTheCheckpoint)
{
    ttd::TimeTravelManager ttd(_context);
    ASSERT_TRUE(ttd.StartRecording());

    ASSERT_TRUE(ScorpionIsRomTag(BankTag(0x0000)));
    const uint8_t romTagAtCapture = BankTag(0x0000);
    const uint64_t frame = CaptureCheckpoint(ttd);

    WritePort(0x1FFD, 0x01);
    ASSERT_TRUE(ScorpionIsRamTag(BankTag(0x0000)));

    ASSERT_TRUE(SeekToFrame(ttd, frame));
    EXPECT_EQ(_context->emulatorState.p1FFD, 0x00);
    EXPECT_EQ(BankTag(0x0000), romTagAtCapture);
}

/// The magic-button trigger is host-armed, not port-derived: it must round-trip too
TEST_F(TtdScorpionPaging_Test, SeekRestoresMagicButtonTrigger)
{
    ttd::TimeTravelManager ttd(_context);
    ASSERT_TRUE(ttd.StartRecording());
    ASSERT_EQ(_context->emulatorState.scorpionDosTrigger, 0);
    const uint64_t frame = CaptureCheckpoint(ttd);

    _context->emulatorState.scorpionDosTrigger = 1;

    ASSERT_TRUE(SeekToFrame(ttd, frame));
    EXPECT_EQ(_context->emulatorState.scorpionDosTrigger, 0);
}
