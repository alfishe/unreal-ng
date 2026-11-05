/// AudioCaptureAnalyzer: the memory capture cut to what came on stop, and the file (streamed WAV) capture.

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

#include "_helpers/testpathhelper.h"
#include "debugger/analyzers/analyzermanager.h"
#include "debugger/analyzers/audiocapture/audiocaptureanalyzer.h"
#include "emulator/emulatorcontext.h"

class AudioCaptureAnalyzer_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _context = new EmulatorContext(LoggerLevel::LogError);
        _manager = new AnalyzerManager(_context);
        auto analyzer = std::make_unique<AudioCaptureAnalyzer>(_context);
        _capture = analyzer.get();
        _manager->registerAnalyzer("audiocapture", std::move(analyzer));
        ASSERT_TRUE(_manager->activate("audiocapture"));
    }

    void TearDown() override
    {
        delete _manager;
        delete _context;
    }

    void Feed(size_t frames, int16_t base = 100)
    {
        for (size_t i = 0; i < frames; i++)
            _manager->dispatchAudioSample(static_cast<int16_t>(base + i % 50), static_cast<int16_t>(-base));
    }

    static uint32_t Le32(const std::string& bytes, size_t offset)
    {
        uint32_t v = 0;
        for (int i = 3; i >= 0; i--)
            v = (v << 8) | static_cast<uint8_t>(bytes[offset + i]);
        return v;
    }

    EmulatorContext* _context = nullptr;
    AnalyzerManager* _manager = nullptr;
    AudioCaptureAnalyzer* _capture = nullptr;
};

TEST_F(AudioCaptureAnalyzer_Test, StopCutsTheBufferToWhatCame)
{
    _capture->startCapture(10000 * 2);
    Feed(300);
    _capture->stopCapture();

    EXPECT_EQ(_capture->getCapturedSamples(), 600u);
    EXPECT_EQ(_capture->getBuffer().size(), 600u);   // no zero tail up to the 10000-frame target
    EXPECT_EQ(_capture->getBuffer()[0], 100);
    EXPECT_EQ(_capture->getBuffer()[1], -100);
}

TEST_F(AudioCaptureAnalyzer_Test, CompletedCaptureKeepsTheWholeTarget)
{
    _capture->startCapture(200);
    Feed(500);
    EXPECT_TRUE(_capture->isCaptureComplete());
    _capture->stopCapture();
    EXPECT_EQ(_capture->getBuffer().size(), 200u);
}

TEST_F(AudioCaptureAnalyzer_Test, StreamsToAWavFileOverManyChunks)
{
    const std::string path = TestPathHelper::GetUniqueTestScratchPath("audiocapture-stream.wav");
    constexpr size_t kFrames = 20000;   // more than one writer chunk (8192 frames)

    std::string error;
    ASSERT_TRUE(_capture->startStreamCapture(path, 44100, 0, AudioSourceType::MasterMix, error)) << error;
    Feed(kFrames);
    _capture->stopCapture();

    EXPECT_TRUE(_capture->isStreamClosed());
    EXPECT_FALSE(_capture->hasStreamFailed());
    EXPECT_EQ(_capture->getCapturedSamples(), kFrames * 2);
    EXPECT_TRUE(_capture->getBuffer().empty());

    std::ifstream in(path, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    ASSERT_EQ(bytes.size(), 44 + kFrames * 4);
    EXPECT_EQ(Le32(bytes, 40), kFrames * 4);   // the header was patched with the real length
    EXPECT_EQ(static_cast<int16_t>(bytes[44] | (bytes[45] << 8)), 100);
}

TEST_F(AudioCaptureAnalyzer_Test, StreamStopsByItselfAtTheTarget)
{
    const std::string path = TestPathHelper::GetUniqueTestScratchPath("audiocapture-target.wav");
    std::string error;
    ASSERT_TRUE(_capture->startStreamCapture(path, 44100, 1000 * 2, AudioSourceType::MasterMix, error)) << error;
    Feed(3000);
    EXPECT_TRUE(_capture->isCaptureComplete());
    _capture->stopCapture();   // joins the writer

    EXPECT_EQ(_capture->getCapturedSamples(), 2000u);
    EXPECT_EQ(std::filesystem::file_size(path), 44u + 1000u * 4u);
}

TEST_F(AudioCaptureAnalyzer_Test, StreamIntoAMissingFolderIsRefused)
{
    std::string error;
    EXPECT_FALSE(_capture->startStreamCapture("/nonexistent-folder-for-test/x.wav", 44100, 0,
                                              AudioSourceType::MasterMix, error));
    EXPECT_FALSE(error.empty());
    EXPECT_FALSE(_capture->isCaptureArmed());
}
