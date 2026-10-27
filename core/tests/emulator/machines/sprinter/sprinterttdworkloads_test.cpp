// Heavy Sprinter software under TTD v2 (owner request 2026-10-08: full coverage of the Sprinter): programs from the
// MAME pack's system disk (UNREAL_SPRINTER_HDD, the raw sp_hdd_sys.img; not in the repository, skipped without it)
// run while TTD records, and the recording replays from its first checkpoint and from the middle one with no
// difference in any device or memory region (SprinterZxSession_Test::ExpectReplaysFromStartAndMiddle). The demos
// drive the video modes, the accelerator, the CTC tick (IM 2) and the Covox-Blaster fed from the hard disk.
//
// Boot-bound: BIOS 3.06, DSS 1.71 from the hard disk and the program's start-up (turbo, not recorded), then a few
// hundred recorded frames replayed twice - seconds per program.

#include "sprinterzxsession.h"

#include "_helpers/soundcardscope.h"
#include "base/featuremanager.h"
#include "emulator/sound/sprinter/covoxblaster.h"

#include <set>

class SprinterTtdWorkload_Test : public SprinterZxSession_Test
{
protected:
    SoundCardScope _shippedSound;   ///< the machine as it ships: the AY and the NeoGS too (held while SetUp creates it)

    /// At the DSS prompt: change to `directory`, start `program`, run `startFrames` (turbo), then record `frames`
    /// frames; returns the Covox-Blaster state from before the recording
    CovoxBlasterState RunAndRecord(const std::string& directory, const std::string& program, int startFrames, int frames)
    {
        BootToPrompt();
        Dss("cd " + directory);
        EmulatorTestHelper::RunFramesFast(_emulator.get(), 10);
        Dss(program);
        EmulatorTestHelper::RunFramesFast(_emulator.get(), startFrames);
        _emulator->DisableTurboMode();
        ToBoundary();

        FeatureManager* features = _emulator->GetFeatureManager();
        features->setFeature(Features::kDebugMode, true);
        features->setFeature(Features::kTimeTravel, true);
        const CovoxBlasterState before = _decoder->GetCovoxBlaster().State();
        EXPECT_TRUE(_context->pTimeTravelController->StartRecording());
        for (int f = 0; f < frames; f++)
        {
            ToBoundary();
            _pictures.insert(PictureHash());
        }
        return before;
    }

    /// Stop the recording at this boundary and replay it
    void StopAndReplay()
    {
        ToBoundary();
        const uint64_t endFrame = Frame();
        const MachineState recorded = Recordable();
        _context->pTimeTravelController->StopRecording();
        ExpectReplaysFromStartAndMiddle(endFrame, recorded);
    }

    uint64_t PictureHash()
    {
        uint32_t* fb = nullptr;
        size_t size = 0;
        _context->pScreen->GetFramebufferData(&fb, &size);
        uint64_t h = 1469598103934665603ull;
        for (size_t i = 0; i < size / 4; i += 7)
            h = (h ^ fb[i]) * 1099511628211ull;
        return h;
    }

    std::set<uint64_t> _pictures;
};

// Bad Apple (C:\DEMOS\BADAPPLE): 1-bit video frames, one CTC tick per 20.48 ms, the sound through the Covox-Blaster
TEST_F(SprinterTtdWorkload_Test, BadApple_ReplaysWithTheVideoAndTheCovoxBlaster)
{
    const CovoxBlasterState before = RunAndRecord("demos\\badapple", "badapple", 500, 300);
    const CovoxBlasterState& after = _decoder->GetCovoxBlaster().State();
    EXPECT_GE(_pictures.size(), 10u) << "video frames";
    EXPECT_GT(after.ringWrites - before.ringWrites, 0u) << "the Covox-Blaster is fed";
    EXPECT_GT(after.ticks - before.ticks, 0u) << "and plays";
    StopAndReplay();
}

// deMarche's dontBlink (C:\DEMOS\DNTBLINK): the accelerator's graphics, the music streamed from the hard disk into the
// Covox-Blaster in the CTC handler
TEST_F(SprinterTtdWorkload_Test, DontBlink_ReplaysWithTheAcceleratorAndTheDiskStream)
{
    const CovoxBlasterState before = RunAndRecord("demos\\dntblink", "dntblink", 1000, 300);
    const CovoxBlasterState& after = _decoder->GetCovoxBlaster().State();
    EXPECT_GE(_pictures.size(), 10u) << "the demo moves";
    EXPECT_GT(after.ringWrites - before.ringWrites, 0u) << "the music streams from the disk";
    EXPECT_GT(after.ticks - before.ticks, 0u);
    StopAndReplay();
}
