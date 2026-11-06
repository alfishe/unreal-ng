/// @file analysisbundlemedia_test.cpp
/// @brief The recording's media travel with the trace bundle (docs/emulator/analysis/analysis-layers.md,
/// "Experiments"): the base analysis instance replays disk reads from the port journal and needs no disk, but a free
/// machine forked from it reads the drive itself. The bundle carries the source's floppies and tape (an in-memory
/// copy at Capture, or an image file added by hand), the base gets them before the session loads, and
/// MachineStateTransfer copies them into the free machine. A fork made before TR-DOS reads the disk stays on the
/// recorded path; without the media it leaves it at the first read.

#include "pch.h"

#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "base/featuremanager.h"
#include "debugger/analyzers/analysis/analysisinstance.h"
#include "debugger/analyzers/analysis/replaytracer.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/media/mediamanager.h"
#include "emulator/media/medium.h"
#include "emulator/memory/memory.h"
#include "emulator/sound/soundmanager.h"
#include "loaders/snapshot/machinestatetransfer.h"

using namespace analysis;

namespace
{

const char* kDisk = "loaders/trd/zx-format8.trd";

class AnalysisBundleMedia_Test : public ::testing::Test
{
protected:
    Emulator* _source = nullptr;
    ttd::TimeTravelController* _ttd = nullptr;

    void TearDown() override
    {
        if (_source != nullptr)
            EmulatorTestHelper::CleanupEmulator(_source);
    }

    /// A Pentagon with the disk in drive A under a TTD recording. @p bootDos: the reset boots TR-DOS, which reads the
    /// catalogue at once (the recording starts before the first read)
    void MakeSource(bool bootDos)
    {
        _source = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError, RamPowerOn::Zero);
        ASSERT_NE(_source, nullptr);
        EmulatorContext* context = _source->GetContext();
        _source->GetFeatureManager()->setFeature(Features::kTimeTravel, true);
        _source->GetFeatureManager()->setFeature(Features::kSoundGeneration, false);
        if (context->pSoundManager != nullptr)
            context->pSoundManager->mute();
        context->pMemory->UpdateFeatureCache();
        std::string error;
        ASSERT_TRUE(_source->LoadDisk(TestPathHelper::GetTestDataPath(kDisk), 0, &error)) << error;
        if (bootDos)
        {
            context->config.reset_rom = RM_DOS;
            _source->Reset();
        }
        _source->EnableTurboMode();
        _ttd = context->pTimeTravelController;
        ASSERT_NE(_ttd, nullptr);
    }

    void Record(uint64_t frames, TraceBundle& bundle)
    {
        ASSERT_TRUE(_ttd->StartRecording());
        _source->RunNFrames(static_cast<unsigned>(frames), /*skipBreakpoints=*/true);
        std::string error;
        ASSERT_TRUE(TraceBundle::Capture(*_source, bundle, error)) << error;
    }

    const Medium* SourceDisk() { return _source->GetContext()->pMediaManager->GetMedium("fdd.a"); }

    static const Medium* DiskOf(const AnalysisInstance& instance)
    {
        return instance.Context()->pMediaManager->GetMedium("fdd.a");
    }

    static bool HasMediaIssue(const ttd::TimeTravelController& ttd)
    {
        for (const ttd::TTDRestoreIssue& issue : ttd.LastEngineCheck().issues)
            if (issue.kind == ttd::TTDRestoreIssueKind::MediaVersionDiffers)
                return true;
        return false;
    }

    static std::vector<uint8_t> Ram(const AnalysisInstance& instance)
    {
        EmulatorContext* context = instance.Context();
        const size_t bytes = static_cast<size_t>(context->config.ramsize) * 1024;
        const uint8_t* ram = context->pMemory->RAMPageAddress(0);
        return std::vector<uint8_t>(ram, ram + bytes);
    }

    /// Fork the base at frame @p from into a free machine, run it @p frames frames, and compare its RAM with the base
    /// seeked to the same frame. The first differing offset, or -1 when the fork stayed on the recorded path
    static long ForkAndCompare(const TraceBundle& bundle, uint64_t from, uint64_t frames, bool& freeHasDisk)
    {
        AnalysisInstance base;
        std::string error;
        EXPECT_TRUE(base.Make(bundle, "analysis-media-base", true, error)) << error;
        ttd::TimeTravelController* ttd = base.Context()->pTimeTravelController;
        ttd::TTDTimePoint start;
        start.frame = bundle.firstFrame + from;
        EXPECT_TRUE(ttd->SeekTo(start, nullptr));

        AnalysisInstance free;
        EXPECT_TRUE(free.Make(bundle, "analysis-media-free", false, error)) << error;
        const MachineStateTransfer::Report report = MachineStateTransfer::Apply(*base.Context(), *free.Context());
        EXPECT_TRUE(report.ok) << report.reason;
        freeHasDisk = DiskOf(free) != nullptr;
        const uint64_t forked = free.Frame();
        EXPECT_TRUE(free.RunFrames(frames));

        ttd::TTDTimePoint end;
        end.frame = forked + frames;
        EXPECT_TRUE(ttd->SeekTo(end, nullptr));
        EXPECT_EQ(base.Frame(), free.Frame());
        const std::vector<uint8_t> recorded = Ram(base);
        const std::vector<uint8_t> forkRam = Ram(free);
        EXPECT_EQ(recorded.size(), forkRam.size());
        for (size_t i = 0; i < recorded.size() && i < forkRam.size(); ++i)
            if (recorded[i] != forkRam[i])
                return static_cast<long>(i);
        return -1;
    }
};

}  // namespace

// Capture copies drive A into the bundle; the base has it before the session loads (a seek finds the recording's
// medium, no media issue); MachineStateTransfer copies it on into a free machine
TEST_F(AnalysisBundleMedia_Test, TheBundleCarriesTheDiskIntoTheBaseAndTheFreeMachine)
{
    MakeSource(false);
    TraceBundle bundle;
    Record(2, bundle);
    if (HasFatalFailure())
        return;
    ASSERT_EQ(bundle.media.size(), 1u);
    EXPECT_EQ(bundle.media[0].slot, "fdd.a");
    ASSERT_NE(bundle.media[0].medium, nullptr);
    ASSERT_NE(bundle.media[0].medium->Floppy(), nullptr);
    EXPECT_EQ(bundle.media[0].medium->Identity(), SourceDisk()->Identity()) << "an untouched disk keeps its file's identity";

    AnalysisInstance base;
    std::string error;
    ASSERT_TRUE(base.Make(bundle, "analysis-media-base", true, error)) << error;
    const Medium* baseDisk = DiskOf(base);
    ASSERT_NE(baseDisk, nullptr) << "the base's drive A holds the recording's disk";
    EXPECT_EQ(baseDisk->Identity(), SourceDisk()->Identity());
    EXPECT_NE(baseDisk->Source().path, SourceDisk()->Source().path) << "a copy: it never stands for the source's file";
    ttd::TimeTravelController* ttd = base.Context()->pTimeTravelController;
    ttd::TTDTimePoint start;
    start.frame = bundle.firstFrame;
    ASSERT_TRUE(ttd->SeekTo(start, nullptr));
    EXPECT_NE(DiskOf(base), nullptr) << "a seek keeps the disk";
    EXPECT_FALSE(HasMediaIssue(*ttd)) << ttd->LastEngineCheck().message;

    AnalysisInstance free;
    ASSERT_TRUE(free.Make(bundle, "analysis-media-free", false, error)) << error;
    EXPECT_EQ(DiskOf(free), nullptr) << "a free machine starts empty: the disk comes with the state";
    const MachineStateTransfer::Report report = MachineStateTransfer::Apply(*base.Context(), *free.Context());
    ASSERT_TRUE(report.ok) << report.reason;
    bool listed = false;
    for (const MachineStateTransfer::Item& item : report.items)
        listed |= item.name == "fdd.a" && item.status == MachineStateTransfer::ItemStatus::Copied;
    EXPECT_TRUE(listed) << report.ToString();
    ASSERT_NE(DiskOf(free), nullptr);
    EXPECT_NE(DiskOf(free)->Floppy(), nullptr);
    EXPECT_EQ(DiskOf(free)->Identity(), SourceDisk()->Identity()) << "the same disk, untouched";
}

// A bundle built from a session file gets the disk from its image file: opened by the base's media manager with
// the same identity as on the recording machine
TEST_F(AnalysisBundleMedia_Test, AnImageFileAddedToTheBundleGoesIntoTheBase)
{
    MakeSource(false);
    TraceBundle bundle;
    Record(2, bundle);
    if (HasFatalFailure())
        return;
    std::string error;
    EXPECT_FALSE(bundle.AddMediumFile("fdd.a", TestPathHelper::GetTestDataPath("loaders/trd/no-such.trd"), error));
    EXPECT_FALSE(error.empty());
    ASSERT_TRUE(bundle.AddMediumFile("fdd.a", TestPathHelper::GetTestDataPath(kDisk), error)) << error;
    ASSERT_EQ(bundle.media.size(), 1u) << "the file replaces the captured copy of the same slot";
    EXPECT_EQ(bundle.media[0].medium, nullptr);

    AnalysisInstance base;
    ASSERT_TRUE(base.Make(bundle, "analysis-media-base", true, error)) << error;
    ASSERT_NE(DiskOf(base), nullptr);
    EXPECT_EQ(DiskOf(base)->Identity(), SourceDisk()->Identity());
    EXPECT_EQ(DiskOf(base)->Access(), AccessMode::Session) << "the image file is never written";
    ttd::TTDTimePoint start;
    start.frame = bundle.firstFrame;
    ASSERT_TRUE(base.Context()->pTimeTravelController->SeekTo(start, nullptr));
    EXPECT_NE(DiskOf(base), nullptr);
}

// TR-DOS boots, seeks and reads the catalogue after the fork (the seek starts near frame 88, the reads follow): with
// the bundle's disk the fork stays on the recorded path; with the media taken out of the bundle (the old behaviour)
// the free machine reads an empty drive and leaves it
TEST_F(AnalysisBundleMedia_Test, AForkBeforeADiskReadStaysOnTheRecordedPath)
{
    MakeSource(true);
    constexpr uint64_t kFork = 80;
    constexpr uint64_t kFrames = 70;
    TraceBundle bundle;
    Record(kFork + kFrames, bundle);
    if (HasFatalFailure())
        return;
    ASSERT_EQ(bundle.media.size(), 1u);

    bool freeHasDisk = false;
    EXPECT_EQ(ForkAndCompare(bundle, kFork, kFrames, freeHasDisk), -1) << "the fork left the recording";
    EXPECT_TRUE(freeHasDisk);

    TraceBundle empty = bundle;
    empty.media.clear();
    EXPECT_NE(ForkAndCompare(empty, kFork, kFrames, freeHasDisk), -1) << "without the disk the boot reads an empty drive";
    EXPECT_FALSE(freeHasDisk);
}
