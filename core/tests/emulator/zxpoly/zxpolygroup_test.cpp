#include "stdafx.h"
#include "pch.h"

#include "_helpers/testpathhelper.h"
#include "common/image/imagehelper.h"
#include "common/modulelogger.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/notifications.h"
#include "emulator/zxpoly/zxpolygroup.h"
#include "emulator/zxpoly/zxpolyscreencomposer.h"
#include "emulator/video/screen.h"
#include "debugger/analyzers/rom-print/screenocr.h"
#include "3rdparty/message-center/messagecenter.h"

#include <cstdlib>
#include <string>
#include <tuple>
#include <vector>

/// ZX-Poly group on stock models: the .zxp corpus runs in lockstep on four
/// instances. Boot-bound: every case runs hundreds of emulated frames on four
/// machines, so it is well over the 50 ms guideline by design.
///
/// Set ZXPOLY_DUMP_PNG=1 to write the composed frames to
/// scratch/zxpoly/<model>/<title>-<frame>.png for visual review.
class ZXPolyGroup_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        MessageCenter::DisposeDefaultMessageCenter();
    }

    void TearDown() override
    {
        _group.reset();
        MessageCenter::DisposeDefaultMessageCenter();
    }

    void CreateGroup(const std::string& model)
    {
        _group = std::make_unique<ZXPolyGroup>("zxpoly-test");
        std::string error;
        ASSERT_TRUE(_group->Create(model, &error)) << error;
        for (size_t m = 0; m < ZXPolyGroup::MODULES; m++)
            _group->GetInstance(m)->EnableTurboMode();
    }

    /// Runs frames, checking lockstep after each; returns frames completed in lockstep
    unsigned RunInLockstep(unsigned frames, ZXPolyGroup::Divergence& divergence)
    {
        for (unsigned f = 0; f < frames; f++)
        {
            _group->RunFrame();
            divergence = _group->CheckLockstep();
            if (divergence.diverged)
                return f;
        }
        return frames;
    }

    void DumpPng(const std::string& model, const std::string& title, unsigned frame)
    {
        if (std::getenv("ZXPOLY_DUMP_PNG") == nullptr)
            return;
        auto save = [&](const std::string& suffix) {
            std::vector<uint32_t> picture;
            _group->Compose(picture);
            const std::string path = TestPathHelper::GetTestScratchPath(
                "zxpoly/" + model + "/" + title + "-" + std::to_string(frame) + suffix + ".png");
            ImageHelper::SavePNG(path, reinterpret_cast<uint8_t*>(picture.data()),
                                 picture.size() * sizeof(uint32_t), ZXPolyScreenComposer::OUT_WIDTH,
                                 ZXPolyScreenComposer::OUT_HEIGHT);
        };
        save("");

        // The same frame as a stock Spectrum shows it: the master alone (mode 0)
        const uint8_t mode = _group->GetVideoMode();
        _group->SetVideoMode(0);
        save("-classic");
        _group->SetVideoMode(mode);
    }

    std::unique_ptr<ZXPolyGroup> _group;
};

class ZXPolyGroupCorpus_Test : public ZXPolyGroup_Test,
                               public ::testing::WithParamInterface<std::tuple<std::string, std::string>>
{
};

TEST_P(ZXPolyGroupCorpus_Test, ZxpRunsInLockstep)
{
    const std::string model = std::get<0>(GetParam());
    const std::string file = std::get<1>(GetParam());
    const std::string title = file.substr(0, file.find('.'));

    CreateGroup(model);
    std::string error;
    ASSERT_TRUE(_group->LoadZXP(TestPathHelper::GetTestDataPath("machines/zxpoly/zxp/" + file), &error)) << error;

    // Right after the load the four modules hold identical registers
    ZXPolyGroup::Divergence divergence = _group->CheckLockstep();
    ASSERT_FALSE(divergence.diverged) << "after load: module " << divergence.module << ": " << divergence.what;

    DumpPng(model, title, 0);

    constexpr unsigned FRAMES = 250;    // 5 s of emulated time
    const unsigned done = RunInLockstep(FRAMES, divergence);
    DumpPng(model, title, done);

    EXPECT_EQ(done, FRAMES) << title << " on " << model << ": module " << divergence.module << " left lockstep at frame "
                            << done << ": " << divergence.what;
}

INSTANTIATE_TEST_SUITE_P(
    Corpus, ZXPolyGroupCorpus_Test,
    ::testing::Combine(::testing::Values(std::string("128k"), std::string("PENTAGON")),
                       ::testing::Values(std::string("Alien8.zxp"), std::string("buratino_adventures.zxp"),
                                         std::string("ComandoQuatro.zxp"), std::string("flyshark.zxp"),
                                         std::string("OFCZXPOLY.zxp"), std::string("SummerSanta2022.zxp"),
                                         std::string("fh.zxp"))),
    [](const ::testing::TestParamInfo<std::tuple<std::string, std::string>>& info) {
        std::string file = std::get<1>(info.param);
        std::string name = std::get<0>(info.param) + "_" + file.substr(0, file.find('.'));
        for (char& c : name)
            if (!isalnum(static_cast<unsigned char>(c)))
                c = '_';
        return name;
    });

class ZXPolyGroupModels_Test : public ZXPolyGroup_Test, public ::testing::WithParamInterface<std::string>
{
};

/// Stock snapshot on the master, replicated into the slaves: identical planes,
/// so the group must stay in lockstep indefinitely (the calibration state)
TEST_P(ZXPolyGroupModels_Test, ReplicatedSnapshotStaysInLockstep)
{
    CreateGroup(GetParam());
    ASSERT_TRUE(_group->GetInstance(0)->LoadSnapshot(TestPathHelper::GetTestDataPath("loaders/sna/Dizzy X.sna")));

    _group->ReplicateFromMaster();
    ZXPolyGroup::Divergence divergence = _group->CheckLockstep();
    ASSERT_FALSE(divergence.diverged) << divergence.what;

    ZXPolyGroup::Divergence afterRun;
    constexpr unsigned FRAMES = 200;
    const unsigned done = RunInLockstep(FRAMES, afterRun);
    EXPECT_EQ(done, FRAMES) << "module " << afterRun.module << " at frame " << done << ": " << afterRun.what;
}

INSTANTIATE_TEST_SUITE_P(Models, ZXPolyGroupModels_Test, ::testing::Values(std::string("128k"), std::string("PENTAGON")),
                         [](const ::testing::TestParamInfo<std::string>& info) {
                             return info.param == "128k" ? std::string("Spectrum128") : info.param;
                         });

/// Gameplay: a deterministic input script (menu keys, then Sinclair/QAOP
/// directions and fire) drives every title past its menu; the group must stay
/// in lockstep with keys applied at frame boundaries to all four instances.
/// Boot-bound (1500 frames on four machines per case).
TEST_P(ZXPolyGroupCorpus_Test, ScriptedPlayStaysInLockstep)
{
    const std::string model = std::get<0>(GetParam());
    const std::string file = std::get<1>(GetParam());
    const std::string title = file.substr(0, file.find('.')) + "-play";

    CreateGroup(model);
    std::string error;
    ASSERT_TRUE(_group->LoadZXP(TestPathHelper::GetTestDataPath("machines/zxpoly/zxp/" + file), &error)) << error;
    if (std::getenv("ZXPOLY_PORT_CHECK") != nullptr)
        _group->EnablePortReadCheck(true);

    // Menu keys first (keyboard / start selections), then play keys
    static const ZXKeysEnum menuKeys[] = {ZXKEY_1, ZXKEY_ENTER, ZXKEY_0, ZXKEY_SPACE, ZXKEY_1, ZXKEY_ENTER};
    static const ZXKeysEnum playKeys[] = {ZXKEY_Q, ZXKEY_A, ZXKEY_O, ZXKEY_P, ZXKEY_M, ZXKEY_SPACE,
                                          ZXKEY_6, ZXKEY_7, ZXKEY_8, ZXKEY_9, ZXKEY_0, ZXKEY_Z, ZXKEY_X};

    uint32_t rng = 0x2A5F17u;   // fixed seed: the same script every run
    auto next = [&]() {
        rng = rng * 1664525u + 1013904223u;
        return rng >> 16;
    };

    constexpr unsigned FRAMES = 1500;   // 30 s of emulated time
    ZXPolyGroup::Divergence divergence;
    ZXKeysEnum held = ZXKEY_NONE;
    unsigned done = 0;
    for (unsigned f = 0; f < FRAMES; f++)
    {
        // Every 10 frames: release the held key; press the next one for 6 frames
        if (f % 10 == 0)
        {
            if (held != ZXKEY_NONE)
                _group->ReleaseKey(held);
            held = f < 600 ? menuKeys[(f / 100) % std::size(menuKeys)] : playKeys[next() % std::size(playKeys)];
            _group->PressKey(held);
        }
        else if (f % 10 == 6 && held != ZXKEY_NONE)
        {
            _group->ReleaseKey(held);
            held = ZXKEY_NONE;
        }

        _group->RunFrame();
        divergence = _group->CheckLockstep();
        if (divergence.diverged)
            break;
        done = f + 1;
        if (done % 250 == 0)
            DumpPng(model, title, done);
    }

    DumpPng(model, title, done);
    EXPECT_EQ(done, FRAMES) << title << " on " << model << ": module " << divergence.module
                            << " left lockstep at frame " << done << ": " << divergence.what;
}

struct ZXPolyDiskCase
{
    const char* model;
    const char* file;
    uint8_t expectedMode;
};

class ZXPolyGroupDisk_Test : public ZXPolyGroup_Test, public ::testing::WithParamInterface<ZXPolyDiskCase>
{
};

/// The loader path: a TR-DOS disk with a ZX-Poly multiloader boots on the
/// master alone, streams the planes into the parked slaves through the #3D00
/// IO window, then locks with a local reset (SETPOLYMAIN). After the lock the
/// four run in lockstep. Boot-bound: TR-DOS loading takes thousands of frames
TEST_P(ZXPolyGroupDisk_Test, MultiloaderBootsAndRunsInLockstep)
{
    const ZXPolyDiskCase& param = GetParam();
    const std::string title = std::string(param.file).substr(0, std::string(param.file).find('.')) + "-disk";

    CreateGroup(param.model);
    std::string error;
    ASSERT_TRUE(_group->BootDisk(TestPathHelper::GetTestDataPath(std::string("machines/zxpoly/trd/") + param.file),
                                 &error))
        << error;

    unsigned bootFrames = 0;
    constexpr unsigned MAX_BOOT_FRAMES = 6000;
    while (!_group->IsLocked() && bootFrames < MAX_BOOT_FRAMES)
    {
        _group->RunFrame();
        bootFrames++;
    }
    DumpPng(param.model, title, 0);
    ASSERT_TRUE(_group->IsLocked()) << "no SETPOLYMAIN lock after " << bootFrames << " frames";
    if (std::getenv("ZXPOLY_M1_TRACE") != nullptr)
        _group->EnableInstructionTrace(true);

    EXPECT_EQ(_group->GetVideoMode(), param.expectedMode);
    for (size_t m = 1; m < ZXPolyGroup::MODULES; m++)
        EXPECT_GT(_group->GetOverlayBytes(m), 10000u) << "module " << m << " received no plane data";

    // Input script after the lock: the start key, then play keys (fixed seed)
    static const ZXKeysEnum playKeys[] = {ZXKEY_Q, ZXKEY_A, ZXKEY_O, ZXKEY_P, ZXKEY_M, ZXKEY_SPACE, ZXKEY_Z,
                                          ZXKEY_X, ZXKEY_6, ZXKEY_7, ZXKEY_8, ZXKEY_9, ZXKEY_0, ZXKEY_E};
    uint32_t rng = 0x51D0u;
    ZXKeysEnum held = ZXKEY_NONE;

    ZXPolyGroup::Divergence divergence;
    constexpr unsigned FRAMES = 1000;
    unsigned done = 0;
    for (; done < FRAMES; done++)
    {
        if (done >= 100 && done % 8 == 0)
        {
            if (held != ZXKEY_NONE)
                _group->ReleaseKey(held);
            rng = rng * 1664525u + 1013904223u;
            held = done < 140 ? ZXKEY_1 : playKeys[(rng >> 16) % std::size(playKeys)];
            _group->PressKey(held);
        }

        _group->RunFrame();
        divergence = _group->CheckLockstep();
        if (divergence.diverged)
            break;
        if ((done + 1) % 200 == 0)
            DumpPng(param.model, title, done + 1);
    }
    EXPECT_EQ(done, FRAMES) << title << ": module " << divergence.module << " left lockstep " << done
                            << " frames after the lock (boot took " << bootFrames << "): " << divergence.what;
}

INSTANTIATE_TEST_SUITE_P(Loaders, ZXPolyGroupDisk_Test,
                         ::testing::Values(ZXPolyDiskCase{"PENTAGON", "atw2.trd", 4},
                                           ZXPolyDiskCase{"PENTAGON", "zxword.trd", 5}),
                         [](const ::testing::TestParamInfo<ZXPolyDiskCase>& info) {
                             std::string name = std::string(info.param.model) + "_" + info.param.file;
                             for (char& c : name)
                                 if (!isalnum(static_cast<unsigned char>(c)))
                                     c = '_';
                             return name;
                         });

/// A system reset of the master is a ZX-Poly system RESET: #3D00 back to 0,
/// ports unlocked, slaves parked, only CPU0 shown (mode 0). Afterwards the
/// composed picture is exactly the master's classic screen
TEST_P(ZXPolyGroupModels_Test, MasterResetReturnsToLoaderPhase)
{
    CreateGroup(GetParam());
    std::string error;
    ASSERT_TRUE(_group->LoadZXP(TestPathHelper::GetTestDataPath("machines/zxpoly/zxp/Alien8.zxp"), &error)) << error;
    _group->RunFrames(20);
    ASSERT_TRUE(_group->IsLocked());

    _group->GetInstance(0)->Reset();
    _group->RunFrames(5);

    EXPECT_FALSE(_group->IsLocked());
    EXPECT_EQ(_group->GetVideoMode(), 0);
    EXPECT_EQ(_group->GetPort3D00(), 0);

    // Mode 0 composes CPU0 alone: slave screens (still the game) must not show
    std::vector<uint32_t> composed;
    _group->Compose(composed);
    std::array<const uint8_t*, 4> masterOnly;
    masterOnly.fill(_group->GetScreenMemory(0));
    std::vector<uint32_t> expected(composed.size());
    uint32_t palette[16];
    _group->GetContext(0)->pScreen->GetRGBAPalette16(palette);
    const bool flash = ((_group->GetContext(0)->emulatorState.frame_counter >> 4) & 1u) != 0;
    ZXPolyScreenComposer::Compose(masterOnly, 0, flash, palette, expected.data());
    EXPECT_EQ(composed, expected);
}

/// The ZX-Poly Test ROM (zxpolytest.prom, the platform's own self-test and
/// demo) - the coupled machine: the slaves are released by #3D00 D0 before any
/// lock, reset one by one with injected JP commands (R0 D5), run their own
/// test code, HALT, and report through the IO window; CPU0 resets itself, RAM0
/// is mapped at #0000. Every check must print OK, then the mode 4 and mode 5
/// demo pictures are streamed into the slaves. Boot-bound (ROM tests + ZX0
/// unpacking run for hundreds of frames)
TEST_P(ZXPolyGroupModels_Test, TestRomPassesAllChecks)
{
    CreateGroup(GetParam());
    std::string error;
    ASSERT_TRUE(_group->LoadPROM(TestPathHelper::GetTestDataPath("machines/zxpoly/rom/zxpolytest.prom"), &error))
        << error;

    const std::string master = _group->GetInstance(0)->GetUUID();
    auto screen = [&]() { return ScreenOCR::ocrScreen(master); };

    unsigned frames = 0;
    while (screen().find("PRESS ANY KEY") == std::string::npos && frames < 3000)
    {
        _group->RunFrame();
        frames++;
    }
    const std::string text = screen();
    DumpPng(GetParam(), "testrom-checks", frames);
    ASSERT_NE(text.find("PRESS ANY KEY"), std::string::npos) << "after " << frames << " frames:\n" << text;
    EXPECT_EQ(text.find("BAD"), std::string::npos) << text;
    EXPECT_EQ(text.find("NON"), std::string::npos) << text;
    for (const char* check : {"CPU0", "CPU1", "CPU2", "CPU3", "RAM0"})
        EXPECT_NE(text.find(check), std::string::npos) << check << " missing:\n" << text;

    // Mode 4 demo: four ZX0-packed planes streamed through the IO window
    _group->PressKey(ZXKEY_SPACE);
    _group->RunFrames(5);
    _group->ReleaseKey(ZXKEY_SPACE);
    _group->RunFrames(400);
    EXPECT_EQ(_group->GetVideoMode(), 4);
    DumpPng(GetParam(), "testrom-mode4", frames);

    // Mode 5 demo (512 x 384)
    _group->PressKey(ZXKEY_SPACE);
    _group->RunFrames(5);
    _group->ReleaseKey(ZXKEY_SPACE);
    _group->RunFrames(400);
    EXPECT_EQ(_group->GetVideoMode(), 5);
    DumpPng(GetParam(), "testrom-mode5", frames);
}
