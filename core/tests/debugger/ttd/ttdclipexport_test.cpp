/// @file ttdclipexport_test.cpp
/// @brief TimeTravelController::ExportClip: a TTD range on disk equals the live
/// frames it was recorded from (final picture + plane B), with one meta line
/// per frame. The guest changes the border every 34 T-states, so every frame
/// differs and a static decode could not pass.

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include "base/featuremanager.h"
#include "common/modulelogger.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/ttdcompression.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/video/screen.h"

class TTD_ClipExport_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ttd::TimeTravelController* _ttd = nullptr;
    Screen* _screen = nullptr;
    std::filesystem::path _dir;

    void SetUp() override
    {
        _emulator = new Emulator(LoggerLevel::LogError);
        ASSERT_TRUE(_emulator->Init());
        _context = _emulator->GetContext();
        _ttd = _context->pTimeTravelController;
        _screen = _context->pScreen;

        FeatureManager* fm = _emulator->GetFeatureManager();
        fm->setFeature(Features::kDebugMode, true);
        fm->setFeature(Features::kTimeTravel, true);
        fm->setFeature(Features::kScreenHQ, true);
        fm->setFeature(Features::kZXDLSS, true);
        _context->pMemory->UpdateFeatureCache();

        // DI; LD A,0; loop: OUT (#FE),A; INC A; AND 7; JR loop
        const uint8_t program[] = {0xF3, 0x3E, 0x00, 0xD3, 0xFE, 0x3C, 0xE6, 0x07, 0x18, 0xF9};
        for (size_t i = 0; i < sizeof(program); ++i)
            _context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), program[i]);
        Z80* z80 = _context->pCore->GetZ80();
        z80->pc = 0x8000;
        z80->sp = 0xBFF0;

        _dir = std::filesystem::temp_directory_path() /
               ("unreal-ng-clip-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    }

    void TearDown() override
    {
        std::error_code ec;
        std::filesystem::remove_all(_dir, ec);
        if (_emulator)
        {
            _emulator->Stop();
            _emulator->Release();
            delete _emulator;
        }
    }

    static std::vector<uint8_t> ReadFile(const std::filesystem::path& path)
    {
        std::ifstream in(path, std::ios::binary);
        return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }
};

TEST_F(TTD_ClipExport_Test, ExportedFramesEqualLiveFrames)
{
    std::map<uint64_t, std::vector<uint8_t>> liveRgba;
    std::map<uint64_t, std::vector<uint16_t>> livePlaneB;
    ASSERT_TRUE(_ttd->StartRecording());
    for (int i = 0; i < 12; ++i)
    {
        _emulator->RunNFrames(1, /*skipBreakpoints=*/true);
        const uint64_t frame = _context->emulatorState.frame_counter - 1;
        uint32_t* fb = nullptr;
        size_t fbSize = 0;
        _screen->GetFramebufferData(&fb, &fbSize);
        liveRgba[frame].assign(reinterpret_cast<uint8_t*>(fb), reinterpret_cast<uint8_t*>(fb) + fbSize);
        size_t count = 0;
        const uint16_t* planeB = _screen->GetPlaneB(&count);
        ASSERT_NE(planeB, nullptr);
        livePlaneB[frame].assign(planeB, planeB + count);
    }
    _ttd->StopRecording();

    // Chunks of 4 over 9 frames: two full chunks and a partial one
    ttd::TimeTravelController::TTDClipExportOptions options;
    options.fromFrame = liveRgba.begin()->first + 1;
    options.toFrame = options.fromFrame + 8;
    options.directory = _dir.string();
    options.chunkFrames = 4;
    const auto result = _ttd->ExportClip(options);
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.frames, 9u);
    EXPECT_TRUE(result.planeB);

    const size_t frameBytes = static_cast<size_t>(result.width) * result.height * 4;
    const size_t planeBBytes = static_cast<size_t>(result.width) * result.height * 2;
    ASSERT_EQ(frameBytes, liveRgba.begin()->second.size());
    ASSERT_TRUE(std::filesystem::exists(_dir / "clip.json"));
    {
        // The live palette plane B's color indices were drawn in
        std::ifstream in(_dir / "clip.json");
        const std::string json((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        uint32_t palette[16];
        _screen->GetRGBAPalette16(palette);
        std::string expected = "\"palette16\": [";
        for (int c = 0; c < 16; ++c)
        {
            char hex[16];
            std::snprintf(hex, sizeof(hex), "\"#%02x%02x%02x\"", palette[c] & 0xFF, (palette[c] >> 8) & 0xFF,
                          (palette[c] >> 16) & 0xFF);
            expected += (c ? ", " : "") + std::string(hex);
        }
        EXPECT_NE(json.find(expected + "]"), std::string::npos) << json;
    }

    uint64_t frame = options.fromFrame;
    for (uint32_t chunk = 0; frame <= options.toFrame; ++chunk)
    {
        char rgbaName[32], planeBName[32];
        std::snprintf(rgbaName, sizeof(rgbaName), "rgba_%04u.zst", chunk);
        std::snprintf(planeBName, sizeof(planeBName), "planeb_%04u.zst", chunk);
        const auto rgbaZ = ReadFile(_dir / rgbaName);
        const auto planeBZ = ReadFile(_dir / planeBName);
        const uint64_t framesInChunk = std::min<uint64_t>(options.chunkFrames, options.toFrame - frame + 1);
        std::vector<uint8_t> rgba(framesInChunk * frameBytes);
        std::vector<uint8_t> planeB(framesInChunk * planeBBytes);
        ASSERT_TRUE(ttd::codec::Decompress(rgbaZ, rgba.size(), rgba.data())) << rgbaName;
        ASSERT_TRUE(ttd::codec::Decompress(planeBZ, planeB.size(), planeB.data())) << planeBName;

        for (uint64_t k = 0; k < framesInChunk; ++k, ++frame)
        {
            EXPECT_TRUE(std::equal(liveRgba.at(frame).begin(), liveRgba.at(frame).end(), rgba.begin() + k * frameBytes))
                << "RGBA of frame " << frame;
            const auto* pb = reinterpret_cast<const uint16_t*>(planeB.data() + k * planeBBytes);
            EXPECT_TRUE(std::equal(livePlaneB.at(frame).begin(), livePlaneB.at(frame).end(), pb))
                << "plane B of frame " << frame;
        }
    }

    std::ifstream meta(_dir / "meta.jsonl");
    std::string line;
    uint64_t lines = 0;
    while (std::getline(meta, line))
    {
        EXPECT_NE(line.find("\"frame\": " + std::to_string(options.fromFrame + lines)), std::string::npos) << line;
        lines++;
    }
    EXPECT_EQ(lines, 9u);

    const auto pos = _ttd->CurrentPosition();
    EXPECT_EQ(pos.frame, options.toFrame) << "export leaves the machine at the last exported frame";
}

// VisitComposedFrames hands the same pictures in memory (zxdlss-render renders
// TTD files with it): every frame in order, equal to the live frame, and a
// callback returning false stops the walk at that frame
TEST_F(TTD_ClipExport_Test, VisitComposedFramesDeliversLiveFramesAndStops)
{
    std::map<uint64_t, std::vector<uint8_t>> liveRgba;
    std::map<uint64_t, std::vector<uint16_t>> livePlaneB;
    ASSERT_TRUE(_ttd->StartRecording());
    for (int i = 0; i < 10; ++i)
    {
        _emulator->RunNFrames(1, /*skipBreakpoints=*/true);
        const uint64_t frame = _context->emulatorState.frame_counter - 1;
        uint32_t* fb = nullptr;
        size_t fbSize = 0;
        _screen->GetFramebufferData(&fb, &fbSize);
        liveRgba[frame].assign(reinterpret_cast<uint8_t*>(fb), reinterpret_cast<uint8_t*>(fb) + fbSize);
        size_t count = 0;
        const uint16_t* planeB = _screen->GetPlaneB(&count);
        ASSERT_NE(planeB, nullptr);
        livePlaneB[frame].assign(planeB, planeB + count);
    }
    _ttd->StopRecording();

    const uint64_t from = liveRgba.begin()->first + 1, to = from + 6;
    std::vector<uint64_t> visited;
    const std::string error = _ttd->VisitComposedFrames(from, to, [&](const ttd::TimeTravelController::TTDComposedFrame& f) {
        visited.push_back(f.frame);
        EXPECT_TRUE(std::equal(liveRgba.at(f.frame).begin(), liveRgba.at(f.frame).end(), f.rgba)) << "RGBA of frame " << f.frame;
        EXPECT_NE(f.planeB, nullptr);
        if (f.planeB)
            EXPECT_TRUE(std::equal(livePlaneB.at(f.frame).begin(), livePlaneB.at(f.frame).end(), f.planeB))
                << "plane B of frame " << f.frame;
        return true;
    });
    EXPECT_TRUE(error.empty()) << error;
    ASSERT_EQ(visited.size(), 7u);
    for (size_t k = 0; k < visited.size(); ++k)
        EXPECT_EQ(visited[k], from + k);

    size_t calls = 0;
    const std::string stopped = _ttd->VisitComposedFrames(from, to, [&](const ttd::TimeTravelController::TTDComposedFrame&) {
        return ++calls < 3;
    });
    EXPECT_TRUE(stopped.empty()) << stopped;
    EXPECT_EQ(calls, 3u);
    EXPECT_EQ(_ttd->CurrentPosition().frame, from + 2) << "the walk stays at the frame where it stopped";

    EXPECT_FALSE(_ttd->VisitComposedFrames(to, from, [](const auto&) { return true; }).empty()) << "reversed range";
}

TEST_F(TTD_ClipExport_Test, RefusedWhileRecordingAndOutsideSession)
{
    ASSERT_TRUE(_ttd->StartRecording());
    _emulator->RunNFrames(3, true);

    ttd::TimeTravelController::TTDClipExportOptions options;
    options.directory = _dir.string();
    options.fromFrame = _context->emulatorState.frame_counter - 2;
    options.toFrame = options.fromFrame;
    EXPECT_FALSE(_ttd->ExportClip(options).ok) << "must refuse while recording";

    _ttd->StopRecording();
    options.toFrame = options.fromFrame + 1000;
    const auto result = _ttd->ExportClip(options);
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error.find("outside the session"), std::string::npos) << result.error;
}
