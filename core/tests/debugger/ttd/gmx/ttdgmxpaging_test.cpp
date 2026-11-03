#include "stdafx.h"
#include "pch.h"

#include "debugger/ttd/gmx/ttdgmxpaging.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "emulator/ports/models/scorpiongmxfixture.h"

/// @brief TTD restore of the Scorpion GMX card's register file (GMX design section 6).
///
/// #00, #78FD, the scroll start, #DFFD and the Magic-button freeze are not in the model-agnostic TTDChipsetState;
/// GmxPaging (70) carries them next to the host Scorpion's ScorpionProfROM (6), which holds #1FFD, #7EFD and the
/// plane. A seek across a write to any of them must page the same memory as the recording did.
class TtdGmxPaging_Test : public ScorpionGmxFixture
{
protected:
    /// Record one checkpoint with the machine as it is, then stop
    uint64_t CaptureCheckpoint(ttd::TimeTravelController& ttd)
    {
        _context->emulatorState.frame_counter++;   // the frame ends as in emulation: its counter first, then the boundary
        ttd.OnFrameBoundary();
        const uint64_t frame = _context->emulatorState.frame_counter;
        ttd.StopRecording();
        return frame;
    }

    bool SeekToFrame(ttd::TimeTravelController& ttd, uint64_t frame)
    {
        ttd::TTDTimePoint target;
        target.frame = frame;
        target.tInFrame = 0;
        return ttd.SeekTo(target);
    }
};

TEST_F(TtdGmxPaging_Test, RecordingCarriesTheCardAndTheHostState)
{
    ttd::TimeTravelController ttd(_context);
    ASSERT_TRUE(ttd.StartRecording());
    EXPECT_TRUE(ttd.GetPeripheralRegistry().IsRegistered(ttd::TTDDeviceType::GmxPaging));
    EXPECT_TRUE(ttd.GetPeripheralRegistry().IsRegistered(ttd::TTDDeviceType::ScorpionProfROM));
    ttd.StopRecording();
}

TEST_F(TtdGmxPaging_Test, SeekRestoresTheLatchesAndThePagesBehindThem)
{
    ttd::TimeTravelController ttd(_context);
    ASSERT_TRUE(ttd.StartRecording());

    Out(0x7EFD, 0x48);   // plane 4, 640x200
    Out(0x78FD, 0x25);   // page #27 at #8000
    Out(0x7FFD, 0x03);
    Out(0x1FFD, 0x10);
    Out(0xDFFD, 0x05);   // page #5B at #C000
    Out(0x7AFD, 0xA0);
    Out(0x7CFD, 0x12);
    Out(0x0000, 0x02);   // scheme 2, no RECONF
    ASSERT_EQ(RamPageAt(0x8000), 0x27);
    ASSERT_EQ(RamPageAt(0xC000), 0x5B);
    ASSERT_EQ(RomPageAt(0x0000), 16);
    const uint64_t frame = CaptureCheckpoint(ttd);

    Out(0x7EFD, 0x00);
    Out(0x78FD, 0x00);
    Out(0x1FFD, 0x00);
    Out(0xDFFD, 0x00);
    Out(0x7AFD, 0x00);
    Out(0x7CFD, 0x00);
    Out(0x0000, 0x10);
    ASSERT_EQ(RamPageAt(0xC000), 3);

    ASSERT_TRUE(SeekToFrame(ttd, frame));
    const GmxState& gmx = State().gmx;
    EXPECT_EQ(gmx.p00, 0x02);
    EXPECT_EQ(gmx.p78FD, 0x25);
    EXPECT_EQ(gmx.p7AFD, 0xA0);
    EXPECT_EQ(gmx.p7CFD, 0x12);
    EXPECT_EQ(State().pDFFD, 0x05);
    EXPECT_EQ(_gmx->Plane(), 4);
    EXPECT_EQ(RamPageAt(0x8000), 0x27) << "the #8000 window was not re-paged from the restored #78FD";
    EXPECT_EQ(RamPageAt(0xC000), 0x5B) << "the #C000 window was not re-paged from the restored #DFFD";
    EXPECT_EQ(RomPageAt(0x0000), 16) << "the ROM plane was not restored";
}

TEST_F(TtdGmxPaging_Test, SeekRestoresTheMagicButtonFreeze)
{
    ttd::TimeTravelController ttd(_context);
    ASSERT_TRUE(ttd.StartRecording());

    Out(0x7FFD, 0x03);
    ASSERT_TRUE(_gmx->OnMagicButton());
    Out(0x7FFD, 0x06);
    ASSERT_EQ(In(0x7AFD) & 0x07, 0x03);
    const uint64_t frame = CaptureCheckpoint(ttd);

    (void)In(0x00FF);
    ASSERT_EQ(In(0x7AFD) & 0x07, 0x06);

    ASSERT_TRUE(SeekToFrame(ttd, frame));
    EXPECT_EQ(State().gmx.magicLock, 1);
    EXPECT_EQ(In(0x7AFD) & 0x07, 0x03) << "the read-back is frozen again at the value of the press";
}

TEST_F(TtdGmxPaging_Test, SeekRestoresAPendingReconfiguration)
{
    ttd::TimeTravelController ttd(_context);
    ASSERT_TRUE(ttd.StartRecording());

    Out(0x0000, 0x0A);   // RECONF scheme 2: waits for the end of the OUT
    ASSERT_TRUE(_gmx->IsReconfigurationPending());
    const uint64_t frame = CaptureCheckpoint(ttd);

    _gmx->RestoreReconfigurationPending(false);
    ASSERT_FALSE(_gmx->IsReconfigurationPending());

    ASSERT_TRUE(SeekToFrame(ttd, frame));
    EXPECT_TRUE(_gmx->IsReconfigurationPending()) << "the RECONF of the recorded instruction was lost";
}

TEST_F(TtdGmxPaging_Test, TheHashSeesEveryCardLatch)
{
    ttd::TTDGmxPaging blob(_context);
    const uint64_t base = blob.TTDHashState();
    Out(0x78FD, 0x01);
    const uint64_t page = blob.TTDHashState();
    EXPECT_NE(page, base);
    Out(0x7CFD, 0x01);
    EXPECT_NE(blob.TTDHashState(), page) << "a scroll start change is a divergence";
}
