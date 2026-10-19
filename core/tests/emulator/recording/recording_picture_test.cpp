#include "stdafx.h"
#include "pch.h"

#include <cstring>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/video/displaygeometry.h"
#include "emulator/zxpoly/zxpolygroup.h"
#include "emulator/video/screen.h"
#include "recordingmanager.h"

/// A recording shows the picture DisplayGeometry names for every machine and every video mode - the same one the
/// window and a screenshot show - through the real RecordingManager::CaptureFrame, into a sink encoder

namespace
{
class PictureSink : public EncoderBase
{
public:
    bool Start(const std::string&, const EncoderConfig& config) override
    {
        startedConfig = config;
        _recording = true;
        return true;
    }
    void Stop() override { _recording = false; }
    bool IsRecording() const override { return _recording; }
    std::string GetType() const override { return "sink"; }
    std::string GetDisplayName() const override { return "picture sink"; }
    bool SupportsVideo() const override { return true; }
    bool SupportsAudio() const override { return false; }
    void OnVideoFrame(const FramebufferDescriptor& framebuffer, double timestamp) override
    {
        timestamps.push_back(timestamp);
        width = framebuffer.width;
        height = framebuffer.height;
        pixels.assign(framebuffer.memoryBuffer, framebuffer.memoryBuffer + framebuffer.memoryBufferSize);
    }

    EncoderConfig startedConfig{};
    uint16_t width = 0;
    uint16_t height = 0;
    std::vector<uint8_t> pixels;
    std::vector<double> timestamps;
    bool _recording = false;
};

/// A pattern where every pixel names its own place: a wrong row, column or stride shows as a mismatch
void WritePattern(const FramebufferDescriptor& fb)
{
    ASSERT_NE(fb.memoryBuffer, nullptr);
    for (uint16_t y = 0; y < fb.height; y++)
    {
        uint8_t* row = fb.memoryBuffer + static_cast<size_t>(y) * fb.width * 4;
        for (uint16_t x = 0; x < fb.width; x++)
        {
            row[x * 4 + 0] = static_cast<uint8_t>(x & 0xFF);
            row[x * 4 + 1] = static_cast<uint8_t>((x >> 8) & 0xFF);
            row[x * 4 + 2] = static_cast<uint8_t>(y & 0xFF);
            row[x * 4 + 3] = 0xA5;
        }
    }
}

/// The recorded picture is the frame stretched by the nearest pixel, every pixel
void ExpectNearestOf(const PictureSink& sink, const FramebufferDescriptor& fb, const std::string& what)
{
    ASSERT_EQ(sink.pixels.size(), static_cast<size_t>(sink.width) * sink.height * 4) << what;
    for (uint32_t y = 0; y < sink.height; y++)
    {
        const uint8_t* srcRow = fb.memoryBuffer + static_cast<size_t>(static_cast<uint64_t>(y) * fb.height / sink.height) * fb.width * 4;
        for (uint32_t x = 0; x < sink.width; x += 7)  // every 7th column: cheap and not aligned to a repeat
        {
            const uint8_t* expected = srcRow + static_cast<size_t>(static_cast<uint64_t>(x) * fb.width / sink.width) * 4;
            ASSERT_EQ(std::memcmp(sink.pixels.data() + (static_cast<size_t>(y) * sink.width + x) * 4, expected, 4), 0)
                << what << ": pixel (" << x << ", " << y << ")";
        }
    }
}
}  // namespace

class RecordingPicture_Test : public ::testing::Test
{
};

/// Every machine the build knows, in the mode it boots with. Boot-bound: one machine per model
TEST_F(RecordingPicture_Test, EveryMachineRecordsTheCrtPictureOfItsFrame)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    ASSERT_NE(manager, nullptr);
    int checked = 0;
    for (const TMemModel& model : manager->GetAvailableModels())
    {
        Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator(model.ShortName, LoggerLevel::LogError, RamPowerOn::Zero);
        if (emulator == nullptr)
            continue;
        EmulatorContext* context = emulator->GetContext();
        if (context->pScreen && context->pRecordingManager)
        {
            emulator->RunNFrames(2);
            auto sink = std::make_unique<PictureSink>();
            PictureSink* sinkPtr = sink.get();
            ASSERT_TRUE(context->pRecordingManager->StartRecordingWithEncoder(
                TestPathHelper::GetUniqueTestScratchPath(std::string("picture-") + model.ShortName) + ".mp4", std::move(sink)))
                << model.ShortName;

            // The file's size is the picture of the frame the recording started on
            const FramebufferDescriptor fb = context->pScreen->GetFramebufferDescriptor();
            WritePattern(fb);
            context->pRecordingManager->CaptureFrame(fb);

            const DisplayGeometry::Size picture = DisplayGeometry::Picture(fb.width, fb.height);
            EXPECT_EQ(sinkPtr->startedConfig.videoWidth, picture.width) << model.ShortName;
            EXPECT_EQ(sinkPtr->startedConfig.videoHeight, picture.height) << model.ShortName;
            EXPECT_EQ(sinkPtr->width, picture.width) << model.ShortName;
            EXPECT_EQ(sinkPtr->height, picture.height) << model.ShortName;
            EXPECT_LT(sinkPtr->width * 100 / sinkPtr->height, 140) << model.ShortName << ": not ultra-wide";
            ExpectNearestOf(*sinkPtr, fb, model.ShortName);
            checked++;
            context->pRecordingManager->StopRecording();
        }
        EmulatorTestHelper::CleanupEmulator(emulator);
    }
    EXPECT_GT(checked, 3);
}

/// A machine that switches video mode while recording (the Profi's BIOS, a hi-res program): the file keeps the size it
/// started with and the new picture is fitted into it, never cut and never dropped
TEST_F(RecordingPicture_Test, AModeSwitchKeepsTheFilesSize)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PROFI", LoggerLevel::LogError, RamPowerOn::Zero);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();

    // Park the CPU so the BIOS cannot change the mode back while it is walked
    context->pMemory->DirectWriteToZ80Memory(0x8000, 0xF3);  // DI
    context->pMemory->DirectWriteToZ80Memory(0x8001, 0x76);  // HALT
    context->pCore->GetZ80()->pc = 0x8000;
    context->pPortDecoder->DecodePortOut(0xDFFD, 0x00, 0);  // the normal 352x288 picture
    emulator->RunNFrames(2);
    ASSERT_EQ(context->pScreen->GetFramebufferDescriptor().width, 352) << "the normal Profi frame";

    auto sink = std::make_unique<PictureSink>();
    PictureSink* sinkPtr = sink.get();
    ASSERT_TRUE(context->pRecordingManager->StartRecordingWithEncoder(
        TestPathHelper::GetUniqueTestScratchPath("picture-profi-switch") + ".mp4", std::move(sink)));
    EXPECT_EQ(sinkPtr->startedConfig.videoWidth, 352u);
    EXPECT_EQ(sinkPtr->startedConfig.videoHeight, 288u);

    FramebufferDescriptor first = context->pScreen->GetFramebufferDescriptor();
    WritePattern(first);
    context->pRecordingManager->CaptureFrame(first);  // the file's size is taken from its first frame

    context->pPortDecoder->DecodePortOut(0xDFFD, 0x80, 0);  // hi-res: the 608x288 frame
    emulator->RunNFrames(2);
    FramebufferDescriptor fb = context->pScreen->GetFramebufferDescriptor();
    ASSERT_EQ(fb.width, 608) << "the hi-res switch took";
    WritePattern(fb);
    context->pRecordingManager->CaptureFrame(fb);

    EXPECT_EQ(sinkPtr->width, 352) << "the file keeps its size";
    EXPECT_EQ(sinkPtr->height, 288);
    // The hi-res picture (608x498 as shown) is the 352:288 shape: it fills the file, nothing cut
    EXPECT_EQ(sinkPtr->pixels.size(), static_cast<size_t>(352) * 288 * 4);

    context->pRecordingManager->StopRecording();
    EmulatorTestHelper::CleanupEmulator(emulator);
}

/// The master of a ZX-Poly group records the group's composed frame, the one the window and a screenshot show (2x)
TEST_F(RecordingPicture_Test, AZXPolyMasterRecordsTheComposedFrame)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    std::string error;
    std::shared_ptr<Emulator> master = manager->CreateZXPolyMachine("", "ZXPOLY-128K", "", &error);
    ASSERT_NE(master, nullptr) << error;
    EmulatorContext* context = master->GetContext();
    ASSERT_NE(context->pRecordingManager, nullptr);

    ZXPolyGroup* group = manager->GetZXPolyGroup(master->GetId());
    ASSERT_NE(group, nullptr);
    master->RunNFrames(3);
    ASSERT_GT(group->GetDisplayWidth(), 352u) << "the group composed its 2x display frame";

    auto sink = std::make_unique<PictureSink>();
    PictureSink* sinkPtr = sink.get();
    ASSERT_TRUE(context->pRecordingManager->StartRecordingWithEncoder(
        TestPathHelper::GetUniqueTestScratchPath("picture-zxpoly") + ".mp4", std::move(sink)));
    EXPECT_EQ(sinkPtr->startedConfig.videoWidth, group->GetDisplayWidth());
    EXPECT_EQ(sinkPtr->startedConfig.videoHeight, group->GetDisplayHeight());

    master->RunNFrames(2);
    context->pRecordingManager->CaptureFrame(context->pScreen->GetFramebufferDescriptor());
    EXPECT_EQ(sinkPtr->width, group->GetDisplayWidth());
    EXPECT_EQ(sinkPtr->height, group->GetDisplayHeight());

    // The same pixels the window shows
    std::vector<uint8_t> display(static_cast<size_t>(group->GetDisplayWidth()) * group->GetDisplayHeight() * 4);
    ASSERT_TRUE(group->CopyDisplay(display.data(), display.size()));
    ASSERT_EQ(sinkPtr->pixels.size(), display.size());

    context->pRecordingManager->StopRecording();
    manager->RemoveEmulator(master->GetId());
}

/// A video card's picture (the FT812 of the TS-Conf VDAC2, 1024x768 / 800x600 VGA modes) has square pixels of its
/// own: a recording that starts on it has its size and its pixels, not the CRT's 352:288 shape (it was 1024x838)
TEST_F(RecordingPicture_Test, AVideoCardsPictureKeepsItsSize)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError, RamPowerOn::Zero);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    emulator->RunNFrames(2);

    std::vector<uint8_t> card(static_cast<size_t>(1024) * 768 * 4);
    context->pScreen->SetExternalPicture(card.data(), 1024, 768, 16949);
    const FramebufferDescriptor fb = context->pScreen->GetFramebufferDescriptor();
    ASSERT_EQ(fb.width, 1024);
    WritePattern(fb);

    auto sink = std::make_unique<PictureSink>();
    PictureSink* sinkPtr = sink.get();
    ASSERT_TRUE(context->pRecordingManager->StartRecordingWithEncoder(
        TestPathHelper::GetUniqueTestScratchPath("picture-external") + ".mp4", std::move(sink)));
    EXPECT_EQ(sinkPtr->startedConfig.videoWidth, 1024u);
    EXPECT_EQ(sinkPtr->startedConfig.videoHeight, 768u);

    context->pRecordingManager->CaptureFrame(fb);
    EXPECT_EQ(sinkPtr->width, 1024);
    EXPECT_EQ(sinkPtr->height, 768);
    ASSERT_EQ(sinkPtr->pixels.size(), card.size());
    EXPECT_EQ(sinkPtr->pixels, card) << "every pixel as the card drew it";

    context->pRecordingManager->StopRecording();
    context->pScreen->ClearExternalPicture();
    EmulatorTestHelper::CleanupEmulator(emulator);
}

/// A program that switches the monitor to the video card while recording: the card's 4:3 picture is fitted into the
/// file with its aspect kept (bars above and below), not dropped as a "picture of another size"
TEST_F(RecordingPicture_Test, ASwitchToTheVideoCardIsFittedIntoTheFile)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError, RamPowerOn::Zero);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    emulator->RunNFrames(2);

    auto sink = std::make_unique<PictureSink>();
    PictureSink* sinkPtr = sink.get();
    ASSERT_TRUE(context->pRecordingManager->StartRecordingWithEncoder(
        TestPathHelper::GetUniqueTestScratchPath("picture-external-switch") + ".mp4", std::move(sink)));
    const FramebufferDescriptor native = context->pScreen->GetFramebufferDescriptor();
    ASSERT_EQ(native.width, 352);
    context->pRecordingManager->CaptureFrame(native);
    ASSERT_EQ(sinkPtr->width, 352);
    ASSERT_EQ(sinkPtr->height, 288);

    std::vector<uint8_t> card(static_cast<size_t>(1024) * 768 * 4);
    context->pScreen->SetExternalPicture(card.data(), 1024, 768, 16949);
    const FramebufferDescriptor fb = context->pScreen->GetFramebufferDescriptor();
    WritePattern(fb);
    sinkPtr->pixels.clear();
    context->pRecordingManager->CaptureFrame(fb);

    ASSERT_EQ(sinkPtr->pixels.size(), static_cast<size_t>(352) * 288 * 4) << "the card's frame was recorded";
    EXPECT_EQ(context->pRecordingManager->GetStats().framesRecorded, 2u);
    // 4:3 in 352x288: 352x264, 12 lines of bars above and below
    const auto pixel = [&](uint32_t x, uint32_t y) { return sinkPtr->pixels.data() + (static_cast<size_t>(y) * 352 + x) * 4; };
    EXPECT_EQ(pixel(176, 0)[3], 0xFF) << "a bar above";
    EXPECT_EQ(pixel(176, 287)[3], 0xFF) << "a bar below";
    EXPECT_EQ(pixel(176, 12)[3], 0xA5) << "the picture starts on line 12";
    EXPECT_EQ(pixel(176, 275)[3], 0xA5) << "and ends on line 275";
    EXPECT_EQ(pixel(0, 144)[3], 0xA5) << "full width";

    context->pRecordingManager->StopRecording();
    context->pScreen->ClearExternalPicture();
    EmulatorTestHelper::CleanupEmulator(emulator);
}

/// A reset while recording (a snapshot load resets the machine too) leaves the file alone: its size stays, its clock
/// goes on. A VDAC2 program loaded into a recording switches to the FT812 picture right after the load
TEST_F(RecordingPicture_Test, AResetKeepsTheRecordingsFile)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError, RamPowerOn::Zero);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    emulator->RunNFrames(2);

    auto sink = std::make_unique<PictureSink>();
    PictureSink* sinkPtr = sink.get();
    ASSERT_TRUE(context->pRecordingManager->StartRecordingWithEncoder(
        TestPathHelper::GetUniqueTestScratchPath("picture-reset") + ".mp4", std::move(sink)));
    context->pRecordingManager->CaptureFrame(context->pScreen->GetFramebufferDescriptor());
    context->pRecordingManager->CaptureFrame(context->pScreen->GetFramebufferDescriptor());
    ASSERT_EQ(sinkPtr->width, 352);

    emulator->Reset();
    std::vector<uint8_t> card(static_cast<size_t>(1024) * 768 * 4);
    context->pScreen->SetExternalPicture(card.data(), 1024, 768, 16949);
    context->pRecordingManager->CaptureFrame(context->pScreen->GetFramebufferDescriptor());

    EXPECT_TRUE(context->pRecordingManager->IsRecording());
    EXPECT_EQ(context->pRecordingManager->GetStats().framesRecorded, 3u) << "the count goes on";
    EXPECT_EQ(sinkPtr->width, 352) << "the card's picture is fitted into the file";
    EXPECT_EQ(sinkPtr->height, 288);
    ASSERT_EQ(sinkPtr->timestamps.size(), 3u);
    EXPECT_GT(sinkPtr->timestamps[2], sinkPtr->timestamps[1]) << "the file's clock goes on";

    context->pRecordingManager->StopRecording();
    context->pScreen->ClearExternalPicture();
    EmulatorTestHelper::CleanupEmulator(emulator);
}
